#include <QTest>
#include <algorithm>
#include <cmath>

#include "inference/CoOffendingAnalyser.h"
#include "core/CrimeEvent.h"

static PersonIncidentRecord pir(const QString& person, const QString& incident,
                                 double weight = 1.0)
{
    PersonIncidentRecord r;
    r.personId   = person;
    r.incidentId = incident;
    r.role       = QStringLiteral("suspect");
    r.roleWeight = weight;
    return r;
}

// Star graph: HUB in every incident, one unique leaf per incident.
static QVector<PersonIncidentRecord> starRecords()
{
    QVector<PersonIncidentRecord> recs;
    for (int i = 1; i <= 4; ++i) {
        recs.append(pir(QStringLiteral("HUB"), QString("I%1").arg(i)));
        recs.append(pir(QString("L%1").arg(i), QString("I%1").arg(i)));
    }
    return recs;
}

// Path graph: A-B-C (A shares I1 with B; B shares I2 with C).
static QVector<PersonIncidentRecord> pathRecords()
{
    return {
        pir(QStringLiteral("A"), QStringLiteral("I1")),
        pir(QStringLiteral("B"), QStringLiteral("I1")),
        pir(QStringLiteral("B"), QStringLiteral("I2")),
        pir(QStringLiteral("C"), QStringLiteral("I2")),
    };
}

class TestCoOffendingDeep2 : public QObject
{
    Q_OBJECT
private slots:
    void testPageRankStarHubHighest();
    void testBetweennessPathMiddleHighest();
    void testBetweennessPathNormalizedToOne();
    void testCommunityDisconnectedCliques();
    void testEmptyGraphReturnsEmpty();
    void testSelfLoopsNoInfiniteLoop();
    void testRiskScoreInRange();
    void testFindLeadsTopK();
    void testSingleNode();
};

// In a star graph, the hub node accumulates PR from all four leaves on each
// iteration; after convergence it should hold the highest PageRank value.
void TestCoOffendingDeep2::testPageRankStarHubHighest()
{
    CoOffendingAnalyser ca;
    ca.buildGraph(starRecords());
    ca.analyse();

    const auto ns = ca.nodes();
    const auto hub = std::find_if(ns.begin(), ns.end(),
        [](const NetworkNode& n){ return n.personId == QStringLiteral("HUB"); });
    QVERIFY2(hub != ns.end(), "HUB node must exist after buildGraph");

    for (const auto& n : ns) {
        if (n.personId == QStringLiteral("HUB")) continue;
        QVERIFY2(hub->pageRank >= n.pageRank,
                 qPrintable(QString("HUB PR %1 must be >= %2 PR %3")
                    .arg(hub->pageRank).arg(n.personId).arg(n.pageRank)));
    }
}

// In the path graph A—B—C, every shortest path from A to C (and C to A) passes
// through B.  Brandes betweenness correctly gives B the highest centrality.
void TestCoOffendingDeep2::testBetweennessPathMiddleHighest()
{
    CoOffendingAnalyser ca;
    ca.buildGraph(pathRecords());
    ca.analyse();

    const auto ns = ca.nodes();
    const auto a  = std::find_if(ns.begin(), ns.end(),
        [](const NetworkNode& n){ return n.personId == QStringLiteral("A"); });
    const auto b  = std::find_if(ns.begin(), ns.end(),
        [](const NetworkNode& n){ return n.personId == QStringLiteral("B"); });
    const auto c  = std::find_if(ns.begin(), ns.end(),
        [](const NetworkNode& n){ return n.personId == QStringLiteral("C"); });

    QVERIFY2(a != ns.end() && b != ns.end() && c != ns.end(),
             "Nodes A, B, C must all exist");

    QVERIFY2(b->betweenness >= a->betweenness,
             qPrintable(QString("B betweenness %1 must >= A betweenness %2")
                .arg(b->betweenness).arg(a->betweenness)));
    QVERIFY2(b->betweenness >= c->betweenness,
             qPrintable(QString("B betweenness %1 must >= C betweenness %2")
                .arg(b->betweenness).arg(c->betweenness)));
    QVERIFY2(b->betweenness > 0.0,
             qPrintable(QString("B betweenness %1 must be > 0 for 3-node path")
                .arg(b->betweenness)));
}

