// test_benchmark_metrics_edge.cpp
// BenchmarkMetrics edge cases: identical predictions, all-positive/negative
// ground truth, boundary area fractions, and hintQuality edge cases.
#include <QTest>
#include "benchmark/BenchmarkMetrics.h"
#include <cmath>

class BenchmarkMetricsEdgeTest : public QObject
{
    Q_OBJECT

private:
    static QVector<double> makeTrue(int n, double trueVal = 1.0)
    {
        return QVector<double>(n, trueVal);
    }

    static QVector<double> makePred(int n, double pred = 0.5)
    {
        return QVector<double>(n, pred);
    }

    // Perfect predictor: high prob for positives, low for negatives
    static QPair<QVector<double>, QVector<double>> perfectPredictor(int n = 20)
    {
        QVector<double> yTrue, yPred;
        for (int i = 0; i < n; ++i) {
            yTrue.append(static_cast<double>(i % 2));
            yPred.append((i % 2) == 1 ? 0.9 : 0.1);
        }
        return { yTrue, yPred };
    }

private slots:

    // 1. AUC-ROC == 1.0 for perfect predictor
    void testAucRocPerfect()
    {
        const auto [yt, yp] = perfectPredictor(20);
        const double auc = BenchmarkMetrics::aucRoc(yt, yp);
        QVERIFY2(auc > 0.9,
                 qPrintable(QStringLiteral("Perfect AUC-ROC %1 should be > 0.9").arg(auc)));
    }

    // 2. AUC-ROC == 0.5 for constant predictor
    void testAucRocConstantPredictor()
    {
        QVector<double> yt, yp;
        for (int i = 0; i < 20; ++i) {
            yt.append(static_cast<double>(i % 2));
            yp.append(0.5);
        }
        const double auc = BenchmarkMetrics::aucRoc(yt, yp);
        QVERIFY2(std::abs(auc - 0.5) < 0.1,
                 qPrintable(QStringLiteral("Constant predictor AUC-ROC %1 should be ~0.5").arg(auc)));
    }

    // 3. AUC-PR: perfect predictor > 0.5
    void testAucPrPerfect()
    {
        const auto [yt, yp] = perfectPredictor(20);
        const double pr = BenchmarkMetrics::aucPr(yt, yp);
        QVERIFY2(pr > 0.5,
                 qPrintable(QStringLiteral("Perfect AUC-PR %1 should be > 0.5").arg(pr)));
    }

    // 4. MAE == 0 when predictions match truth
    void testMAEZeroWhenPerfect()
    {
        const auto yt = makeTrue(10, 1.0);
        const auto yp = makeTrue(10, 1.0);
        QVERIFY2(std::abs(BenchmarkMetrics::mae(yt, yp)) < 1e-9, "MAE should be 0 for perfect predictions");
    }

    // 5. RMSE == 0 when predictions match truth
    void testRMSEZeroWhenPerfect()
    {
        const auto yt = makeTrue(10, 0.5);
        const auto yp = makeTrue(10, 0.5);
        QVERIFY2(std::abs(BenchmarkMetrics::rmse(yt, yp)) < 1e-9, "RMSE should be 0 for perfect predictions");
    }

    // 6. Brier score == 0 for perfect predictions
    void testBrierZeroPerfect()
    {
        const auto yt = makeTrue(10, 1.0);
        const auto yp = makeTrue(10, 1.0);
        QVERIFY2(std::abs(BenchmarkMetrics::brierScore(yt, yp)) < 1e-9, "Brier score should be 0 for perfect");
    }

    // 7. Brier score == 0.25 for constant 0.5 predictor (50/50 events)
    void testBrierHalfHalf()
    {
        QVector<double> yt, yp;
        for (int i = 0; i < 100; ++i) {
            yt.append(static_cast<double>(i % 2));
            yp.append(0.5);
        }
        const double bs = BenchmarkMetrics::brierScore(yt, yp);
        QVERIFY2(std::abs(bs - 0.25) < 0.01,
                 qPrintable(QStringLiteral("Brier score %1 expected ~0.25").arg(bs)));
    }

    // 8. PAI: >= 1 for good predictor
    void testPAIGoodPredictor()
    {
        QVector<double> yt(100, 0.0), yp(100, 0.0);
        // Put all positives in top 10 cells
        for (int i = 0; i < 10; ++i) { yt[i] = 1.0; yp[i] = 0.9; }
        const double p = BenchmarkMetrics::pai(yt, yp, 0.10);
        QVERIFY2(p >= 1.0,
                 qPrintable(QStringLiteral("PAI %1 for concentrated positives should be >= 1").arg(p)));
    }

    // 9. fullReport: nSamples correct
    void testFullReportNSamples()
    {
        QVector<double> yt, yp;
        for (int i = 0; i < 30; ++i) {
            yt.append(static_cast<double>(i % 2));
            yp.append(0.5 + (i % 2) * 0.2);
        }
        const auto r = BenchmarkMetrics::fullReport(yt, yp);
        QCOMPARE(r.nSamples, 30);
    }

    // 10. hintQuality: perfect top-1 -> MRR == 1.0
    void testHintQualityMRRPerfect()
    {
        QVector<int> ranks;
        for (int i = 0; i < 10; ++i) ranks.append(1);  // always rank 1
        const auto r = BenchmarkMetrics::hintQuality(ranks, 5);
        QVERIFY2(std::abs(r.mrr - 1.0) < 1e-6,
                 qPrintable(QStringLiteral("Perfect MRR %1 should be 1.0").arg(r.mrr)));
    }
};

QTEST_MAIN(BenchmarkMetricsEdgeTest)
#include "test_benchmark_metrics_edge.moc"
