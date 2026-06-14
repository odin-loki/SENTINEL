// test_cooffending_deep7.cpp — Deep audit iteration 23: CoOffendingAnalyser
// star/chain PageRank, empty graph, bridge betweenness, community clusters.

#include <QtTest>
#include <algorithm>
#include <cmath>
#include "inference/CoOffendingAnalyser.h"

class CoOffendingDeep7Test : public QObject
{
    Q_OBJECT

    static PersonIncidentRecord pir(const QString& person,
                                    const QString& incident,
                                    double roleWeight = 1.0)
    {
        PersonIncidentRecord r;
        r.personId   = person;
        r.incidentId = incident;
        r.roleWeight = roleWeight;
        return r;
    }

    static double pageRankFor(const CoOffendingAnalyser& ca, const QString& person)
    {
        for (const auto& n : ca.nodes()) {
            if (n.personId == person)
                return n.pageRank;
        }
        return -1.0;
    }

    static double betweennessFor(const CoOffendingAnalyser& ca, const QString& person)
    {
        for (const auto& n : ca.nodes()) {
            if (n.personId == person)
                return n.betweenness;
        }
        return -1.0;
    }

private slots:
    void testStarGraphCenterHasHighestPageRank();
    void testChainGraphEndpointsLowerThanMiddle();
    void testEmptyGraphReturnsEmptyLeads();
    void testBetweennessBridgeNodeHighest();
    void testCommunityDetectionFindsClusters();
    void testBuildGraphWithoutAnalyseReturnsNoLeads();
    void testIsBuiltAfterGraphConstruction();
};

void CoOffendingDeep7Test::testStarGraphCenterHasHighestPageRank()
{
    CoOffendingAnalyser ca;
    ca.buildGraph({
        pir(QStringLiteral("Center"), QStringLiteral("I1")),
        pir(QStringLiteral("S1"), QStringLiteral("I1")),
        pir(QStringLiteral("Center"), QStringLiteral("I2")),
        pir(QStringLiteral("S2"), QStringLiteral("I2")),
        pir(QStringLiteral("Center"), QStringLiteral("I3")),
        pir(QStringLiteral("S3"), QStringLiteral("I3")),
        pir(QStringLiteral("Center"), QStringLiteral("I4")),
        pir(QStringLiteral("S4"), QStringLiteral("I4"))
    });
    ca.analyse();

    const double centerPr = pageRankFor(ca, QStringLiteral("Center"));
    QVERIFY(centerPr > 0.0);

    for (const QString& spoke : {QStringLiteral("S1"), QStringLiteral("S2"),
                                   QStringLiteral("S3"), QStringLiteral("S4")}) {
        const double spokePr = pageRankFor(ca, spoke);
        QVERIFY2(spokePr >= 0.0,
                 qPrintable(QStringLiteral("Missing PageRank for %1").arg(spoke)));
        QVERIFY2(centerPr >= spokePr,
                 qPrintable(QStringLiteral("Center PR %1 < spoke %2 PR %3")
                                .arg(centerPr).arg(spoke).arg(spokePr)));
    }
}

void CoOffendingDeep7Test::testChainGraphEndpointsLowerThanMiddle()
{
    CoOffendingAnalyser ca;
    ca.buildGraph({
        pir(QStringLiteral("A"), QStringLiteral("I1")),
        pir(QStringLiteral("B"), QStringLiteral("I1")),
        pir(QStringLiteral("B"), QStringLiteral("I2")),
        pir(QStringLiteral("C"), QStringLiteral("I2"))
    });
    ca.analyse();

    const double prA = pageRankFor(ca, QStringLiteral("A"));
    const double prB = pageRankFor(ca, QStringLiteral("B"));
    const double prC = pageRankFor(ca, QStringLiteral("C"));

    QVERIFY(prA >= 0.0);
    QVERIFY(prB >= 0.0);
    QVERIFY(prC >= 0.0);
    QVERIFY2(prB > prA,
             qPrintable(QStringLiteral("Middle B PR %1 not > endpoint A PR %2").arg(prB).arg(prA)));
    QVERIFY2(prB > prC,
             qPrintable(QStringLiteral("Middle B PR %1 not > endpoint C PR %2").arg(prB).arg(prC)));
}

