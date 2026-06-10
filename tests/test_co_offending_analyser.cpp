// ─────────────────────────────────────────────────────────────────────────────
// TestCoOffendingAnalyser — unit tests for CoOffendingAnalyser
//
// Coverage:
//   Construction, graph building, NetworkNode / NetworkLead structures,
//   co-offender lookup, density/connectivity, high-frequency co-offending
//   scores, and edge cases (single person, no shared incidents, isolation).
// ─────────────────────────────────────────────────────────────────────────────
#include <QTest>
#include <QCoreApplication>
#include <QSet>
#include <cmath>

#include "inference/CoOffendingAnalyser.h"

class TestCoOffendingAnalyser : public QObject {
    Q_OBJECT
private slots:
    void testDefaultConstruction();
    void testBuildGraphSetsBuiltFlag();
    void testSinglePersonNoEdge();
    void testTwoPersonsOneIncident();
    void testNetworkNodeFields();
    void testNetworkLeadFields();
    void testNoSharedIncidentsNoEdge();
    void testFullyIsolatedSuspects();
    void testHighCooffendingScore();
    void testFindLeadsReturnsSortedByRisk();
    void testFindLeadsForUnknownIncidentIsEmpty();
    void testDegreeCounting();
    void testAnalyseSetsPageRankPositive();
    void testNetworkDensityThreeClique();
    void testRoleWeightAccumulation();
    void testMultipleIncidentsAccumulateEdgeWeight();
    void testLeadReasoningNotEmpty();
    void testCommunityIdAssignedAfterAnalyse();
};

// ─── Helpers ─────────────────────────────────────────────────────────────────

