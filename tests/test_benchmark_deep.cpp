// test_benchmark_deep.cpp — Deep unit tests for BenchmarkMetrics
//
// Tests exact formula correctness, monotonicity, Jensen's inequality (RMSE≥MAE),
// and information-retrieval metrics using hand-calculated reference values.

#include <QTest>
#include <QCoreApplication>
#include <QVector>
#include "benchmark/BenchmarkMetrics.h"

class TestBenchmarkDeep : public QObject {
    Q_OBJECT

    // ── Helpers ──────────────────────────────────────────────────────────────

    // Deterministic pseudo-random values via LCG
    static QVector<double> lcg(int n, unsigned seed = 42u) {
        QVector<double> v(n);
        unsigned state = seed;
        for (int i = 0; i < n; ++i) {
            state = state * 1664525u + 1013904223u;
            v[i]  = (state & 0x7FFFFFFFu) / static_cast<double>(0x7FFFFFFFu);
        }
        return v;
    }

private slots:

    // ── 1. testAUCROCPerfectClassifier ────────────────────────────────────────
    // A classifier that perfectly separates positives from negatives → AUC = 1.0.

    void testAUCROCPerfectClassifier() {
        const QVector<double> yTrue = {1.0, 1.0, 1.0, 0.0, 0.0, 0.0};
        const QVector<double> yPred = {0.9, 0.8, 0.7, 0.3, 0.2, 0.1};

        const double result = BenchmarkMetrics::aucRoc(yTrue, yPred);

        QVERIFY2(qAbs(result - 1.0) < 1e-9,
                 qPrintable(QStringLiteral("AUC-ROC perfect classifier should be 1.0, got %1")
                            .arg(result)));
    }

    // ── 2. testAUCROCRandomClassifier ─────────────────────────────────────────
    // Random predictions uncorrelated with labels → AUC ≈ 0.5 ± 0.1.

    void testAUCROCRandomClassifier() {
        const int n = 200;
        QVector<double> yTrue(n, 0.0);
        for (int i = 0; i < n / 2; ++i) yTrue[i] = 1.0;  // first half positive

        // Noise from LCG with a different seed than any existing test
        const QVector<double> yPred = lcg(n, 99991u);

        const double result = BenchmarkMetrics::aucRoc(yTrue, yPred);

        QVERIFY2(result > 0.4 && result < 0.6,
                 qPrintable(QStringLiteral("AUC-ROC random classifier should be ~0.5 ± 0.1, got %1")
                            .arg(result)));
    }

    // ── 3. testBrierScorePerfect ──────────────────────────────────────────────
    // Predicting exact binary outcomes gives Brier score = 0.0.

    void testBrierScorePerfect() {
        const QVector<double> yTrue = {1.0, 0.0, 1.0, 0.0, 1.0};
        const QVector<double> yPred = {1.0, 0.0, 1.0, 0.0, 1.0};

        const double result = BenchmarkMetrics::brierScore(yTrue, yPred);

        QVERIFY2(qAbs(result) < 1e-9,
                 qPrintable(QStringLiteral("Brier score perfect should be 0.0, got %1")
                            .arg(result)));
    }

    // ── 4. testBrierScoreWorstCase ────────────────────────────────────────────
    // Always predicting 0 when truth is 1 (worst case) → Brier = 1.0.

    void testBrierScoreWorstCase() {
        // All outcomes are 1; all predictions are 0 → (0−1)² = 1 for every sample
        const QVector<double> yTrue = {1.0, 1.0, 1.0, 1.0, 1.0};
        const QVector<double> yPred = {0.0, 0.0, 0.0, 0.0, 0.0};

        const double result = BenchmarkMetrics::brierScore(yTrue, yPred);

        QVERIFY2(qAbs(result - 1.0) < 1e-9,
                 qPrintable(QStringLiteral("Brier score worst case should be 1.0, got %1")
                            .arg(result)));
    }

    // ── 5. testPAIDefinition ──────────────────────────────────────────────────
    // Verify PAI = (crime_fraction / area_fraction) with a hand-calculated case.
    //   n=4, crimes at indices 0 and 1, flag top 50% → hitRate=1.0 → PAI=2.0.

    void testPAIDefinition() {
        const QVector<double> yTrue = {1.0, 1.0, 0.0, 0.0};
        const QVector<double> yPred = {0.9, 0.8, 0.2, 0.1};

        // Flagged = top 2 cells (50% of 4). Both are crime cells.
        // hitRate = 2/2 = 1.0; PAI = 1.0 / 0.5 = 2.0
        const double result = BenchmarkMetrics::pai(yTrue, yPred, 0.5);

        QVERIFY2(qAbs(result - 2.0) < 1e-9,
                 qPrintable(QStringLiteral("PAI expected 2.0, got %1").arg(result)));
    }

    // ── 6. testPEIMonotonicity ────────────────────────────────────────────────
    // For a partially-correct predictor, expanding the flagged area captures
    // more crimes and PEI increases.
    //   Hand-calc: crimes at indices 0,2,4,7; pred descending 0.9…0.0
    //   PEI(area=0.2) = 0.50, PEI(area=0.5) = 0.75  → 0.75 > 0.50 ✓

    void testPEIMonotonicity() {
        const QVector<double> yTrue = {1,0,1,0,1,0,0,1,0,0};
        const QVector<double> yPred = {0.9,0.8,0.7,0.6,0.5,0.4,0.3,0.2,0.1,0.0};

        const double peiSmall = BenchmarkMetrics::pei(yTrue, yPred, 0.2);
        const double peiLarge = BenchmarkMetrics::pei(yTrue, yPred, 0.5);

        QVERIFY2(peiLarge > peiSmall,
                 qPrintable(QStringLiteral("PEI(0.5)=%1 should > PEI(0.2)=%2")
                            .arg(peiLarge).arg(peiSmall)));
    }

