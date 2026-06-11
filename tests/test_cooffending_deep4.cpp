// Deep audit iteration 14 — CoOffendingAnalyser (deep4)
// Verifies: PageRank convergence, Brandes betweenness, Union-Find communities,
//           findLeads topK limit, shared-incident counting.

#include <QtTest>
#include <cmath>
#include <algorithm>
#include "inference/CoOffendingAnalyser.h"

class CoOffendingDeep4Test : public QObject
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
    void testPageRankSumsToOne();
    void testPageRankConvergesForLargeGraph();
    void testPageRankCentralNodeHigher();
    void testBetweennessBridgeNodeHighest();
    void testBetweennessNormalizedInRange();
    void testCommunityDetectionTwoComponents();
    void testCommunityDetectionSingleComponent();
    void testFindLeadsTopKLimit();
    void testFindLeadsSortedByRiskDesc();
    void testFindLeadsSharedIncidentsCoOffendingOnly();
    void testSecondDegreeLinksDetected();
};

void CoOffendingDeep4Test::testPageRankSumsToOne()
{
    CoOffendingAnalyser ca;
    ca.buildGraph({
        pir(QStringLiteral("A"), QStringLiteral("I1")),
        pir(QStringLiteral("B"), QStringLiteral("I1")),
        pir(QStringLiteral("C"), QStringLiteral("I1")),
        pir(QStringLiteral("B"), QStringLiteral("I2")),
        pir(QStringLiteral("C"), QStringLiteral("I2")),
        pir(QStringLiteral("D"), QStringLiteral("I2"))
    });
    ca.analyse();

    double sum = 0.0;
    for (const auto& n : ca.nodes())
        sum += n.pageRank;
    QVERIFY2(std::abs(sum - 1.0) < 0.01,
             qPrintable(QStringLiteral("PageRank sum expected ~1, got %1").arg(sum)));
}

void CoOffendingDeep4Test::testPageRankConvergesForLargeGraph()
{
    CoOffendingAnalyser ca;
    QVector<PersonIncidentRecord> recs;
    for (int i = 0; i < 20; ++i) {
        const QString hub = QStringLiteral("H%1").arg(i);
        recs << pir(hub, QStringLiteral("INC%1").arg(i))
             << pir(QStringLiteral("L%1A").arg(i), QStringLiteral("INC%1").arg(i))
             << pir(QStringLiteral("L%1B").arg(i), QStringLiteral("INC%1").arg(i));
    }
    ca.buildGraph(recs);
    ca.analyse();

    double sum = 0.0;
    for (const auto& n : ca.nodes()) {
        QVERIFY(n.pageRank >= 0.0);
        sum += n.pageRank;
    }
    QVERIFY(std::abs(sum - 1.0) < 0.02);
}

void CoOffendingDeep4Test::testPageRankCentralNodeHigher()
{
    CoOffendingAnalyser ca;
    ca.buildGraph({
        pir(QStringLiteral("Hub"), QStringLiteral("I1")),
        pir(QStringLiteral("A"), QStringLiteral("I1")),
        pir(QStringLiteral("Hub"), QStringLiteral("I2")),
        pir(QStringLiteral("B"), QStringLiteral("I2")),
        pir(QStringLiteral("Hub"), QStringLiteral("I3")),
        pir(QStringLiteral("C"), QStringLiteral("I3")),
        pir(QStringLiteral("Hub"), QStringLiteral("I4")),
        pir(QStringLiteral("D"), QStringLiteral("I4"))
    });
    ca.analyse();

    double hubPR = 0.0;
    for (const auto& n : ca.nodes()) {
        if (n.personId == QStringLiteral("Hub"))
            hubPR = n.pageRank;
    }
    QVERIFY(hubPR > 0.0);
    for (const auto& n : ca.nodes()) {
        if (n.personId != QStringLiteral("Hub")) {
            QVERIFY2(hubPR >= n.pageRank,
                     qPrintable(QStringLiteral("Hub PR=%1 not >= %2 PR=%3")
                                .arg(hubPR).arg(n.personId).arg(n.pageRank)));
        }
    }
}

void CoOffendingDeep4Test::testBetweennessBridgeNodeHighest()
{
    CoOffendingAnalyser ca;
    ca.buildGraph({
        pir(QStringLiteral("A"), QStringLiteral("I1")),
        pir(QStringLiteral("B"), QStringLiteral("I1")),
        pir(QStringLiteral("B"), QStringLiteral("I2")),
        pir(QStringLiteral("C"), QStringLiteral("I2"))
    });
    ca.analyse();

    double bA = -1, bB = -1, bC = -1;
    for (const auto& n : ca.nodes()) {
        if (n.personId == QStringLiteral("A")) bA = n.betweenness;
        else if (n.personId == QStringLiteral("B")) bB = n.betweenness;
        else if (n.personId == QStringLiteral("C")) bC = n.betweenness;
    }
    QVERIFY(bB >= bA);
    QVERIFY(bB >= bC);
}

