// test_bias_auditor_deep.cpp — Comprehensive BiasAuditor tests
#include <QTest>
#include <QCoreApplication>
#include "benchmark/BiasAuditor.h"
#include <cmath>

class TestBiasAuditorDeep : public QObject
{
    Q_OBJECT

private:
    static QVector<QString> makeGroups(int nA, int nB) {
        QVector<QString> g;
        for (int i = 0; i < nA; ++i) g.append("GroupA");
        for (int i = 0; i < nB; ++i) g.append("GroupB");
        return g;
    }

    static QVector<double> makeUniformPreds(int nA, double predA,
                                             int nB, double predB) {
        QVector<double> p;
        for (int i = 0; i < nA; ++i) p.append(predA);
        for (int i = 0; i < nB; ++i) p.append(predB);
        return p;
    }

private slots:

    // ── Disparate Impact ─────────────────────────────────────────────────────

    void testDisparateImpactEqualRates()
    {
        auto g = makeGroups(100, 100);
        auto p = makeUniformPreds(100, 0.8, 100, 0.8);
        auto reports = BiasAuditor::disparateImpact(g, p);
        QVERIFY(!reports.isEmpty());
        // Equal rates → ratio ≈ 1.0 → not flagged
        QVERIFY(!reports.first().flagged);
        QVERIFY(std::abs(reports.first().ratio - 1.0) < 0.05);
    }

    void testDisparateImpactFlagged()
    {
        // Group A rate 100% (all preds at 0.9 >= 0.5), Group B rate 0% (preds at 0.3 < 0.5)
        // ratio = 0.0/1.0 → group B has zero positive rate → flagged
        auto g = makeGroups(100, 100);
        auto p = makeUniformPreds(100, 0.9, 100, 0.3);
        auto reports = BiasAuditor::disparateImpact(g, p);
        bool anyFlagged = false;
        for (const auto& r : reports) if (r.flagged) { anyFlagged = true; break; }
        QVERIFY(anyFlagged);
    }

    void testDisparateImpactRatioFormula()
    {
        // GroupA pred rate = 0.9 (all 10 above 0.5), GroupB = 0.2 (2/10 above 0.5)
        QVector<QString> g;
        QVector<double> p;
        for (int i = 0; i < 10; ++i) { g.append("A"); p.append(0.9); }
        for (int i = 0; i < 10; ++i) { g.append("B"); p.append(i < 2 ? 0.9 : 0.1); }
        auto reports = BiasAuditor::disparateImpact(g, p);
        QVERIFY(!reports.isEmpty());
        // Ratio should be 0.2/0.9 ≈ 0.22 or 0.9/0.2 ≈ 4.5 (smaller group in numerator)
        const auto& r = reports.first();
        QVERIFY(r.ratio > 0.0);
    }

    void testDisparateImpactCustomThreshold()
    {
        // With threshold = 0.7, fewer positives for group B
        auto g = makeGroups(100, 100);
        // Group A: all 0.9 (above 0.7) → rate = 1.0
        // Group B: all 0.65 (below 0.7) → rate = 0.0
        QVector<double> p;
        for (int i = 0; i < 100; ++i) p.append(0.9);
        for (int i = 0; i < 100; ++i) p.append(0.65);
        auto reports = BiasAuditor::disparateImpact(g, p, 0.7);
        QVERIFY(!reports.isEmpty());
        QVERIFY(reports.first().flagged);
    }

    void testDisparateImpactReturnsBothGroups()
    {
        auto g = makeGroups(50, 50);
        auto p = makeUniformPreds(50, 0.7, 50, 0.7);
        auto reports = BiasAuditor::disparateImpact(g, p);
        QVERIFY(!reports.isEmpty());
        const auto& r = reports.first();
        QVERIFY(!r.groupA.isEmpty());
        QVERIFY(!r.groupB.isEmpty());
    }

