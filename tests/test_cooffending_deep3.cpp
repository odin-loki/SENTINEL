// Iteration 12 — CoOffendingAnalyser deep test
#include <QtTest/QtTest>
#include <cmath>
#include "inference/CoOffendingAnalyser.h"
#include "core/CrimeEvent.h"

class CoOffendingDeep3Test : public QObject
{
    Q_OBJECT

    // Helper: build a simple PersonIncidentRecord
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

    // ─── buildGraph ───────────────────────────────────────────────────────

    void testBuildGraphEmpty()
    {
        CoOffendingAnalyser ca;
        ca.buildGraph({});
        QVERIFY(ca.isBuilt());
        QVERIFY(ca.nodes().isEmpty());
    }

    void testBuildGraphSinglePerson()
    {
        CoOffendingAnalyser ca;
        ca.buildGraph({ pir("Alice", "INC001") });
        QVERIFY(ca.isBuilt());
        const auto nodes = ca.nodes();
        QCOMPARE(nodes.size(), 1);
        QCOMPARE(nodes[0].personId, QStringLiteral("Alice"));
        QCOMPARE(nodes[0].degree, 0);  // no co-offenders
    }

    void testBuildGraphTwoPeopleShareIncident()
    {
        CoOffendingAnalyser ca;
        ca.buildGraph({ pir("Alice", "INC001"), pir("Bob", "INC001") });
        QVERIFY(ca.isBuilt());
        const auto nodes = ca.nodes();
        QCOMPARE(nodes.size(), 2);

        // Each should have degree 1 (linked to the other)
        for (const auto& n : nodes) {
            QCOMPARE(n.degree, 1);
        }
    }

    void testEdgeWeightRoleProduct()
    {
        // Alice (roleWeight=1.0) and Bob (roleWeight=0.5) share INC001
        // Edge weight should be 1.0 * 0.5 = 0.5
        CoOffendingAnalyser ca;
        ca.buildGraph({ pir("Alice", "INC001", 1.0), pir("Bob", "INC001", 0.5) });
        const auto nodes = ca.nodes();
        for (const auto& n : nodes) {
            QVERIFY(!n.neighbours.isEmpty());
            for (const double w : n.neighbours) {
                QVERIFY2(std::abs(w - 0.5) < 1e-9,
                         qPrintable(QStringLiteral("edge weight expected 0.5, got %1").arg(w)));
            }
        }
    }

    // ─── PageRank ─────────────────────────────────────────────────────────

    void testPageRankSingleNode()
    {
        CoOffendingAnalyser ca;
        ca.buildGraph({ pir("Alice", "INC001") });
        ca.analyse();
        const auto nodes = ca.nodes();
        QCOMPARE(nodes.size(), 1);
        // Single isolated node: PageRank converges to (1-d)/n = 0.15
        QVERIFY(nodes[0].pageRank > 0.0);
    }

    void testPageRankSumsToOne()
    {
        // PageRank values sum to approximately 1.0
        CoOffendingAnalyser ca;
        ca.buildGraph({
            pir("A", "INC001"), pir("B", "INC001"), pir("C", "INC001"),
            pir("B", "INC002"), pir("C", "INC002"), pir("D", "INC002")
        });
        ca.analyse();
        const auto nodes = ca.nodes();
        double sum = 0.0;
        for (const auto& n : nodes) sum += n.pageRank;
        QVERIFY2(std::abs(sum - 1.0) < 0.01,
                 qPrintable(QStringLiteral("PageRank sum expected ~1, got %1").arg(sum)));
    }