void CoOffendingDeep7Test::testEmptyGraphReturnsEmptyLeads()
{
    CoOffendingAnalyser ca;
    ca.buildGraph({});
    ca.analyse();

    QCOMPARE(ca.nodes().size(), 0);
    QCOMPARE(ca.findLeads(QStringLiteral("I1"), 5).size(), 0);
    QCOMPARE(ca.findLeads(QStringLiteral("UNKNOWN"), 3).size(), 0);
}

void CoOffendingDeep7Test::testBetweennessBridgeNodeHighest()
{
    CoOffendingAnalyser ca;
    ca.buildGraph({
        pir(QStringLiteral("A"), QStringLiteral("I1")),
        pir(QStringLiteral("Bridge"), QStringLiteral("I1")),
        pir(QStringLiteral("Bridge"), QStringLiteral("I2")),
        pir(QStringLiteral("C"), QStringLiteral("I2")),
        pir(QStringLiteral("A"), QStringLiteral("I3")),
        pir(QStringLiteral("D"), QStringLiteral("I3")),
        pir(QStringLiteral("C"), QStringLiteral("I4")),
        pir(QStringLiteral("E"), QStringLiteral("I4"))
    });
    ca.analyse();

    const double bridgeBetw = betweennessFor(ca, QStringLiteral("Bridge"));
    QVERIFY(bridgeBetw > 0.0);

    for (const QString& leaf : {QStringLiteral("A"), QStringLiteral("C"),
                                QStringLiteral("D"), QStringLiteral("E")}) {
        const double leafBetw = betweennessFor(ca, leaf);
        QVERIFY2(bridgeBetw >= leafBetw,
                 qPrintable(QStringLiteral("Bridge betweenness %1 < leaf %2 %3")
                                .arg(bridgeBetw).arg(leaf).arg(leafBetw)));
    }
}

void CoOffendingDeep7Test::testCommunityDetectionFindsClusters()
{
    CoOffendingAnalyser ca;
    ca.buildGraph({
        pir(QStringLiteral("A1"), QStringLiteral("I1")),
        pir(QStringLiteral("A2"), QStringLiteral("I1")),
        pir(QStringLiteral("B1"), QStringLiteral("I2")),
        pir(QStringLiteral("B2"), QStringLiteral("I2")),
        pir(QStringLiteral("C1"), QStringLiteral("I3")),
        pir(QStringLiteral("C2"), QStringLiteral("I3"))
    });
    ca.analyse();

    QMap<int, QSet<QString>> byCommunity;
    for (const auto& n : ca.nodes())
        byCommunity[n.communityId].insert(n.personId);

    QCOMPARE(byCommunity.size(), 3);

    for (const auto& members : byCommunity) {
        QVERIFY2(members.size() == 2,
                 qPrintable(QStringLiteral("Expected pair per community, got %1")
                                .arg(members.size())));
    }
}

void CoOffendingDeep7Test::testBuildGraphWithoutAnalyseReturnsNoLeads()
{
    CoOffendingAnalyser ca;
    ca.buildGraph({
        pir(QStringLiteral("A"), QStringLiteral("I1")),
        pir(QStringLiteral("B"), QStringLiteral("I1"))
    });

    QVERIFY(ca.isBuilt());
    QCOMPARE(ca.findLeads(QStringLiteral("I1"), 5).size(), 0);

    ca.analyse();
    QVERIFY(!ca.findLeads(QStringLiteral("I1"), 5).isEmpty());
}

void CoOffendingDeep7Test::testIsBuiltAfterGraphConstruction()
{
    CoOffendingAnalyser ca;
    QVERIFY(!ca.isBuilt());

    ca.buildGraph({
        pir(QStringLiteral("P1"), QStringLiteral("IX")),
        pir(QStringLiteral("P2"), QStringLiteral("IX"))
    });
    QVERIFY(ca.isBuilt());
    QCOMPARE(ca.nodes().size(), 2);
}

QTEST_GUILESS_MAIN(CoOffendingDeep7Test)
#include "test_cooffending_deep7.moc"
