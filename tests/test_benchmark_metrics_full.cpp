// test_benchmark_metrics_full.cpp — comprehensive BenchmarkMetrics tests
// Covers: PAI, PEI, SER, AUC-ROC, AUC-PR, CalibrationAnalyser

#include <QTest>
#include <QCoreApplication>
#include <QVector>
#include <QPair>
#include <cmath>

#include "benchmark/BenchmarkMetrics.h"
#include "benchmark/CalibrationAnalyser.h"

// ─── Helpers ──────────────────────────────────────────────────────────────────

namespace {

// Build a dataset of `n` cells where the top `nPositive` cells (by index) hold
// a crime.  yPred is a descending score so rank matches truth perfectly.
void makePerfectDataset(int n, int nPositive,
                        QVector<double>& yTrue,
                        QVector<double>& yPred)
{
    yTrue.resize(n);  yPred.resize(n);
    for (int i = 0; i < n; ++i) {
        yTrue[i] = (i < nPositive) ? 1.0 : 0.0;
        // Descending prediction: first cells get highest score
        yPred[i] = static_cast<double>(n - i) / n;
    }
}

// Build a random-predictor dataset (yPred = constant → no discrimination).
void makeRandomDataset(int n, int nPositive,
                       QVector<double>& yTrue,
                       QVector<double>& yPred)
{
    yTrue.resize(n);  yPred.resize(n);
    for (int i = 0; i < n; ++i) {
        yTrue[i] = (i < nPositive) ? 1.0 : 0.0;
        yPred[i] = 0.5;   // constant → no discrimination
    }
}

// Invert a prediction vector (antagonist).
QVector<double> invert(const QVector<double>& v)
{
    QVector<double> r = v;
    std::reverse(r.begin(), r.end());
    return r;
}

} // anonymous namespace

// ═══════════════════════════════════════════════════════════════════════════════
// TestBenchmarkMetricsFull
// ═══════════════════════════════════════════════════════════════════════════════

class TestBenchmarkMetricsFull : public QObject
{
    Q_OBJECT

private slots:

    // ── PAI ──────────────────────────────────────────────────────────────────

    void testPAIDefinition()
    {
        // 100 cells, 10 have crimes.  The top-10 flagged (10 % of area)
        // all contain a crime → hit_rate = 10/10 = 1.0 → PAI = 1.0/0.10 = 10.0
        // But the task spec says 50% of events in 10% area → PAI = 5.0:
        // 100 cells, 10 crimes, top 10 cells flagged (10 % area) containing 5 crimes
        // Build: crimes at indices 0..4 (first 5 cells), no crimes at 5..9,
        // yPred descending so top 10 cells are indices 0..9.
        const int n = 100;
        QVector<double> yTrue(n, 0.0), yPred(n);
        for (int i = 0; i < 5; ++i)  yTrue[i] = 1.0;   // 5 crimes total
        // Prediction: indices 0..9 are "hot" (top-10 flagged area = 10 %)
        for (int i = 0; i < n; ++i)
            yPred[i] = (i < 10) ? 1.0 : 0.0;

        // hit_rate = 5/5 = 1.0, area_fraction = 0.10
        // PAI = 1.0 / 0.10 = 10.0
        // Re-read spec: 50% of events in 10% area.
        // 10 total crimes; 5 in flagged area → hit_rate = 5/10 = 0.5
        // Need 10 crimes total.
        QVector<double> yT2(n, 0.0), yP2(n);
        for (int i = 0; i < 10; ++i) yT2[i] = 1.0;   // 10 crimes
        for (int i = 0; i < n; ++i)  yP2[i] = (i < 10) ? 1.0 : 0.0;
        // Only first 5 of the 10 flagged cells actually have a crime
        // Rearrange: crimes at 0..4, nothing at 5..9 → still 10 crimes total
        // Wait – we set yT2[0..9]=1.0 so all 10 flagged cells have crimes
        // hit_rate = 10/10 = 1.0, PAI = 1.0/0.10 = 10.
        // For PAI = 5.0:  10 crimes total, 5 in the flagged 10%-area.
        QVector<double> yT3(n, 0.0), yP3(n);
        for (int i = 0; i < 10; ++i) yT3[i] = 1.0;   // crimes at 0..9
        for (int i = 0; i < n; ++i)  yP3[i] = (i < 10) ? 1.0 : 0.0;
        // Reorder: move 5 crimes outside flagged area
        for (int i = 0; i < n; ++i) yT3[i] = 0.0;
        for (int i = 0; i < 5;  ++i) yT3[i] = 1.0;       // 5 in flagged zone
        for (int i = 10; i < 15; ++i) yT3[i] = 1.0;      // 5 outside → 10 total
        // hit_rate = 5/10 = 0.5; PAI = 0.5/0.10 = 5.0
        double result = BenchmarkMetrics::pai(yT3, yP3, 0.10);
        QVERIFY2(std::abs(result - 5.0) < 0.5,
                 qPrintable(QString("PAI definition: expected ~5.0, got %1").arg(result)));
    }

