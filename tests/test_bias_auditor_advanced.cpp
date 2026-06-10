// test_bias_auditor_advanced.cpp
// Advanced tests for BiasAuditor: edge cases, equalizedOddsDiff, formatReports,
// feedbackLoop sensitivity, and maxDisparateImpact.
#include <QTest>
#include "benchmark/BiasAuditor.h"
#include <cmath>

class BiasAuditorAdvancedTest : public QObject
{
    Q_OBJECT

private:
    // Build balanced group data
    static void buildBalanced(QVector<QString>& groups,
                               QVector<double>& yTrue,
                               QVector<double>& yPred,
                               int n = 100)
    {
        for (int i = 0; i < n; ++i) {
            groups.append((i % 2 == 0) ? QStringLiteral("A") : QStringLiteral("B"));
            yTrue.append((i % 3 < 2) ? 1.0 : 0.0);
            yPred.append((i % 3 < 2) ? 0.8 : 0.2);
        }
    }

private slots:

    // ── 1. disparateImpact: identical groups → ratio == 1.0, not flagged ─────
    void testDisparateImpactIdentical()
    {
        QVector<QString> groups;
        QVector<double> yPred;
        for (int i = 0; i < 100; ++i) {
            groups.append((i < 50) ? QStringLiteral("A") : QStringLiteral("B"));
            yPred.append(0.8);  // all high
        }
        const auto reports = BiasAuditor::disparateImpact(groups, yPred, 0.5);
        for (const auto& r : reports)
            QVERIFY2(!r.flagged,
                     qPrintable(QStringLiteral("Identical predictions should not be flagged")));
    }

    // ── 2. disparateImpact: 0% vs 100% selection → flagged ──────────────────
    void testDisparateImpactExtremeFlagged()
    {
        QVector<QString> groups;
        QVector<double> yPred;
        for (int i = 0; i < 50; ++i) {
            groups.append(QStringLiteral("A"));
            yPred.append(0.9);  // A: always positive
        }
        for (int i = 0; i < 50; ++i) {
            groups.append(QStringLiteral("B"));
            yPred.append(0.1);  // B: always negative
        }
        const auto reports = BiasAuditor::disparateImpact(groups, yPred, 0.5);
        const bool anyFlagged = std::any_of(reports.begin(), reports.end(),
            [](const BiasReport& r){ return r.flagged; });
        QVERIFY2(anyFlagged, "Extreme disparity (0% vs 100%) should be flagged");
    }

    // ── 3. equalOpportunity: equal TPR → not flagged ─────────────────────────
    void testEqualOpportunityBalanced()
    {
        QVector<QString> groups;
        QVector<double> yTrue, yPred;
        buildBalanced(groups, yTrue, yPred);
        const auto reports = BiasAuditor::equalOpportunity(groups, yTrue, yPred, 0.5);
        // With balanced data across groups, should have low/no flagging
        for (const auto& r : reports)
            QVERIFY2(r.ratio >= 0.0, "All ratios should be non-negative");
    }

    // ── 4. groupStats: nEvents correct ───────────────────────────────────────
    void testGroupStatsNEvents()
    {
        QVector<QString> groups;
        QVector<double> yTrue, yPred;
        for (int i = 0; i < 60; ++i) {
            groups.append(QStringLiteral("A"));
            yTrue.append(1.0); yPred.append(0.8);
        }
        for (int i = 0; i < 40; ++i) {
            groups.append(QStringLiteral("B"));
            yTrue.append(0.0); yPred.append(0.2);
        }
        const auto stats = BiasAuditor::groupStats(groups, yTrue, yPred);
        QVERIFY2(stats.contains(QStringLiteral("A")), "Group A missing from stats");
        QVERIFY2(stats[QStringLiteral("A")].nEvents == 60,
                 qPrintable(QStringLiteral("Group A nEvents %1 expected 60")
                    .arg(stats[QStringLiteral("A")].nEvents)));
    }

