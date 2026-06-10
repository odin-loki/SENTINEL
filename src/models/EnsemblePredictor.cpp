#include "models/EnsemblePredictor.h"

#include <algorithm>
#include <cmath>
#include <numeric>

// ─── Weights ──────────────────────────────────────────────────────────────────

void EnsemblePredictor::setWeights(double wPoisson, double wHawkes)
{
    const double total = wPoisson + wHawkes;
    if (total <= 0.0) return;
    m_wPoisson = wPoisson / total;
    m_wHawkes  = wHawkes  / total;
}

// ─── Calibration ─────────────────────────────────────────────────────────────

void EnsemblePredictor::calibrate(
    const QVector<QPair<double, double>>& calibrationData)
{
    if (calibrationData.size() < 10) return;

    // Isotonic regression: build calibration table with 20 bins
    constexpr int NBINS = 20;
    struct Bin { double sumPred = 0.0; double sumAct = 0.0; int n = 0; };
    QVector<Bin> bins(NBINS);

    for (const auto& [pred, act] : calibrationData) {
        const int idx = std::min(static_cast<int>(pred * NBINS), NBINS - 1);
        bins[idx].sumPred += pred;
        bins[idx].sumAct  += act;
        bins[idx].n++;
    }

    m_calTable.clear();
    m_calTable.append({0.0, 0.0});

    for (int i = 0; i < NBINS; ++i) {
        if (bins[i].n == 0) continue;
        const double rawMean = bins[i].sumPred / bins[i].n;
        const double calMean = bins[i].sumAct  / bins[i].n;
        m_calTable.append({rawMean, calMean});
    }
    m_calTable.append({1.0, 1.0});
    m_calibrated = true;
}

double EnsemblePredictor::applyCal(double rawProb) const
{
    if (!m_calibrated || m_calTable.size() < 2) return rawProb;

    // Linear interpolation
    for (int i = 0; i + 1 < m_calTable.size(); ++i) {
        const double x0 = m_calTable[i].first;
        const double x1 = m_calTable[i + 1].first;
        if (rawProb >= x0 && rawProb <= x1) {
            const double t  = (x1 > x0) ? (rawProb - x0) / (x1 - x0) : 0.0;
            const double y0 = m_calTable[i].second;
            const double y1 = m_calTable[i + 1].second;
            return std::clamp(y0 + t * (y1 - y0), 0.0, 1.0);
        }
    }
    return std::clamp(rawProb, 0.0, 1.0);
}

// ─── Predict ─────────────────────────────────────────────────────────────────