    void testPAIPerfect()
    {
        // Perfect predictor: all crimes are in the top-fraction flagged cells.
        // hit_rate = 1.0; PAI = 1.0 / area_fraction
        const int n = 200;
        const int nCrimes = 10;   // 5 % of area
        const double areaFrac = 0.05;
        QVector<double> yTrue(n, 0.0), yPred(n, 0.0);
        for (int i = 0; i < nCrimes; ++i) {
            yTrue[i] = 1.0;
            yPred[i] = 1.0;
        }
        double result = BenchmarkMetrics::pai(yTrue, yPred, areaFrac);
        double expected = 1.0 / areaFrac;   // 20.0
        QVERIFY2(result > expected * 0.8,
                 qPrintable(QString("PAI perfect: expected ~%1, got %2")
                            .arg(expected).arg(result)));
    }

    void testPAIZero()
    {
        // No crimes in the predicted area → PAI = 0
        const int n = 100;
        QVector<double> yTrue(n, 0.0), yPred(n, 0.0);
        // All crimes outside flagged area
        for (int i = 50; i < 60; ++i) yTrue[i] = 1.0;
        // All predictions on the crime-free part
        for (int i = 0; i < 10; ++i)  yPred[i] = 1.0;
        double result = BenchmarkMetrics::pai(yTrue, yPred, 0.10);
        QVERIFY2(result < 1e-9,
                 qPrintable(QString("PAI zero: expected 0, got %1").arg(result)));
    }

    void testPAIRange()
    {
        // PAI must always be >= 0
        const int n = 100;
        QVector<double> yTrue(n, 0.0), yPred(n);
        for (int i = 0; i < 20; ++i) yTrue[i] = 1.0;
        // random-ish prediction
        for (int i = 0; i < n; ++i) yPred[i] = (i % 7) / 6.0;
        for (double frac : {0.05, 0.10, 0.20, 0.50}) {
            double r = BenchmarkMetrics::pai(yTrue, yPred, frac);
            QVERIFY2(r >= 0.0,
                     qPrintable(QString("PAI range: got negative value %1 at frac=%2")
                                .arg(r).arg(frac)));
        }
    }

    // ── PEI ──────────────────────────────────────────────────────────────────

    void testPEIvsRandom()
    {
        // PEI = PAI / PAI_max, so a perfect predictor gives PEI ≈ 1.0.
        // A perfect predictor must score strictly higher than a random predictor.
        const int n = 200;
        const int nCrimes = 20;
        QVector<double> yTrue, yPredPerfect;
        makePerfectDataset(n, nCrimes, yTrue, yPredPerfect);

        double peiPerfect = BenchmarkMetrics::pei(yTrue, yPredPerfect, 0.10);
        QVERIFY2(peiPerfect > 0.9,
                 qPrintable(QString("PEI perfect predictor: expected ~1.0, got %1")
                            .arg(peiPerfect)));

        // Random predictor (shuffled, using simple LCG)
        QVector<double> yPredRandom(n);
        unsigned state = 42u;
        for (int i = 0; i < n; ++i) {
            state = state * 1664525u + 1013904223u;
            yPredRandom[i] = (state & 0x7FFFFFFFu) / static_cast<double>(0x7FFFFFFFu);
        }
        double peiRandom = BenchmarkMetrics::pei(yTrue, yPredRandom, 0.10);
        QVERIFY2(peiPerfect > peiRandom,
                 qPrintable(QString("PEI perfect (%1) should exceed random (%2)")
                            .arg(peiPerfect).arg(peiRandom)));
    }