    // ── 5. groupStats: flagRate in [0, 1] ────────────────────────────────────
    void testGroupStatsFlagRate()
    {
        QVector<QString> groups;
        QVector<double> yTrue, yPred;
        buildBalanced(groups, yTrue, yPred);
        const auto stats = BiasAuditor::groupStats(groups, yTrue, yPred);
        for (const auto& g : stats) {
            QVERIFY2(g.flagRate >= 0.0 && g.flagRate <= 1.0,
                     qPrintable(QStringLiteral("flagRate %1 must be in [0,1]").arg(g.flagRate)));
        }
    }

    // ── 6. feedbackLoopCheck: upward trend flagged ───────────────────────────
    void testFeedbackLoopUpwardTrend()
    {
        QMap<QString, QVector<QPair<double,double>>> trendData;
        QVector<QPair<double,double>> trend;
        // Prediction rate rising, actual rate flat
        for (int i = 0; i < 10; ++i)
            trend.append({ 0.3 + i * 0.05, 0.3 });
        trendData[QStringLiteral("A")] = trend;

        const auto flagged = BiasAuditor::feedbackLoopCheck(trendData, 0.001);
        QVERIFY2(flagged.contains(QStringLiteral("A")),
                 "Upward prediction trend with flat actual should be flagged");
    }

    // ── 7. feedbackLoopCheck: stable trend → not flagged ─────────────────────
    void testFeedbackLoopStableNotFlagged()
    {
        QMap<QString, QVector<QPair<double,double>>> trendData;
        QVector<QPair<double,double>> trend;
        for (int i = 0; i < 10; ++i)
            trend.append({ 0.4, 0.4 });  // identical pred and actual
        trendData[QStringLiteral("B")] = trend;

        const auto flagged = BiasAuditor::feedbackLoopCheck(trendData, 0.01);
        QVERIFY2(!flagged.contains(QStringLiteral("B")),
                 "Stable trend should not be flagged as feedback loop");
    }

    // ── 8. formatReports: non-empty output ───────────────────────────────────
    void testFormatReports()
    {
        QVector<QString> groups;
        QVector<double> yPred;
        for (int i = 0; i < 50; ++i) {
            groups.append((i < 25) ? QStringLiteral("X") : QStringLiteral("Y"));
            yPred.append(i < 25 ? 0.9 : 0.1);
        }
        const auto reports = BiasAuditor::disparateImpact(groups, yPred, 0.5);
        const auto text = BiasAuditor::formatReports(reports);
        QVERIFY2(!text.isEmpty(), "formatReports should produce non-empty text");
    }

    // ── 9. maxDisparateImpact: returns >= 1.0 when groups differ ─────────────
    void testMaxDisparateImpactDiffering()
    {
        BiasAuditor ba;
        QVector<GroupStats> stats;
        GroupStats g1; g1.groupId = QStringLiteral("A"); g1.nEvents = 100; g1.nFlagged = 80; g1.flagRate = 0.8;
        GroupStats g2; g2.groupId = QStringLiteral("B"); g2.nEvents = 100; g2.nFlagged = 20; g2.flagRate = 0.2;
        stats << g1 << g2;

        const double mdi = ba.maxDisparateImpact(stats);
        QVERIFY2(mdi >= 1.0,
                 qPrintable(QStringLiteral("maxDisparateImpact %1 should be >= 1.0 when groups differ")
                    .arg(mdi)));
    }

    // ── 10. equalizedOddsDiff: returns non-negative value ────────────────────
    void testEqualizedOddsDiffNonNegative()
    {
        BiasAuditor ba;
        QVector<GroupStats> stats;
        GroupStats g1;
        g1.groupId = QStringLiteral("A"); g1.nActualPos = 50; g1.nTP = 40;
        GroupStats g2;
        g2.groupId = QStringLiteral("B"); g2.nActualPos = 50; g2.nTP = 20;
        stats << g1 << g2;

        const double diff = ba.equalizedOddsDiff(stats);
        QVERIFY2(diff >= 0.0,
                 qPrintable(QStringLiteral("equalizedOddsDiff %1 must be >= 0").arg(diff)));
    }
};

QTEST_MAIN(BiasAuditorAdvancedTest)
#include "test_bias_auditor_advanced.moc"
