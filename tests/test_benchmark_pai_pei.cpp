// test_benchmark_pai_pei.cpp
// Validates BenchmarkMetrics PAI, PEI, AUC-ROC, and BrierScore calculations
// against known mathematical properties.
#include <QTest>
#include "benchmark/BenchmarkMetrics.h"
#include <numeric>
#include <cmath>

class BenchmarkPAIPEITest : public QObject
{
    Q_OBJECT

private:
    // Perfect predictor: predicted values ranked match actual crime locations
    static QVector<double> perfectPred(int n, int nCrimes)
    {
        QVector<double> pred(n, 0.0);
        for (int i = 0; i < nCrimes; ++i) pred[i] = 1.0;
        return pred;
    }

    static QVector<double> uniformPred(int n, double val = 0.5)
    {
        return QVector<double>(n, val);
    }

private slots:

    // ── 1. Perfect predictor PAI at 5% → crime_rate / 0.05 ──────────────────
    void testPAIPerfectPredictor()
    {
        // 10 cells, 2 crimes, predict the 2 correctly
        const int N = 100, NC = 5;  // 5% crime cells
        QVector<double> yTrue(N, 0.0), yPred(N, 0.0);
        for (int i = 0; i < NC; ++i) {
            yTrue[i] = 1.0;
            yPred[i] = 1.0;  // predict the crime cells
        }
        for (int i = NC; i < N; ++i) yPred[i] = 0.0;

        // At 5% area flagged: all 5 crimes captured → hit_rate = 1.0
        // PAI = 1.0 / 0.05 = 20.0
        const double pai = BenchmarkMetrics::pai(yTrue, yPred, 0.05);
        QVERIFY2(std::abs(pai - 20.0) < 0.5,
                 qPrintable(QStringLiteral("Perfect PAI5%% expected ~20.0, got %1").arg(pai)));
    }

    // ── 2. Random predictor PAI ≈ 1.0 ────────────────────────────────────────
    void testPAIRandomPredictor()
    {
        const int N = 1000;
        QVector<double> yTrue(N, 0.0), yPred(N, 0.0);
        // 10% crime rate, uniform random predictions
        for (int i = 0; i < N / 10; ++i) yTrue[i] = 1.0;
        for (int i = 0; i < N; ++i)       yPred[i] = static_cast<double>(i % 10) / 10.0;

        // Random predictor PAI ~ 1.0 (hit rate ≈ area fraction)
        const double pai = BenchmarkMetrics::pai(yTrue, yPred, 0.10);
        QVERIFY2(pai >= 0.5 && pai <= 3.0,
                 qPrintable(QStringLiteral("Random PAI10%% expected ~1-2, got %1").arg(pai)));
    }

    // ── 3. PEI in [0, 1] for any predictor ──────────────────────────────────
    void testPEIBounded()
    {
        const int N = 200;
        QVector<double> yTrue(N, 0.0), yPred(N, 0.0);
        for (int i = 0; i < 20; ++i) { yTrue[i] = 1.0; yPred[i] = 0.8 - i * 0.01; }
        for (int i = 20; i < N; ++i) yPred[i] = static_cast<double>(i) / N * 0.2;

        const double pei = BenchmarkMetrics::pei(yTrue, yPred, 0.10);
        QVERIFY2(pei >= 0.0 && pei <= 1.0 + 1e-9,
                 qPrintable(QStringLiteral("PEI %1 must be in [0,1]").arg(pei)));
    }

    // ── 4. AUC-ROC = 1.0 for perfect predictor ───────────────────────────────
    void testAUCROCPerfect()
    {
        const int N = 100, NC = 20;
        QVector<double> yTrue(N, 0.0), yPred(N, 0.0);
        for (int i = 0; i < NC; ++i) { yTrue[i] = 1.0; yPred[i] = 1.0; }
        for (int i = NC; i < N; ++i) yPred[i] = 0.0;

        const double auc = BenchmarkMetrics::aucRoc(yTrue, yPred);
        QVERIFY2(std::abs(auc - 1.0) < 0.05,
                 qPrintable(QStringLiteral("Perfect AUC-ROC expected ~1.0, got %1").arg(auc)));
    }

