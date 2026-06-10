#include "benchmark/CalibrationAnalyser.h"

#include <algorithm>
#include <cmath>
#include <numeric>

// ─────────────────────────────────────────────────────────────────────────────

CalibrationAnalyser::CalibrationAnalyser(int nBins)
    : m_nBins(std::max(nBins, 2))
{
}

// ─── Core analysis ───────────────────────────────────────────────────────────

CalibrationResult CalibrationAnalyser::analyse(
    const QVector<QPair<double,double>>& predActual) const
{
    CalibrationResult res;
    if (predActual.isEmpty()) return res;

    res.nSamples = predActual.size();
    res.nBins    = m_nBins;

    // Build bins
    QVector<double> sumPred(m_nBins, 0.0);
    QVector<double> sumAct (m_nBins, 0.0);
    QVector<int>    cnt    (m_nBins, 0);

    for (const auto& [pred, act] : predActual) {
        const int b = std::min(
            static_cast<int>(pred * m_nBins), m_nBins - 1);
        sumPred[b] += pred;
        sumAct [b] += act;
        cnt    [b]++;
    }

    const double n = static_cast<double>(res.nSamples);
    double ece = 0.0, ace = 0.0, mce = 0.0;

    for (int b = 0; b < m_nBins; ++b) {
        CalibrationBin bin;
        bin.midpoint = (b + 0.5) / m_nBins;
        bin.count    = cnt[b];
        bin.fraction = cnt[b] / n;

        if (cnt[b] > 0) {
            bin.avgPred   = sumPred[b] / cnt[b];
            bin.empirical = sumAct [b] / cnt[b];
        }
        bin.error = std::abs(bin.avgPred - bin.empirical);

        ece += bin.fraction * bin.error;
        ace += bin.error;
        mce  = std::max(mce, bin.error);

        res.bins.append(bin);
    }
    ace /= m_nBins;

    res.ece = ece;
    res.ace = ace;
    res.mce = mce;

    // Brier score
    double bs = 0.0;
    for (const auto& [pred, act] : predActual)
        bs += (pred - act) * (pred - act);
    res.brierScore = bs / n;

    // Log-loss
    res.logLoss = logLossScore(predActual);

    // Sharpness = variance of predictions
    double mean = 0.0;
    for (const auto& [pred, _] : predActual) mean += pred;
    mean /= n;
    double var = 0.0;
    for (const auto& [pred, _] : predActual) var += (pred - mean) * (pred - mean);
    res.sharpness = var / n;

    return res;
}

// ─── Reliability diagram ─────────────────────────────────────────────────────

QVector<QPair<double,double>> CalibrationAnalyser::reliabilityDiagram(
    const QVector<QPair<double,double>>& predActual) const
{
    const auto res = analyse(predActual);
    QVector<QPair<double,double>> points;
    for (const auto& bin : res.bins) {
        if (bin.count > 0)
            points.append({bin.avgPred, bin.empirical});
    }
    return points;
}

// ─── Reliability diagram (structured, from pre-computed bins) ────────────────

QVector<ReliabilityPoint> CalibrationAnalyser::reliabilityDiagram(
    const QVector<CalibrationBin>& bins) const
{
    QVector<ReliabilityPoint> points;
    for (const auto& bin : bins) {
        if (bin.count > 0)
            points.append({bin.midpoint, bin.empirical, bin.count});
    }
    return points;
}

// ─── Isotonic calibration (PAVA — Pool Adjacent Violators Algorithm) ─────────

QVector<QPair<double,double>> CalibrationAnalyser::isotonicCalibrate(
    const QVector<QPair<double,double>>& predActual)
{
    if (predActual.isEmpty()) return {};

    // Sort by predicted probability
    QVector<QPair<double,double>> sorted = predActual;
    std::sort(sorted.begin(), sorted.end(),
              [](const QPair<double,double>& a, const QPair<double,double>& b) {
                  return a.first < b.first;
              });

    const int n = sorted.size();
    QVector<double> y(n);
    for (int i = 0; i < n; ++i) y[i] = sorted[i].second;

    // PAVA: Pool adjacent violators
    // Result: isotonically non-decreasing calibrated probabilities
    QVector<double> cal(n);
    QVector<int>    idx(n);
    int m = 0;

    for (int i = 0; i < n; ++i) {
        cal[m] = y[i];
        idx[m] = 1;
        while (m > 0 && cal[m-1] > cal[m]) {
            // Merge block m into block m-1
            const double pooled = (cal[m-1] * idx[m-1] + cal[m] * idx[m]) /
                                  (idx[m-1] + idx[m]);
            m--;
            cal[m] = pooled;
            idx[m] += idx[m+1];
        }
        m++;
    }

    // Expand blocks back to per-sample
    QVector<double> calExpanded(n);
    int pos = 0;
    for (int b = 0; b < m; ++b) {
        for (int j = 0; j < idx[b]; ++j)
            calExpanded[pos++] = cal[b];
    }

    QVector<QPair<double,double>> result(n);
    for (int i = 0; i < n; ++i)
        result[i] = {calExpanded[i], sorted[i].second};

    return result;
}

// ─── Log-loss ─────────────────────────────────────────────────────────────────

double CalibrationAnalyser::logLossScore(
    const QVector<QPair<double,double>>& pa)
{
    if (pa.isEmpty()) return 0.0;
    constexpr double eps = 1e-15;
    double sum = 0.0;
    for (const auto& [pred, act] : pa) {
        const double p = std::clamp(pred, eps, 1.0 - eps);
        sum += act * std::log(p) + (1.0 - act) * std::log(1.0 - p);
    }
    return -sum / pa.size();
}

// ─── Status string ───────────────────────────────────────────────────────────

QString CalibrationResult::status() const
{
    if (ece < 0.05) return QStringLiteral("EXCELLENT");
    if (ece < 0.10) return QStringLiteral("GOOD");
    if (ece < 0.20) return QStringLiteral("FAIR");
    return QStringLiteral("POOR");
}
