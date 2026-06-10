// test_benchmark_full_report.cpp
// Tests BenchmarkMetrics full API: PAI, PEI, SER, AUC-PR, AUC-ROC,
// MAE, RMSE, Brier Score, fullReport, and hintQuality.
#include <QTest>
#include "benchmark/BenchmarkMetrics.h"
#include <cmath>

class BenchmarkFullReportTest : public QObject
{
    Q_OBJECT

private:
    // Perfect binary predictor: all positives at top, all negatives below
    static void perfectData(QVector<double>& yTrue, QVector<double>& yPred, int n = 100)
    {
        for (int i = 0; i < n/2; ++i) { yTrue.append(1.0); yPred.append(0.9); }
        for (int i = 0; i < n/2; ++i) { yTrue.append(0.0); yPred.append(0.1); }
    }

    // Uniform predictor: always 0.5
    static void uniformData(QVector<double>& yTrue, QVector<double>& yPred, int n = 100)
    {
        for (int i = 0; i < n/2; ++i) { yTrue.append(1.0); yPred.append(0.5); }
        for (int i = 0; i < n/2; ++i) { yTrue.append(0.0); yPred.append(0.5); }
    }

private slots:

    // ── 1. PAI: perfect predictor at 50% area → PAI = 2.0 ───────────────────
    void testPAIPerfect()
    {
        QVector<double> yTrue, yPred;
        perfectData(yTrue, yPred, 100);
        const double p = BenchmarkMetrics::pai(yTrue, yPred, 0.5);
        QVERIFY2(p >= 1.9,
                 qPrintable(QStringLiteral("Perfect PAI@50%% expected ~2.0, got %1").arg(p)));
    }

    // ── 2. PEI in [0, 1] ─────────────────────────────────────────────────────
    void testPEIRange()
    {
        QVector<double> yTrue, yPred;
        perfectData(yTrue, yPred);
        const double pei = BenchmarkMetrics::pei(yTrue, yPred, 0.5);
        QVERIFY2(pei >= 0.0 && pei <= 1.0,
                 qPrintable(QStringLiteral("PEI %1 must be in [0,1]").arg(pei)));
    }

    // ── 3. SER: perfect predictor → SER near 1.0 ────────────────────────────
    void testSERPerfect()
    {
        QVector<double> yTrue, yPred;
        perfectData(yTrue, yPred);
        const double ser = BenchmarkMetrics::ser(yTrue, yPred);
        QVERIFY2(ser >= 0.5 && ser <= 1.0,
                 qPrintable(QStringLiteral("Perfect SER %1 should be in [0.5,1]").arg(ser)));
    }

    // ── 4. AUC-PR: perfect → near 1.0 ───────────────────────────────────────
    void testAUCPRPerfect()
    {
        QVector<double> yTrue, yPred;
        perfectData(yTrue, yPred);
        const double auc = BenchmarkMetrics::aucPr(yTrue, yPred);
        QVERIFY2(auc >= 0.8,
                 qPrintable(QStringLiteral("Perfect AUC-PR %1 expected >= 0.8").arg(auc)));
    }

    // ── 5. AUC-ROC: perfect → 1.0 ────────────────────────────────────────────
    void testAUCROCPerfect()
    {
        QVector<double> yTrue, yPred;
        perfectData(yTrue, yPred);
        const double auc = BenchmarkMetrics::aucRoc(yTrue, yPred);
        QVERIFY2(std::abs(auc - 1.0) < 0.01,
                 qPrintable(QStringLiteral("Perfect AUC-ROC expected 1.0, got %1").arg(auc)));
    }

    // ── 6. MAE: constant offset of 0.5 → MAE = 0.5 ──────────────────────────
    void testMAEConstantOffset()
    {
        QVector<double> actual, pred;
        for (int i = 0; i < 20; ++i) {
            actual.append(static_cast<double>(i));
            pred.append(static_cast<double>(i) + 0.5);
        }
        const double maeV = BenchmarkMetrics::mae(actual, pred);
        QVERIFY2(std::abs(maeV - 0.5) < 0.01,
                 qPrintable(QStringLiteral("MAE expected 0.5, got %1").arg(maeV)));
    }

    // ── 7. RMSE >= MAE ───────────────────────────────────────────────────────
    void testRMSEvsMAE()
    {
        QVector<double> actual, pred;
        actual << 0.0 << 1.0 << 2.0 << 10.0;
        pred   << 0.1 << 1.1 << 2.1 <<  9.0;
        const double rmseV = BenchmarkMetrics::rmse(actual, pred);
        const double maeV  = BenchmarkMetrics::mae(actual, pred);
        QVERIFY2(rmseV >= maeV,
                 qPrintable(QStringLiteral("RMSE %1 should >= MAE %2").arg(rmseV).arg(maeV)));
    }

    // ── 8. Brier score: perfect → 0 ─────────────────────────────────────────
    void testBrierScorePerfect()
    {
        QVector<double> yTrue, yPred;
        for (int i = 0; i < 50; ++i) { yTrue.append(1.0); yPred.append(1.0 - 1e-7); }
        for (int i = 0; i < 50; ++i) { yTrue.append(0.0); yPred.append(1e-7); }
        const double bs = BenchmarkMetrics::brierScore(yTrue, yPred);
        QVERIFY2(bs < 0.01,
                 qPrintable(QStringLiteral("Near-perfect Brier score %1 should be < 0.01").arg(bs)));
    }

    // ── 9. fullReport: nSamples == input size ────────────────────────────────
    void testFullReportNSamples()
    {
        QVector<double> yTrue, yPred;
        perfectData(yTrue, yPred, 100);
        const auto rpt = BenchmarkMetrics::fullReport(yTrue, yPred);
        QVERIFY2(rpt.nSamples == 100,
                 qPrintable(QStringLiteral("fullReport nSamples %1 expected 100").arg(rpt.nSamples)));
    }

    // ── 10. hintQuality: perfect ranks → MRR = 1.0 ──────────────────────────
    void testHintQualityPerfect()
    {
        QVector<int> ranks;
        for (int i = 0; i < 20; ++i) ranks.append(1);  // always rank 1
        const auto hq = BenchmarkMetrics::hintQuality(ranks, 5);
        QVERIFY2(std::abs(hq.mrr - 1.0) < 0.01,
                 qPrintable(QStringLiteral("Perfect MRR expected 1.0, got %1").arg(hq.mrr)));
        QVERIFY2(std::abs(hq.precisionAt1 - 1.0) < 0.01,
                 qPrintable(QStringLiteral("Perfect P@1 expected 1.0, got %1").arg(hq.precisionAt1)));
    }
};

QTEST_MAIN(BenchmarkFullReportTest)
#include "test_benchmark_full_report.moc"
