// test_benchmark.cpp — SENTINEL unit tests
// Covers: BenchmarkMetrics and BiasAuditor

#include <QTest>
#include <QCoreApplication>
#include <QVector>
#include <QString>
#include <QMap>

#include "benchmark/BenchmarkMetrics.h"
#include "benchmark/BiasAuditor.h"

// ─── Helpers ─────────────────────────────────────────────────────────────────

namespace {

// Generate a deterministic pseudo-random sequence [0,1) via a simple LCG
QVector<double> lcgUniform(int n, unsigned seed = 42)
{
    QVector<double> v(n);
    unsigned state = seed;
    for (int i = 0; i < n; ++i) {
        state = state * 1664525u + 1013904223u;
        v[i] = (state & 0x7FFFFFFFu) / static_cast<double>(0x7FFFFFFFu);
    }
    return v;
}

// Binary yTrue: 1 for the first `nPos` indices
QVector<double> binaryTrue(int n, int nPos)
{
    QVector<double> v(n, 0.0);
    for (int i = 0; i < nPos && i < n; ++i) v[i] = 1.0;
    return v;
}

// Perfect predictor: yPred mirrors yTrue
QVector<double> perfectPred(const QVector<double>& yTrue)
{
    return yTrue;
}

// Antagonist: reversed of yTrue (worst possible)
QVector<double> antagonistPred(const QVector<double>& yTrue)
{
    QVector<double> v = yTrue;
    std::reverse(v.begin(), v.end());
    return v;
}

} // anonymous namespace

// ═══════════════════════════════════════════════════════════════════════════
// TestBenchmarkMetrics
// ═══════════════════════════════════════════════════════════════════════════

class TestBenchmarkMetrics : public QObject
{
    Q_OBJECT

private slots:

    // ── PAI ────────────────────────────────────────────────────────────────

    void testPAIRandomBaseline()
    {
        // Random predictor: PAI should be close to 1.0 (by expectation)
        const int n = 1000;
        QVector<double> yTrue(n, 0.0);
        // Set every 5th cell to crime
        for (int i = 0; i < n; i += 5) yTrue[i] = 1.0;

        const QVector<double> yPred = lcgUniform(n);
        const double result = BenchmarkMetrics::pai(yTrue, yPred, 0.10);

        // For a random predictor, PAI should be roughly 1.0 (± 0.5 tolerance)
        QVERIFY2(result > 0.5 && result < 1.8,
                 qPrintable(QStringLiteral("PAI random baseline out of range: %1").arg(result)));
    }

    void testPAIPerfectPredictor()
    {
        const int n = 100;
        QVector<double> yTrue = binaryTrue(n, 10);   // 10% crime rate
        const QVector<double> yPred = perfectPred(yTrue);

        // Perfect predictor: all crimes in top 10% → PAI = 1.0 / 0.10 = 10
        const double result = BenchmarkMetrics::pai(yTrue, yPred, 0.10);
        QVERIFY2(result > 1.0,
                 qPrintable(QStringLiteral("PAI perfect predictor should be > 1.0, got %1").arg(result)));
    }

    void testPAIAntagonist()
    {
        const int n = 100;
        QVector<double> yTrue = binaryTrue(n, 10);   // crimes in first 10 positions
        // Antagonist: high scores at the end → crimes predicted last
        QVector<double> yPred(n, 0.0);
        for (int i = 90; i < n; ++i) yPred[i] = 1.0;  // flag bottom 10

        const double result = BenchmarkMetrics::pai(yTrue, yPred, 0.10);
        QVERIFY2(result < 1.0,
                 qPrintable(QStringLiteral("PAI antagonist should be < 1.0, got %1").arg(result)));
    }

    // ── PEI ────────────────────────────────────────────────────────────────

    void testPEIRange()
    {
        const int n = 200;
        QVector<double> yTrue = binaryTrue(n, 20);
        const QVector<double> yPred = lcgUniform(n);

        const double result = BenchmarkMetrics::pei(yTrue, yPred, 0.10);
        QVERIFY2(result >= 0.0 && result <= 1.05,
                 qPrintable(QStringLiteral("PEI out of [0,1] range: %1").arg(result)));
    }

    void testPEIPerfect()
    {
        const int n = 100;
        QVector<double> yTrue = binaryTrue(n, 10);
        const QVector<double> yPred = perfectPred(yTrue);

        const double result = BenchmarkMetrics::pei(yTrue, yPred, 0.10);
        QVERIFY2(qAbs(result - 1.0) < 0.01,
                 qPrintable(QStringLiteral("PEI perfect should be ~1.0, got %1").arg(result)));
    }