    void testPageRankCentralNodeHigher()
    {
        // Star topology: center "Hub" connects to A,B,C,D via shared incidents
        // Hub participates in all incidents, others only in one → Hub should have highest rank
        CoOffendingAnalyser ca;
        ca.buildGraph({
            pir("Hub", "I1"), pir("A", "I1"),
            pir("Hub", "I2"), pir("B", "I2"),
            pir("Hub", "I3"), pir("C", "I3"),
            pir("Hub", "I4"), pir("D", "I4")
        });
        ca.analyse();
        const auto nodes = ca.nodes();

        // Find Hub's PageRank
        double hubPR = -1.0;
        for (const auto& n : nodes) {
            if (n.personId == QStringLiteral("Hub")) { hubPR = n.pageRank; break; }
        }
        QVERIFY(hubPR > 0.0);

        // Hub should have higher PageRank than any leaf
        for (const auto& n : nodes) {
            if (n.personId != QStringLiteral("Hub")) {
                QVERIFY2(hubPR >= n.pageRank,
                         qPrintable(QStringLiteral("Hub PR=%1 not >= %2 PR=%3").arg(hubPR).arg(n.personId).arg(n.pageRank)));
            }
        }
    }

    void testPageRankAllInRange()
    {
        CoOffendingAnalyser ca;
        ca.buildGraph({
            pir("A", "INC1"), pir("B", "INC1"), pir("C", "INC2"),
            pir("A", "INC2"), pir("D", "INC3"), pir("B", "INC3")
        });
        ca.analyse();
        for (const auto& n : ca.nodes()) {
            QVERIFY2(n.pageRank >= 0.0,
                     qPrintable(QStringLiteral("Negative PageRank for %1").arg(n.personId)));
        }
    }

    // ─── Betweenness centrality ────────────────────────────────────────────

    void testBetweennessPathGraph()
    {
        // Path: A—B—C  → B is the only bridge → highest betweenness
        CoOffendingAnalyser ca;
        ca.buildGraph({
            pir("A", "INC1"), pir("B", "INC1"),
            pir("B", "INC2"), pir("C", "INC2")
        });
        ca.analyse();
        const auto nodes = ca.nodes();

        double bA = -1, bB = -1, bC = -1;
        for (const auto& n : nodes) {
            if (n.personId == "A") bA = n.betweenness;
            else if (n.personId == "B") bB = n.betweenness;
            else if (n.personId == "C") bC = n.betweenness;
        }

        QVERIFY(bA >= 0.0);
        QVERIFY(bB >= 0.0);
        QVERIFY(bC >= 0.0);
        // B should have the highest betweenness as the bridge node
        QVERIFY2(bB >= bA, qPrintable(QStringLiteral("B betweenness %1 should >= A %2").arg(bB).arg(bA)));
        QVERIFY2(bB >= bC, qPrintable(QStringLiteral("B betweenness %1 should >= C %2").arg(bB).arg(bC)));
    }

    void testBetweennessAllInRange()
    {
        CoOffendingAnalyser ca;
        ca.buildGraph({
            pir("A", "I1"), pir("B", "I1"), pir("C", "I1"),
            pir("B", "I2"), pir("C", "I2"), pir("D", "I2")
        });
        ca.analyse();
        for (const auto& n : ca.nodes()) {
            QVERIFY2(n.betweenness >= 0.0,
                     qPrintable(QStringLiteral("Negative betweenness for %1").arg(n.personId)));
            QVERIFY2(n.betweenness <= 1.0,
                     qPrintable(QStringLiteral("Betweenness > 1 for %1: %2").arg(n.personId).arg(n.betweenness)));
        }
    }

    // ─── Community detection ──────────────────────────────────────────────

    void testCommunityDetectionTwoComponents()
    {
        // Component 1: A,B share INC1
        // Component 2: C,D share INC2
        CoOffendingAnalyser ca;
        ca.buildGraph({
            pir("A", "INC1"), pir("B", "INC1"),
            pir("C", "INC2"), pir("D", "INC2")
        });
        ca.analyse();
        const auto nodes = ca.nodes();

        // Extract community IDs
        QSet<int> communities;
        for (const auto& n : nodes) communities.insert(n.communityId);

        QVERIFY2(communities.size() == 2,
                 qPrintable(QStringLiteral("Expected 2 communities, got %1").arg(communities.size())));
    }

