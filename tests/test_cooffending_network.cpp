// ─────────────────────────────────────────────────────────────────────────────
// TestCoOffendingNetwork — network-level unit tests for CoOffendingAnalyser
//
// Tests: empty network, single-person isolation, two-person link, dense
//        5-person network, score clamping, frequency-based ranking, edge
//        symmetry, isolated node, lead generation, and personId validity.
// ─────────────────────────────────────────────────────────────────────────────
#include <QTest>
#include <QCoreApplication>
#include <QSet>
#include <cmath>
#include "inference/CoOffendingAnalyser.h"

class TestCoOffendingNetwork : public QObject {
    Q_OBJECT
private slots:
    void testEmptyNetworkReturnsNothing();
    void testSinglePersonNetwork();
    void testTwoPersonsSharedIncident();
    void testNetworkDensity();
    void testCoOffendingScoreRange();
    void testMostFrequentCoOffender();
    void testSymmetricLinks();
    void testIsolatedPersonNoLinks();
    void testNetworkLeadsGenerated();
    void testPersonIdInLead();
};

// ─── Helper ──────────────────────────────────────────────────────────────────

static PersonIncidentRecord rec(const QString& person,
                                const QString& incident,
                                const QString& role = "suspect",
                                double weight = 1.0)
{
    PersonIncidentRecord r;
    r.personId   = person;
    r.incidentId = incident;
    r.role       = role;
    r.roleWeight = weight;
    return r;
}

// ─── 1. Empty network ─────────────────────────────────────────────────────────
// No records supplied → graph has no nodes; findLeads must return empty.

void TestCoOffendingNetwork::testEmptyNetworkReturnsNothing()
{
    CoOffendingAnalyser analyser;
    analyser.buildGraph({});
    analyser.analyse();

    QVERIFY(analyser.isBuilt());
    QCOMPARE(analyser.nodes().size(), 0);

    // Any incident ID query on an empty graph should yield no leads.
    const auto leads = analyser.findLeads("INC_PHANTOM", 10);
    QCOMPARE(leads.size(), 0);
}

// ─── 2. Single-person network ─────────────────────────────────────────────────
// One person in one incident with no co-participants → no edges.

void TestCoOffendingNetwork::testSinglePersonNetwork()
{
    CoOffendingAnalyser analyser;
    analyser.buildGraph({ rec("SOLO", "INC_SOLO") });
    analyser.analyse();

    const auto nodes = analyser.nodes();
    QCOMPARE(nodes.size(), 1);

    const NetworkNode& solo = nodes.first();
    QCOMPARE(solo.personId, QString("SOLO"));
    QCOMPARE(solo.degree, 0);
    QVERIFY(solo.neighbours.isEmpty());

    // The single direct participant is still returned as a lead.
    const auto leads = analyser.findLeads("INC_SOLO", 5);
    QCOMPARE(leads.size(), 1);
    QCOMPARE(leads.first().personId, QString("SOLO"));
    QCOMPARE(leads.first().connectionType, QString("direct_participant"));
}

// ─── 3. Two persons in same incident → co-offending link found ───────────────

void TestCoOffendingNetwork::testTwoPersonsSharedIncident()
{
    CoOffendingAnalyser analyser;
    analyser.buildGraph({
        rec("PA", "INC_PAIR"),
        rec("PB", "INC_PAIR")
    });
    analyser.analyse();

    const auto nodes = analyser.nodes();
    QCOMPARE(nodes.size(), 2);

    // Each node must know about the other.
    for (const auto& node : nodes) {
        QCOMPARE(node.degree, 1);
        QVERIFY2(node.neighbours.size() == 1, "Expected exactly one neighbour");
    }

    // Both appear as direct participants in leads.
    const auto leads = analyser.findLeads("INC_PAIR", 5);
    QCOMPARE(leads.size(), 2);

    QSet<QString> leadIds;
    for (const auto& l : leads) leadIds.insert(l.personId);
    QVERIFY(leadIds.contains("PA"));
    QVERIFY(leadIds.contains("PB"));

    for (const auto& l : leads)
        QCOMPARE(l.connectionType, QString("direct_participant"));
}

// ─── 4. Dense network — 5 persons sharing 3 incidents ────────────────────────
// Every person appears in every incident → fully-connected K5 graph.
// Degree of every node must be 4 (connected to all others).

