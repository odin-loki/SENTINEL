// test_bias_auditor_deep4.cpp — Deep audit iteration 27: BiasAuditor
// groupStats TP/FP rates, equalOpportunity, maxDisparateImpact instance method.
#include <QTest>
#include "benchmark/BiasAuditor.h"

class TestBiasAuditorDeep4 : public QObject
{
    Q_OBJECT

    static QVector<QString> groupsAB(int nA, int nB)
    {
        QVector<QString> g;
        for (int i = 0; i < nA; ++i) g.append(QStringLiteral("A"));
        for (int i = 0; i < nB; ++i) g.append(QStringLiteral("B"));
        return g;
    }

private slots:

    void testGroupStatsCountsEvents()
    {
        const auto groups = groupsAB(10, 5);
        QVector<double> yTrue(15, 0.0);
        QVector<double> yPred(15, 0.6);

        const auto stats = BiasAuditor::groupStats(groups, yTrue, yPred);
        QCOMPARE(stats[QStringLiteral("A")].nEvents, 10);
        QCOMPARE(stats[QStringLiteral("B")].nEvents, 5);
    }

    void testEqualOpportunityReportsGroups()
    {
        const auto groups = groupsAB(8, 8);
        QVector<double> yTrue = {1,1,1,1,0,0,0,0, 1,1,0,0,0,0,0,0};
        QVector<double> yPred(16, 0.7);

        const auto reports = BiasAuditor::equalOpportunity(groups, yTrue, yPred);
        QVERIFY(!reports.isEmpty());
        for (const auto& r : reports)
            QVERIFY(!r.metric.isEmpty());
    }

    void testMaxDisparateImpactOnInstance()
    {
        BiasAuditor auditor;
        QVector<GroupStats> stats;
        GroupStats a;
        a.groupId  = QStringLiteral("A");
        a.nEvents  = 100;
        a.nFlagged = 50;
        a.flagRate = 0.5;
        GroupStats b;
        b.groupId  = QStringLiteral("B");
        b.nEvents  = 100;
        b.nFlagged = 10;
        b.flagRate = 0.1;
        stats = { a, b };

        const double di = auditor.maxDisparateImpact(stats);
        QVERIFY(di > 1.0);
    }

    void testDisparateImpactFlagsLowRatio()
    {
        const auto groups = groupsAB(20, 20);
        QVector<double> yPred;
        for (int i = 0; i < 20; ++i) yPred.append(0.9);
        for (int i = 0; i < 20; ++i) yPred.append(0.1);

        const auto reports = BiasAuditor::disparateImpact(groups, yPred);
        bool flagged = false;
        for (const auto& r : reports) {
            if (r.flagged) flagged = true;
        }
        QVERIFY(flagged);
    }

    void testFormatReportsIncludesMetric()
    {
        BiasReport rep;
        rep.metric  = QStringLiteral("disparate_impact");
        rep.groupA  = QStringLiteral("North");
        rep.groupB  = QStringLiteral("South");
        rep.ratio   = 0.7;
        rep.flagged = true;

        const QString text = BiasAuditor::formatReports({ rep });
        QVERIFY(text.contains(QStringLiteral("disparate_impact")));
        QVERIFY(text.contains(QStringLiteral("North")));
    }
};

QTEST_GUILESS_MAIN(TestBiasAuditorDeep4)
#include "test_bias_auditor_deep4.moc"