    // ── 5. AUC-ROC in (0.4, 0.6) for anti-correlated predictor ──────────────
    void testAUCROCRandom()
    {
        // Predict high for true negatives, low for true positives → AUC ~0
        // This proves AUC is sensitive to the ordering
        const int N = 100, NC = 50;
        QVector<double> yTrue(N, 0.0), yPred(N, 0.0);
        for (int i = 0; i < NC; ++i) yTrue[i] = 1.0;
        // Anti-predictor: give high score to non-crime cells
        for (int i = 0; i < NC; ++i)  yPred[i] = 0.2;   // crimes → low score
        for (int i = NC; i < N; ++i)  yPred[i] = 0.8;   // non-crimes → high score

        const double auc = BenchmarkMetrics::aucRoc(yTrue, yPred);
        // Perfect anti-predictor → AUC ≈ 0 (i.e., < 0.5)
        QVERIFY2(auc < 0.5,
                 qPrintable(QStringLiteral("Anti-predictor AUC-ROC expected < 0.5, got %1").arg(auc)));
    }

    // ── 6. Brier score = 0 for perfect predictor ─────────────────────────────
    void testBrierScorePerfect()
    {
        const int N = 100;
        QVector<double> yTrue(N, 0.0);
        for (int i = 0; i < 50; ++i) yTrue[i] = 1.0;

        // Perfect: predict exactly what happened
        const double bs = BenchmarkMetrics::brierScore(yTrue, yTrue);
        QVERIFY2(std::abs(bs) < 1e-9,
                 qPrintable(QStringLiteral("Perfect Brier score expected 0, got %1").arg(bs)));
    }

    // ── 7. Brier score = 0.25 for uniform 0.5 predictor ─────────────────────
    void testBrierScoreUniform()
    {
        // 50% true crimes, predict 0.5 everywhere → BS = 0.5² * N / N = 0.25
        const int N = 200;
        QVector<double> yTrue(N, 0.0);
        for (int i = 0; i < N / 2; ++i) yTrue[i] = 1.0;
        const QVector<double> yPred = uniformPred(N, 0.5);

        const double bs = BenchmarkMetrics::brierScore(yTrue, yPred);
        QVERIFY2(std::abs(bs - 0.25) < 0.01,
                 qPrintable(QStringLiteral("Uniform Brier score expected ~0.25, got %1").arg(bs)));
    }

    // ── 8. MAE = 0 for perfect predictor ─────────────────────────────────────
    void testMAEPerfect()
    {
        QVector<double> yTrue = { 1.0, 0.0, 1.0, 0.0, 1.0 };
        const double mae = BenchmarkMetrics::mae(yTrue, yTrue);
        QVERIFY2(std::abs(mae) < 1e-9,
                 qPrintable(QStringLiteral("Perfect MAE expected 0, got %1").arg(mae)));
    }

    // ── 9. RMSE >= MAE always ─────────────────────────────────────────────────
    void testRMSEGeqMAE()
    {
        QVector<double> yTrue = { 1.0, 0.0, 1.0, 0.5, 0.8, 0.2, 0.0, 1.0 };
        QVector<double> yPred = { 0.8, 0.1, 0.7, 0.6, 0.5, 0.4, 0.3, 0.9 };

        const double mae  = BenchmarkMetrics::mae(yTrue, yPred);
        const double rmse = BenchmarkMetrics::rmse(yTrue, yPred);

        QVERIFY2(rmse >= mae - 1e-9,
                 qPrintable(QStringLiteral("RMSE (%1) must be >= MAE (%2)").arg(rmse).arg(mae)));
    }

    // ── 10. fullReport PAI > 1 for a reasonably good predictor ───────────────
    void testFullReportPAIGood()
    {
        const int N = 500, NC = 25;  // 5% crime rate
        QVector<double> yTrue(N, 0.0), yPred(N, 0.0);
        // Good predictor: top 50 cells flagged; 20 of the 25 crimes are there
        for (int i = 0; i < NC; ++i)   yTrue[i] = 1.0;
        for (int i = 0; i < 20; ++i)   yPred[i] = 0.9;   // true positive cells
        for (int i = 20; i < 50; ++i)  yPred[i] = 0.7;   // false positive cells
        for (int i = NC; i < N; ++i)   yPred[i] = 0.01;  // predicted negative

        const auto report = BenchmarkMetrics::fullReport(yTrue, yPred);
        QVERIFY2(report.pai10pct > 1.0,
                 qPrintable(QStringLiteral("Good predictor PAI@10%% expected > 1.0, got %1")
                    .arg(report.pai10pct)));
        QVERIFY2(report.aucRoc > 0.5,
                 qPrintable(QStringLiteral("Good predictor AUC-ROC expected > 0.5, got %1")
                    .arg(report.aucRoc)));
        QVERIFY2(!report.reportText().isEmpty(), "reportText() must be non-empty");
    }
};

QTEST_MAIN(BenchmarkPAIPEITest)
#include "test_benchmark_pai_pei.moc"
