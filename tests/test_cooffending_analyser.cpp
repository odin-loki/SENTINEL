// test_cooffending_analyser.cpp
// Tests CoOffendingAnalyser: graph building, PageRank, betweenness, community
// detection, findLeads, and edge cases.
#include <QTest>
#include "inference/CoOffendingAnalyser.h"
#include "core/CrimeEvent.h"
#include <cmath>
#include <algorithm>

class CoOffendingAnalyserTest : public QObject
{
    Q_OBJECT

private:
    // Build a simple 3-person triangle: P1–P2–P3 all share incident I1
    // Plus P1 and P2 share incident I2.
    static QVector<PersonIncidentRecord> triangleRecords()
    {
        QVector<PersonIncidentRecord> recs;
        auto add = [&](const QString& p, const QString& i, const QString& role, double w) {
            PersonIncidentRecord r;
            r.personId   = p;
            r.incidentId = i;
            r.role       = role;
            r.roleWeight = w;
            recs.append(r);
        };
        add(QStringLiteral("P1"), QStringLiteral("I1"), QStringLiteral("suspect"), 1.0);
        add(QStringLiteral("P2"), QStringLiteral("I1"), QStringLiteral("suspect"), 1.0);
        add(QStringLiteral("P3"), QStringLiteral("I1"), QStringLiteral("associate"), 0.5);
        add(QStringLiteral("P1"), QStringLiteral("I2"), QStringLiteral("suspect"), 1.0);
        add(QStringLiteral("P2"), QStringLiteral("I2"), QStringLiteral("witness"), 0.3);
        return recs;
    }

private slots:

    // ── 1. isBuilt() false before buildGraph() ────────────────────────────────
    void testNotBuiltBeforeGraph()
    {
        CoOffendingAnalyser ca;
        QVERIFY(!ca.isBuilt());
    }

    // ── 2. isBuilt() true after buildGraph() ─────────────────────────────────
    void testBuiltAfterGraph()
    {
        CoOffendingAnalyser ca;
        ca.buildGraph(triangleRecords());
        QVERIFY2(ca.isBuilt(), "isBuilt() should be true after buildGraph()");
    }

    // ── 3. nodes() returns correct count ─────────────────────────────────────
    void testNodeCount()
    {
        CoOffendingAnalyser ca;
        ca.buildGraph(triangleRecords());
        const auto ns = ca.nodes();
        QVERIFY2(ns.size() == 3,
                 qPrintable(QStringLiteral("Expected 3 nodes, got %1").arg(ns.size())));
    }

    // ── 4. PageRank: all ranks sum to ~1.0 ───────────────────────────────────
    void testPageRankSumToOne()
    {
        CoOffendingAnalyser ca;
        ca.buildGraph(triangleRecords());
        ca.analyse();

        double sumRank = 0.0;
        for (const auto& n : ca.nodes())
            sumRank += n.pageRank;

        QVERIFY2(std::abs(sumRank - 1.0) < 0.05,
                 qPrintable(QStringLiteral("PageRank sum %1 should be ~1.0").arg(sumRank)));
    }

    // ── 5. PageRank: P1 (most connected) has highest rank ────────────────────
    void testPageRankMostConnectedHighest()
    {
        CoOffendingAnalyser ca;
        ca.buildGraph(triangleRecords());
        ca.analyse();

        const auto ns = ca.nodes();
        const auto p1It = std::find_if(ns.begin(), ns.end(),
            [](const NetworkNode& n){ return n.personId == QStringLiteral("P1"); });
        QVERIFY2(p1It != ns.end(), "P1 node not found");

        const double maxRank = std::max_element(ns.begin(), ns.end(),
            [](const NetworkNode& a, const NetworkNode& b){ return a.pageRank < b.pageRank; })->pageRank;

        QVERIFY2(p1It->pageRank == maxRank || std::abs(p1It->pageRank - maxRank) < 0.05,
                 qPrintable(QStringLiteral("P1 rank %1 should be near max %2")
                    .arg(p1It->pageRank).arg(maxRank)));
    }

    // ── 6. Degree: each node has degree >= 1 ─────────────────────────────────
    void testNodeDegreePositive()
    {
        CoOffendingAnalyser ca;
        ca.buildGraph(triangleRecords());
        for (const auto& n : ca.nodes()) {
            QVERIFY2(n.degree >= 1 || !n.neighbours.isEmpty(),
                     qPrintable(QStringLiteral("Node %1 should have degree >= 1").arg(n.personId)));
        }
    }

    // ── 7. Community IDs assigned after analyse() ────────────────────────────
    void testCommunityIdsAssigned()
    {
        CoOffendingAnalyser ca;
        ca.buildGraph(triangleRecords());
        ca.analyse();
        for (const auto& n : ca.nodes()) {
            QVERIFY2(n.communityId >= 0,
                     qPrintable(QStringLiteral("Node %1 communityId should be >= 0").arg(n.personId)));
        }
    }

    // ── 8. findLeads() returns leads for a known incident ────────────────────
    void testFindLeadsNotEmpty()
    {
        CoOffendingAnalyser ca;
        ca.buildGraph(triangleRecords());
        ca.analyse();
        const auto leads = ca.findLeads(QStringLiteral("I1"), 5);
        QVERIFY2(!leads.isEmpty(), "findLeads for I1 should return at least one lead");
    }

    // ── 9. findLeads() topK respected ────────────────────────────────────────
    void testFindLeadsTopK()
    {
        CoOffendingAnalyser ca;
        ca.buildGraph(triangleRecords());
        ca.analyse();
        const auto leads = ca.findLeads(QStringLiteral("I1"), 2);
        QVERIFY2(leads.size() <= 2,
                 qPrintable(QStringLiteral("findLeads topK=2 returned %1 leads").arg(leads.size())));
    }

    // ── 10. Empty records → nodes empty, no crash ─────────────────────────────
    void testEmptyRecordsNoCrash()
    {
        CoOffendingAnalyser ca;
        ca.buildGraph({});
        ca.analyse();
        QVERIFY(ca.nodes().isEmpty());
    }
};

QTEST_MAIN(CoOffendingAnalyserTest)
#include "test_cooffending_analyser.moc"
