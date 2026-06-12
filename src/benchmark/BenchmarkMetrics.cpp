#include "benchmark/BenchmarkMetrics.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <QStringList>

namespace {

// Build a vector of indices sorted by yPred descending
QVector<int> sortedIndicesDesc(const QVector<double>& yPred)
{
    QVector<int> idx(yPred.size());
    std::iota(idx.begin(), idx.end(), 0);
    std::sort(idx.begin(), idx.end(), [&](int a, int b){
        return yPred[a] > yPred[b];
    });
    return idx;
}

// Trapezoidal integration over y vs x (both vectors same length)
double trapz(const QVector<double>& x, const QVector<double>& y)
{
    double area = 0.0;
    for (int i = 1; i < x.size(); ++i)
        area += 0.5 * (x[i] - x[i-1]) * (y[i] + y[i-1]);
    return area;
}

} // anonymous namespace

// ─── PAI ─────────────────────────────────────────────────────────────────────

double BenchmarkMetrics::pai(const QVector<double>& yTrue,
                              const QVector<double>& yPred,
                              double areaFraction)
{
    const int n = yTrue.size();
    if (n == 0 || yPred.size() != n || areaFraction <= 0.0 || areaFraction > 1.0) return 0.0;

    const double totalCrimes = std::accumulate(yTrue.begin(), yTrue.end(), 0.0);
    if (totalCrimes == 0.0) return 0.0;

    const int nFlagged = std::max(1, static_cast<int>(std::round(n * areaFraction)));
    const auto idx = sortedIndicesDesc(yPred);

    double crimesInFlagged = 0.0;
    for (int i = 0; i < nFlagged && i < n; ++i)
        crimesInFlagged += yTrue[idx[i]];

    const double hitRate = crimesInFlagged / totalCrimes;
    return hitRate / areaFraction;
}

// ─── PEI ─────────────────────────────────────────────────────────────────────

double BenchmarkMetrics::pei(const QVector<double>& yTrue,
                              const QVector<double>& yPred,
                              double areaFraction)
{
    const int n = yTrue.size();
    if (n == 0 || yPred.size() != n || areaFraction <= 0.0 || areaFraction > 1.0) return 0.0;

    const double totalCrimes = std::accumulate(yTrue.begin(), yTrue.end(), 0.0);
    if (totalCrimes == 0.0) return 0.0;

    const int nFlagged = std::max(1, static_cast<int>(std::round(n * areaFraction)));

    // Perfect predictor flags nFlagged cells that contain min(nFlagged, totalCrimes) crimes
    const double perfectCrimes = std::min(static_cast<double>(nFlagged), totalCrimes);
    const double perfectHitRate = perfectCrimes / totalCrimes;
    const double paiMax = perfectHitRate / areaFraction;
    if (paiMax <= 0.0) return 0.0;

    return pai(yTrue, yPred, areaFraction) / paiMax;
}

// ─── SER ─────────────────────────────────────────────────────────────────────

double BenchmarkMetrics::ser(const QVector<double>& yTrue,
                              const QVector<double>& yPred)
{
    const int n = yTrue.size();
    if (n == 0 || yPred.size() != n) return 0.0;

    const double totalCrimes = std::accumulate(yTrue.begin(), yTrue.end(), 0.0);
    if (totalCrimes == 0.0) return 0.0;

    const auto idx = sortedIndicesDesc(yPred);

    // Build lift curve: x = fraction of area searched, y = fraction of crimes found
    QVector<double> x(n + 1), y(n + 1);
    x[0] = 0.0; y[0] = 0.0;
    double cumCrimes = 0.0;
    for (int i = 0; i < n; ++i) {
        cumCrimes += yTrue[idx[i]];
        x[i+1] = static_cast<double>(i+1) / n;
        y[i+1] = cumCrimes / totalCrimes;
    }

    const double areaModel   = trapz(x, y);
    const double areaRandom  = 0.5;  // diagonal
    // Perfect model: crimes concentrated at front
    // area_perfect = 1 - 0.5 * (totalCrimes/n)^2   simplified as:
    // integral = totalCrimes/n * 1.0 + (1 - totalCrimes/n) * 1.0 - trapezoid tip
    // Simpler: perfect area = 1 - crimeFraction/2  where crimeFraction = totalCrimes/n
    const double crimeFrac   = totalCrimes / n;
    const double areaPerfect = 1.0 - 0.5 * crimeFrac;

    if (areaPerfect <= areaRandom) return 0.0;
    return (areaModel - areaRandom) / (areaPerfect - areaRandom);
}