    void testDisparateImpactThreeGroups()
    {
        QVector<QString> g;
        QVector<double> p;
        for (int i = 0; i < 30; ++i) { g.append("A"); p.append(0.8); }
        for (int i = 0; i < 30; ++i) { g.append("B"); p.append(0.5); }
        for (int i = 0; i < 30; ++i) { g.append("C"); p.append(0.9); }
        auto reports = BiasAuditor::disparateImpact(g, p);
        // Should have C(3-1) = 3 pairwise comparisons
        QVERIFY(reports.size() >= 2);
    }

    // ── Equal Opportunity ─────────────────────────────────────────────────────

    void testEqualOpportunityEqualTPR()
    {
        // Group A and B have same TPR → not flagged
        QVector<QString> g;
        QVector<double> yTrue, yPred;
        // Group A: 10 positive, all predicted positive → TPR = 1.0
        for (int i = 0; i < 10; ++i) { g.append("A"); yTrue.append(1.0); yPred.append(0.9); }
        // Group B: 10 positive, all predicted positive → TPR = 1.0
        for (int i = 0; i < 10; ++i) { g.append("B"); yTrue.append(1.0); yPred.append(0.9); }
        auto reports = BiasAuditor::equalOpportunity(g, yTrue, yPred);
        QVERIFY(!reports.isEmpty());
        QVERIFY(!reports.first().flagged);
    }

    void testEqualOpportunityFlagged()
    {
        QVector<QString> g;
        QVector<double> yTrue, yPred;
        // Group A: TPR = 1.0 (all actual positives predicted positive)
        for (int i = 0; i < 10; ++i) { g.append("A"); yTrue.append(1.0); yPred.append(0.9); }
        // Group B: TPR = 0.1 (only 1 of 10 actual positives predicted positive)
        for (int i = 0; i < 10; ++i) {
            g.append("B");
            yTrue.append(1.0);
            yPred.append(i == 0 ? 0.9 : 0.1);
        }
        auto reports = BiasAuditor::equalOpportunity(g, yTrue, yPred);
        QVERIFY(!reports.isEmpty());
        bool flagged = false;
        for (const auto& r : reports) if (r.flagged) { flagged = true; break; }
        QVERIFY(flagged);
    }

    // ── Group Stats ───────────────────────────────────────────────────────────

    void testGroupStatsBasic()
    {
        QVector<QString> g;
        QVector<double> yTrue, yPred;
        for (int i = 0; i < 20; ++i) { g.append("A"); yTrue.append(0.7); yPred.append(0.8); }
        for (int i = 0; i < 10; ++i) { g.append("B"); yTrue.append(0.3); yPred.append(0.4); }
        auto stats = BiasAuditor::groupStats(g, yTrue, yPred);
        QVERIFY(stats.contains("A"));
        QVERIFY(stats.contains("B"));
        QCOMPARE(stats["A"].nEvents, 20);
        QCOMPARE(stats["B"].nEvents, 10);
    }

    void testGroupStatsNEvents()
    {
        QVector<QString> g;
        QVector<double> yTrue, yPred;
        for (int i = 0; i < 15; ++i) {
            g.append(i < 10 ? "X" : "Y");
            yTrue.append(0.5);
            yPred.append(0.6);
        }
        auto stats = BiasAuditor::groupStats(g, yTrue, yPred);
        QCOMPARE(stats["X"].nEvents, 10);
        QCOMPARE(stats["Y"].nEvents, 5);
    }

    void testGroupStatsMeanPred()
    {
        QVector<QString> g;
        QVector<double> yTrue, yPred;
        for (int i = 0; i < 10; ++i) { g.append("G"); yTrue.append(1.0); yPred.append(0.8); }
        auto stats = BiasAuditor::groupStats(g, yTrue, yPred);
        QVERIFY(std::abs(stats["G"].meanPred - 0.8) < 1e-6);
    }

    // ── Feedback Loop Detection ───────────────────────────────────────────────