EnsemblePrediction EnsemblePredictor::predict(
    const QString& zoneId,
    const QDateTime& dt,
    const QString& crimeType,
    double lat, double lon) const
{
    EnsemblePrediction result;

    double poissonProb   = 0.0;
    double poissonCount  = 0.0;
    double poissonCI90lo = 0.0;
    double poissonCI90hi = 0.0;
    bool   havePoi       = false;

    double hawkesProb  = 0.0;
    double hawkesCount = 0.0;
    bool   haveHawks   = false;

    // ── Poisson component ───────────────────────────────────────────────────
    if (m_poisson && m_poisson->isFitted()) {
        const auto pred = m_poisson->predict(zoneId, dt, crimeType);
        poissonProb   = pred.probAtLeastOne;
        poissonCount  = pred.expectedCount;
        poissonCI90lo = pred.ci90.first;
        poissonCI90hi = pred.ci90.second;
        havePoi       = true;
    }

    // ── Hawkes component ────────────────────────────────────────────────────
    if (m_hawkes && m_hawkes->isFitted()) {
        const double tDays   = QDateTime::fromSecsSinceEpoch(0).daysTo(dt);
        const double lam     = m_hawkes->intensity(tDays, lat, lon);
        hawkesProb   = 1.0 - std::exp(-lam);
        hawkesCount  = lam;
        haveHawks    = true;
    }

    // ── Combine ─────────────────────────────────────────────────────────────
    if (havePoi && haveHawks) {
        result.probCrime     = m_wPoisson * poissonProb + m_wHawkes * hawkesProb;
        result.expectedCount = m_wPoisson * poissonCount + m_wHawkes * hawkesCount;
        result.poissonWeight = m_wPoisson;
        result.hawkesWeight  = m_wHawkes;

        // Aleatoric = CI width from Poisson (irreducible)
        result.uncertaintyAleatoric = (poissonCI90hi - poissonCI90lo) / 3.29;  // ≈ std dev

        // Epistemic = model disagreement
        result.uncertaintyEpistemic = std::abs(poissonProb - hawkesProb) / 2.0;

        result.dominantModel = (m_wPoisson > m_wHawkes)
                               ? QStringLiteral("poisson") : QStringLiteral("hawkes");
    } else if (havePoi) {
        result.probCrime     = poissonProb;
        result.expectedCount = poissonCount;
        result.poissonWeight = 1.0;
        result.hawkesWeight  = 0.0;
        result.uncertaintyAleatoric = (poissonCI90hi - poissonCI90lo) / 3.29;
        result.dominantModel = QStringLiteral("poisson");
    } else if (haveHawks) {
        result.probCrime     = hawkesProb;
        result.expectedCount = hawkesCount;
        result.poissonWeight = 0.0;
        result.hawkesWeight  = 1.0;
        // Poisson approximation for aleatoric when Hawkes-only
        result.uncertaintyAleatoric = std::sqrt(std::max(0.0, hawkesCount)) / 3.29;
        result.dominantModel = QStringLiteral("hawkes");
    }

    // Apply calibration if available
    result.probCrime  = applyCal(std::clamp(result.probCrime, 0.0, 1.0));
    result.ci90       = {result.expectedCount * 0.65, result.expectedCount * 1.55};
    result.calibrated = m_calibrated;

    // 95% confidence interval on probCrime using combined uncertainty
    {
        const double sigma = std::sqrt(
            result.uncertaintyAleatoric * result.uncertaintyAleatoric +
            result.uncertaintyEpistemic * result.uncertaintyEpistemic);
        result.ciLow95  = std::clamp(result.probCrime - 1.96 * sigma, 0.0, 1.0);
        result.ciHigh95 = std::clamp(result.probCrime + 1.96 * sigma, 0.0, 1.0);
    }

    return result;
}

// ─── Risk grid ───────────────────────────────────────────────────────────────

QVector<QVector<EnsemblePrediction>> EnsemblePredictor::riskGrid(
    const QDateTime& dt,
    double latMin, double latMax,
    double lonMin, double lonMax,
    int gridN) const
{
    QVector<QVector<EnsemblePrediction>> grid(gridN,
        QVector<EnsemblePrediction>(gridN));

    const double dLat = (latMax - latMin) / gridN;
    const double dLon = (lonMax - lonMin) / gridN;

    for (int r = 0; r < gridN; ++r) {
        for (int c = 0; c < gridN; ++c) {
            const double lat = latMin + (r + 0.5) * dLat;
            const double lon = lonMin + (c + 0.5) * dLon;
            grid[r][c] = predict(QStringLiteral("grid_%1_%2").arg(r).arg(c),
                                  dt, QStringLiteral("all"), lat, lon);
        }
    }
    return grid;
}

// ─── ECE ─────────────────────────────────────────────────────────────────────

double EnsemblePredictor::ece(const QVector<QPair<double, double>>& predActual,
                               int bins)
{
    if (predActual.isEmpty()) return 0.0;

    QVector<double> sumPred(bins, 0.0);
    QVector<double> sumAct(bins,  0.0);
    QVector<int>    cnt(bins,     0);

    for (const auto& [pred, act] : predActual) {
        const int b = std::min(static_cast<int>(pred * bins), bins - 1);
        sumPred[b] += pred;
        sumAct[b]  += act;
        cnt[b]++;
    }

    double ece = 0.0;
    const double n = static_cast<double>(predActual.size());

    for (int b = 0; b < bins; ++b) {
        if (cnt[b] == 0) continue;
        const double avgPred = sumPred[b] / cnt[b];
        const double avgAct  = sumAct[b]  / cnt[b];
        ece += (cnt[b] / n) * std::abs(avgPred - avgAct);
    }
    return ece;
}

// ─── Brier score ─────────────────────────────────────────────────────────────

double EnsemblePredictor::brierScore(
    const QVector<QPair<double, double>>& predActual)
{
    if (predActual.isEmpty()) return 0.0;
    double sum = 0.0;
    for (const auto& [pred, act] : predActual)
        sum += (pred - act) * (pred - act);
    return sum / predActual.size();
}