void TestCoOffendingNetwork::testNetworkDensity()
{
    QVector<PersonIncidentRecord> records;
    for (int inc = 0; inc < 3; ++inc) {
        const QString incId = QString("DENSE_INC%1").arg(inc);
        for (int p = 0; p < 5; ++p)
            records << rec(QString("DP%1").arg(p), incId);
    }

    CoOffendingAnalyser analyser;
    analyser.buildGraph(records);
    analyser.analyse();

    const auto nodes = analyser.nodes();
    QCOMPARE(nodes.size(), 5);

    for (const auto& node : nodes) {
        QVERIFY2(node.degree == 4,
                 qPrintable(QString("%1 degree=%2 expected 4")
                            .arg(node.personId).arg(node.degree)));
        QVERIFY2(node.neighbours.size() == 4,
                 qPrintable(QString("%1 neighbours=%2 expected 4")
                            .arg(node.personId).arg(node.neighbours.size())));
    }
}

// ─── 5. coOffendingScore (riskScore) is always in [0, 1] ─────────────────────
// Build a mixed hub-and-spoke network and verify every lead's riskScore ∈ [0,1].

void TestCoOffendingNetwork::testCoOffendingScoreRange()
{
    QVector<PersonIncidentRecord> records;
    for (int i = 1; i <= 6; ++i) {
        const QString inc = QString("SCORE_INC%1").arg(i);
        records << rec("HUB",               inc, "suspect", 1.0)
                << rec(QString("S%1").arg(i), inc, "witness", 0.5);
    }

    CoOffendingAnalyser analyser;
    analyser.buildGraph(records);
    analyser.analyse();

    const auto leads = analyser.findLeads("SCORE_INC1", 20);
    QVERIFY2(leads.size() > 0, "Expected at least one lead");

    for (const auto& lead : leads) {
        QVERIFY2(lead.riskScore >= 0.0,
                 qPrintable(QString("%1 riskScore=%2 < 0")
                            .arg(lead.personId).arg(lead.riskScore)));
        QVERIFY2(lead.riskScore <= 1.0,
                 qPrintable(QString("%1 riskScore=%2 > 1")
                            .arg(lead.personId).arg(lead.riskScore)));
    }
}

// ─── 6. Most-frequent co-offender has highest risk score ─────────────────────
// HIGH_FREQ appears in 8 incidents; LOW_FREQ in 1.  When we query the incident
// that both share, HIGH_FREQ must have a higher riskScore than LOW_FREQ.

void TestCoOffendingNetwork::testMostFrequentCoOffender()
{
    QVector<PersonIncidentRecord> records;

    // HIGH_FREQ shares 8 incidents with distinct partners + 1 incident with LOW_FREQ.
    for (int i = 0; i < 8; ++i) {
        const QString inc = QString("FREQ_INC%1").arg(i);
        records << rec("HIGH_FREQ",                    inc)
                << rec(QString("PARTNER%1").arg(i),    inc);
    }
    // Shared incident between HIGH_FREQ and LOW_FREQ.
    records << rec("HIGH_FREQ", "SHARED_INC")
            << rec("LOW_FREQ",  "SHARED_INC");

    CoOffendingAnalyser analyser;
    analyser.buildGraph(records);
    analyser.analyse();

    const auto leads = analyser.findLeads("SHARED_INC", 10);
    QVERIFY2(leads.size() >= 2, "Expected at least HIGH_FREQ and LOW_FREQ in leads");

    double highScore = -1.0, lowScore = 2.0;
    for (const auto& lead : leads) {
        if (lead.personId == "HIGH_FREQ") highScore = lead.riskScore;
        if (lead.personId == "LOW_FREQ")  lowScore  = lead.riskScore;
    }

    QVERIFY2(highScore >= 0.0, "HIGH_FREQ not found in leads");
    QVERIFY2(lowScore  <= 1.0, "LOW_FREQ not found in leads");
    QVERIFY2(highScore > lowScore,
             qPrintable(QString("HIGH_FREQ riskScore=%1 LOW_FREQ riskScore=%2 — expected HIGH > LOW")
                        .arg(highScore).arg(lowScore)));
}

// ─── 7. Symmetric links ───────────────────────────────────────────────────────
// For every node A and every neighbour B of A, B must also list A as a neighbour.

