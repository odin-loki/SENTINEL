// test_benchmark_metrics_deep3.cpp
// Exhaustive verification of BenchmarkMetrics formulas.
//
// Covers:
//   PAI  — perfect prediction (10.0), uniform distribution (≈ 1.0), edge cases
//   AUC-ROC — perfect (1.0), worst (0.0), random (0.5)
//   AUC-PR  — perfect, empty positive set
//   RMSE    — known-value check
//   MAE     — known-value check
//   Brier   — perfect (0.0), always-0.5 (0.25)
//   Log-loss — near-perfect (≈ 0), all-wrong (> 30), empty
//   MRR / hintQuality — known rank list

#include <QTest>
#include <cmath>

#include "benchmark/BenchmarkMetrics.h"

class TestBenchmarkMetricsDeep3 : public QObject
{
    Q_OBJECT

private slots:

    // ── PAI ──────────────────────────────────────────────────────────────────

    void testPaiPerfect10pct()
    {
        // 100 cells, 10 crimes in the top 10% (highest predicted cells).
        // Construct: crimes at indices 0..9, yPred strictly decreasing so that
        // sorted order is [0,1,...,99] → top 10 cells are exactly the 10 crimes.
        // PAI = (10/10) / 0.10 = 10.0
        QVector<double> yTrue(100, 0.0);
        QVector<double> yPred(100);
        for (int i = 0; i < 100; ++i) {
            yPred[i] = 1.0 - (i / 100.0);   // strictly decreasing: 1.00, 0.99, ...
        }
        for (int i = 0; i < 10; ++i)
            yTrue[i] = 1.0;   // crimes at the top-predicted cells

        const double result = BenchmarkMetrics::pai(yTrue, yPred, 0.10);
        QVERIFY2(qAbs(result - 10.0) < 1e-9,
                 qPrintable(QStringLiteral("PAI perfect at 10%% should be 10.0, got %1").arg(result)));
    }

    void testPaiUniformDistributionApproximatelyOne()
    {
        // 100 cells, 10 crimes uniformly spread: exactly 1 crime per decile.
        // Predictions strictly decrease (0.99, 0.98, ...).
        // Sorted order: [0,1,...,99].
        // Crimes at positions 0,10,20,...,90 → top 10% includes pos 0..9 → 1 crime.
        // hitRate = 1/10 = 0.10, PAI = 0.10/0.10 = 1.0 exactly.
        QVector<double> yTrue(100, 0.0);
        QVector<double> yPred(100);
        for (int i = 0; i < 100; ++i)
            yPred[i] = 1.0 - (i / 100.0);
        for (int i = 0; i < 10; ++i)
            yTrue[i * 10] = 1.0;   // 1 crime per decile

        const double result = BenchmarkMetrics::pai(yTrue, yPred, 0.10);
        QVERIFY2(qAbs(result - 1.0) < 1e-9,
                 qPrintable(QStringLiteral("PAI uniform distribution at 10%% should be 1.0, got %1").arg(result)));
    }

    void testPaiAllCrimesAtBottom()
    {
        // All crimes at the lowest-predicted cells → PAI at 10% should be 0.
        // nFlagged=1 (max(1, round(10*0.10)) = max(1,1)=1 for n=10 cells)
        // Let's use 100 cells; crimes at positions 90..99 (low predictions).
        QVector<double> yTrue(100, 0.0);
        QVector<double> yPred(100);
        for (int i = 0; i < 100; ++i)
            yPred[i] = 1.0 - (i / 100.0);
        for (int i = 90; i < 100; ++i)
            yTrue[i] = 1.0;

        const double result = BenchmarkMetrics::pai(yTrue, yPred, 0.10);
        // Top 10% = positions 0..9 → no crimes → hitRate = 0 → PAI = 0
        QVERIFY2(qAbs(result) < 1e-9,
                 qPrintable(QStringLiteral("PAI with crimes at bottom should be 0.0, got %1").arg(result)));
    }

    void testPaiEmptyVectors()
    {
        QCOMPARE(BenchmarkMetrics::pai({}, {}, 0.10), 0.0);
    }

    void testPaiZeroAreaFraction()
    {
        QVector<double> yTrue = {1.0, 0.0, 1.0, 0.0};
        QVector<double> yPred = {0.9, 0.8, 0.7, 0.6};
        QCOMPARE(BenchmarkMetrics::pai(yTrue, yPred, 0.0), 0.0);
    }