// Two completely disconnected 3-cliques (A-group and B-group share no incident)
// must receive different community IDs because union-find only merges nodes that
// are reachable via edges.
void TestCoOffendingDeep2::testCommunityDisconnectedCliques()
{
    QVector<PersonIncidentRecord> recs;
    recs.append(pir("PA1", "IA"));
    recs.append(pir("PA2", "IA"));
    recs.append(pir("PA3", "IA"));
    recs.append(pir("PB1", "IB"));
    recs.append(pir("PB2", "IB"));
    recs.append(pir("PB3", "IB"));

    CoOffendingAnalyser ca;
    ca.buildGraph(recs);
    ca.analyse();

    const auto ns = ca.nodes();
    QCOMPARE(ns.size(), 6);

    int communityA = -1, communityB = -1;
    for (const auto& n : ns) {
        if (n.personId.startsWith(QStringLiteral("PA")))
            communityA = (communityA == -1) ? n.communityId : communityA;
        else
            communityB = (communityB == -1) ? n.communityId : communityB;
    }

    QVERIFY2(communityA >= 0, "A-group must have a valid community ID (>= 0)");
    QVERIFY2(communityB >= 0, "B-group must have a valid community ID (>= 0)");
    QVERIFY2(communityA != communityB,
             qPrintable(QString("Disconnected cliques must be in different communities "
                                "(A=%1, B=%2)").arg(communityA).arg(communityB)));

    // All A-nodes share one community; all B-nodes share another.
    for (const auto& n : ns) {
        if (n.personId.startsWith(QStringLiteral("PA")))
            QCOMPARE(n.communityId, communityA);
        else
            QCOMPARE(n.communityId, communityB);
    }
}

// Before buildGraph() or after buildGraph({}), isBuilt() / nodes() / findLeads()
// must return sensible empty results without crashing.
void TestCoOffendingDeep2::testEmptyGraphReturnsEmpty()
{
    CoOffendingAnalyser ca;
    QVERIFY(!ca.isBuilt());

    const auto preLeads = ca.findLeads(QStringLiteral("I1"), 5);
    QVERIFY2(preLeads.isEmpty(), "findLeads before build must return empty");

    ca.buildGraph({});
    ca.analyse();

    QVERIFY2(ca.nodes().isEmpty(), "Empty buildGraph must produce 0 nodes");

    const auto postLeads = ca.findLeads(QStringLiteral("I1"), 5);
    QVERIFY2(postLeads.isEmpty(), "findLeads on empty graph must return empty");
}

// Adding the same person twice to the same incident is deduplicated by QSet,
// so no self-loop edge is created and analyse() must finish without hanging.
void TestCoOffendingDeep2::testSelfLoopsNoInfiniteLoop()
{
    QVector<PersonIncidentRecord> recs;
    recs.append(pir("P1", "I1"));
    recs.append(pir("P1", "I1"));  // duplicate — collapsed by QSet
    recs.append(pir("P2", "I1"));

    CoOffendingAnalyser ca;
    ca.buildGraph(recs);
    ca.analyse();   // must complete

    const auto ns = ca.nodes();
    QVERIFY2(ns.size() == 2, "Duplicate record must not create an extra node");

    for (const auto& n : ns) {
        QVERIFY2(!n.neighbours.contains(n.personId),
                 qPrintable(QString("Node %1 must not have a self-loop")
                    .arg(n.personId)));
    }
}

// For the 3-node path A-B-C the only non-trivial shortest path is A→C (and C→A),
// both go through B.  Brandes gives raw betw[B] = 2; norm = 1/((3-1)*(3-2)) = 0.5.
// betweenness[B] = 2 * 0.5 = 1.0.
void TestCoOffendingDeep2::testBetweennessPathNormalizedToOne()
{
    CoOffendingAnalyser ca;
    ca.buildGraph(pathRecords());
    ca.analyse();

    const auto ns = ca.nodes();
    const auto b  = std::find_if(ns.begin(), ns.end(),
        [](const NetworkNode& n){ return n.personId == QStringLiteral("B"); });
    QVERIFY2(b != ns.end(), "Node B must exist");

    const double bw = b->betweenness;
    QVERIFY2(std::abs(bw - 1.0) < 1e-9,
             qPrintable(QString("B betweenness must be 1.0 for A-B-C path, got %1").arg(bw)));
}