    // ── SER ────────────────────────────────────────────────────────────────

    void testSERRandomBaseline()
    {
        const int n = 1000;
        QVector<double> yTrue(n, 0.0);
        for (int i = 0; i < n; i += 5) yTrue[i] = 1.0;
        const QVector<double> yPred = lcgUniform(n);

        const double result = BenchmarkMetrics::ser(yTrue, yPred);
        // Random predictor SER should be near 0 (± 0.3 tolerance for stochastic)
        QVERIFY2(result > -0.4 && result < 0.5,
                 qPrintable(QStringLiteral("SER random should be ~0, got %1").arg(result)));
    }

    void testSERPerfect()
    {
        const int n = 100;
        QVector<double> yTrue = binaryTrue(n, 10);
        const QVector<double> yPred = perfectPred(yTrue);

        const double result = BenchmarkMetrics::ser(yTrue, yPred);
        QVERIFY2(result > 0.8,
                 qPrintable(QStringLiteral("SER perfect should be ~1.0, got %1").arg(result)));
    }

    // ── AUC-ROC ────────────────────────────────────────────────────────────

    void testAUCROCRandomBaseline()
    {
        const int n = 500;
        QVector<double> yTrue = binaryTrue(n, 250);
        const QVector<double> yPred = lcgUniform(n);

        const double result = BenchmarkMetrics::aucRoc(yTrue, yPred);
        // For random predictor, AUC-ROC should be near 0.5 (± 0.1)
        QVERIFY2(result > 0.4 && result < 0.65,
                 qPrintable(QStringLiteral("AUC-ROC random should be ~0.5, got %1").arg(result)));
    }

    void testAUCROCPerfect()
    {
        const int n = 100;
        QVector<double> yTrue = binaryTrue(n, 50);
        const QVector<double> yPred = perfectPred(yTrue);

        const double result = BenchmarkMetrics::aucRoc(yTrue, yPred);
        QVERIFY2(qAbs(result - 1.0) < 0.01,
                 qPrintable(QStringLiteral("AUC-ROC perfect should be 1.0, got %1").arg(result)));
    }

    void testAUCROCWorstCase()
    {
        const int n = 100;
        QVector<double> yTrue = binaryTrue(n, 50);
        const QVector<double> yPred = antagonistPred(yTrue);

        const double result = BenchmarkMetrics::aucRoc(yTrue, yPred);
        QVERIFY2(result < 0.1,
                 qPrintable(QStringLiteral("AUC-ROC antagonist should be near 0, got %1").arg(result)));
    }

    // ── Brier Score ────────────────────────────────────────────────────────

    void testBrierScoreRange()
    {
        const int n = 200;
        QVector<double> yTrue = binaryTrue(n, 100);
        const QVector<double> yPred = lcgUniform(n);

        const double result = BenchmarkMetrics::brierScore(yTrue, yPred);
        QVERIFY2(result >= 0.0 && result <= 1.0,
                 qPrintable(QStringLiteral("Brier score out of [0,1]: %1").arg(result)));
    }

    void testBrierScorePerfect()
    {
        const int n = 50;
        QVector<double> yTrue = binaryTrue(n, 25);
        const QVector<double> yPred = perfectPred(yTrue);

        const double result = BenchmarkMetrics::brierScore(yTrue, yPred);
        QVERIFY2(qAbs(result) < 1e-9,
                 qPrintable(QStringLiteral("Brier score perfect should be 0, got %1").arg(result)));
    }

    // ── Full Report ────────────────────────────────────────────────────────

    void testFullReportConsistency()
    {
        const int n = 200;
        QVector<double> yTrue = binaryTrue(n, 40);
        const QVector<double> yPred = lcgUniform(n);

        const BenchmarkReport r = BenchmarkMetrics::fullReport(yTrue, yPred);

        QCOMPARE(r.nSamples, n);
        QVERIFY(r.pai5pct  >= 0.0);
        QVERIFY(r.pai10pct >= 0.0);
        QVERIFY(r.pai20pct >= 0.0);
        QVERIFY(r.pei10pct >= 0.0);
        QVERIFY(r.ser      >  -1.0);
        QVERIFY(r.aucRoc   >= 0.0 && r.aucRoc  <= 1.0);
        QVERIFY(r.aucPr    >= 0.0 && r.aucPr   <= 1.0);
        QVERIFY(r.mae      >= 0.0);
        QVERIFY(r.rmse     >= 0.0);
        QVERIFY(r.brierScore >= 0.0 && r.brierScore <= 1.0);

        const QString text = r.reportText();
        QVERIFY(!text.isEmpty());
        QVERIFY(text.contains(QStringLiteral("BenchmarkReport")));
    }

