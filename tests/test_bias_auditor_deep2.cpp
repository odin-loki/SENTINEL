// test_bias_auditor_deep2.cpp — Second-pass deep audit tests for BiasAuditor
#include <QTest>
#include "benchmark/BiasAuditor.h"
#include <cmath>

class TestBiasAuditorDeep2 : public QObject
{
    Q_OBJECT

private:
    static QVector<QString> groups(int nA, int nB) {
        QVector<QString> g;
        for (int i = 0; i < nA; ++i) g.append(QStringLiteral("A"));
        for (int i = 0; i < nB; ++i) g.append(QStringLiteral("B"));
        return g;
    }
    static QVector<double> preds(int nA, double pA, int nB, double pB) {
        QVector<double> p;
        for (int i = 0; i < nA; ++i) p.append(pA);
        for (int i = 0; i < nB; ++i) p.append(pB);
        return p;
    }

private slots:

    // ── disparateImpact ────────────────────────────────────────────────────────

    void testDisparateImpactBalanced()
    {
        // Both groups with identical selection rate → ratio = 1.0, not flagged
        auto g = groups(100, 100);
        auto p = preds(100, 0.8, 100, 0.8);
        auto reports = BiasAuditor::disparateImpact(g, p);
        QVERIFY(!reports.isEmpty());
        QVERIFY(!reports.first().flagged);
        QVERIFY(std::abs(reports.first().ratio - 1.0) < 1e-6);
    }

    void testDisparateImpact100vs0()
    {
        // Group A: 100% selection rate (all preds ≥ 0.5)
        // Group B: 0% selection rate (all preds < 0.5)
        // ratio = 0.0/1.0 = 0.0 < 0.80 → flagged
        auto g = groups(50, 50);
        auto p = preds(50, 0.9, 50, 0.1);
        auto reports = BiasAuditor::disparateImpact(g, p);
        QVERIFY(!reports.isEmpty());
        const auto& r = reports.first();
        QVERIFY(r.flagged);
        QVERIFY(std::abs(r.ratio - 0.0) < 1e-6);
    }

    // ── equalOpportunity ──────────────────────────────────────────────────────

    void testEqualOpportunityEqualTPR()
    {
        // Both groups: all actual positives correctly predicted → TPR = 1.0
        // ratio = 1.0/1.0 = 1.0, not flagged
        QVector<QString> g;
        QVector<double> yTrue, yPred;
        for (int i = 0; i < 10; ++i) { g.append(QStringLiteral("A")); yTrue.append(1.0); yPred.append(0.9); }
        for (int i = 0; i < 10; ++i) { g.append(QStringLiteral("B")); yTrue.append(1.0); yPred.append(0.9); }
        auto reports = BiasAuditor::equalOpportunity(g, yTrue, yPred);
        QVERIFY(!reports.isEmpty());
        const auto& r = reports.first();
        QVERIFY(std::abs(r.ratio - 1.0) < 1e-6);
        QVERIFY(!r.flagged);
    }

    // ── groupStats ────────────────────────────────────────────────────────────

    void testGroupStatsConfusionMatrixSum()
    {
        // nTP + nFP + nFN + nTN must equal nEvents for every group
        QVector<QString> g;
        QVector<double> yTrue, yPred;
        // Group A: 10 events, mix of pos/neg actual and pred
        g.append(QStringLiteral("A")); yTrue.append(1.0); yPred.append(0.9); // TP
        g.append(QStringLiteral("A")); yTrue.append(1.0); yPred.append(0.4); // FN
        g.append(QStringLiteral("A")); yTrue.append(0.0); yPred.append(0.8); // FP
        g.append(QStringLiteral("A")); yTrue.append(0.0); yPred.append(0.2); // TN
        g.append(QStringLiteral("A")); yTrue.append(1.0); yPred.append(0.6); // TP
        g.append(QStringLiteral("A")); yTrue.append(0.0); yPred.append(0.3); // TN
        // Group B: 4 events
        g.append(QStringLiteral("B")); yTrue.append(1.0); yPred.append(0.7); // TP
        g.append(QStringLiteral("B")); yTrue.append(0.0); yPred.append(0.6); // FP
        g.append(QStringLiteral("B")); yTrue.append(1.0); yPred.append(0.3); // FN
        g.append(QStringLiteral("B")); yTrue.append(0.0); yPred.append(0.1); // TN

        auto stats = BiasAuditor::groupStats(g, yTrue, yPred);

        for (auto it = stats.constBegin(); it != stats.constEnd(); ++it) {
            const auto& s = it.value();
            const int confSum = s.nTP + s.nFP + s.nFN + s.nTN;
            QCOMPARE(confSum, s.nEvents);
        }
    }

