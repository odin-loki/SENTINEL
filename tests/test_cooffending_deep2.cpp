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
    void testCommunityDisconnectedCliques();
    void testEmptyGraphReturnsEmpty();
    void testSelfLoopsNoInfiniteLoop();
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

QTEST_GUILESS_MAIN(TestCoOffendingDeep2)
#include "test_cooffending_deep2.moc"
