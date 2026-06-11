#pragma once
#include <QVector>
#include <QString>
#include <QMap>

struct BenchmarkReport {
    double pai5pct    = 0.0;   // Predictive Accuracy Index at 5% area flagged
    double pai10pct   = 0.0;   // PAI at 10%
    double pai20pct   = 0.0;   // PAI at 20%
    double pei10pct   = 0.0;   // Predictive Efficiency Index at 10%
    double ser        = 0.0;   // Search Efficiency Ratio (area under lift)
    double aucRoc     = 0.0;   // AUC-ROC (trapezoidal rule)
    double aucPr      = 0.0;   // Area under precision-recall curve
    double mae        = 0.0;   // Mean Absolute Error
    double rmse       = 0.0;   // Root Mean Squared Error
    double brierScore = 0.0;   // Brier score for calibration
    int    nSamples   = 0;
    QString reportText() const;  // formatted summary
};

// Hint quality evaluation (information retrieval metrics)
struct HintBenchmarkResult {
    double precisionAt1  = 0.0;   // Was top lead correct?
    double precisionAt3  = 0.0;   // Was 1+ of top 3 correct?
    double precisionAt5  = 0.0;
    double mrr           = 0.0;   // Mean Reciprocal Rank
    double ndcg          = 0.0;   // Normalised Discounted Cumulative Gain
    double coverage      = 0.0;   // fraction of cases with >=1 correct lead
    double falseLeadRate = 0.0;
    int    nCases        = 0;
    QString reportText() const;
};

class BenchmarkMetrics {
public:
    // PAI = hit_rate / area_fraction
    // y_true: binary crime occurrence per cell
    // y_pred: predicted probability per cell
    // area_fraction: e.g. 0.05 for top 5% flagged
    static double pai(const QVector<double>& yTrue,
                      const QVector<double>& yPred,
                      double areaFraction);

    // PEI = PAI / PAI_max
    static double pei(const QVector<double>& yTrue,
                      const QVector<double>& yPred,
                      double areaFraction);

    // SER = (area_under_lift - 0.5) / (area_perfect - 0.5)
    static double ser(const QVector<double>& yTrue,
                      const QVector<double>& yPred);

    static double aucRoc(const QVector<double>& yTrue,
                         const QVector<double>& yPred);

    static double aucPr(const QVector<double>& yTrue,
                        const QVector<double>& yPred);

    static double mae(const QVector<double>& yTrue,
                      const QVector<double>& yPred);

    static double rmse(const QVector<double>& yTrue,
                       const QVector<double>& yPred);

    static double brierScore(const QVector<double>& yTrue,
                              const QVector<double>& yPred);

    static double logLoss(const QVector<double>& yTrue,
                          const QVector<double>& yPred);

    // Full benchmark report
    static BenchmarkReport fullReport(const QVector<double>& yTrue,
                                      const QVector<double>& yPred);

    // Hint quality: evaluate ranked lists against ground truth
    // relevantRanks[i] = rank (1-based) of the correct answer in case i
    //                    0 if no correct answer in the top-K results
    static HintBenchmarkResult hintQuality(const QVector<int>& relevantRanks,
                                            int topK = 5);
};