    void testCommunityDetectionSingleComponent()
    {
        // All connected: A—B—C—D
        CoOffendingAnalyser ca;
        ca.buildGraph({
            pir("A", "INC1"), pir("B", "INC1"),
            pir("B", "INC2"), pir("C", "INC2"),
            pir("C", "INC3"), pir("D", "INC3")
        });
        ca.analyse();
        const auto nodes = ca.nodes();

        QSet<int> communities;
        for (const auto& n : nodes) communities.insert(n.communityId);
        QCOMPARE(communities.size(), 1);
    }

    // ─── findLeads ────────────────────────────────────────────────────────

    void testFindLeadsUnknownIncident()
    {
        CoOffendingAnalyser ca;
        ca.buildGraph({ pir("Alice", "INC001") });
        ca.analyse();
        const auto leads = ca.findLeads("UNKNOWN_INC", 5);
        QVERIFY(leads.isEmpty());
    }

    void testFindLeadsCountBounded()
    {
        // 6 people all in same incident → topK=3 should return ≤ 3
        CoOffendingAnalyser ca;
        QVector<PersonIncidentRecord> recs;
        for (int i = 1; i <= 6; ++i)
            recs.append(pir(QStringLiteral("P%1").arg(i), "INC001"));
        ca.buildGraph(recs);
        ca.analyse();

        const auto leads = ca.findLeads("INC001", 3);
        QVERIFY2(leads.size() <= 3,
                 qPrintable(QStringLiteral("Expected ≤3 leads, got %1").arg(leads.size())));
    }

    void testFindLeadsRiskScoreInRange()
    {
        CoOffendingAnalyser ca;
        ca.buildGraph({
            pir("A", "I1"), pir("B", "I1"), pir("C", "I1"),
            pir("B", "I2"), pir("D", "I2")
        });
        ca.analyse();
        const auto leads = ca.findLeads("I1", 10);
        for (const auto& lead : leads) {
            QVERIFY2(lead.riskScore >= 0.0 && lead.riskScore <= 1.0,
                     qPrintable(QStringLiteral("riskScore out of [0,1]: %1").arg(lead.riskScore)));
        }
    }

    void testFindLeadsSortedByRiskDesc()
    {
        CoOffendingAnalyser ca;
        ca.buildGraph({
            pir("A", "I1"), pir("B", "I1"), pir("C", "I1"),
            pir("B", "I2"), pir("C", "I2"), pir("D", "I2"),
            pir("D", "I3"), pir("E", "I3")
        });
        ca.analyse();
        const auto leads = ca.findLeads("I1", 10);
        for (int i = 1; i < leads.size(); ++i) {
            QVERIFY2(leads[i-1].riskScore >= leads[i].riskScore,
                     qPrintable(QStringLiteral("Leads not sorted: [%1]=%2 > [%3]=%4")
                                .arg(i-1).arg(leads[i-1].riskScore).arg(i).arg(leads[i].riskScore)));
        }
    }

    void testFindLeadsBeforeAnalyseReturnsEmpty()
    {
        CoOffendingAnalyser ca;
        ca.buildGraph({ pir("A", "INC1"), pir("B", "INC1") });
        // Note: analyse() NOT called
        const auto leads = ca.findLeads("INC1", 5);
        QVERIFY(leads.isEmpty());
    }

    // ─── Second-degree links ──────────────────────────────────────────────

    void testSecondDegreeLinksDetected()
    {
        // A and B share INC1; B and C share INC2 → C is second-degree link for INC1
        CoOffendingAnalyser ca;
        ca.buildGraph({
            pir("A", "INC1"), pir("B", "INC1"),
            pir("B", "INC2"), pir("C", "INC2")
        });
        ca.analyse();
        const auto leads = ca.findLeads("INC1", 10);

        bool foundC = false;
        for (const auto& lead : leads) {
            if (lead.personId == "C" &&
                lead.connectionType == QStringLiteral("second_degree"))
                foundC = true;
        }
        QVERIFY2(foundC, "Expected second-degree link to C from INC1");
    }
};

QTEST_GUILESS_MAIN(CoOffendingDeep3Test)
#include "test_cooffending_deep3.moc"