    // ── 7. testMAEMeanSquaredError ────────────────────────────────────────────
    // Verify the MAE formula against a hand-calculated example.
    //   yTrue=[1,2,3,4], yPred=[2,2,2,2] → errors=[1,0,1,2] → MAE=4/4=1.0

    void testMAEMeanSquaredError() {
        const QVector<double> yTrue = {1.0, 2.0, 3.0, 4.0};
        const QVector<double> yPred = {2.0, 2.0, 2.0, 2.0};

        const double result = BenchmarkMetrics::mae(yTrue, yPred);

        QVERIFY2(qAbs(result - 1.0) < 1e-9,
                 qPrintable(QStringLiteral("MAE expected 1.0, got %1").arg(result)));
    }

    // ── 8. testRMSEAlwaysGEMAE ───────────────────────────────────────────────
    // By the power-mean inequality: RMSE = sqrt(E[e²]) ≥ E[|e|] = MAE.
    // Verified on two independent inputs.

    void testRMSEAlwaysGEMAE() {
        // Case A: deterministic LCG inputs
        {
            const QVector<double> yt = lcg(40, 7u);
            const QVector<double> yp = lcg(40, 13u);
            const double maeA  = BenchmarkMetrics::mae(yt, yp);
            const double rmseA = BenchmarkMetrics::rmse(yt, yp);
            QVERIFY2(rmseA >= maeA - 1e-9,
                     qPrintable(QStringLiteral("RMSE=%1 should >= MAE=%2 (LCG input)")
                                .arg(rmseA).arg(maeA)));
        }
        // Case B: hand-calculated — errors = [1, 2, 3]
        // MAE = 2.0, RMSE = sqrt(14/3) ≈ 2.160
        {
            const QVector<double> yt = {0.0, 0.0, 0.0};
            const QVector<double> yp = {1.0, 2.0, 3.0};
            const double maeB  = BenchmarkMetrics::mae(yt, yp);
            const double rmseB = BenchmarkMetrics::rmse(yt, yp);

            QVERIFY2(qAbs(maeB - 2.0) < 1e-9,
                     qPrintable(QStringLiteral("MAE expected 2.0, got %1").arg(maeB)));
            QVERIFY2(rmseB >= maeB - 1e-9,
                     qPrintable(QStringLiteral("RMSE=%1 should >= MAE=%2 (hand input)")
                                .arg(rmseB).arg(maeB)));
            // RMSE ≈ 2.160 > 2.000 when errors are not equal
            QVERIFY2(rmseB > maeB,
                     qPrintable(QStringLiteral("RMSE=%1 should strictly > MAE=%2 for unequal errors")
                                .arg(rmseB).arg(maeB)));
        }
    }

    // ── 9. testAUCPRPerfect ───────────────────────────────────────────────────
    // Perfect predictor — all positives ranked first → AUC-PR = 1.0.
    // Hand-verified: recallVec=[0,0.5,1,1,1], precVec=[1,1,1,0.67,0.5]
    // trapz = 0.5 + 0.5 + 0 + 0 = 1.0

    void testAUCPRPerfect() {
        const QVector<double> yTrue = {1.0, 1.0, 0.0, 0.0};
        const QVector<double> yPred = {0.9, 0.8, 0.2, 0.1};

        const double result = BenchmarkMetrics::aucPr(yTrue, yPred);

        QVERIFY2(qAbs(result - 1.0) < 1e-9,
                 qPrintable(QStringLiteral("AUC-PR perfect should be 1.0, got %1")
                            .arg(result)));
    }

    // ── 10. testMRR ───────────────────────────────────────────────────────────
    // Mean Reciprocal Rank: if the true item is at rank k, MRR = 1/k.

    void testMRR() {
        // Single case: true item at rank 1 → MRR = 1.0
        {
            const auto r = BenchmarkMetrics::hintQuality({1}, 5);
            QVERIFY2(qAbs(r.mrr - 1.0) < 1e-9,
                     qPrintable(QStringLiteral("MRR at rank 1 should be 1.0, got %1")
                                .arg(r.mrr)));
        }
        // Single case: true item at rank 2 → MRR = 0.5
        {
            const auto r = BenchmarkMetrics::hintQuality({2}, 5);
            QVERIFY2(qAbs(r.mrr - 0.5) < 1e-9,
                     qPrintable(QStringLiteral("MRR at rank 2 should be 0.5, got %1")
                                .arg(r.mrr)));
        }
        // Single case: true item at rank 4 → MRR = 0.25
        {
            const auto r = BenchmarkMetrics::hintQuality({4}, 5);
            QVERIFY2(qAbs(r.mrr - 0.25) < 1e-9,
                     qPrintable(QStringLiteral("MRR at rank 4 should be 0.25, got %1")
                                .arg(r.mrr)));
        }
        // Multiple cases: ranks [1, 2] → MRR = (1.0 + 0.5) / 2 = 0.75
        {
            const auto r = BenchmarkMetrics::hintQuality({1, 2}, 5);
            QVERIFY2(qAbs(r.mrr - 0.75) < 1e-9,
                     qPrintable(QStringLiteral("MRR for ranks [1,2] should be 0.75, got %1")
                                .arg(r.mrr)));
        }
    }
};

// ─── main ─────────────────────────────────────────────────────────────────────

QTEST_GUILESS_MAIN(TestBenchmarkDeep)

#include "test_benchmark_deep.moc"
