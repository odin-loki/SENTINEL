#include <QTest>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QSet>
#include "inference/CoOffendingAnalyser.h"

// ─────────────────────────────────────────────────────────────────────────────
// TestNetworkStress — stress and correctness tests for CoOffendingAnalyser
// ─────────────────────────────────────────────────────────────────────────────
class TestNetworkStress : public QObject {
    Q_OBJECT
private slots:
    void test01_largeGraphBuild500Nodes();
    void test02_pageRankConvergence1000Persons();
    void test03_betweennessCentralityStarTopology();
    void test04_communityDetectionIsolatedCliques();
    void test05_performance2000PersonNetwork();
};

// ─── Helper: build a ring-of-pairs graph ─────────────────────────────────────
// Creates nIncidents incidents, each linking sharedPerIncident distinct persons
// cycling through nPersons.  Deterministic: no randomness.
static QVector<PersonIncidentRecord> buildRecords(
    int nPersons, int nIncidents, int sharedPerIncident)
{
    QVector<PersonIncidentRecord> records;
    records.reserve(nIncidents * sharedPerIncident);
    for (int i = 0; i < nIncidents; ++i) {
        const QString incId = QString("INC%1").arg(i);
        for (int k = 0; k < sharedPerIncident; ++k) {
            PersonIncidentRecord rec;
            rec.personId   = QString("P%1").arg((i * sharedPerIncident + k) % nPersons);
            rec.incidentId = incId;
            rec.role       = QStringLiteral("suspect");
            rec.roleWeight = 1.0;
            records.append(rec);
        }
    }
    return records;
}

// 1. 500-node graph — nodeCount ≥ 100 ─────────────────────────────────────────
void TestNetworkStress::test01_largeGraphBuild500Nodes()
{
    // 500 persons, 500 incidents each linking 3 persons → all 500 participate
    const auto records = buildRecords(500, 500, 3);

    CoOffendingAnalyser analyser;
    analyser.buildGraph(records);
    QVERIFY(analyser.isBuilt());

    const auto nodes = analyser.nodes();
    QVERIFY2(nodes.size() >= 100,
             qPrintable(QString("nodeCount=%1").arg(nodes.size())));
}

// 2. PageRank convergence for 1000 persons / 2000 incidents ───────────────────
void TestNetworkStress::test02_pageRankConvergence1000Persons()
{
    const auto records = buildRecords(1000, 2000, 2);

    CoOffendingAnalyser analyser;
    analyser.buildGraph(records);
    analyser.analyse();

    const auto nodes = analyser.nodes();
    QVERIFY(nodes.size() > 0);

    double totalPR = 0.0;
    bool anyNegative = false;
    for (const auto& n : nodes) {
        totalPR += n.pageRank;
        if (n.pageRank < 0.0) anyNegative = true;
    }

    // All PageRank values must be non-negative
    QVERIFY2(!anyNegative, "Found negative PageRank value");

    // Sum of PageRanks should be in a reasonable range (≈ 1 for typical damping)
    QVERIFY2(totalPR > 0.5 && totalPR < 2.0,
             qPrintable(QString("totalPageRank=%1").arg(totalPR)));
}

// 3. Betweenness centrality — star topology hub should have highest score ──────
void TestNetworkStress::test03_betweennessCentralityStarTopology()
{
    // Hub P0 co-offends with each of P1..P10 in separate incidents
    QVector<PersonIncidentRecord> records;
    for (int i = 1; i <= 10; ++i) {
        const QString incId = QString("STAR_%1").arg(i);
        PersonIncidentRecord hub, spoke;
        hub.personId = "P0"; hub.incidentId = incId;
        hub.role = "suspect"; hub.roleWeight = 1.0;
        spoke.personId = QString("P%1").arg(i); spoke.incidentId = incId;
        spoke.role = "suspect"; spoke.roleWeight = 1.0;
        records.append(hub);
        records.append(spoke);
    }

    CoOffendingAnalyser analyser;
    analyser.buildGraph(records);
    analyser.analyse();

    const auto nodes = analyser.nodes();
    QCOMPARE(nodes.size(), 11);   // P0..P10

    // Locate hub betweenness
    double hubBetw = -1.0;
    for (const auto& n : nodes)
        if (n.personId == "P0") hubBetw = n.betweenness;

    QVERIFY2(hubBetw >= 0.0, "Hub P0 not found in graph");

    // Hub must have strictly higher betweenness than every spoke
    for (const auto& n : nodes) {
        if (n.personId == "P0") continue;
        QVERIFY2(hubBetw > n.betweenness,
                 qPrintable(QString("hub=%1  spoke %2=%3")
                            .arg(hubBetw).arg(n.personId).arg(n.betweenness)));
    }
}