// ─── AUC-ROC ─────────────────────────────────────────────────────────────────

double BenchmarkMetrics::aucRoc(const QVector<double>& yTrue,
                                 const QVector<double>& yPred)
{
    const int n = yTrue.size();
    if (n == 0 || yPred.size() != n) return 0.0;

    const auto idx = sortedIndicesDesc(yPred);

    const double nPos = std::accumulate(yTrue.begin(), yTrue.end(), 0.0);
    const double nNeg = n - nPos;
    if (nPos == 0.0 || nNeg == 0.0) return 0.5;

    // Walk thresholds from high to low, accumulate TPR/FPR
    QVector<double> fprVec, tprVec;
    fprVec.reserve(n + 2);
    tprVec.reserve(n + 2);
    fprVec.append(0.0);
    tprVec.append(0.0);

    double tp = 0.0, fp = 0.0;
    for (int i = 0; i < n; ++i) {
        if (yTrue[idx[i]] >= 0.5) tp += 1.0;
        else                       fp += 1.0;
        fprVec.append(fp / nNeg);
        tprVec.append(tp / nPos);
    }

    return trapz(fprVec, tprVec);
}

// ─── AUC-PR ──────────────────────────────────────────────────────────────────

double BenchmarkMetrics::aucPr(const QVector<double>& yTrue,
                                const QVector<double>& yPred)
{
    const int n = yTrue.size();
    if (n == 0 || yPred.size() != n) return 0.0;

    const auto idx = sortedIndicesDesc(yPred);

    const double nPos = std::accumulate(yTrue.begin(), yTrue.end(), 0.0);
    if (nPos == 0.0) return 0.0;

    QVector<double> recallVec, precVec;
    recallVec.reserve(n + 2);
    precVec.reserve(n + 2);
    recallVec.append(0.0);
    precVec.append(1.0);  // precision starts at 1 by convention

    double tp = 0.0;
    for (int i = 0; i < n; ++i) {
        if (yTrue[idx[i]] >= 0.5) tp += 1.0;
        double prec   = tp / (i + 1);
        double recall = tp / nPos;
        recallVec.append(recall);
        precVec.append(prec);
    }

    return trapz(recallVec, precVec);
}

// ─── MAE ─────────────────────────────────────────────────────────────────────

double BenchmarkMetrics::mae(const QVector<double>& yTrue,
                              const QVector<double>& yPred)
{
    const int n = yTrue.size();
    if (n == 0 || yPred.size() != n) return 0.0;
    double sum = 0.0;
    for (int i = 0; i < n; ++i)
        sum += std::abs(yTrue[i] - yPred[i]);
    return sum / n;
}

// ─── RMSE ────────────────────────────────────────────────────────────────────

double BenchmarkMetrics::rmse(const QVector<double>& yTrue,
                               const QVector<double>& yPred)
{
    const int n = yTrue.size();
    if (n == 0 || yPred.size() != n) return 0.0;
    double sum = 0.0;
    for (int i = 0; i < n; ++i) {
        const double diff = yTrue[i] - yPred[i];
        sum += diff * diff;
    }
    return std::sqrt(sum / n);
}

// ─── Brier Score ─────────────────────────────────────────────────────────────

double BenchmarkMetrics::brierScore(const QVector<double>& yTrue,
                                     const QVector<double>& yPred)
{
    const int n = yTrue.size();
    if (n == 0 || yPred.size() != n) return 0.0;
    double sum = 0.0;
    for (int i = 0; i < n; ++i) {
        const double diff = yPred[i] - yTrue[i];
        sum += diff * diff;
    }
    return sum / n;
}

// ─── Log Loss ────────────────────────────────────────────────────────────────

double BenchmarkMetrics::logLoss(const QVector<double>& yTrue,
                                  const QVector<double>& yPred)
{
    const int n = yTrue.size();
    if (n == 0 || yPred.size() != n) return 0.0;

    constexpr double eps = 1e-15;
    double sum = 0.0;
    for (int i = 0; i < n; ++i) {
        const double p = std::clamp(yPred[i], eps, 1.0 - eps);
        const double y = yTrue[i];
        sum += y * std::log(p) + (1.0 - y) * std::log(1.0 - p);
    }
    return -sum / n;
}

