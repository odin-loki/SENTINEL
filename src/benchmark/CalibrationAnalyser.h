#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// CalibrationAnalyser — model calibration measurement and plotting data
//
// Calibration: whether predicted probabilities match empirical frequencies.
// A perfectly calibrated model: 40% of events predicted at 0.4 actually occur.
//
// Metrics produced:
//   ECE  — Expected Calibration Error (weighted average bin error)
//   MCE  — Maximum Calibration Error (worst bin)
//   ACE  — Average Calibration Error (equal-width unweighted)
//   Reliability diagram data — for visualisation
//   Sharpness — variance of predictions (higher = more discriminative)
//
// Published basis:
//   Guo et al. (2017) "On Calibration of Modern Neural Networks"
//   Niculescu-Mizil & Caruana (2005) "Predicting Good Probabilities"
// ─────────────────────────────────────────────────────────────────────────────
#include <QVector>
#include <QPair>
#include <QString>

struct ReliabilityPoint {
    double meanPredicted   = 0.0;
    double fractionPositive = 0.0;
    int    count            = 0;
};

struct CalibrationBin {
    double midpoint    = 0.0;   // bin centre (e.g. 0.05 for [0, 0.1])
    double avgPred     = 0.0;   // mean predicted probability in bin
    double empirical   = 0.0;   // empirical positive rate
    double error       = 0.0;   // |avgPred - empirical|
    int    count       = 0;
    double fraction    = 0.0;   // count / total
};

struct CalibrationResult {
    double ece          = 0.0;   // expected calibration error
    double mce          = 0.0;   // maximum calibration error
    double ace          = 0.0;   // average calibration error (unweighted)
    double brierScore   = 0.0;
    double logLoss      = 0.0;
    double sharpness    = 0.0;   // var(predictions)
    int    nSamples     = 0;
    int    nBins        = 0;
    QVector<CalibrationBin> bins;
    QString status() const;      // "EXCELLENT" | "GOOD" | "FAIR" | "POOR"
};

class CalibrationAnalyser {
public:
    explicit CalibrationAnalyser(int nBins = 10);

    // predActual: list of (predicted_prob, actual_binary {0,1})
    CalibrationResult analyse(const QVector<QPair<double,double>>& predActual) const;

    // Reliability diagram data: list of (avgPred, empirical) per bin
    // for direct use in a QLineSeries chart
    QVector<QPair<double,double>> reliabilityDiagram(
        const QVector<QPair<double,double>>& predActual) const;

    // Structured reliability diagram from pre-computed bins (skips count==0 bins)
    QVector<ReliabilityPoint> reliabilityDiagram(
        const QVector<CalibrationBin>& bins) const;

    // Isotonic regression calibration:
    // Returns calibrated (pred, actual) vector after PAVA-based correction.
    static QVector<QPair<double,double>> isotonicCalibrate(
        const QVector<QPair<double,double>>& predActual);

private:
    int m_nBins;
    static double logLossScore(const QVector<QPair<double,double>>& pa);
};
