// test_bias_auditor_deep5.cpp — Deep audit iteration 30: BiasAuditor
// feedbackLoopCheck, equalizedOddsDiff, formatReports, false positive rates.
#include <QTest>
#include "benchmark/BiasAuditor.h"

class TestBiasAuditorDeep5 : public QObject
{
    Q_OBJECT

private slots:

    void testFeedbackLoopFlagsDivergingTrend()
    {
        QMap<QString, QVector<QPair<double, double>>> trends;
        trends[QStringLiteral("A")] = {
            { 0.05, 0.10 }, { 0.20, 0.10 }, { 0.40, 0.10 }
        };
        trends[QStringLiteral("B")] = {
            { 0.10, 0.10 }, { 0.11, 0.10 }, { 0.12, 0.10 }
        };

        const auto flagged = BiasAuditor::feedbackLoopCheck(trends, 0.01);
        QVERIFY(flagged.contains(QStringLiteral("A")));
    }

    void testEqualizedOddsDiffPositiveWhenTprDiffers()
    {
        BiasAuditor auditor;
        QVector<GroupStats> stats;

        GroupStats a;
        a.groupId       = QStringLiteral("A");
        a.nTP           = 8;
        a.nFN           = 2;
        a.nActualPos    = 10;
        a.truePositiveRate = 0.8;

        GroupStats b;
        b.groupId       = QStringLiteral("B");
        b.nTP           = 2;
        b.nFN           = 8;
        b.nActualPos    = 10;
        b.truePositiveRate = 0.2;

        stats = { a, b };
        const double diff = auditor.equalizedOddsDiff(stats);
        QVERIFY(diff > 0.0);
    }

    void testFormatReportsIncludesMetric()
    {
        BiasReport rep;
        rep.metric  = QStringLiteral("disparate_impact");
        rep.groupA  = QStringLiteral("North");
        rep.groupB  = QStringLiteral("South");
        rep.ratio   = 0.6;
        rep.flagged = true;

        const QString text = BiasAuditor::formatReports({ rep });
        QVERIFY(text.contains(QStringLiteral("disparate_impact")));
        QVERIFY(text.contains(QStringLiteral("North")));
    }

    void testGroupStatsFalsePositiveRate()
    {
        QVector<QString> groups = {
            QStringLiteral("G"), QStringLiteral("G"), QStringLiteral("G"), QStringLiteral("G")
        };
        QVector<double> yTrue = { 0, 0, 1, 1 };
        QVector<double> yPred = { 0.9, 0.2, 0.8, 0.1 };

        const auto stats = BiasAuditor::groupStats(groups, yTrue, yPred);
        QVERIFY(stats[QStringLiteral("G")].falsePositiveRate >= 0.0);
        QVERIFY(stats[QStringLiteral("G")].falsePositiveRate <= 1.0);
    }

    void testDisparateImpactRatioBounded()
    {
        const QVector<QString> groups(10, QStringLiteral("X"));
        const QVector<double> yPred(10, 0.6);

        const auto reports = BiasAuditor::disparateImpact(groups, yPred);
        QVERIFY(reports.isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestBiasAuditorDeep5)
#include "test_bias_auditor_deep5.moc"