    void testPaiAllZeroTrue()
    {
        // No crimes in yTrue → total crimes = 0 → PAI = 0 (guard)
        const QVector<double> yTrue(20, 0.0);
        const QVector<double> yPred(20, 0.5);
        QCOMPARE(BenchmarkMetrics::pai(yTrue, yPred, 0.10), 0.0);
    }

    // ── AUC-ROC ──────────────────────────────────────────────────────────────

    void testAucRocPerfectClassifier()
    {
        // Positives all score higher than negatives → AUC = 1.0
        QVector<double> yTrue = {1.0, 1.0, 1.0, 1.0, 1.0, 0.0, 0.0, 0.0, 0.0, 0.0};
        QVector<double> yPred = {0.9, 0.8, 0.7, 0.6, 0.5, 0.4, 0.3, 0.2, 0.1, 0.0};

        const double result = BenchmarkMetrics::aucRoc(yTrue, yPred);
        QVERIFY2(qAbs(result - 1.0) < 1e-9,
                 qPrintable(QStringLiteral("Perfect AUC-ROC should be 1.0, got %1").arg(result)));
    }

    void testAucRocWorstClassifier()
    {
        // All negatives score higher than positives → AUC = 0.0
        // After sorting by yPred desc: negatives appear first, then positives.
        // FPR rises from 0 to 1 while TPR stays at 0, then TPR rises at FPR=1.
        // Trapezoid area = 0.
        QVector<double> yTrue = {0.0, 0.0, 0.0, 0.0, 0.0, 1.0, 1.0, 1.0, 1.0, 1.0};
        QVector<double> yPred = {0.9, 0.8, 0.7, 0.6, 0.5, 0.4, 0.3, 0.2, 0.1, 0.0};

        const double result = BenchmarkMetrics::aucRoc(yTrue, yPred);
        QVERIFY2(qAbs(result) < 1e-9,
                 qPrintable(QStringLiteral("Worst-case AUC-ROC should be 0.0, got %1").arg(result)));
    }

    void testAucRocRandomClassifier()
    {
        // Construct a dataset that gives AUC = 0.5 analytically.
        //
        // 4 events: pos at ranks 1 & 4, neg at ranks 2 & 3 (1=highest pred).
        //   yTrue = [1, 0, 0, 1], yPred = [0.9, 0.7, 0.5, 0.3]
        //
        // Walk sorted by yPred desc:
        //   i=0: yTrue=1 → tp=1, fp=0 → fpr=0.0, tpr=0.5
        //   i=1: yTrue=0 → tp=1, fp=1 → fpr=0.5, tpr=0.5
        //   i=2: yTrue=0 → tp=1, fp=2 → fpr=1.0, tpr=0.5
        //   i=3: yTrue=1 → tp=2, fp=2 → fpr=1.0, tpr=1.0
        //
        // fprVec = [0.0, 0.0, 0.5, 1.0, 1.0]
        // tprVec = [0.0, 0.5, 0.5, 0.5, 1.0]
        //
        // trapz = 0.5*(0.5-0.0)*(0.5+0.5) + 0.5*(1.0-0.5)*(0.5+0.5) = 0.25 + 0.25 = 0.5
        QVector<double> yTrue = {1.0, 0.0, 0.0, 1.0};
        QVector<double> yPred = {0.9, 0.7, 0.5, 0.3};

        const double result = BenchmarkMetrics::aucRoc(yTrue, yPred);
        QVERIFY2(qAbs(result - 0.5) < 1e-9,
                 qPrintable(QStringLiteral("AUC-ROC for symmetric dataset should be 0.5, got %1").arg(result)));
    }

    void testAucRocAllPositive()
    {
        // nNeg == 0 → return 0.5 (undefined / guard)
        QVector<double> yTrue = {1.0, 1.0, 1.0};
        QVector<double> yPred = {0.9, 0.5, 0.1};
        QCOMPARE(BenchmarkMetrics::aucRoc(yTrue, yPred), 0.5);
    }

    void testAucRocAllNegative()
    {
        // nPos == 0 → return 0.5 (undefined / guard)
        QVector<double> yTrue = {0.0, 0.0, 0.0};
        QVector<double> yPred = {0.9, 0.5, 0.1};
        QCOMPARE(BenchmarkMetrics::aucRoc(yTrue, yPred), 0.5);
    }

