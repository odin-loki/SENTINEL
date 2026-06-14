// test_benchmark_metrics_deep7.cpp — Deep audit iteration 26: BenchmarkMetrics
// PAI ordering, AUC bounds, reportText, hint benchmark MRR.
#include <QtTest/QtTest>
#include <cmath>
#include "benchmark/BenchmarkMetrics.h"

class BenchmarkMetricsDeep7Test : public QObject
{
    Q_OBJECT

private slots:

    void testPaiHigherAtSmallerAreaWhenConcentrated()
    {
        QVector<double> yTrue = { 1, 1, 0, 0, 0, 0, 0, 0, 0, 0 };
        QVector<double> yPred = { 0.9, 0.8, 0.1, 0.05, 0.04, 0.03, 0.02, 0.01, 0.005, 0.001 };

        const double pai5  = BenchmarkMetrics::pai(yTrue, yPred, 0.05);
        const double pai20 = BenchmarkMetrics::pai(yTrue, yPred, 0.20);
        QVERIFY2(pai5 >= pai20,
                 qPrintable(QStringLiteral("pai5=%1 pai20=%2").arg(pai5).arg(pai20)));
    }

    void testAucRocPerfectClassifier()
    {
        QVector<double> yTrue = { 0, 0, 1, 1 };
        QVector<double> yPred = { 0.1, 0.2, 0.8, 0.9 };
        const double auc = BenchmarkMetrics::aucRoc(yTrue, yPred);
        QVERIFY(auc >= 0.99);
    }

    void testReportTextNonEmpty()
    {
        BenchmarkReport rep;
        rep.pai10pct = 1.5;
        rep.aucRoc   = 0.75;
        rep.nSamples = 50;
        QVERIFY(!rep.reportText().trimmed().isEmpty());
    }

    void testMaeRmseZeroForPerfectPredictions()
    {
        QVector<double> y = { 1.0, 2.0, 3.0 };
        QCOMPARE(BenchmarkMetrics::mae(y, y), 0.0);
        QCOMPARE(BenchmarkMetrics::rmse(y, y), 0.0);
    }

    void testHintBenchmarkMrrWithTopHit()
    {
        const QVector<int> ranks = { 2, 1 };
        const auto result = BenchmarkMetrics::hintQuality(ranks, 5);
        QVERIFY(result.mrr > 0.0);
        QVERIFY(result.nCases == 2);
    }
};

QTEST_GUILESS_MAIN(BenchmarkMetricsDeep7Test)
#include "test_benchmark_metrics_deep7.moc"
