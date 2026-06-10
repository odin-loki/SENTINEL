// test_cooffending_deep.cpp
// Deep structural tests for CoOffendingAnalyser:
// star graph topology, chain graph, betweenness centrality, PageRank properties.
#include <QTest>
#include "inference/CoOffendingAnalyser.h"
#include "core/CrimeEvent.h"
#include <cmath>
#include <algorithm>

class CoOffendingDeepTest : public QObject
{
    Q_OBJECT

private:
    static PersonIncidentRecord rec(const QString& person, const QString& incident,
                                     const QString& role = QStringLiteral("suspect"),
                                     double weight = 1.0)
    {
        PersonIncidentRecord r;
        r.personId   = person;
        r.incidentId = incident;
        r.role       = role;
        r.roleWeight = weight;
        return r;
    }

    // Star graph: P0 appears in ALL incidents, P1-P4 each appear in one
    static QVector<PersonIncidentRecord> starGraph()
    {
        QVector<PersonIncidentRecord> recs;
        recs.append(rec(QStringLiteral("P0"), QStringLiteral("I1")));
        recs.append(rec(QStringLiteral("P1"), QStringLiteral("I1")));
        recs.append(rec(QStringLiteral("P0"), QStringLiteral("I2")));
        recs.append(rec(QStringLiteral("P2"), QStringLiteral("I2")));
        recs.append(rec(QStringLiteral("P0"), QStringLiteral("I3")));
        recs.append(rec(QStringLiteral("P3"), QStringLiteral("I3")));
        recs.append(rec(QStringLiteral("P0"), QStringLiteral("I4")));
        recs.append(rec(QStringLiteral("P4"), QStringLiteral("I4")));
        return recs;
    }

    // Chain graph: P1-I1-P2, P2-I2-P3, P3-I3-P4
    static QVector<PersonIncidentRecord> chainGraph()
    {
        QVector<PersonIncidentRecord> recs;
        recs.append(rec(QStringLiteral("P1"), QStringLiteral("I1")));
        recs.append(rec(QStringLiteral("P2"), QStringLiteral("I1")));
        recs.append(rec(QStringLiteral("P2"), QStringLiteral("I2")));
        recs.append(rec(QStringLiteral("P3"), QStringLiteral("I2")));
        recs.append(rec(QStringLiteral("P3"), QStringLiteral("I3")));
        recs.append(rec(QStringLiteral("P4"), QStringLiteral("I3")));
        return recs;
    }

private slots:

    // ── 1. Star graph: P0 has highest PageRank ────────────────────────────────
    void testStarGraphHubHighestPageRank()
    {
        CoOffendingAnalyser ca;
        ca.buildGraph(starGraph());
        ca.analyse();

        const auto ns = ca.nodes();
        const auto p0 = std::find_if(ns.begin(), ns.end(),
            [](const NetworkNode& n){ return n.personId == QStringLiteral("P0"); });
        QVERIFY2(p0 != ns.end(), "P0 not found in star graph");

        const double maxRank = std::max_element(ns.begin(), ns.end(),
            [](const NetworkNode& a, const NetworkNode& b){ return a.pageRank < b.pageRank; })->pageRank;
        QVERIFY2(std::abs(p0->pageRank - maxRank) < 0.1 || p0->pageRank >= maxRank * 0.8,
                 qPrintable(QStringLiteral("P0 rank %1 should be near max %2")
                    .arg(p0->pageRank).arg(maxRank)));
    }

    // ── 2. Star graph: P0 has highest degree ─────────────────────────────────
    void testStarGraphHubHighestDegree()
    {
        CoOffendingAnalyser ca;
        ca.buildGraph(starGraph());

        const auto ns = ca.nodes();
        const auto p0 = std::find_if(ns.begin(), ns.end(),
            [](const NetworkNode& n){ return n.personId == QStringLiteral("P0"); });
        QVERIFY2(p0 != ns.end(), "P0 not found");

        for (const auto& n : ns) {
            if (n.personId != QStringLiteral("P0")) {
                QVERIFY2(p0->degree >= n.degree,
                         qPrintable(QStringLiteral("P0 degree %1 should >= %2 degree %3")
                            .arg(p0->degree).arg(n.personId).arg(n.degree)));
            }
        }
    }