    // ── AUC-PR ───────────────────────────────────────────────────────────────

    void testAucPrPerfectClassifier()
    {
        // All positives score higher than all negatives → AUC-PR = 1.0
        QVector<double> yTrue = {1.0, 1.0, 1.0, 0.0, 0.0, 0.0};
        QVector<double> yPred = {0.9, 0.8, 0.7, 0.3, 0.2, 0.1};

        const double result = BenchmarkMetrics::aucPr(yTrue, yPred);
        // precision stays 1.0 while recall goes 0→1; trapz = 1.0
        QVERIFY2(qAbs(result - 1.0) < 1e-9,
                 qPrintable(QStringLiteral("Perfect AUC-PR should be 1.0, got %1").arg(result)));
    }

    void testAucPrNoPositives()
    {
        // nPos == 0 → return 0.0 (guard)
        QVector<double> yTrue = {0.0, 0.0, 0.0};
        QVector<double> yPred = {0.9, 0.5, 0.1};
        QCOMPARE(BenchmarkMetrics::aucPr(yTrue, yPred), 0.0);
    }

    // ── RMSE ─────────────────────────────────────────────────────────────────

    void testRmseKnownValues()
    {
        // predictions = [0, 1], actuals = [1, 0]
        // errors = [-1, 1], MSE = (1 + 1)/2 = 1.0, RMSE = 1.0
        const QVector<double> yTrue = {1.0, 0.0};
        const QVector<double> yPred = {0.0, 1.0};
        const double result = BenchmarkMetrics::rmse(yTrue, yPred);
        QVERIFY2(qAbs(result - 1.0) < 1e-9,
                 qPrintable(QStringLiteral("RMSE([0,1],[1,0]) should be 1.0, got %1").arg(result)));
    }

    void testRmsePerfect()
    {
        const QVector<double> yTrue = {0.3, 0.7, 0.5};
        const QVector<double> yPred = {0.3, 0.7, 0.5};
        QVERIFY(qAbs(BenchmarkMetrics::rmse(yTrue, yPred)) < 1e-12);
    }

    void testRmseEmpty()
    {
        QCOMPARE(BenchmarkMetrics::rmse({}, {}), 0.0);
    }

    void testRmseKnownFourValues()
    {
        // errors = [1, 2, 3, 4], MSE = (1+4+9+16)/4 = 30/4 = 7.5, RMSE = sqrt(7.5)
        const QVector<double> yTrue = {0.0, 0.0, 0.0, 0.0};
        const QVector<double> yPred = {1.0, 2.0, 3.0, 4.0};
        const double expected = std::sqrt(7.5);
        const double result = BenchmarkMetrics::rmse(yTrue, yPred);
        QVERIFY2(qAbs(result - expected) < 1e-9,
                 qPrintable(QStringLiteral("RMSE([1,2,3,4],[0,0,0,0]) should be %1, got %2")
                                .arg(expected).arg(result)));
    }

    // ── MAE ──────────────────────────────────────────────────────────────────

    void testMaeKnownValues()
    {
        // |errors| = [1, 2, 3], MAE = 6/3 = 2.0
        const QVector<double> yTrue = {0.0, 0.0, 0.0};
        const QVector<double> yPred = {1.0, 2.0, 3.0};
        const double result = BenchmarkMetrics::mae(yTrue, yPred);
        QVERIFY2(qAbs(result - 2.0) < 1e-9,
                 qPrintable(QStringLiteral("MAE([1,2,3],[0,0,0]) should be 2.0, got %1").arg(result)));
    }

    void testMaePerfect()
    {
        const QVector<double> yTrue = {0.2, 0.5, 0.8};
        const QVector<double> yPred = {0.2, 0.5, 0.8};
        QVERIFY(qAbs(BenchmarkMetrics::mae(yTrue, yPred)) < 1e-12);
    }

    void testMaeEmpty()
    {
        QCOMPARE(BenchmarkMetrics::mae({}, {}), 0.0);
    }

    // ── Brier Score ───────────────────────────────────────────────────────────

    void testBrierScorePerfect()
    {
        // Perfect binary predictions
        const QVector<double> yTrue = {1.0, 1.0, 0.0, 0.0};
        const QVector<double> yPred = {1.0, 1.0, 0.0, 0.0};
        const double result = BenchmarkMetrics::brierScore(yTrue, yPred);
        QVERIFY2(qAbs(result) < 1e-12,
                 qPrintable(QStringLiteral("Brier score perfect should be 0.0, got %1").arg(result)));
    }

