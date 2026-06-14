// test_benchmark_metrics_deep8.cpp — Deep audit iteration 30: BenchmarkMetrics
// PEI bounds, SER, brierScore, fullReport, hint NDCG.
#include <QtTest/QtTest>
#include <cmath>
#include "benchmark/BenchmarkMetrics.h"

class BenchmarkMetricsDeep8Test : public QObject
{
    Q_OBJECT

private slots:

    void testPeiBetweenZeroAndOne()
    {
        QVector<double> yTrue = { 1, 1, 0, 0, 0, 0, 0, 0, 0, 0 };
        QVector<double> yPred = { 0.95, 0.85, 0.2, 0.15, 0.1, 0.08, 0.06, 0.04, 0.02, 0.01 };

        const double pei = BenchmarkMetrics::pei(yTrue, yPred, 0.10);
        QVERIFY(pei >= 0.0 && pei <= 1.0 + 1e-6);
    }

    void testSerFiniteForRankedPredictions()
    {
        QVector<double> yTrue = { 1, 0, 1, 0, 1, 0 };
        QVector<double> yPred = { 0.9, 0.1, 0.8, 0.2, 0.7, 0.3 };
        const double ser = BenchmarkMetrics::ser(yTrue, yPred);
        QVERIFY(std::isfinite(ser));
    }

    void testBrierScoreLowerForBetterCalibration()
    {
        QVector<double> yTrue = { 1, 0, 1, 0 };
        QVector<double> good  = { 0.9, 0.1, 0.85, 0.15 };
        QVector<double> poor  = { 0.5, 0.5, 0.5, 0.5 };

        QVERIFY(BenchmarkMetrics::brierScore(yTrue, good)
                < BenchmarkMetrics::brierScore(yTrue, poor));
    }

    void testFullReportPopulatesFields()
    {
        QVector<double> yTrue = { 1, 0, 1, 0, 0, 0, 0, 0, 0, 0 };
        QVector<double> yPred = { 0.9, 0.2, 0.8, 0.1, 0.05, 0.04, 0.03, 0.02, 0.01, 0.005 };

        const auto rep = BenchmarkMetrics::fullReport(yTrue, yPred);
        QCOMPARE(rep.nSamples, 10);
        QVERIFY(rep.aucRoc >= 0.0 && rep.aucRoc <= 1.0);
        QVERIFY(!rep.reportText().isEmpty());
    }

    void testHintNdcgPositiveWithEarlyHit()
    {
        const QVector<int> ranks = { 1, 3 };
        const auto result = BenchmarkMetrics::hintQuality(ranks, 5);
        QVERIFY(result.ndcg > 0.0);
        QVERIFY(result.precisionAt1 > 0.0);
    }
};

QTEST_GUILESS_MAIN(BenchmarkMetricsDeep8Test)
#include "test_benchmark_metrics_deep8.moc"