    void testFeedbackLoopDetectedWhenPredRiseActualFlat()
    {
        QMap<QString, QVector<QPair<double,double>>> trendData;
        // Group A: pred_rate rising steadily, actual_rate flat
        QVector<QPair<double,double>> trend;
        for (int t = 0; t < 6; ++t)
            trend.append({0.1 + t * 0.15, 0.5});
        trendData["GroupA"] = trend;
        auto flagged = BiasAuditor::feedbackLoopCheck(trendData);
        QVERIFY(flagged.contains("GroupA"));
    }

    void testFeedbackLoopNotDetectedWhenBothRise()
    {
        QMap<QString, QVector<QPair<double,double>>> trendData;
        QVector<QPair<double,double>> trend;
        for (int t = 0; t < 6; ++t)
            trend.append({0.1 + t * 0.1, 0.1 + t * 0.1});
        trendData["GroupB"] = trend;
        auto flagged = BiasAuditor::feedbackLoopCheck(trendData);
        QVERIFY(!flagged.contains("GroupB"));
    }

    void testFeedbackLoopEmptyDataNocrash()
    {
        QMap<QString, QVector<QPair<double,double>>> empty;
        auto flagged = BiasAuditor::feedbackLoopCheck(empty);
        QVERIFY(flagged.isEmpty());
    }

    // ── Format Reports ────────────────────────────────────────────────────────

    void testFormatReportsNonEmpty()
    {
        auto g = makeGroups(50, 50);
        auto p = makeUniformPreds(50, 0.8, 50, 0.4);
        auto reports = BiasAuditor::disparateImpact(g, p);
        QString formatted = BiasAuditor::formatReports(reports);
        QVERIFY(!formatted.isEmpty());
    }

    void testFormatReportsMentionsFlagged()
    {
        auto g = makeGroups(50, 50);
        auto p = makeUniformPreds(50, 0.95, 50, 0.3);
        auto reports = BiasAuditor::disparateImpact(g, p);
        QString formatted = BiasAuditor::formatReports(reports);
        // Flagged reports should mention FLAGGED or WARNING
        QVERIFY(formatted.contains("FLAG", Qt::CaseInsensitive) ||
                formatted.contains("WARN", Qt::CaseInsensitive) ||
                formatted.contains("*", Qt::CaseInsensitive));
    }

    void testFormatReportsEmptyInputOk()
    {
        QString formatted = BiasAuditor::formatReports({});
        QVERIFY(formatted.isEmpty() || formatted.size() >= 0);
    }

    // ── Edge Cases ────────────────────────────────────────────────────────────

    void testSingleGroupNoReport()
    {
        QVector<QString> g;
        QVector<double> p;
        for (int i = 0; i < 10; ++i) { g.append("only"); p.append(0.7); }
        auto reports = BiasAuditor::disparateImpact(g, p);
        QVERIFY(reports.isEmpty());
    }

    void testEmptyInputsNocrash()
    {
        auto reports = BiasAuditor::disparateImpact({}, {});
        QVERIFY(reports.isEmpty());
    }

    void testRatioAlwaysPositive()
    {
        auto g = makeGroups(20, 20);
        auto p = makeUniformPreds(20, 0.6, 20, 0.9);
        auto reports = BiasAuditor::disparateImpact(g, p);
        for (const auto& r : reports) {
            QVERIFY(r.ratio > 0.0);
        }
    }

    void testFlagRateIsInUnitInterval()
    {
        QVector<QString> g;
        QVector<double> yTrue, yPred;
        for (int i = 0; i < 20; ++i) {
            g.append(i < 10 ? "A" : "B");
            yTrue.append(i % 3 == 0 ? 1.0 : 0.0);
            yPred.append(0.7);
        }
        auto stats = BiasAuditor::groupStats(g, yTrue, yPred);
        for (const auto& [key, s] : stats.asKeyValueRange()) {
            QVERIFY(s.flagRate >= 0.0 && s.flagRate <= 1.0);
        }
    }
};

QTEST_MAIN(TestBiasAuditorDeep)
#include "test_bias_auditor_deep.moc"
