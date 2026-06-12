#include "models/EnsemblePredictor.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace {

// Pool Adjacent Violators Algorithm — enforce non-decreasing calibrated values.
void applyPava(QVector<double>& y)
{
    const int n = y.size();
    if (n <= 1) return;

    QVector<double> cal(n);
    QVector<int>    idx(n);
    int m = 0;

    for (int i = 0; i < n; ++i) {
        cal[m] = y[i];
        idx[m] = 1;
        while (m > 0 && cal[m - 1] > cal[m]) {
            const double pooled = (cal[m - 1] * idx[m - 1] + cal[m] * idx[m]) /
                                  static_cast<double>(idx[m - 1] + idx[m]);
            --m;
            cal[m] = pooled;
            idx[m] += idx[m + 1];
        }
        ++m;
    }

    int pos = 0;
    for (int b = 0; b < m; ++b) {
        for (int j = 0; j < idx[b]; ++j)
            y[pos++] = cal[b];
    }
}

} // namespace

// ─── Weights ──────────────────────────────────────────────────────────────────

void EnsemblePredictor::setWeights(double wPoisson, double wHawkes)
{
    const double total = wPoisson + wHawkes;
    if (total <= 0.0) {
        m_wPoisson = 0.5;
        m_wHawkes  = 0.5;
        return;
    }
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
        const double clampedPred = std::clamp(pred, 0.0, 1.0);
        const int idx = std::min(static_cast<int>(clampedPred * NBINS), NBINS - 1);
        bins[idx].sumPred += clampedPred;
        bins[idx].sumAct  += act;
        bins[idx].n++;
    }

    QVector<double> rawMeans;
    QVector<double> calMeans;
    rawMeans.reserve(NBINS);
    calMeans.reserve(NBINS);

    for (int i = 0; i < NBINS; ++i) {
        if (bins[i].n == 0) continue;
        rawMeans.append(bins[i].sumPred / bins[i].n);
        calMeans.append(bins[i].sumAct  / bins[i].n);
    }

    if (rawMeans.isEmpty()) return;

    applyPava(calMeans);

    m_calTable.clear();
    m_calTable.append({0.0, 0.0});
    for (int i = 0; i < rawMeans.size(); ++i)
        m_calTable.append({rawMeans[i], std::clamp(calMeans[i], 0.0, 1.0)});
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
    double wPoisson = m_wPoisson;
    double wHawkes  = m_wHawkes;
    const double wTotal = wPoisson + wHawkes;
    if (wTotal <= 0.0) {
        wPoisson = 0.5;
        wHawkes  = 0.5;
    } else {
        wPoisson /= wTotal;
        wHawkes  /= wTotal;
    }

    if (havePoi && haveHawks) {
        result.probCrime     = wPoisson * poissonProb + wHawkes * hawkesProb;
        result.expectedCount = wPoisson * poissonCount + wHawkes * hawkesCount;
        result.poissonWeight = wPoisson;
        result.hawkesWeight  = wHawkes;

        // Aleatoric = CI width from Poisson (irreducible)
        result.uncertaintyAleatoric = (poissonCI90hi - poissonCI90lo) / 3.29;  // ≈ std dev

        // Epistemic = model disagreement
        result.uncertaintyEpistemic = std::abs(poissonProb - hawkesProb) / 2.0;

        if (std::abs(wPoisson - wHawkes) < 1e-9)
            result.dominantModel = QStringLiteral("equal");
        else
            result.dominantModel = (wPoisson > wHawkes)
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
    result.calibrated = m_calibrated && (havePoi || haveHawks);

    // 95% confidence interval on probCrime using combined uncertainty
    if (!havePoi && !haveHawks) {
        result.ciLow95  = 0.0;
        result.ciHigh95 = 0.0;
    } else {
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
    const int n = std::max(gridN, 1);
    QVector<QVector<EnsemblePrediction>> grid(n,
        QVector<EnsemblePrediction>(n));

    const double dLat = (latMax - latMin) / n;
    const double dLon = (lonMax - lonMin) / n;

    for (int r = 0; r < n; ++r) {
        for (int c = 0; c < n; ++c) {
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
    if (predActual.isEmpty() || bins <= 0) return 0.0;

    QVector<double> sumPred(bins, 0.0);
    QVector<double> sumAct(bins,  0.0);
    QVector<int>    cnt(bins,     0);

    for (const auto& [pred, act] : predActual) {
        const int b = std::clamp(static_cast<int>(pred * bins), 0, bins - 1);
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