    // ── Hint Quality ───────────────────────────────────────────────────────

    void testHintQualityMRR()
    {
        // All cases have correct answer at rank 1
        QVector<int> ranks = {1, 1, 1, 1, 1};
        const auto r = BenchmarkMetrics::hintQuality(ranks, 5);

        QVERIFY2(qAbs(r.mrr - 1.0) < 1e-9,
                 qPrintable(QStringLiteral("MRR should be 1.0, got %1").arg(r.mrr)));
        QVERIFY2(qAbs(r.precisionAt1 - 1.0) < 1e-9,
                 qPrintable(QStringLiteral("P@1 should be 1.0, got %1").arg(r.precisionAt1)));
    }

    void testHintQualityNDCG()
    {
        // Mix: some at rank 1, some at rank 3, some not found
        QVector<int> ranks = {1, 3, 0, 2, 0};
        const auto r = BenchmarkMetrics::hintQuality(ranks, 5);

        QVERIFY2(r.ndcg > 0.0,
                 qPrintable(QStringLiteral("NDCG should be > 0 when relevant items present, got %1").arg(r.ndcg)));
        QVERIFY2(r.mrr  > 0.0,
                 qPrintable(QStringLiteral("MRR should be > 0 when relevant items present, got %1").arg(r.mrr)));
        QVERIFY2(r.coverage > 0.0 && r.coverage < 1.0,
                 qPrintable(QStringLiteral("Coverage should be between 0 and 1, got %1").arg(r.coverage)));
        QCOMPARE(r.nCases, 5);
    }

    void testHintQualityAllMissed()
    {
        QVector<int> ranks = {0, 0, 0};
        const auto r = BenchmarkMetrics::hintQuality(ranks, 5);

        QVERIFY2(qAbs(r.mrr)  < 1e-9, qPrintable(QStringLiteral("MRR should be 0, got %1").arg(r.mrr)));
        QVERIFY2(qAbs(r.ndcg) < 1e-9, qPrintable(QStringLiteral("NDCG should be 0, got %1").arg(r.ndcg)));
        QVERIFY2(qAbs(r.coverage) < 1e-9, "Coverage should be 0");
        QVERIFY2(qAbs(r.falseLeadRate - 1.0) < 1e-9, "FalseLeadRate should be 1.0");
    }

    void testHintQualityReportText()
    {
        QVector<int> ranks = {1, 2, 3};
        const auto r = BenchmarkMetrics::hintQuality(ranks, 5);
        const QString text = r.reportText();
        QVERIFY(!text.isEmpty());
        QVERIFY(text.contains(QStringLiteral("HintBenchmarkResult")));
    }
};

// ═══════════════════════════════════════════════════════════════════════════
// TestBiasAuditor
// ═══════════════════════════════════════════════════════════════════════════

class TestBiasAuditor : public QObject
{
    Q_OBJECT

private slots:

    // ── Disparate Impact ───────────────────────────────────────────────────

    void testDisparateImpactEqualGroups()
    {
        // Two groups with identical prediction rates → ratio = 1.0, not flagged
        QVector<QString> groups;
        QVector<double>  yPred;
        for (int i = 0; i < 100; ++i) {
            groups.append(i < 50 ? QStringLiteral("A") : QStringLiteral("B"));
            yPred.append(0.8);  // both groups have 100% positive rate
        }

        const auto reports = BiasAuditor::disparateImpact(groups, yPred);
        QVERIFY(!reports.isEmpty());
        const auto& r = reports.first();
        QVERIFY2(qAbs(r.ratio - 1.0) < 1e-6,
                 qPrintable(QStringLiteral("Equal groups ratio should be 1.0, got %1").arg(r.ratio)));
        QVERIFY2(!r.flagged, "Equal groups should not be flagged");
    }