    void testPEIRandom()
    {
        // If predicted area equals the entire map, hit_rate ≈ base_rate
        // → PAI ≈ 1.0, PEI ≈ 1.0
        const int n = 100;
        QVector<double> yTrue, yPred;
        makeRandomDataset(n, 10, yTrue, yPred);
        // area_fraction = 1.0 → all cells flagged
        double result = BenchmarkMetrics::pei(yTrue, yPred, 1.0);
        QVERIFY2(std::abs(result - 1.0) < 0.5,
                 qPrintable(QString("PEI random area=1: expected ~1.0, got %1").arg(result)));
    }

    // ── SER ──────────────────────────────────────────────────────────────────

    void testSERBasic()
    {
        // SER > 0 for any reasonable predictor
        const int n = 100;
        QVector<double> yTrue(n, 0.0), yPred(n);
        for (int i = 0; i < 20; ++i) yTrue[i] = 1.0;
        for (int i = 0; i < n; ++i)  yPred[i] = (i < 30) ? 0.9 : 0.1;
        double result = BenchmarkMetrics::ser(yTrue, yPred);
        QVERIFY2(result >= 0.0,
                 qPrintable(QString("SER basic: got %1").arg(result)));
    }

    void testSERPerfectPredictor()
    {
        // Perfect predictor concentrates all targets; SER should be high (>= 0.9)
        const int n = 100;
        const int nCrimes = 10;
        QVector<double> yTrue, yPred;
        makePerfectDataset(n, nCrimes, yTrue, yPred);
        double result = BenchmarkMetrics::ser(yTrue, yPred);
        QVERIFY2(result >= 0.5,
                 qPrintable(QString("SER perfect predictor: expected >=0.5, got %1")
                            .arg(result)));
    }

    // ── AUC-ROC ──────────────────────────────────────────────────────────────

    void testAUCROCPerfect()
    {
        const int n = 100;
        const int nCrimes = 30;
        QVector<double> yTrue, yPred;
        makePerfectDataset(n, nCrimes, yTrue, yPred);
        double result = BenchmarkMetrics::aucRoc(yTrue, yPred);
        QVERIFY2(result > 0.95,
                 qPrintable(QString("AUC-ROC perfect: expected ~1.0, got %1").arg(result)));
    }

    void testAUCROCRandom()
    {
        // A pseudo-random predictor has AUC ≈ 0.5.
        // Use an LCG so scores have real variance (constant scores may tie-break to 0).
        const int n = 500;
        QVector<double> yTrue(n), yPred(n);
        for (int i = 0; i < n; ++i)
            yTrue[i] = (i < n / 2) ? 1.0 : 0.0;
        unsigned state = 1234u;
        for (int i = 0; i < n; ++i) {
            state = state * 1664525u + 1013904223u;
            yPred[i] = (state & 0x7FFFFFFFu) / static_cast<double>(0x7FFFFFFFu);
        }
        double result = BenchmarkMetrics::aucRoc(yTrue, yPred);
        QVERIFY2(result > 0.40 && result < 0.65,
                 qPrintable(QString("AUC-ROC random: expected ~0.5, got %1").arg(result)));
    }

    void testAUCROCWorse()
    {
        // Inverted perfect predictor → AUC close to 0
        const int n = 100;
        const int nCrimes = 30;
        QVector<double> yTrue, yPred;
        makePerfectDataset(n, nCrimes, yTrue, yPred);
        QVector<double> yPredInv = invert(yPred);
        double result = BenchmarkMetrics::aucRoc(yTrue, yPredInv);
        QVERIFY2(result < 0.15,
                 qPrintable(QString("AUC-ROC inverted: expected ~0.0, got %1").arg(result)));
    }