// ─── Full Report ─────────────────────────────────────────────────────────────

BenchmarkReport BenchmarkMetrics::fullReport(const QVector<double>& yTrue,
                                              const QVector<double>& yPred)
{
    BenchmarkReport r;
    r.nSamples   = yTrue.size();
    r.pai5pct    = pai(yTrue, yPred, 0.05);
    r.pai10pct   = pai(yTrue, yPred, 0.10);
    r.pai20pct   = pai(yTrue, yPred, 0.20);
    r.pei10pct   = pei(yTrue, yPred, 0.10);
    r.ser        = ser(yTrue, yPred);
    r.aucRoc     = aucRoc(yTrue, yPred);
    r.aucPr      = aucPr(yTrue, yPred);
    r.mae        = mae(yTrue, yPred);
    r.rmse       = rmse(yTrue, yPred);
    r.brierScore = brierScore(yTrue, yPred);
    return r;
}

QString BenchmarkReport::reportText() const
{
    return QStringLiteral(
        "BenchmarkReport (n=%1)\n"
        "  PAI  5%%: %2\n"
        "  PAI 10%%: %3\n"
        "  PAI 20%%: %4\n"
        "  PEI 10%%: %5\n"
        "  SER:      %6\n"
        "  AUC-ROC:  %7\n"
        "  AUC-PR:   %8\n"
        "  MAE:      %9\n"
        "  RMSE:     %10\n"
        "  Brier:    %11\n"
    ).arg(nSamples)
     .arg(pai5pct,   0, 'f', 4)
     .arg(pai10pct,  0, 'f', 4)
     .arg(pai20pct,  0, 'f', 4)
     .arg(pei10pct,  0, 'f', 4)
     .arg(ser,       0, 'f', 4)
     .arg(aucRoc,    0, 'f', 4)
     .arg(aucPr,     0, 'f', 4)
     .arg(mae,       0, 'f', 4)
     .arg(rmse,      0, 'f', 4)
     .arg(brierScore,0, 'f', 4);
}

// ─── Hint Quality ─────────────────────────────────────────────────────────────

HintBenchmarkResult BenchmarkMetrics::hintQuality(const QVector<int>& relevantRanks,
                                                    int topK)
{
    const int n = relevantRanks.size();
    HintBenchmarkResult r;
    r.nCases = n;
    if (n == 0) return r;

    int coveredCases = 0;
    int falseCases   = 0;
    double mrrSum    = 0.0;
    double ndcgSum   = 0.0;

    // Ideal DCG: single relevant item at rank 1
    const double idcg = 1.0 / std::log2(2.0);  // log2(1+1)

    int p1 = 0, p3 = 0, p5 = 0;

    for (int i = 0; i < n; ++i) {
        const int rank = relevantRanks[i];
        if (rank > 0 && rank <= topK) {
            ++coveredCases;
            mrrSum  += 1.0 / rank;
            ndcgSum += (1.0 / std::log2(rank + 1.0)) / idcg;
            if (rank == 1)           ++p1;
            if (rank <= 3)           ++p3;
            if (rank <= 5)           ++p5;
        } else {
            ++falseCases;
        }
    }

    r.precisionAt1  = static_cast<double>(p1) / n;
    r.precisionAt3  = static_cast<double>(p3) / n;
    r.precisionAt5  = static_cast<double>(p5) / n;
    r.mrr           = mrrSum  / n;
    r.ndcg          = ndcgSum / n;
    r.coverage      = static_cast<double>(coveredCases) / n;
    r.falseLeadRate = static_cast<double>(falseCases) / n;
    return r;
}

QString HintBenchmarkResult::reportText() const
{
    return QStringLiteral(
        "HintBenchmarkResult (n=%1)\n"
        "  P@1:           %2\n"
        "  P@3:           %3\n"
        "  P@5:           %4\n"
        "  MRR:           %5\n"
        "  NDCG:          %6\n"
        "  Coverage:      %7\n"
        "  FalseLeadRate: %8\n"
    ).arg(nCases)
     .arg(precisionAt1,  0, 'f', 4)
     .arg(precisionAt3,  0, 'f', 4)
     .arg(precisionAt5,  0, 'f', 4)
     .arg(mrr,           0, 'f', 4)
     .arg(ndcg,          0, 'f', 4)
     .arg(coverage,      0, 'f', 4)
     .arg(falseLeadRate, 0, 'f', 4);
}