    void testBrierScoreAlwaysHalf()
    {
        // Predict 0.5 for every event (binary labels) → Brier = 0.25
        // For each: (0.5 - 1)^2 = 0.25 or (0.5 - 0)^2 = 0.25 → mean = 0.25
        const int n = 100;
        QVector<double> yTrue(n), yPred(n, 0.5);
        for (int i = 0; i < n; ++i)
            yTrue[i] = (i % 2 == 0) ? 1.0 : 0.0;

        const double result = BenchmarkMetrics::brierScore(yTrue, yPred);
        QVERIFY2(qAbs(result - 0.25) < 1e-9,
                 qPrintable(QStringLiteral("Brier always-0.5 should be 0.25, got %1").arg(result)));
    }

    void testBrierScoreWorstCase()
    {
        // All predictions inverted → Brier = 1.0
        const QVector<double> yTrue = {1.0, 0.0, 1.0, 0.0};
        const QVector<double> yPred = {0.0, 1.0, 0.0, 1.0};
        const double result = BenchmarkMetrics::brierScore(yTrue, yPred);
        QVERIFY2(qAbs(result - 1.0) < 1e-9,
                 qPrintable(QStringLiteral("Worst-case Brier should be 1.0, got %1").arg(result)));
    }

    void testBrierScoreEmpty()
    {
        QCOMPARE(BenchmarkMetrics::brierScore({}, {}), 0.0);
    }

    // ── Log Loss ─────────────────────────────────────────────────────────────

    void testLogLossNearPerfectPredictions()
    {
        // Predictions very close to labels → log loss ≈ 0
        // yPred clamped to [eps, 1-eps]; using 1.0 → clamped to 1-1e-15
        const QVector<double> yTrue = {1.0, 1.0, 0.0, 0.0};
        const QVector<double> yPred = {1.0, 1.0, 0.0, 0.0};

        const double result = BenchmarkMetrics::logLoss(yTrue, yPred);
        // With eps=1e-15 clamping: log(1-1e-15) ≈ -1e-15 per term → ~1e-15 total
        QVERIFY2(result < 1e-10,
                 qPrintable(QStringLiteral("Near-perfect log loss should be ≈ 0, got %1").arg(result)));
    }

    void testLogLossAllWrongPredictions()
    {
        // All predictions completely wrong → large log loss
        const QVector<double> yTrue = {1.0, 1.0, 0.0, 0.0};
        const QVector<double> yPred = {0.0, 0.0, 1.0, 1.0};

        const double result = BenchmarkMetrics::logLoss(yTrue, yPred);
        // Each term: -log(1e-15) ≈ 34.5 → mean ≈ 34.5
        QVERIFY2(result > 30.0,
                 qPrintable(QStringLiteral("All-wrong log loss should be > 30, got %1").arg(result)));
    }

    void testLogLossHalfConfidence()
    {
        // All predictions = 0.5 → logLoss = -log(0.5) = log(2) ≈ 0.693
        const int n = 100;
        QVector<double> yTrue(n), yPred(n, 0.5);
        for (int i = 0; i < n; ++i)
            yTrue[i] = (i % 2 == 0) ? 1.0 : 0.0;

        const double result = BenchmarkMetrics::logLoss(yTrue, yPred);
        const double expected = std::log(2.0);  // ≈ 0.6931
        QVERIFY2(qAbs(result - expected) < 1e-9,
                 qPrintable(QStringLiteral("Log loss at 0.5 should be log(2)=%1, got %2")
                                .arg(expected).arg(result)));
    }

    void testLogLossEmpty()
    {
        QCOMPARE(BenchmarkMetrics::logLoss({}, {}), 0.0);
    }

    void testLogLossKnownMixedCase()
    {
        // yTrue=[1,0], yPred=[0.9, 0.1]
        // loss = -(1*log(0.9) + 0*log(0.1) + 0*log(0.1) + 1*log(0.9)) / 2
        //       = -(log(0.9) + log(0.9)) / 2 = -log(0.9)
        const QVector<double> yTrue = {1.0, 0.0};
        const QVector<double> yPred = {0.9, 0.1};
        const double expected = -std::log(0.9);
        const double result = BenchmarkMetrics::logLoss(yTrue, yPred);
        QVERIFY2(qAbs(result - expected) < 1e-9,
                 qPrintable(QStringLiteral("Log loss([1,0],[0.9,0.1]) should be %1, got %2")
                                .arg(expected).arg(result)));
    }