static PersonIncidentRecord rec(const QString& person,
                                 const QString& incident,
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

// ─── 1. Default construction ─────────────────────────────────────────────────

void TestCoOffendingAnalyser::testDefaultConstruction()
{
    CoOffendingAnalyser a;
    QVERIFY(!a.isBuilt());
    QVERIFY(a.nodes().isEmpty());
}

// ─── 2. buildGraph sets the built flag ───────────────────────────────────────

void TestCoOffendingAnalyser::testBuildGraphSetsBuiltFlag()
{
    CoOffendingAnalyser a;
    QVector<PersonIncidentRecord> records;
    records << rec("P1", "I1") << rec("P2", "I1");

    a.buildGraph(records);
    QVERIFY(a.isBuilt());
}

// ─── 3. Single person creates one node but no edges ──────────────────────────

void TestCoOffendingAnalyser::testSinglePersonNoEdge()
{
    CoOffendingAnalyser a;
    QVector<PersonIncidentRecord> records;
    records << rec("SOLO", "I1");

    a.buildGraph(records);
    QVERIFY(a.isBuilt());

    const auto nodes = a.nodes();
    QCOMPARE(nodes.size(), 1);
    QCOMPARE(nodes.first().personId, QStringLiteral("SOLO"));
    QCOMPARE(nodes.first().degree, 0);
    QVERIFY(nodes.first().neighbours.isEmpty());
}

// ─── 4. Two persons sharing one incident → symmetric edge weight 1.0 ─────────

void TestCoOffendingAnalyser::testTwoPersonsOneIncident()
{
    CoOffendingAnalyser a;
    QVector<PersonIncidentRecord> records;
    records << rec("A", "INC1") << rec("B", "INC1");

    a.buildGraph(records);

    const auto nodes = a.nodes();
    QCOMPARE(nodes.size(), 2);

    bool aSeesB = false, bSeesA = false;
    for (const auto& n : nodes) {
        if (n.personId == "A") {
            QCOMPARE(n.degree, 1);
            QVERIFY(n.neighbours.contains("B"));
            QVERIFY(std::abs(n.neighbours["B"] - 1.0) < 1e-9);
            aSeesB = true;
        }
        if (n.personId == "B") {
            QCOMPARE(n.degree, 1);
            QVERIFY(n.neighbours.contains("A"));
            bSeesA = true;
        }
    }
    QVERIFY(aSeesB);
    QVERIFY(bSeesA);
}

// ─── 5. NetworkNode fields are populated after buildGraph ────────────────────

void TestCoOffendingAnalyser::testNetworkNodeFields()
{
    CoOffendingAnalyser a;
    QVector<PersonIncidentRecord> records;
    records << rec("X", "I1") << rec("Y", "I1") << rec("X", "I2");

    a.buildGraph(records);

    bool xFound = false;
    for (const auto& n : a.nodes()) {
        if (n.personId == "X") {
            xFound = true;
            QVERIFY(n.incidentIds.contains("I1"));
            QVERIFY(n.incidentIds.contains("I2"));
            QCOMPARE(n.degree, 1);  // only shares I1 with Y
        }
    }
    QVERIFY(xFound);
}

// ─── 6. NetworkLead fields are populated after findLeads ─────────────────────

void TestCoOffendingAnalyser::testNetworkLeadFields()
{
    CoOffendingAnalyser a;
    QVector<PersonIncidentRecord> records;
    records << rec("A", "I1") << rec("B", "I1");
    a.buildGraph(records);
    a.analyse();

    const auto leads = a.findLeads("I1", 10);
    QVERIFY(!leads.isEmpty());

    for (const auto& lead : leads) {
        QVERIFY(!lead.personId.isEmpty());
        QVERIFY(!lead.connectionType.isEmpty());
        QVERIFY(lead.riskScore >= 0.0);
        QVERIFY(lead.sharedIncidents >= 0);
    }
}

// ─── 7. Persons with no shared incidents have no edge ────────────────────────

void TestCoOffendingAnalyser::testNoSharedIncidentsNoEdge()
{
    CoOffendingAnalyser a;
    QVector<PersonIncidentRecord> records;
    // Two separate incidents, each with a different suspect
    records << rec("A", "INCX") << rec("B", "INCY");

    a.buildGraph(records);

    const auto nodes = a.nodes();
    QCOMPARE(nodes.size(), 2);

    for (const auto& n : nodes) {
        QCOMPARE(n.degree, 0);
        QVERIFY(n.neighbours.isEmpty());
    }
}

// ─── 8. Fully isolated suspects: each in their own incident ──────────────────

void TestCoOffendingAnalyser::testFullyIsolatedSuspects()
{
    CoOffendingAnalyser a;
    QVector<PersonIncidentRecord> records;
    for (int i = 0; i < 5; ++i)
        records << rec(QString("ISO%1").arg(i), QString("INC%1").arg(i));

    a.buildGraph(records);
    a.analyse();

    QCOMPARE(a.nodes().size(), 5);
    for (const auto& n : a.nodes()) {
        QCOMPARE(n.degree, 0);
        QVERIFY(n.neighbours.isEmpty());
    }

    // findLeads for an isolated incident: only the one person, no second-degree
    const auto leads = a.findLeads("INC0", 10);
    QCOMPARE(leads.size(), 1);
    QCOMPARE(leads.first().personId, QStringLiteral("ISO0"));
}

// ─── 9. High co-offending frequency → high risk score ────────────────────────

void TestCoOffendingAnalyser::testHighCooffendingScore()
{
    CoOffendingAnalyser a;
    QVector<PersonIncidentRecord> records;

    // HIGH_A and HIGH_B share 8 incidents
    for (int i = 0; i < 8; ++i) {
        const QString inc = QString("JOINT%1").arg(i);
        records << rec("HIGH_A", inc) << rec("HIGH_B", inc);
    }
    // LOW_A and LOW_B share only 1 incident
    records << rec("LOW_A", "LONE1") << rec("LOW_B", "LONE1");

    a.buildGraph(records);
    a.analyse();

    const auto leadsHigh = a.findLeads("JOINT0", 10);
    const auto leadsLow  = a.findLeads("LONE1",  10);

    QVERIFY(!leadsHigh.isEmpty());
    QVERIFY(!leadsLow.isEmpty());

    // HIGH_A appears in 8 shared incidents → risk score should exceed LOW_A's
    double riskHigh = 0.0, riskLow = 0.0;
    for (const auto& lead : leadsHigh)
        if (lead.personId == "HIGH_A" || lead.personId == "HIGH_B")
            riskHigh = qMax(riskHigh, lead.riskScore);
    for (const auto& lead : leadsLow)
        if (lead.personId == "LOW_A" || lead.personId == "LOW_B")
            riskLow = qMax(riskLow, lead.riskScore);

    QVERIFY2(riskHigh > riskLow,
             qPrintable(QString("riskHigh=%1  riskLow=%2").arg(riskHigh).arg(riskLow)));
}

// ─── 10. findLeads returns leads sorted descending by riskScore ───────────────

void TestCoOffendingAnalyser::testFindLeadsReturnsSortedByRisk()
{
    CoOffendingAnalyser a;
    QVector<PersonIncidentRecord> records;
    // Hub shares incidents with 5 spokes: hub gets highest centrality
    for (int i = 1; i <= 5; ++i) {
        const QString inc = QString("SI%1").arg(i);
        records << rec("HUB", inc) << rec(QString("SP%1").arg(i), inc);
    }
    a.buildGraph(records);
    a.analyse();

    const auto leads = a.findLeads("SI1", 10);
    QVERIFY(leads.size() >= 2);

    for (int i = 1; i < leads.size(); ++i) {
        QVERIFY2(leads[i - 1].riskScore >= leads[i].riskScore,
                 qPrintable(QString("leads[%1].risk=%2 < leads[%3].risk=%4")
                            .arg(i-1).arg(leads[i-1].riskScore)
                            .arg(i).arg(leads[i].riskScore)));
    }
}

// ─── 11. findLeads for unknown incident ID returns empty ─────────────────────

void TestCoOffendingAnalyser::testFindLeadsForUnknownIncidentIsEmpty()
{
    CoOffendingAnalyser a;
    QVector<PersonIncidentRecord> records;
    records << rec("A", "KNOWN") << rec("B", "KNOWN");
    a.buildGraph(records);
    a.analyse();

    const auto leads = a.findLeads("NONEXISTENT", 10);
    QVERIFY(leads.isEmpty());
}

// ─── 12. Degree counting matches number of distinct co-offenders ──────────────

void TestCoOffendingAnalyser::testDegreeCounting()
{
    CoOffendingAnalyser a;
    QVector<PersonIncidentRecord> records;
    // HUB co-offends with 4 unique partners across 4 separate incidents
    for (int i = 0; i < 4; ++i) {
        const QString inc = QString("D_INC%1").arg(i);
        records << rec("HUB", inc) << rec(QString("PT%1").arg(i), inc);
    }
    a.buildGraph(records);

    for (const auto& n : a.nodes()) {
        if (n.personId == "HUB")
            QCOMPARE(n.degree, 4);
        else
            QCOMPARE(n.degree, 1);
    }
}

// ─── 13. analyse() sets pageRank > 0 for all nodes ──────────────────────────

void TestCoOffendingAnalyser::testAnalyseSetsPageRankPositive()
{
    CoOffendingAnalyser a;
    QVector<PersonIncidentRecord> records;
    records << rec("A", "I1") << rec("B", "I1") << rec("C", "I1");
    a.buildGraph(records);
    a.analyse();

    for (const auto& n : a.nodes()) {
        QVERIFY2(n.pageRank > 0.0,
                 qPrintable(QString("%1 pageRank=%2").arg(n.personId).arg(n.pageRank)));
    }
}

// ─── 14. Three-person clique: all have degree 2 (fully connected) ─────────────

void TestCoOffendingAnalyser::testNetworkDensityThreeClique()
{
    CoOffendingAnalyser a;
    QVector<PersonIncidentRecord> records;
    // A-B, B-C, A-C all share separate incidents
    records << rec("A", "AB") << rec("B", "AB")
            << rec("B", "BC") << rec("C", "BC")
            << rec("A", "AC") << rec("C", "AC");

    a.buildGraph(records);
    a.analyse();

    const auto nodes = a.nodes();
    QCOMPARE(nodes.size(), 3);
    for (const auto& n : nodes) {
        QCOMPARE(n.degree, 2);
        QCOMPARE(n.neighbours.size(), 2);
    }
}

// ─── 15. Role weight accumulates: associate (0.5) vs suspect (1.0) ────────────

void TestCoOffendingAnalyser::testRoleWeightAccumulation()
{
    CoOffendingAnalyser a;
    QVector<PersonIncidentRecord> records;
    records << rec("S1", "I1", "suspect",   1.0)
            << rec("S2", "I1", "associate", 0.5);

    a.buildGraph(records);

    for (const auto& n : a.nodes()) {
        if (n.personId == "S1") {
            QVERIFY(n.neighbours.contains("S2"));
            // weight = 1.0 * 0.5 = 0.5
            QVERIFY(std::abs(n.neighbours["S2"] - 0.5) < 1e-9);
        }
    }
}

// ─── 16. Multiple shared incidents accumulate the edge weight ─────────────────

void TestCoOffendingAnalyser::testMultipleIncidentsAccumulateEdgeWeight()
{
    CoOffendingAnalyser a;
    QVector<PersonIncidentRecord> records;
    // Three incidents, each contributing weight 1.0 × 1.0 = 1.0  → total = 3.0
    for (int i = 0; i < 3; ++i) {
        const QString inc = QString("MULTI_INC%1").arg(i);
        records << rec("X", inc) << rec("Y", inc);
    }
    a.buildGraph(records);

    for (const auto& n : a.nodes()) {
        if (n.personId == "X") {
            QVERIFY(n.neighbours.contains("Y"));
            QVERIFY2(std::abs(n.neighbours["Y"] - 3.0) < 1e-9,
                     qPrintable(QString("w=%1").arg(n.neighbours["Y"])));
        }
    }
}

// ─── 17. Lead reasoning field is non-empty ───────────────────────────────────

void TestCoOffendingAnalyser::testLeadReasoningNotEmpty()
{
    CoOffendingAnalyser a;
    QVector<PersonIncidentRecord> records;
    records << rec("P", "I1") << rec("Q", "I1");
    a.buildGraph(records);
    a.analyse();

    const auto leads = a.findLeads("I1", 10);
    QVERIFY(!leads.isEmpty());
    for (const auto& lead : leads)
        QVERIFY2(!lead.reasoning.isEmpty(),
                 qPrintable(QString("lead %1 has empty reasoning").arg(lead.personId)));
}

// ─── 18. Community IDs are assigned (>= 0) after analyse() ───────────────────

void TestCoOffendingAnalyser::testCommunityIdAssignedAfterAnalyse()
{
    CoOffendingAnalyser a;
    QVector<PersonIncidentRecord> records;
    records << rec("A", "I1") << rec("B", "I1") << rec("C", "I2");
    a.buildGraph(records);
    a.analyse();

    for (const auto& n : a.nodes())
        QVERIFY2(n.communityId >= 0,
                 qPrintable(QString("%1 communityId=%2").arg(n.personId).arg(n.communityId)));
}

// ─────────────────────────────────────────────────────────────────────────────

QTEST_MAIN(TestCoOffendingAnalyser)
#include "test_co_offending_analyser.moc"
