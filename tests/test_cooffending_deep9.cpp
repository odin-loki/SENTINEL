// test_cooffending_deep9.cpp — Deep audit iteration 29: CoOffendingAnalyser
// PageRank, betweenness, connectionType, community assignment.
#include <QTest>
#include "inference/CoOffendingAnalyser.h"

class CoOffendingDeep9Test : public QObject
{
    Q_OBJECT

    static PersonIncidentRecord pir(const QString& person, const QString& incident)
    {
        PersonIncidentRecord r;
        r.personId   = person;
        r.incidentId = incident;
        r.roleWeight = 1.0;
        return r;
    }

private slots:

    void testPageRankPositiveAfterAnalyse()
    {
        CoOffendingAnalyser ca;
        ca.buildGraph({
            pir(QStringLiteral("A"), QStringLiteral("I1")),
            pir(QStringLiteral("B"), QStringLiteral("I1")),
            pir(QStringLiteral("C"), QStringLiteral("I2")),
        });
        ca.analyse();

        bool anyPositive = false;
        for (const auto& n : ca.nodes()) {
            if (n.pageRank > 0.0)
                anyPositive = true;
        }
        QVERIFY(anyPositive);
    }

    void testBetweennessNonNegative()
    {
        CoOffendingAnalyser ca;
        ca.buildGraph({
            pir(QStringLiteral("Hub"), QStringLiteral("H0")),
            pir(QStringLiteral("S1"), QStringLiteral("H1")),
            pir(QStringLiteral("Hub"), QStringLiteral("H1")),
            pir(QStringLiteral("S2"), QStringLiteral("H2")),
            pir(QStringLiteral("Hub"), QStringLiteral("H2")),
        });
        ca.analyse();

        for (const auto& n : ca.nodes())
            QVERIFY(n.betweenness >= 0.0);
    }

    void testFindLeadsSetsConnectionType()
    {
        CoOffendingAnalyser ca;
        ca.buildGraph({
            pir(QStringLiteral("P1"), QStringLiteral("INC9")),
            pir(QStringLiteral("P2"), QStringLiteral("INC9")),
        });
        ca.analyse();

        const auto leads = ca.findLeads(QStringLiteral("INC9"), 5);
        QVERIFY(!leads.isEmpty());
        QVERIFY(!leads.first().connectionType.isEmpty());
    }

    void testIsBuiltAfterBuildGraph()
    {
        CoOffendingAnalyser ca;
        QVERIFY(!ca.isBuilt());
        ca.buildGraph({ pir(QStringLiteral("X"), QStringLiteral("Y")) });
        QVERIFY(ca.isBuilt());
    }

    void testCommunityIdsAssigned()
    {
        CoOffendingAnalyser ca;
        ca.buildGraph({
            pir(QStringLiteral("A"), QStringLiteral("G1")),
            pir(QStringLiteral("B"), QStringLiteral("G1")),
            pir(QStringLiteral("C"), QStringLiteral("G2")),
        });
        ca.analyse();

        bool hasCommunity = false;
        for (const auto& n : ca.nodes()) {
            if (n.communityId >= 0)
                hasCommunity = true;
        }
        QVERIFY(hasCommunity);
    }
};

QTEST_GUILESS_MAIN(CoOffendingDeep9Test)
#include "test_cooffending_deep9.moc"