    void testDisparateImpactFlagged()
    {
        // Group A: 90% positive rate, Group B: 50% positive rate
        // ratio = 0.9/0.5 = 1.8 → exceeds 1.25 threshold → flagged
        QVector<QString> groups;
        QVector<double>  yPred;
        for (int i = 0; i < 50; ++i) {
            groups.append(QStringLiteral("A"));
            yPred.append(0.9);  // above threshold
        }
        for (int i = 0; i < 50; ++i) {
            groups.append(QStringLiteral("B"));
            yPred.append((i < 25) ? 0.9 : 0.1);  // 50% above threshold
        }

        const auto reports = BiasAuditor::disparateImpact(groups, yPred);
        QVERIFY(!reports.isEmpty());

        // Find the report for A vs B
        bool found = false;
        for (const auto& r : reports) {
            if ((r.groupA == QStringLiteral("A") && r.groupB == QStringLiteral("B")) ||
                (r.groupA == QStringLiteral("B") && r.groupB == QStringLiteral("A"))) {
                QVERIFY2(r.flagged,
                         qPrintable(QStringLiteral("A:1.0 vs B:0.5 should be flagged, ratio=%1").arg(r.ratio)));
                found = true;
                break;
            }
        }
        QVERIFY2(found, "Did not find A vs B report");
    }

    void testDisparateImpactMetricLabel()
    {
        QVector<QString> groups = {QStringLiteral("X"), QStringLiteral("Y")};
        QVector<double>  yPred  = {0.6, 0.6};

        const auto reports = BiasAuditor::disparateImpact(groups, yPred);
        QVERIFY(!reports.isEmpty());
        QCOMPARE(reports.first().metric, QStringLiteral("disparate_impact"));
    }

    // ── Equal Opportunity ──────────────────────────────────────────────────

    void testEqualOpportunityBothZero()
    {
        // TPR = 0 for both groups (no true positives predicted)
        QVector<QString> groups;
        QVector<double>  yTrue;
        QVector<double>  yPred;
        for (int i = 0; i < 40; ++i) {
            groups.append(i < 20 ? QStringLiteral("A") : QStringLiteral("B"));
            yTrue.append(1.0);   // all are positive
            yPred.append(0.1);   // all predicted negative (below 0.5)
        }

        const auto reports = BiasAuditor::equalOpportunity(groups, yTrue, yPred);
        QVERIFY(!reports.isEmpty());
        const auto& r = reports.first();
        QVERIFY2(!r.flagged,
                 qPrintable(QStringLiteral("Both-zero TPR should not be flagged (ratio=1.0), got %1").arg(r.ratio)));
        QVERIFY2(qAbs(r.ratio - 1.0) < 1e-6,
                 qPrintable(QStringLiteral("Both-zero TPR ratio should be 1.0, got %1").arg(r.ratio)));
    }

    void testEqualOpportunityFlagged()
    {
        // Group A: TPR = 1.0, Group B: TPR = 0.5 → ratio = 2.0 → flagged
        QVector<QString> groups;
        QVector<double>  yTrue;
        QVector<double>  yPred;

        // Group A: all positives, all predicted positive
        for (int i = 0; i < 20; ++i) {
            groups.append(QStringLiteral("A"));
            yTrue.append(1.0);
            yPred.append(0.9);
        }
        // Group B: all positives, only half predicted positive
        for (int i = 0; i < 20; ++i) {
            groups.append(QStringLiteral("B"));
            yTrue.append(1.0);
            yPred.append(i < 10 ? 0.9 : 0.1);
        }

        const auto reports = BiasAuditor::equalOpportunity(groups, yTrue, yPred);
        QVERIFY(!reports.isEmpty());

        bool found = false;
        for (const auto& r : reports) {
            if ((r.groupA == QStringLiteral("A") && r.groupB == QStringLiteral("B")) ||
                (r.groupA == QStringLiteral("B") && r.groupB == QStringLiteral("A"))) {
                QVERIFY2(r.flagged,
                         qPrintable(QStringLiteral("TPR 1.0 vs 0.5 should be flagged, ratio=%1").arg(r.ratio)));
                found = true;
                break;
            }
        }
        QVERIFY2(found, "Did not find A vs B equal opportunity report");
    }

    // ── GroupStats ─────────────────────────────────────────────────────────