// riskScore() is internal but is surfaced via findLeads().riskScore.
// For a well-connected graph the scores must all lie in [0, 1].
void TestCoOffendingDeep2::testRiskScoreInRange()
{
    // Build a denser graph: 4 people sharing incidents in different combos.
    QVector<PersonIncidentRecord> recs;
    recs.append(pir("X1", "IA"));
    recs.append(pir("X2", "IA"));
    recs.append(pir("X2", "IB"));
    recs.append(pir("X3", "IB"));
    recs.append(pir("X3", "IC"));
    recs.append(pir("X4", "IC"));

    CoOffendingAnalyser ca;
    ca.buildGraph(recs);
    ca.analyse();

    // findLeads for "IA" pulls direct & second-degree candidates
    const auto leads = ca.findLeads(QStringLiteral("IA"), 10);
    for (const auto& l : leads) {
        QVERIFY2(l.riskScore >= 0.0 && l.riskScore <= 1.0,
                 qPrintable(QString("riskScore %1 for %2 must be in [0,1]")
                    .arg(l.riskScore).arg(l.personId)));
    }

    // Also check all nodes directly.
    for (const auto& node : ca.nodes()) {
        QVERIFY2(node.pageRank >= 0.0,
                 qPrintable(QString("pageRank %1 for %2 must be >= 0").arg(node.pageRank).arg(node.personId)));
        QVERIFY2(node.betweenness >= 0.0,
                 qPrintable(QString("betweenness %1 for %2 must be >= 0").arg(node.betweenness).arg(node.personId)));
    }
}

// findLeads must return at most topK entries, and they must be sorted by
// riskScore descending (no adjacent pair is out of order).
void TestCoOffendingDeep2::testFindLeadsTopK()
{
    // 6 people all in one incident — 6 direct participants.
    QVector<PersonIncidentRecord> recs;
    for (int i = 1; i <= 6; ++i)
        recs.append(pir(QString("P%1").arg(i), QStringLiteral("IX")));

    CoOffendingAnalyser ca;
    ca.buildGraph(recs);
    ca.analyse();

    const int topK = 3;
    const auto leads = ca.findLeads(QStringLiteral("IX"), topK);
    QVERIFY2(leads.size() <= topK,
             qPrintable(QString("findLeads must return <= %1 leads, got %2").arg(topK).arg(leads.size())));

    for (int i = 1; i < leads.size(); ++i) {
        QVERIFY2(leads[i - 1].riskScore >= leads[i].riskScore,
                 qPrintable(QString("Leads not sorted DESC: [%1]=%2 > [%3]=%4")
                    .arg(i - 1).arg(leads[i - 1].riskScore).arg(i).arg(leads[i].riskScore)));
    }
}

// Single-node graph: buildGraph + analyse must not crash.
// findLeads for an unrelated incidentId must return empty.
void TestCoOffendingDeep2::testSingleNode()
{
    QVector<PersonIncidentRecord> recs;
    recs.append(pir("SOLO", "INCIDENT_1"));

    CoOffendingAnalyser ca;
    ca.buildGraph(recs);
    ca.analyse();  // must not crash or hang

    QCOMPARE(ca.nodes().size(), 1);

    // Known incident: the solo person is the only participant — no second-degree.
    const auto leads = ca.findLeads(QStringLiteral("INCIDENT_1"), 5);
    // Should return the solo participant as a direct lead (1 entry).
    QVERIFY2(leads.size() <= 1, "Single-node graph must produce at most 1 lead");

    // Unknown incident: no participants → empty result.
    const auto unknown = ca.findLeads(QStringLiteral("NO_SUCH_INCIDENT"), 5);
    QVERIFY2(unknown.isEmpty(), "findLeads for unknown incident must be empty");
}

QTEST_GUILESS_MAIN(TestCoOffendingDeep2)
#include "test_cooffending_deep2.moc"
