#include <QTest>
#include "benchmark/BenchmarkMetrics.h"
#include <cmath>

class TestBenchmarkMetricsDeep2 : public QObject
{
    Q_OBJECT

private slots:

    void testPaiPerfectPrediction()
    {
        // 5 crimes get high pred, 5 non-crimes get low pred
        // area_fraction=0.5 → nFlagged=5 → all 5 crimes captured → PAI = 1/0.5 = 2
        const QVector<double> yTrue = {1.0, 1.0, 1.0, 1.0, 1.0, 0.0, 0.0, 0.0, 0.0, 0.0};
        const QVector<double> yPred = {0.9, 0.8, 0.7, 0.6, 0.5, 0.4, 0.3, 0.2, 0.1, 0.0};
        const double result = BenchmarkMetrics::pai(yTrue, yPred, 0.5);
        QVERIFY2(qAbs(result - 2.0) < 1e-9,
                 qPrintable(QStringLiteral("Perfect PAI at 50%% should be 2.0, got %1").arg(result)));
    }

    void testPaiEmptyInput()
    {
        QCOMPARE(BenchmarkMetrics::pai({}, {}, 0.1), 0.0);
    }

    void testPaiZeroAreaFraction()
    {
        const QVector<double> yTrue = {1.0, 0.0};
        const QVector<double> yPred = {0.9, 0.1};
        QCOMPARE(BenchmarkMetrics::pai(yTrue, yPred, 0.0), 0.0);
    }

    void testAucRocPerfectClassifier()
    {
        // Positives get high scores, negatives get low → AUC = 1.0
        const QVector<double> yTrue = {1.0, 1.0, 1.0, 1.0, 1.0, 0.0, 0.0, 0.0, 0.0, 0.0};
        const QVector<double> yPred = {0.9, 0.8, 0.7, 0.6, 0.5, 0.4, 0.3, 0.2, 0.1, 0.0};
        const double result = BenchmarkMetrics::aucRoc(yTrue, yPred);
        QVERIFY2(qAbs(result - 1.0) < 1e-9,
                 qPrintable(QStringLiteral("Perfect classifier AUC-ROC should be 1.0, got %1").arg(result)));
    }

    void testAucRocRandomClassifier()
    {
        // Interleaved so predictions are uncorrelated with labels → AUC = 0.5
        // Constructed analytically to give exactly 0.5
        const QVector<double> yTrue = {1.0, 0.0, 0.0, 1.0, 1.0, 0.0, 0.0, 1.0};
        const QVector<double> yPred = {0.9, 0.8, 0.7, 0.6, 0.5, 0.4, 0.3, 0.2};
        const double result = BenchmarkMetrics::aucRoc(yTrue, yPred);
        QVERIFY2(qAbs(result - 0.5) < 1e-9,
                 qPrintable(QStringLiteral("Random classifier AUC-ROC should be 0.5, got %1").arg(result)));
    }

    void testAucRocEmptyInput()
    {
        QCOMPARE(BenchmarkMetrics::aucRoc({}, {}), 0.0);
    }

    void testBrierScorePerfectPredictions()
    {
        // Predictions exactly match labels → BS = 0
        const QVector<double> yTrue = {1.0, 0.0, 1.0, 0.0};
        const QVector<double> yPred = {1.0, 0.0, 1.0, 0.0};
        const double result = BenchmarkMetrics::brierScore(yTrue, yPred);
        QVERIFY2(qAbs(result - 0.0) < 1e-12,
                 qPrintable(QStringLiteral("Perfect predictions → Brier=0, got %1").arg(result)));
    }

    void testBrierScoreWorstPredictions()
    {
        // yTrue=1 predicted as 0, yTrue=0 predicted as 1 → BS = mean(1²+1²) = 1.0
        const QVector<double> yTrue = {1.0, 0.0};
        const QVector<double> yPred = {0.0, 1.0};
        const double result = BenchmarkMetrics::brierScore(yTrue, yPred);
        QVERIFY2(qAbs(result - 1.0) < 1e-12,
                 qPrintable(QStringLiteral("Worst predictions → Brier=1.0, got %1").arg(result)));
    }

    void testBrierScoreEmptyInput()
    {
        QCOMPARE(BenchmarkMetrics::brierScore({}, {}), 0.0);
    }