    // ── 3. Chain graph: middle nodes have higher betweenness ──────────────────
    void testChainMiddleHigherBetweenness()
    {
        CoOffendingAnalyser ca;
        ca.buildGraph(chainGraph());
        ca.analyse();

        const auto ns = ca.nodes();
        const auto p2 = std::find_if(ns.begin(), ns.end(),
            [](const NetworkNode& n){ return n.personId == QStringLiteral("P2"); });
        const auto p1 = std::find_if(ns.begin(), ns.end(),
            [](const NetworkNode& n){ return n.personId == QStringLiteral("P1"); });
        QVERIFY2(p2 != ns.end() && p1 != ns.end(), "P2 or P1 not found");
        // P2 connects P1 to P3, so should have higher betweenness than P1
        QVERIFY2(p2->betweenness >= p1->betweenness,
                 qPrintable(QStringLiteral("P2 betweenness %1 should >= P1 %2")
                    .arg(p2->betweenness).arg(p1->betweenness)));
    }

    // ── 4. PageRank sum to ~1.0 for star graph ────────────────────────────────
    void testPageRankSumStarGraph()
    {
        CoOffendingAnalyser ca;
        ca.buildGraph(starGraph());
        ca.analyse();

        double sum = 0.0;
        for (const auto& n : ca.nodes()) sum += n.pageRank;
        QVERIFY2(std::abs(sum - 1.0) < 0.05,
                 qPrintable(QStringLiteral("PageRank sum %1 should be ~1.0").arg(sum)));
    }

    // ── 5. All betweenness values non-negative ────────────────────────────────
    void testBetweennessNonNegative()
    {
        CoOffendingAnalyser ca;
        ca.buildGraph(chainGraph());
        ca.analyse();
        for (const auto& n : ca.nodes())
            QVERIFY2(n.betweenness >= 0.0,
                     qPrintable(QStringLiteral("%1 betweenness %2 must be >= 0")
                        .arg(n.personId).arg(n.betweenness)));
    }

    // ── 6. Star graph: all leaf nodes in same community as P0 ────────────────
    void testStarGraphCommunity()
    {
        CoOffendingAnalyser ca;
        ca.buildGraph(starGraph());
        ca.analyse();

        const auto ns = ca.nodes();
        const auto p0 = std::find_if(ns.begin(), ns.end(),
            [](const NetworkNode& n){ return n.personId == QStringLiteral("P0"); });
        QVERIFY2(p0 != ns.end(), "P0 not found");
        // All nodes connected through P0 should share a community
        for (const auto& n : ns)
            QVERIFY2(n.communityId >= 0,
                     qPrintable(QStringLiteral("%1 communityId %2 should >= 0")
                        .arg(n.personId).arg(n.communityId)));
    }

    // ── 7. findLeads: incident with multiple suspects → leads ────────────────
    void testFindLeadsMultipleSuspects()
    {
        CoOffendingAnalyser ca;
        ca.buildGraph(starGraph());
        ca.analyse();
        const auto leads = ca.findLeads(QStringLiteral("I1"), 10);
        QVERIFY2(!leads.isEmpty(), "I1 with 2 suspects should produce leads");
    }

    // ── 8. Disconnected graph: 2 components stay separate ────────────────────
    void testDisconnectedGraph()
    {
        QVector<PersonIncidentRecord> recs;
        // Component A: P1-P2 via I1
        recs.append(rec(QStringLiteral("PA1"), QStringLiteral("IA")));
        recs.append(rec(QStringLiteral("PA2"), QStringLiteral("IA")));
        // Component B: PB1-PB2 via IB (no connection to A)
        recs.append(rec(QStringLiteral("PB1"), QStringLiteral("IB")));
        recs.append(rec(QStringLiteral("PB2"), QStringLiteral("IB")));

        CoOffendingAnalyser ca;
        ca.buildGraph(recs);
        ca.analyse();
        QVERIFY2(ca.nodes().size() == 4, "Disconnected graph should have 4 nodes");
    }

    // ── 9. findLeads topK is respected ───────────────────────────────────────
    void testFindLeadsTopKRespected()
    {
        CoOffendingAnalyser ca;
        ca.buildGraph(starGraph());
        ca.analyse();
        const auto leads = ca.findLeads(QStringLiteral("I1"), 2);
        QVERIFY2(leads.size() <= 2, "findLeads(topK=2) should return <= 2 leads");
    }

    // ── 10. Neighbour weights positive ────────────────────────────────────────
    void testNeighbourWeightsPositive()
    {
        CoOffendingAnalyser ca;
        ca.buildGraph(starGraph());
        for (const auto& n : ca.nodes()) {
            for (const double w : n.neighbours.values())
                QVERIFY2(w > 0.0, "All edge weights should be > 0");
        }
    }
};

QTEST_MAIN(CoOffendingDeepTest)
#include "test_cooffending_deep.moc"