void TestCoOffendingNetwork::testSymmetricLinks()
{
    // Create a small network: chain A–B–C, plus C–D
    QVector<PersonIncidentRecord> records;
    records << rec("SYM_A", "SYM_INC1") << rec("SYM_B", "SYM_INC1")
            << rec("SYM_B", "SYM_INC2") << rec("SYM_C", "SYM_INC2")
            << rec("SYM_C", "SYM_INC3") << rec("SYM_D", "SYM_INC3");

    CoOffendingAnalyser analyser;
    analyser.buildGraph(records);

    const auto nodes = analyser.nodes();
    // Build a lookup map for easy access.
    QMap<QString, NetworkNode> nodeMap;
    for (const auto& n : nodes) nodeMap[n.personId] = n;

    for (const auto& nodeA : nodes) {
        for (const auto& nbId : nodeA.neighbours.keys()) {
            QVERIFY2(nodeMap.contains(nbId),
                     qPrintable(QString("neighbour %1 of %2 not in graph")
                                .arg(nbId, nodeA.personId)));
            QVERIFY2(nodeMap[nbId].neighbours.contains(nodeA.personId),
                     qPrintable(QString("link %1→%2 exists but reverse %2→%1 missing")
                                .arg(nodeA.personId, nbId)));
        }
    }
}

// ─── 8. Isolated person has no co-offenders ───────────────────────────────────
// A person who appears in an incident alone accumulates no edges.

void TestCoOffendingNetwork::testIsolatedPersonNoLinks()
{
    // ALONE is the only participant in INC_ALONE;
    // separate pair P_X / P_Y share a different incident (to populate the graph).
    QVector<PersonIncidentRecord> records;
    records << rec("ALONE",  "INC_ALONE")
            << rec("P_X",    "INC_XY")
            << rec("P_Y",    "INC_XY");

    CoOffendingAnalyser analyser;
    analyser.buildGraph(records);
    analyser.analyse();

    bool foundAlone = false;
    for (const auto& node : analyser.nodes()) {
        if (node.personId == "ALONE") {
            foundAlone = true;
            QVERIFY2(node.degree == 0,
                     qPrintable(QString("ALONE degree=%1 expected 0").arg(node.degree)));
            QVERIFY2(node.neighbours.isEmpty(),
                     "ALONE should have no neighbours");
        }
    }
    QVERIFY2(foundAlone, "ALONE should be present in the graph");
}

// ─── 9. Connected network produces non-empty leads ────────────────────────────

void TestCoOffendingNetwork::testNetworkLeadsGenerated()
{
    QVector<PersonIncidentRecord> records;
    // Triangle: A–B–C each pair shares a dedicated incident.
    records << rec("TRI_A", "TRI_INC_AB") << rec("TRI_B", "TRI_INC_AB")
            << rec("TRI_B", "TRI_INC_BC") << rec("TRI_C", "TRI_INC_BC")
            << rec("TRI_A", "TRI_INC_AC") << rec("TRI_C", "TRI_INC_AC");

    CoOffendingAnalyser analyser;
    analyser.buildGraph(records);
    analyser.analyse();

    const auto leads = analyser.findLeads("TRI_INC_AB", 10);
    QVERIFY2(leads.size() > 0, "findLeads should return non-empty results for a connected network");

    // For the AB incident: TRI_A and TRI_B are direct participants; TRI_C is second-degree.
    QSet<QString> leadIds;
    for (const auto& l : leads) leadIds.insert(l.personId);
    QVERIFY(leadIds.contains("TRI_A"));
    QVERIFY(leadIds.contains("TRI_B"));
}

// ─── 10. Every lead contains a valid person ID ────────────────────────────────
// All personIds in the returned leads must come from the set of persons fed
// into buildGraph (no phantom IDs).

void TestCoOffendingNetwork::testPersonIdInLead()
{
    QVector<PersonIncidentRecord> records;
    const QStringList knownPersons = {"PID_1", "PID_2", "PID_3", "PID_4", "PID_5"};

    for (int i = 0; i < 4; ++i) {
        const QString inc = QString("PID_INC%1").arg(i);
        records << rec(knownPersons[i],   inc)
                << rec(knownPersons[i+1], inc);
    }

    CoOffendingAnalyser analyser;
    analyser.buildGraph(records);
    analyser.analyse();

    // Collect all node IDs from the graph (ground truth set).
    QSet<QString> graphPersonIds;
    for (const auto& node : analyser.nodes())
        graphPersonIds.insert(node.personId);

    // Query leads for each incident and verify every personId is valid.
    for (int i = 0; i < 4; ++i) {
        const QString inc = QString("PID_INC%1").arg(i);
        const auto leads = analyser.findLeads(inc, 10);
        for (const auto& lead : leads) {
            QVERIFY2(graphPersonIds.contains(lead.personId),
                     qPrintable(QString("lead personId '%1' not in graph for incident %2")
                                .arg(lead.personId, inc)));
            QVERIFY2(!lead.personId.isEmpty(), "lead personId must not be empty");
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────

QTEST_MAIN(TestCoOffendingNetwork)
#include "test_cooffending_network.moc"