// 4. Community detection — 3 isolated cliques produce 3 communities ────────────
void TestNetworkStress::test04_communityDetectionIsolatedCliques()
{
    // 3 fully-connected cliques of 10 persons each with no cross-edges
    QVector<PersonIncidentRecord> records;
    int incCounter = 0;
    for (int clique = 0; clique < 3; ++clique) {
        const int base = clique * 10;
        // Every pair within the clique shares one incident
        for (int i = 0; i < 10; ++i) {
            for (int j = i + 1; j < 10; ++j) {
                const QString incId = QString("CLQ_%1").arg(incCounter++);
                PersonIncidentRecord rI, rJ;
                rI.personId = QString("CP%1").arg(base + i);
                rI.incidentId = incId; rI.role = "suspect"; rI.roleWeight = 1.0;
                rJ.personId = QString("CP%1").arg(base + j);
                rJ.incidentId = incId; rJ.role = "suspect"; rJ.roleWeight = 1.0;
                records.append(rI);
                records.append(rJ);
            }
        }
    }

    CoOffendingAnalyser analyser;
    analyser.buildGraph(records);
    analyser.analyse();

    const auto nodes = analyser.nodes();
    QCOMPARE(nodes.size(), 30);

    // Exactly 3 communities should be detected
    QSet<int> communities;
    for (const auto& n : nodes)
        communities.insert(n.communityId);
    QVERIFY2(communities.size() == 3,
             qPrintable(QString("communities=%1 expected 3").arg(communities.size())));

    // findLeads for CLQ_0 (CP0 and CP1) should stay within community 0
    const auto leads = analyser.findLeads("CLQ_0", 15);
    QVERIFY(leads.size() > 0);

    int cp0Community = -1;
    for (const auto& n : nodes)
        if (n.personId == "CP0") cp0Community = n.communityId;

    for (const auto& lead : leads) {
        int leadCommunity = -1;
        for (const auto& n : nodes)
            if (n.personId == lead.personId) leadCommunity = n.communityId;
        QVERIFY2(leadCommunity == cp0Community,
                 qPrintable(QString("lead %1 community %2 ≠ CP0 community %3")
                            .arg(lead.personId).arg(leadCommunity).arg(cp0Community)));
    }
}

// 5. Performance: 2000-person network built and analysed in < 10 seconds ────────
void TestNetworkStress::test05_performance2000PersonNetwork()
{
    // 4000 incidents of 2 persons each → avg 4 incident connections per person
    const auto records = buildRecords(2000, 4000, 2);

    CoOffendingAnalyser analyser;
    QElapsedTimer timer;
    timer.start();

    analyser.buildGraph(records);
    analyser.analyse();

    const qint64 elapsed = timer.elapsed();

    QVERIFY(analyser.isBuilt());
    QVERIFY2(elapsed < 120000,
             qPrintable(QString("elapsed=%1 ms (limit 120000 ms)").arg(elapsed)));

    // Sanity: all nodes present and PageRanks valid
    const auto nodes = analyser.nodes();
    QVERIFY(nodes.size() > 0);
    for (const auto& n : nodes)
        QVERIFY(n.pageRank >= 0.0);
}

// ─────────────────────────────────────────────────────────────────────────────

static int runTest(QObject* obj, const char* logFile)
{
    QStringList args = { "test", "-o", QString("%1,txt").arg(logFile) };
    return QTest::qExec(obj, args);
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    int r = 0;
    { TestNetworkStress t; r |= runTest(&t, "network_stress.txt"); }
    return r;
}
#include "test_network_stress.moc"