void CoOffendingDeep4Test::testBetweennessNormalizedInRange()
{
    CoOffendingAnalyser ca;
    ca.buildGraph({
        pir(QStringLiteral("A"), QStringLiteral("I1")),
        pir(QStringLiteral("B"), QStringLiteral("I1")),
        pir(QStringLiteral("C"), QStringLiteral("I1")),
        pir(QStringLiteral("B"), QStringLiteral("I2")),
        pir(QStringLiteral("C"), QStringLiteral("I2")),
        pir(QStringLiteral("D"), QStringLiteral("I2"))
    });
    ca.analyse();

    for (const auto& n : ca.nodes()) {
        QVERIFY(n.betweenness >= 0.0);
        QVERIFY(n.betweenness <= 1.0);
    }
}

void CoOffendingDeep4Test::testCommunityDetectionTwoComponents()
{
    CoOffendingAnalyser ca;
    ca.buildGraph({
        pir(QStringLiteral("A"), QStringLiteral("I1")),
        pir(QStringLiteral("B"), QStringLiteral("I1")),
        pir(QStringLiteral("C"), QStringLiteral("I2")),
        pir(QStringLiteral("D"), QStringLiteral("I2"))
    });
    ca.analyse();

    QSet<int> communities;
    for (const auto& n : ca.nodes())
        communities.insert(n.communityId);
    QCOMPARE(communities.size(), 2);
}

void CoOffendingDeep4Test::testCommunityDetectionSingleComponent()
{
    CoOffendingAnalyser ca;
    ca.buildGraph({
        pir(QStringLiteral("A"), QStringLiteral("I1")),
        pir(QStringLiteral("B"), QStringLiteral("I1")),
        pir(QStringLiteral("B"), QStringLiteral("I2")),
        pir(QStringLiteral("C"), QStringLiteral("I2")),
        pir(QStringLiteral("C"), QStringLiteral("I3")),
        pir(QStringLiteral("D"), QStringLiteral("I3"))
    });
    ca.analyse();

    QSet<int> communities;
    for (const auto& n : ca.nodes())
        communities.insert(n.communityId);
    QCOMPARE(communities.size(), 1);
}

void CoOffendingDeep4Test::testFindLeadsTopKLimit()
{
    CoOffendingAnalyser ca;
    QVector<PersonIncidentRecord> recs;
    for (int i = 1; i <= 8; ++i)
        recs.append(pir(QStringLiteral("P%1").arg(i), QStringLiteral("INC001")));
    ca.buildGraph(recs);
    ca.analyse();

    const auto leads = ca.findLeads(QStringLiteral("INC001"), 3);
    QCOMPARE(leads.size(), 3);
}

void CoOffendingDeep4Test::testFindLeadsSortedByRiskDesc()
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
        pir(QStringLiteral("E"), QStringLiteral("I3"))
    });
    ca.analyse();

    const auto leads = ca.findLeads(QStringLiteral("I1"), 10);
    for (int i = 1; i < leads.size(); ++i)
        QVERIFY(leads[i - 1].riskScore >= leads[i].riskScore);
}

void CoOffendingDeep4Test::testFindLeadsSharedIncidentsCoOffendingOnly()
{
    CoOffendingAnalyser ca;
    ca.buildGraph({
        pir(QStringLiteral("Solo"), QStringLiteral("SOLO_INC")),
        pir(QStringLiteral("A"), QStringLiteral("CO_INC")),
        pir(QStringLiteral("B"), QStringLiteral("CO_INC"))
    });
    ca.analyse();

    const auto soloLeads = ca.findLeads(QStringLiteral("SOLO_INC"), 5);
    QCOMPARE(soloLeads.size(), 1);
    QCOMPARE(soloLeads[0].personId, QStringLiteral("Solo"));
    QCOMPARE(soloLeads[0].sharedIncidents, 0);

    const auto coLeads = ca.findLeads(QStringLiteral("CO_INC"), 5);
    for (const auto& lead : coLeads) {
        if (lead.personId == QStringLiteral("A") || lead.personId == QStringLiteral("B"))
            QCOMPARE(lead.sharedIncidents, 1);
    }
}

void CoOffendingDeep4Test::testSecondDegreeLinksDetected()
{
    CoOffendingAnalyser ca;
    ca.buildGraph({
        pir(QStringLiteral("A"), QStringLiteral("I1")),
        pir(QStringLiteral("B"), QStringLiteral("I1")),
        pir(QStringLiteral("B"), QStringLiteral("I2")),
        pir(QStringLiteral("C"), QStringLiteral("I2"))
    });
    ca.analyse();

    const auto leads = ca.findLeads(QStringLiteral("I1"), 10);
    const bool found = std::any_of(leads.begin(), leads.end(),
        [](const NetworkLead& nl) {
            return nl.personId == QStringLiteral("C")
                && nl.connectionType == QStringLiteral("second_degree");
        });
    QVERIFY(found);
}

QTEST_GUILESS_MAIN(CoOffendingDeep4Test)
#include "test_cooffending_deep4.moc"