    // ── MRR (via hintQuality) ─────────────────────────────────────────────────

    void testMrrKnownRanks()
    {
        // ranks = [1, 2, 3]: MRR = (1/1 + 1/2 + 1/3) / 3
        const QVector<int> ranks = {1, 2, 3};
        const double expected = (1.0/1 + 1.0/2 + 1.0/3) / 3.0;
        const HintBenchmarkResult result = BenchmarkMetrics::hintQuality(ranks, 5);
        QVERIFY2(qAbs(result.mrr - expected) < 1e-9,
                 qPrintable(QStringLiteral("MRR({1,2,3}) should be %1, got %2")
                                .arg(expected).arg(result.mrr)));
        QCOMPARE(result.nCases, 3);
        QVERIFY(qAbs(result.coverage - 1.0) < 1e-9);  // all in top-5
    }

    void testMrrAllTopRank()
    {
        // All at rank 1: MRR = 1.0
        const QVector<int> ranks = {1, 1, 1, 1};
        const HintBenchmarkResult result = BenchmarkMetrics::hintQuality(ranks, 5);
        QVERIFY2(qAbs(result.mrr - 1.0) < 1e-9,
                 qPrintable(QStringLiteral("MRR({1,1,1,1}) should be 1.0, got %1").arg(result.mrr)));
        QVERIFY2(qAbs(result.precisionAt1 - 1.0) < 1e-9, "P@1 should be 1.0");
    }

    void testMrrNoCorrectAnswers()
    {
        // rank = 0 means no correct answer in top-K → MRR = 0.0
        const QVector<int> ranks = {0, 0, 0};
        const HintBenchmarkResult result = BenchmarkMetrics::hintQuality(ranks, 5);
        QVERIFY2(qAbs(result.mrr) < 1e-9,
                 qPrintable(QStringLiteral("MRR with all rank=0 should be 0.0, got %1").arg(result.mrr)));
        QVERIFY2(qAbs(result.coverage) < 1e-9, "Coverage should be 0.0");
        QVERIFY2(qAbs(result.falseLeadRate - 1.0) < 1e-9, "FalseLeadRate should be 1.0");
    }

    void testMrrMixedCase()
    {
        // ranks = [1, 0, 3, 0]: 2 correct, 2 not found
        // MRR = (1 + 0 + 1/3 + 0) / 4 = (4/3) / 4 = 1/3
        const QVector<int> ranks = {1, 0, 3, 0};
        const double expected = (1.0/1 + 0.0 + 1.0/3 + 0.0) / 4.0;
        const HintBenchmarkResult result = BenchmarkMetrics::hintQuality(ranks, 5);
        QVERIFY2(qAbs(result.mrr - expected) < 1e-9,
                 qPrintable(QStringLiteral("MRR({1,0,3,0}) should be %1, got %2")
                                .arg(expected).arg(result.mrr)));
        QVERIFY2(qAbs(result.coverage - 0.5) < 1e-9, "Coverage should be 0.5");
    }

    void testMrrAboveTopK()
    {
        // Rank 6 > topK=5 → treated as not found → MRR contribution = 0
        const QVector<int> ranks = {1, 6};
        const HintBenchmarkResult result = BenchmarkMetrics::hintQuality(ranks, 5);
        // Only rank=1 contributes: MRR = (1/1 + 0) / 2 = 0.5
        QVERIFY2(qAbs(result.mrr - 0.5) < 1e-9,
                 qPrintable(QStringLiteral("MRR({1,6},topK=5) should be 0.5, got %1").arg(result.mrr)));
    }

    void testMrrEmpty()
    {
        const HintBenchmarkResult result = BenchmarkMetrics::hintQuality({}, 5);
        QCOMPARE(result.nCases, 0);
        QVERIFY(qAbs(result.mrr) < 1e-12);
    }

    // ── Precision @ K ────────────────────────────────────────────────────────