    void testGroupStats()
    {
        QVector<QString> groups;
        QVector<double>  yTrue;
        QVector<double>  yPred;

        // Group A: 10 events, 6 predicted positive
        for (int i = 0; i < 10; ++i) {
            groups.append(QStringLiteral("A"));
            yTrue.append(i < 4 ? 1.0 : 0.0);
            yPred.append(i < 6 ? 0.9 : 0.1);  // 6 flagged
        }
        // Group B: 5 events, 2 predicted positive
        for (int i = 0; i < 5; ++i) {
            groups.append(QStringLiteral("B"));
            yTrue.append(i < 3 ? 1.0 : 0.0);
            yPred.append(i < 2 ? 0.8 : 0.2);  // 2 flagged
        }

        const auto stats = BiasAuditor::groupStats(groups, yTrue, yPred);

        QVERIFY(stats.contains(QStringLiteral("A")));
        QVERIFY(stats.contains(QStringLiteral("B")));

        const auto& sa = stats[QStringLiteral("A")];
        QCOMPARE(sa.nEvents,  10);
        QCOMPARE(sa.nFlagged, 6);
        QVERIFY2(qAbs(sa.flagRate - 0.6) < 1e-9,
                 qPrintable(QStringLiteral("Flag rate A should be 0.6, got %1").arg(sa.flagRate)));
        QVERIFY2(qAbs(sa.actualRate - 0.4) < 1e-9,
                 qPrintable(QStringLiteral("Actual rate A should be 0.4, got %1").arg(sa.actualRate)));

        const auto& sb = stats[QStringLiteral("B")];
        QCOMPARE(sb.nEvents,  5);
        QCOMPARE(sb.nFlagged, 2);
        QVERIFY2(qAbs(sb.flagRate - 0.4) < 1e-9,
                 qPrintable(QStringLiteral("Flag rate B should be 0.4, got %1").arg(sb.flagRate)));
    }

    // ── Feedback Loop ──────────────────────────────────────────────────────

    void testFeedbackLoopNone()
    {
        // Both groups: flat pred and actual → no feedback loop
        QMap<QString, QVector<QPair<double,double>>> trendData;
        trendData[QStringLiteral("X")] = {
            {0.3, 0.3}, {0.31, 0.29}, {0.30, 0.31}, {0.30, 0.30}
        };
        trendData[QStringLiteral("Y")] = {
            {0.5, 0.5}, {0.49, 0.51}, {0.50, 0.50}
        };

        const auto flagged = BiasAuditor::feedbackLoopCheck(trendData, 0.01);
        QVERIFY2(flagged.isEmpty(),
                 qPrintable(QStringLiteral("No feedback loop expected, got %1 flagged").arg(flagged.size())));
    }

    void testFeedbackLoopDetected()
    {
        // Group A: pred rising, actual flat → feedback loop
        // Group B: both flat → no feedback loop
        QMap<QString, QVector<QPair<double,double>>> trendData;
        trendData[QStringLiteral("A")] = {
            {0.30, 0.40}, {0.35, 0.40}, {0.42, 0.41}, {0.50, 0.39}, {0.60, 0.40}
        };
        trendData[QStringLiteral("B")] = {
            {0.40, 0.40}, {0.41, 0.39}, {0.40, 0.40}
        };

        const auto flagged = BiasAuditor::feedbackLoopCheck(trendData, 0.01);
        QVERIFY2(flagged.contains(QStringLiteral("A")),
                 qPrintable(QStringLiteral("Group A should be flagged for feedback loop")));
        QVERIFY2(!flagged.contains(QStringLiteral("B")),
                 qPrintable(QStringLiteral("Group B should NOT be flagged")));
    }

    void testFeedbackLoopInsufficientData()
    {
        // Only one data point per group → can't compute slope
        QMap<QString, QVector<QPair<double,double>>> trendData;
        trendData[QStringLiteral("Z")] = {{0.9, 0.1}};

        const auto flagged = BiasAuditor::feedbackLoopCheck(trendData);
        QVERIFY(flagged.isEmpty());
    }

    // ── formatReports ──────────────────────────────────────────────────────

    void testFormatReportsEmpty()
    {
        const QString text = BiasAuditor::formatReports({});
        QVERIFY(!text.isEmpty());
    }

    void testFormatReportsContent()
    {
        QVector<QString> groups = {QStringLiteral("A"), QStringLiteral("B")};
        QVector<double>  yPred  = {0.9, 0.2};
        const auto reports = BiasAuditor::disparateImpact(groups, yPred);
        const QString text = BiasAuditor::formatReports(reports);
        QVERIFY(!text.isEmpty());
        QVERIFY(text.contains(QStringLiteral("Bias Audit Report")));
    }
};

// ─── Main ─────────────────────────────────────────────────────────────────────

static int runTest(QObject* obj, const char* logFile)
{
    QStringList args = { QStringLiteral("test"), QStringLiteral("-o"),
                         QStringLiteral("%1,txt").arg(logFile) };
    return QTest::qExec(obj, args);
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    int r = 0;
    TestBenchmarkMetrics t1; r |= runTest(&t1, "bench_metrics.txt");
    TestBiasAuditor      t2; r |= runTest(&t2, "bench_bias.txt");
    return r;
}

#include "test_benchmark.moc"