    void testMaeKnownErrors()
    {
        // Each prediction is exactly 1.0 above ground truth → MAE = 1.0
        const QVector<double> yTrue = {1.0, 2.0, 3.0};
        const QVector<double> yPred = {2.0, 3.0, 4.0};
        const double result = BenchmarkMetrics::mae(yTrue, yPred);
        QVERIFY2(qAbs(result - 1.0) < 1e-9,
                 qPrintable(QStringLiteral("MAE should be 1.0, got %1").arg(result)));
    }

    void testMaeEmptyInput()
    {
        QCOMPARE(BenchmarkMetrics::mae({}, {}), 0.0);
    }

    void testRmseKnownErrors()
    {
        // Each prediction is exactly 1.0 above ground truth → RMSE = 1.0
        const QVector<double> yTrue = {1.0, 2.0, 3.0};
        const QVector<double> yPred = {2.0, 3.0, 4.0};
        const double result = BenchmarkMetrics::rmse(yTrue, yPred);
        QVERIFY2(qAbs(result - 1.0) < 1e-9,
                 qPrintable(QStringLiteral("RMSE should be 1.0, got %1").arg(result)));
    }

    void testRmseEmptyInput()
    {
        QCOMPARE(BenchmarkMetrics::rmse({}, {}), 0.0);
    }

    void testRmseMaeRelation()
    {
        // RMSE >= MAE always (Jensen's inequality)
        const QVector<double> yTrue = {1.0, 2.0, 3.0, 4.0, 5.0};
        const QVector<double> yPred = {1.5, 1.0, 4.0, 3.5, 6.0};
        const double mae  = BenchmarkMetrics::mae(yTrue, yPred);
        const double rmse = BenchmarkMetrics::rmse(yTrue, yPred);
        QVERIFY2(rmse >= mae - 1e-12,
                 qPrintable(QStringLiteral("RMSE (%1) must be >= MAE (%2)").arg(rmse).arg(mae)));
    }

    void testBrierScoreRange()
    {
        // Brier score is always in [0, 1] for binary predictions in [0, 1]
        const QVector<double> yTrue = {1.0, 0.0, 1.0, 0.0, 1.0};
        const QVector<double> yPred = {0.7, 0.3, 0.6, 0.2, 0.8};
        const double result = BenchmarkMetrics::brierScore(yTrue, yPred);
        QVERIFY(result >= 0.0);
        QVERIFY(result <= 1.0);
    }

    void testAucRocRange()
    {
        const QVector<double> yTrue = {1.0, 0.0, 1.0, 0.0};
        const QVector<double> yPred = {0.8, 0.3, 0.7, 0.2};
        const double result = BenchmarkMetrics::aucRoc(yTrue, yPred);
        QVERIFY(result >= 0.0 && result <= 1.0);
    }

    void testAucPrPerfectClassifier()
    {
        // Perfect positive predictions come before negatives → AUC-PR = 1.0
        const QVector<double> yTrue = {1.0, 1.0, 1.0, 0.0, 0.0, 0.0};
        const QVector<double> yPred = {0.9, 0.8, 0.7, 0.3, 0.2, 0.1};
        const double result = BenchmarkMetrics::aucPr(yTrue, yPred);
        QVERIFY2(qAbs(result - 1.0) < 1e-9,
                 qPrintable(QStringLiteral("Perfect classifier AUC-PR should be 1.0, got %1").arg(result)));
    }

    void testPeiLeqOne()
    {
        // PEI should be <= 1.0 for any valid inputs
        const QVector<double> yTrue = {1.0, 1.0, 0.0, 0.0, 1.0};
        const QVector<double> yPred = {0.8, 0.7, 0.3, 0.2, 0.9};
        const double result = BenchmarkMetrics::pei(yTrue, yPred, 0.4);
        QVERIFY2(result <= 1.0 + 1e-9,
                 qPrintable(QStringLiteral("PEI should be <= 1.0, got %1").arg(result)));
        QVERIFY(result >= 0.0);
    }
};

QTEST_GUILESS_MAIN(TestBenchmarkMetricsDeep2)
#include "test_benchmark_metrics_deep2.moc"