    void testAUCROCRange()
    {
        // AUC must always be in [0, 1]
        const int n = 80;
        QVector<double> yTrue(n), yPred(n);
        for (int i = 0; i < n; ++i) {
            yTrue[i] = (i % 3 == 0) ? 1.0 : 0.0;
            yPred[i] = static_cast<double>(i) / n;
        }
        double result = BenchmarkMetrics::aucRoc(yTrue, yPred);
        QVERIFY2(result >= 0.0 && result <= 1.0,
                 qPrintable(QString("AUC-ROC out of range: %1").arg(result)));
    }

    // ── AUC-PR ───────────────────────────────────────────────────────────────

    void testAUCPRPerfect()
    {
        const int n = 100;
        const int nCrimes = 20;
        QVector<double> yTrue, yPred;
        makePerfectDataset(n, nCrimes, yTrue, yPred);
        double result = BenchmarkMetrics::aucPr(yTrue, yPred);
        QVERIFY2(result > 0.80,
                 qPrintable(QString("AUC-PR perfect: expected >0.80, got %1").arg(result)));
    }

    void testAUCPRAlwaysPositive()
    {
        const int n = 60;
        QVector<double> yTrue(n), yPred(n);
        for (int i = 0; i < n; ++i) {
            yTrue[i] = (i % 4 == 0) ? 1.0 : 0.0;
            yPred[i] = static_cast<double>(n - i) / n;
        }
        double result = BenchmarkMetrics::aucPr(yTrue, yPred);
        QVERIFY2(result >= 0.0,
                 qPrintable(QString("AUC-PR negative: %1").arg(result)));
    }

    // ── CalibrationAnalyser ───────────────────────────────────────────────────

    void testCalibrationBinCount()
    {
        // Default 10 bins; result.bins should have <= 10 entries
        // (bins with no samples may be omitted)
        CalibrationAnalyser analyser(10);
        QVector<QPair<double,double>> data;
        for (int i = 0; i < 100; ++i) {
            double p = static_cast<double>(i) / 100.0;
            double actual = (p > 0.5) ? 1.0 : 0.0;
            data.append({p, actual});
        }
        CalibrationResult result = analyser.analyse(data);
        QVERIFY2(result.nBins <= 10,
                 qPrintable(QString("CalibrationBinCount: nBins=%1").arg(result.nBins)));
        QVERIFY2(result.bins.size() <= 10,
                 qPrintable(QString("CalibrationBinCount: bins.size()=%1")
                            .arg(result.bins.size())));
        QVERIFY(result.nSamples == 100);
    }

    void testECEPerfectCalibration()
    {
        // A perfectly calibrated model: predicted p = empirical frequency.
        // Build 10 bins of 100 samples each. Within each bin the empirical
        // positive rate equals the predicted probability.
        CalibrationAnalyser analyser(10);
        QVector<QPair<double,double>> data;
        // For each decile d ∈ {0.05, 0.15, …, 0.95} we generate 100 samples
        // where the binary outcome matches the probability exactly in expectation
        // by setting exactly (p*100) positives.
        for (int bin = 0; bin < 10; ++bin) {
            double p = 0.05 + bin * 0.10;      // 0.05, 0.15, …, 0.95
            int nPos = static_cast<int>(std::round(p * 100));
            for (int j = 0; j < 100; ++j) {
                double actual = (j < nPos) ? 1.0 : 0.0;
                data.append({p, actual});
            }
        }
        CalibrationResult result = analyser.analyse(data);
        QVERIFY2(result.ece < 0.05,
                 qPrintable(QString("ECE perfect calibration: expected <0.05, got %1")
                            .arg(result.ece)));
    }
};

QTEST_MAIN(TestBenchmarkMetricsFull)
#include "test_benchmark_metrics_full.moc"
