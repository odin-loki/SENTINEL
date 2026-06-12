// test_benchmark_metrics_deep4.cpp — Iteration 21 deep audit: SER, PEI edge cases,
// hintQuality NDCG, reportText helpers, and vector-size guards.
#include <QTest>
#include <cmath>

#include "benchmark/BenchmarkMetrics.h"

class TestBenchmarkMetricsDeep4 : public QObject
{
    Q_OBJECT

private slots:
    void testSerPerfectPredictorIsOne();
    void testSerRandomPredictorNearZero();
    void testPeiAreaFractionGreaterThanOneReturnsZero();
    void testPaiAreaFractionGreaterThanOneReturnsZero();
    void testSerEmptyAndZeroCrimes();
    void testHintQualityNdcgPerfectRankOne();
    void testHintBenchmarkResultReportText();
    void testVectorSizeMismatchReturnsZero();
};

void TestBenchmarkMetricsDeep4::testSerPerfectPredictorIsOne()
{
    QVector<double> yTrue(100, 0.0);
    QVector<double> yPred(100);
    for (int i = 0; i < 100; ++i)
        yPred[i] = 1.0 - (i / 100.0);
    for (int i = 0; i < 10; ++i)
        yTrue[i] = 1.0;

    const double result = BenchmarkMetrics::ser(yTrue, yPred);
    QVERIFY2(qAbs(result - 1.0) < 1e-6,
             qPrintable(QStringLiteral("Perfect SER should be 1.0, got %1").arg(result)));
}

void TestBenchmarkMetricsDeep4::testSerRandomPredictorNearZero()
{
    // Crimes uniformly spread; predictions uncorrelated with crime locations.
    QVector<double> yTrue(100, 0.0);
    QVector<double> yPred(100);
    for (int i = 0; i < 100; ++i)
        yPred[i] = 1.0 - (i / 100.0);
    for (int i = 0; i < 10; ++i)
        yTrue[i * 10] = 1.0;

    const double result = BenchmarkMetrics::ser(yTrue, yPred);
    QVERIFY2(result >= 0.0 && result < 0.25,
             qPrintable(QStringLiteral("Uniform SER should be near 0, got %1").arg(result)));
}

void TestBenchmarkMetricsDeep4::testPeiAreaFractionGreaterThanOneReturnsZero()
{
    QVector<double> yTrue(10, 0.0);
    QVector<double> yPred(10);
    for (int i = 0; i < 10; ++i) {
        yTrue[i] = (i < 3) ? 1.0 : 0.0;
        yPred[i] = 1.0 - (i / 10.0);
    }

    QCOMPARE(BenchmarkMetrics::pei(yTrue, yPred, 1.5), 0.0);
    QCOMPARE(BenchmarkMetrics::pai(yTrue, yPred, 1.5), 0.0);
}

void TestBenchmarkMetricsDeep4::testPaiAreaFractionGreaterThanOneReturnsZero()
{
    const QVector<double> yTrue = {1.0, 0.0, 1.0, 0.0};
    const QVector<double> yPred = {0.9, 0.8, 0.7, 0.6};
    QCOMPARE(BenchmarkMetrics::pai(yTrue, yPred, 1.01), 0.0);
    QCOMPARE(BenchmarkMetrics::pai(yTrue, yPred, 2.0), 0.0);
}

void TestBenchmarkMetricsDeep4::testSerEmptyAndZeroCrimes()
{
    QCOMPARE(BenchmarkMetrics::ser({}, {}), 0.0);

    const QVector<double> yZero(20, 0.0);
    const QVector<double> yPred(20, 0.5);
    QCOMPARE(BenchmarkMetrics::ser(yZero, yPred), 0.0);
}

void TestBenchmarkMetricsDeep4::testHintQualityNdcgPerfectRankOne()
{
    const QVector<int> ranks = {1, 1, 1};
    const HintBenchmarkResult result = BenchmarkMetrics::hintQuality(ranks, 5);

    QVERIFY2(qAbs(result.ndcg - 1.0) < 1e-9,
             qPrintable(QStringLiteral("NDCG for all rank-1 should be 1.0, got %1").arg(result.ndcg)));
    QVERIFY2(qAbs(result.mrr - 1.0) < 1e-9, "MRR should match NDCG for rank-1");
    QVERIFY2(qAbs(result.precisionAt1 - 1.0) < 1e-9, "P@1 should be 1.0");
}

void TestBenchmarkMetricsDeep4::testHintBenchmarkResultReportText()
{
    const QVector<int> ranks = {1, 3, 0};
    const HintBenchmarkResult result = BenchmarkMetrics::hintQuality(ranks, 5);
    const QString text = result.reportText();

    QCOMPARE(result.nCases, 3);
    QVERIFY(text.contains(QStringLiteral("HintBenchmarkResult")));
    QVERIFY(text.contains(QStringLiteral("P@1")));
    QVERIFY(text.contains(QStringLiteral("NDCG")));
    QVERIFY(text.contains(QStringLiteral("FalseLeadRate")));
    QVERIFY(text.contains(QStringLiteral("n=3")));
}

void TestBenchmarkMetricsDeep4::testVectorSizeMismatchReturnsZero()
{
    const QVector<double> yTrue = {1.0, 0.0, 1.0};
    const QVector<double> yPred = {0.9, 0.1};

    QCOMPARE(BenchmarkMetrics::pai(yTrue, yPred, 0.1), 0.0);
    QCOMPARE(BenchmarkMetrics::pei(yTrue, yPred, 0.1), 0.0);
    QCOMPARE(BenchmarkMetrics::ser(yTrue, yPred), 0.0);
    QCOMPARE(BenchmarkMetrics::aucRoc(yTrue, yPred), 0.0);
    QCOMPARE(BenchmarkMetrics::aucPr(yTrue, yPred), 0.0);
    QCOMPARE(BenchmarkMetrics::mae(yTrue, yPred), 0.0);
    QCOMPARE(BenchmarkMetrics::rmse(yTrue, yPred), 0.0);
    QCOMPARE(BenchmarkMetrics::brierScore(yTrue, yPred), 0.0);
    QCOMPARE(BenchmarkMetrics::logLoss(yTrue, yPred), 0.0);
}

QTEST_GUILESS_MAIN(TestBenchmarkMetricsDeep4)
#include "test_benchmark_metrics_deep4.moc"
