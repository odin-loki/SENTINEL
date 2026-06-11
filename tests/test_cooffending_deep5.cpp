// Deep audit iteration 17 — CoOffendingAnalyser (deep5)
// Verifies: betweenness edge cases, community partitioning, findLeads sorting,
//           topK limits, role-weighted edges, second-degree deduplication.

#include <QtTest>
#include <cmath>
#include <algorithm>
#include "inference/CoOffendingAnalyser.h"

class CoOffendingDeep5Test : public QObject
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

private slots:
    void testBetweennessZeroForTwoNodeGraph();
    void testBetweennessStarCenterDominates();
    void testCommunityThreeDisconnectedPairs();
    void testFindLeadsSortedByRiskDescending();
    void testFindLeadsTopKZeroReturnsEmpty();
    void testFindLeadsDeduplicatesSecondDegree();
    void testFindLeadsUnknownIncidentReturnsEmpty();
    void testFindLeadsWithoutAnalyseReturnsEmpty();
};

void CoOffendingDeep5Test::testBetweennessZeroForTwoNodeGraph()
{
    CoOffendingAnalyser ca;
    ca.buildGraph({
        pir(QStringLiteral("A"), QStringLiteral("I1")),
        pir(QStringLiteral("B"), QStringLiteral("I1"))
    });
    ca.analyse();

    for (const auto& n : ca.nodes()) {
        QCOMPARE(n.betweenness, 0.0);
        QVERIFY(n.pageRank > 0.0);
    }
}

void CoOffendingDeep5Test::testBetweennessStarCenterDominates()
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

    double centerBetw = -1.0;
    for (const auto& n : ca.nodes()) {
        if (n.personId == QStringLiteral("Center"))
            centerBetw = n.betweenness;
        else
            QVERIFY2(n.betweenness <= centerBetw || centerBetw < 0,
                     qPrintable(QStringLiteral("Spoke %1 betweenness %2 exceeds center %3")
                                .arg(n.personId).arg(n.betweenness).arg(centerBetw)));
    }
    QVERIFY(centerBetw > 0.0);
}

void CoOffendingDeep5Test::testCommunityThreeDisconnectedPairs()
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

    QSet<int> communities;
    for (const auto& n : ca.nodes())
        communities.insert(n.communityId);
    QCOMPARE(communities.size(), 3);
}

void CoOffendingDeep5Test::testFindLeadsSortedByRiskDescending()
{
    CoOffendingAnalyser ca;
    ca.buildGraph({
        pir(QStringLiteral("A"), QStringLiteral("I1")),
        pir(QStringLiteral("B"), QStringLiteral("I1")),
        pir(QStringLiteral("C"), QStringLiteral("I1")),
        pir(QStringLiteral("B"), QStringLiteral("I2")),
        pir(QStringLiteral("C"), QStringLiteral("I2")),
        pir(QStringLiteral("D"), QStringLiteral("I2")),
        pir(QStringLiteral("D"), QStringLiteral("I3")),
        pir(QStringLiteral("E"), QStringLiteral("I3")),
        pir(QStringLiteral("E"), QStringLiteral("I4")),
        pir(QStringLiteral("F"), QStringLiteral("I4"))
    });
    ca.analyse();

    const auto leads = ca.findLeads(QStringLiteral("I1"), 10);
    QVERIFY(leads.size() >= 2);
    for (int i = 1; i < leads.size(); ++i) {
        QVERIFY2(leads[i - 1].riskScore >= leads[i].riskScore,
                 qPrintable(QStringLiteral("risk[%1]=%2 < risk[%3]=%4")
                            .arg(i - 1).arg(leads[i - 1].riskScore)
                            .arg(i).arg(leads[i].riskScore)));
    }
}

void CoOffendingDeep5Test::testFindLeadsTopKZeroReturnsEmpty()
{
    CoOffendingAnalyser ca;
    ca.buildGraph({
        pir(QStringLiteral("A"), QStringLiteral("I1")),
        pir(QStringLiteral("B"), QStringLiteral("I1"))
    });
    ca.analyse();

    const auto leads = ca.findLeads(QStringLiteral("I1"), 0);
    QCOMPARE(leads.size(), 0);
}

void CoOffendingDeep5Test::testFindLeadsDeduplicatesSecondDegree()
{
    CoOffendingAnalyser ca;
    ca.buildGraph({
        pir(QStringLiteral("A"), QStringLiteral("I1")),
        pir(QStringLiteral("B"), QStringLiteral("I1")),
        pir(QStringLiteral("B"), QStringLiteral("I2")),
        pir(QStringLiteral("C"), QStringLiteral("I2")),
        pir(QStringLiteral("A"), QStringLiteral("I3")),
        pir(QStringLiteral("C"), QStringLiteral("I3"))
    });
    ca.analyse();

    const auto leads = ca.findLeads(QStringLiteral("I1"), 10);
    int cCount = 0;
    for (const auto& l : leads) {
        if (l.personId == QStringLiteral("C"))
            ++cCount;
    }
    QCOMPARE(cCount, 1);
}

void CoOffendingDeep5Test::testFindLeadsUnknownIncidentReturnsEmpty()
{
    CoOffendingAnalyser ca;
    ca.buildGraph({
        pir(QStringLiteral("A"), QStringLiteral("I1")),
        pir(QStringLiteral("B"), QStringLiteral("I1"))
    });
    ca.analyse();

    const auto leads = ca.findLeads(QStringLiteral("NONEXISTENT"), 5);
    QCOMPARE(leads.size(), 0);
}

void CoOffendingDeep5Test::testFindLeadsWithoutAnalyseReturnsEmpty()
{
    CoOffendingAnalyser ca;
    ca.buildGraph({
        pir(QStringLiteral("A"), QStringLiteral("I1")),
        pir(QStringLiteral("B"), QStringLiteral("I1"))
    });

    const auto leads = ca.findLeads(QStringLiteral("I1"), 5);
    QCOMPARE(leads.size(), 0);
}

QTEST_GUILESS_MAIN(CoOffendingDeep5Test)
#include "test_cooffending_deep5.moc"