    void testPrecisionAtKKnownValues()
    {
        // ranks = [1, 2, 4, 6, 1]
        // P@1: rank==1 → 2 cases → 2/5 = 0.4
        // P@3: rank<=3 → 3 cases → 3/5 = 0.6
        // P@5: rank<=5 → 4 cases → 4/5 = 0.8 (rank=6 is out)
        const QVector<int> ranks = {1, 2, 4, 6, 1};
        const HintBenchmarkResult result = BenchmarkMetrics::hintQuality(ranks, 5);

        QVERIFY2(qAbs(result.precisionAt1 - 0.4) < 1e-9,
                 qPrintable(QStringLiteral("P@1 should be 0.4, got %1").arg(result.precisionAt1)));
        QVERIFY2(qAbs(result.precisionAt3 - 0.6) < 1e-9,
                 qPrintable(QStringLiteral("P@3 should be 0.6, got %1").arg(result.precisionAt3)));
        QVERIFY2(qAbs(result.precisionAt5 - 0.8) < 1e-9,
                 qPrintable(QStringLiteral("P@5 should be 0.8, got %1").arg(result.precisionAt5)));
    }

    // ── Full report sanity ────────────────────────────────────────────────────

    void testFullReportFieldsConsistent()
    {
        // yTrue: 20% positive, predictions rank positives higher
        const int n = 50;
        QVector<double> yTrue(n, 0.0), yPred(n);
        for (int i = 0; i < 10; ++i) yTrue[i] = 1.0;
        for (int i = 0; i < n;  ++i) yPred[i] = 1.0 - (i / static_cast<double>(n));

        const BenchmarkReport rep = BenchmarkMetrics::fullReport(yTrue, yPred);

        QCOMPARE(rep.nSamples, n);
        QVERIFY(rep.pai5pct  >= 0.0);
        QVERIFY(rep.pai10pct >= 0.0);
        QVERIFY(rep.pai20pct >= 0.0);
        QVERIFY(rep.pei10pct >= 0.0);
        QVERIFY(rep.ser      >= 0.0);
        QVERIFY(rep.aucRoc   >= 0.0 && rep.aucRoc <= 1.0);
        QVERIFY(rep.aucPr    >= 0.0 && rep.aucPr  <= 1.0);
        QVERIFY(rep.mae      >= 0.0);
        QVERIFY(rep.rmse     >= 0.0);
        QVERIFY(rep.brierScore >= 0.0 && rep.brierScore <= 1.0);

        // Perfect ranking → AUC-ROC should be 1.0, PAI at 20% should be 5.0
        QVERIFY2(qAbs(rep.aucRoc - 1.0) < 1e-9,
                 qPrintable(QStringLiteral("Full report AUC-ROC should be 1.0, got %1").arg(rep.aucRoc)));
        QVERIFY2(qAbs(rep.pai20pct - 5.0) < 1e-9,
                 qPrintable(QStringLiteral("Full report PAI@20%% should be 5.0, got %1").arg(rep.pai20pct)));

        // reportText() must contain expected labels
        const QString text = rep.reportText();
        QVERIFY(text.contains(QStringLiteral("PAI")));
        QVERIFY(text.contains(QStringLiteral("AUC")));
        QVERIFY(text.contains(QStringLiteral("RMSE")));
    }

    // ── PEI consistency ───────────────────────────────────────────────────────

    void testPeiPerfectPredictionIsOne()
    {
        // Perfect predictor → PEI = 1.0
        QVector<double> yTrue(100, 0.0), yPred(100);
        for (int i = 0; i < 10; ++i) yTrue[i] = 1.0;
        for (int i = 0; i < 100; ++i) yPred[i] = 1.0 - (i / 100.0);

        const double result = BenchmarkMetrics::pei(yTrue, yPred, 0.10);
        QVERIFY2(qAbs(result - 1.0) < 1e-9,
                 qPrintable(QStringLiteral("PEI perfect predictor should be 1.0, got %1").arg(result)));
    }

    void testPeiRangeZeroToOne()
    {
        // For a reasonably-predicted dataset, PEI must be in [0, 1]
        QVector<double> yTrue(20, 0.0), yPred(20);
        for (int i = 0; i < 4; ++i) yTrue[i] = 1.0;
        for (int i = 0; i < 20; ++i) yPred[i] = (i < 10) ? 0.6 : 0.3;  // noisy

        const double result = BenchmarkMetrics::pei(yTrue, yPred, 0.20);
        QVERIFY(result >= 0.0);
        QVERIFY(result <= 1.0 + 1e-9);  // allow tiny floating-point overshoot
    }
};

QTEST_GUILESS_MAIN(TestBenchmarkMetricsDeep3)
#include "test_benchmark_metrics_deep3.moc"