    void testGroupStatsFPRNumerical()
    {
        // Group: 3 actual negatives, 2 predicted positive (FP) and 1 negative (TN)
        // FPR = FP/(FP+TN) = 2/3
        QVector<QString> g;
        QVector<double> yTrue, yPred;
        g.append(QStringLiteral("G")); yTrue.append(0.0); yPred.append(0.8); // FP
        g.append(QStringLiteral("G")); yTrue.append(0.0); yPred.append(0.9); // FP
        g.append(QStringLiteral("G")); yTrue.append(0.0); yPred.append(0.1); // TN

        auto stats = BiasAuditor::groupStats(g, yTrue, yPred);
        QVERIFY(stats.contains(QStringLiteral("G")));
        const auto& s = stats[QStringLiteral("G")];
        QCOMPARE(s.nFP, 2);
        QCOMPARE(s.nTN, 1);
        const double expectedFPR = 2.0 / 3.0;
        QVERIFY(std::abs(s.falsePositiveRate - expectedFPR) < 1e-9);
    }

    // ── feedbackLoopCheck ─────────────────────────────────────────────────────

    void testFeedbackLoopRisingPredFlatActual()
    {
        // pred_rate rises, actual_rate flat → should be flagged
        QMap<QString, QVector<QPair<double,double>>> td;
        QVector<QPair<double,double>> s;
        for (int i = 0; i < 6; ++i)
            s.append({0.1 * i, 0.5}); // pred rising, actual flat
        td[QStringLiteral("BadGroup")] = s;
        auto flagged = BiasAuditor::feedbackLoopCheck(td, 0.01);
        QVERIFY(flagged.contains(QStringLiteral("BadGroup")));
    }

    void testFeedbackLoopNoFlagWhenActualAlsoRises()
    {
        // Both pred and actual rise → should NOT be flagged
        QMap<QString, QVector<QPair<double,double>>> td;
        QVector<QPair<double,double>> s;
        for (int i = 0; i < 6; ++i)
            s.append({0.1 * i, 0.1 * i}); // both rising equally
        td[QStringLiteral("GoodGroup")] = s;
        auto flagged = BiasAuditor::feedbackLoopCheck(td, 0.01);
        QVERIFY(!flagged.contains(QStringLiteral("GoodGroup")));
    }

    // ── trendSlope (via feedbackLoopCheck) ────────────────────────────────────

    void testTrendSlopeLinear()
    {
        // The series [0,1,2,3,4] has OLS slope = 1.0 exactly.
        // Verify by checking flag boundary: flagged at sensitivity=0.99, not at 1.01
        QMap<QString, QVector<QPair<double,double>>> td;
        QVector<QPair<double,double>> s;
        for (int i = 0; i < 5; ++i)
            s.append({static_cast<double>(i), 0.0}); // pred=[0,1,2,3,4], actual flat at 0
        td[QStringLiteral("G")] = s;

        // slope = 1.0 > 0.99 AND actual slope ≈ 0 ≤ 0.99 → flagged
        QVERIFY(BiasAuditor::feedbackLoopCheck(td, 0.99).contains(QStringLiteral("G")));
        // slope = 1.0 ≤ 1.01 → NOT flagged
        QVERIFY(!BiasAuditor::feedbackLoopCheck(td, 1.01).contains(QStringLiteral("G")));
    }

    // ── maxDisparateImpact ────────────────────────────────────────────────────

    void testMaxDisparateImpactCorrectRatio()
    {
        // Group A: 8/10 flagged → rate 0.8
        // Group B: 4/10 flagged → rate 0.4
        // maxDisparateImpact = 0.8 / 0.4 = 2.0
        GroupStats a, b;
        a.nEvents = 10; a.nFlagged = 8;
        b.nEvents = 10; b.nFlagged = 4;

        BiasAuditor auditor;
        const double result = auditor.maxDisparateImpact({a, b});
        QVERIFY(std::abs(result - 2.0) < 1e-9);
    }

    void testMaxDisparateImpactEmptyGroup()
    {
        // A group with nEvents == 0 → returns 0.0
        GroupStats a, b;
        a.nEvents = 10; a.nFlagged = 5;
        b.nEvents = 0;  b.nFlagged = 0;

        BiasAuditor auditor;
        QCOMPARE(auditor.maxDisparateImpact({a, b}), 0.0);
    }
};

QTEST_GUILESS_MAIN(TestBiasAuditorDeep2)
#include "test_bias_auditor_deep2.moc"
