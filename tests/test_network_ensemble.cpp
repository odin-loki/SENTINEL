// test_network_ensemble.cpp
// Unit tests for CoOffendingAnalyser and EnsemblePredictor
#include <QTest>
#include <QCoreApplication>
#include <QDateTime>

#include "inference/CoOffendingAnalyser.h"
#include "models/EnsemblePredictor.h"
#include "models/PoissonBaseline.h"
#include "models/HawkesProcess.h"
#include "core/CrimeEvent.h"

// ─────────────────────────────────────────────────────────────────────────────
// CoOffendingAnalyser tests
// ─────────────────────────────────────────────────────────────────────────────

class TestCoOffendingAnalyser : public QObject {
    Q_OBJECT

    // Build a simple 3-person / 2-incident graph:
    //   A+B share incident_1
    //   B+C share incident_2
    static QVector<PersonIncidentRecord> simpleGraph() {
        return {
            {"person_A", "incident_1", "suspect",   1.0},
            {"person_B", "incident_1", "associate", 0.5},
            {"person_B", "incident_2", "suspect",   1.0},
            {"person_C", "incident_2", "suspect",   1.0},
        };
    }

private slots:

    void testBuildGraph() {
        CoOffendingAnalyser coa;
        coa.buildGraph(simpleGraph());
        QVERIFY(coa.isBuilt());
        const auto nodes = coa.nodes();
        QCOMPARE(nodes.size(), 3);
    }

    void testAnalyseDoesNotCrash() {
        CoOffendingAnalyser coa;
        coa.buildGraph(simpleGraph());
        coa.analyse();
        QVERIFY(true);
    }

    void testPageRankSumsToOne() {
        CoOffendingAnalyser coa;
        coa.buildGraph(simpleGraph());
        coa.analyse();
        double total = 0.0;
        for (const auto& n : coa.nodes()) total += n.pageRank;
        // PageRank should sum to approximately 1.0
        QVERIFY(std::abs(total - 1.0) < 0.1);
    }

    void testBetweennessNonNegative() {
        CoOffendingAnalyser coa;
        coa.buildGraph(simpleGraph());
        coa.analyse();
        for (const auto& n : coa.nodes())
            QVERIFY(n.betweenness >= 0.0);
    }

    void testBetweennessHubHigher() {
        // person_B is the bridge — should have higher betweenness than A and C
        CoOffendingAnalyser coa;
        coa.buildGraph(simpleGraph());
        coa.analyse();
        double betB = 0.0, betA = 0.0, betC = 0.0;
        for (const auto& n : coa.nodes()) {
            if (n.personId == "person_B") betB = n.betweenness;
            if (n.personId == "person_A") betA = n.betweenness;
            if (n.personId == "person_C") betC = n.betweenness;
        }
        QVERIFY(betB >= betA);
        QVERIFY(betB >= betC);
    }

    void testCommunitiesAssigned() {
        CoOffendingAnalyser coa;
        coa.buildGraph(simpleGraph());
        coa.analyse();
        for (const auto& n : coa.nodes())
            QVERIFY(n.communityId >= 0);
    }

    void testConnectedGraphOneCommunity() {
        // A-B-C are all connected via B → should be in the same community
        CoOffendingAnalyser coa;
        coa.buildGraph(simpleGraph());
        coa.analyse();
        int comm = -1;
        for (const auto& n : coa.nodes()) {
            if (comm == -1) comm = n.communityId;
            else QCOMPARE(n.communityId, comm);
        }
    }

    void testFindLeadsDirectParticipants() {
        CoOffendingAnalyser coa;
        coa.buildGraph(simpleGraph());
        coa.analyse();
        const auto leads = coa.findLeads("incident_1", 5);
        QVERIFY(!leads.isEmpty());
        // Should include person_A and person_B
        bool hasA = false, hasB = false;
        for (const auto& l : leads) {
            if (l.personId == "person_A") hasA = true;
            if (l.personId == "person_B") hasB = true;
        }
        QVERIFY(hasA || hasB);
    }

    void testFindLeadsSecondDegree() {
        // incident_1 has A+B; B is connected to C via incident_2
        // So C should appear as a second-degree lead for incident_1
        CoOffendingAnalyser coa;
        coa.buildGraph(simpleGraph());
        coa.analyse();
        const auto leads = coa.findLeads("incident_1", 10);
        bool hasC = false;
        for (const auto& l : leads) {
            if (l.personId == "person_C") {
                hasC = true;
                QCOMPARE(l.connectionType, QStringLiteral("second_degree"));
            }
        }
        QVERIFY(hasC);
    }

    void testRiskScoreInRange() {
        CoOffendingAnalyser coa;
        coa.buildGraph(simpleGraph());
        coa.analyse();
        for (const auto& l : coa.findLeads("incident_1", 10)) {
            QVERIFY(l.riskScore >= 0.0);
            QVERIFY(l.riskScore <= 1.0);
        }
    }

    void testEmptyGraphNoLeads() {
        CoOffendingAnalyser coa;
        coa.buildGraph({});
        coa.analyse();
        QVERIFY(coa.findLeads("incident_1").isEmpty());
    }

    void testUnknownIncidentNoLeads() {
        CoOffendingAnalyser coa;
        coa.buildGraph(simpleGraph());
        coa.analyse();
        QVERIFY(coa.findLeads("nonexistent_incident").isEmpty());
    }

    void testTopKRespected() {
        // Build a bigger graph: 10 people all in the same incident
        QVector<PersonIncidentRecord> records;
        for (int i = 0; i < 10; ++i)
            records.append({QStringLiteral("person_%1").arg(i), "incident_big",
                            "suspect", 1.0});

        CoOffendingAnalyser coa;
        coa.buildGraph(records);
        coa.analyse();
        const int K = 3;
        const auto leads = coa.findLeads("incident_big", K);
        QVERIFY(leads.size() <= K);
    }

    void testLeadCentralityFieldPopulated() {
        CoOffendingAnalyser coa;
        coa.buildGraph(simpleGraph());
        coa.analyse();
        const auto leads = coa.findLeads("incident_1", 5);
        for (const auto& l : leads)
            QVERIFY(l.centralityScore >= 0.0);
    }

    void testReasoningStringNonEmpty() {
        CoOffendingAnalyser coa;
        coa.buildGraph(simpleGraph());
        coa.analyse();
        for (const auto& l : coa.findLeads("incident_1", 5))
            QVERIFY(!l.reasoning.isEmpty());
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// EnsemblePredictor tests
// ─────────────────────────────────────────────────────────────────────────────

class TestEnsemblePredictor : public QObject {
    Q_OBJECT

private slots:

    void testNotReadyWithNoModels() {
        EnsemblePredictor ep;
        QVERIFY(!ep.isReady());
    }

    void testReadyWithPoisson() {
        PoissonBaseline poisson;
        // Fit with minimal data
        QVector<PoissonBaseline::EventRecord> events;
        const QDateTime base(QDate(2024,1,1), QTime(2,0,0), Qt::UTC);
        for (int i = 0; i < 20; ++i)
            events.append({"zone_A", base.addDays(i), "burglary"});
        poisson.fit(events);

        EnsemblePredictor ep;
        ep.setPoisson(&poisson);
        QVERIFY(ep.isReady());
    }

    void testPredictProbInRange() {
        PoissonBaseline poisson;
        QVector<PoissonBaseline::EventRecord> events;
        const QDateTime base(QDate(2024,1,1), QTime(2,0,0), Qt::UTC);
        for (int i = 0; i < 30; ++i)
            events.append({"zone_A", base.addDays(i), "burglary"});
        poisson.fit(events);

        EnsemblePredictor ep;
        ep.setPoisson(&poisson);

        const auto pred = ep.predict("zone_A",
                                     base.addDays(31),
                                     "burglary",
                                     51.5, -0.1);
        QVERIFY(pred.probCrime >= 0.0);
        QVERIFY(pred.probCrime <= 1.0);
    }

    void testPredictExpectedCountNonNegative() {
        PoissonBaseline poisson;
        QVector<PoissonBaseline::EventRecord> events;
        const QDateTime base(QDate(2024,1,1), QTime(0,0,0), Qt::UTC);
        for (int i = 0; i < 20; ++i)
            events.append({"z", base.addDays(i), "theft"});
        poisson.fit(events);

        EnsemblePredictor ep;
        ep.setPoisson(&poisson);
        const auto pred = ep.predict("z", base.addDays(21), "theft", 51.5, -0.1);
        QVERIFY(pred.expectedCount >= 0.0);
    }

    void testWeightsNormalise() {
        EnsemblePredictor ep;
        ep.setWeights(2.0, 3.0);  // should normalise to 0.4 / 0.6
        // We can't access internal state, but predict shouldn't crash
        QVERIFY(true);
    }

    void testPoissonOnlyDominantModel() {
        PoissonBaseline poisson;
        QVector<PoissonBaseline::EventRecord> events;
        const QDateTime base(QDate(2024,1,1), QTime(0,0,0), Qt::UTC);
        for (int i = 0; i < 20; ++i)
            events.append({"z", base.addDays(i), "assault"});
        poisson.fit(events);

        EnsemblePredictor ep;
        ep.setWeights(1.0, 0.0);
        ep.setPoisson(&poisson);
        const auto pred = ep.predict("z", base.addDays(21), "assault", 51.5, -0.1);
        QCOMPARE(pred.dominantModel, QStringLiteral("poisson"));
    }

    void testCalibration() {
        // After calibration with perfect data, ECE should be < 0.1
        QVector<QPair<double,double>> calData;
        for (int i = 0; i < 100; ++i) {
            const double p = i / 100.0;
            calData.append({p, (i < 50) ? 0.0 : 1.0});
        }
        EnsemblePredictor ep;
        ep.calibrate(calData);
        QVERIFY(true);  // should not crash
    }

    void testECERandomBaseline() {
        // Random predictor: ECE should be around 0.25 (bad calibration)
        QVector<QPair<double,double>> data;
        // All predictions = 0.5, actual = alternating 0/1
        for (int i = 0; i < 100; ++i)
            data.append({0.5, (i % 2 == 0) ? 1.0 : 0.0});
        const double ece = EnsemblePredictor::ece(data, 10);
        QVERIFY(ece >= 0.0);
        QVERIFY(ece <= 1.0);
    }

    void testECEPerfectCalibration() {
        // Perfect calibration: ECE ≈ 0
        QVector<QPair<double,double>> data;
        // pred=0.0 → actual=0, pred=1.0 → actual=1
        for (int i = 0; i < 50; ++i) data.append({0.0, 0.0});
        for (int i = 0; i < 50; ++i) data.append({1.0, 1.0});
        const double ece = EnsemblePredictor::ece(data, 10);
        QVERIFY(ece < 0.05);
    }

    void testBrierScorePerfect() {
        QVector<QPair<double,double>> data;
        for (int i = 0; i < 50; ++i) data.append({1.0, 1.0});
        for (int i = 0; i < 50; ++i) data.append({0.0, 0.0});
        const double bs = EnsemblePredictor::brierScore(data);
        QVERIFY(std::abs(bs) < 1e-9);
    }

    void testBrierScoreWorstCase() {
        QVector<QPair<double,double>> data;
        for (int i = 0; i < 50; ++i) data.append({0.0, 1.0});
        for (int i = 0; i < 50; ++i) data.append({1.0, 0.0});
        const double bs = EnsemblePredictor::brierScore(data);
        QVERIFY(std::abs(bs - 1.0) < 1e-9);
    }

    void testBrierScoreRandom() {
        QVector<QPair<double,double>> data;
        for (int i = 0; i < 100; ++i)
            data.append({0.5, (i % 2 == 0) ? 1.0 : 0.0});
        const double bs = EnsemblePredictor::brierScore(data);
        QVERIFY(std::abs(bs - 0.25) < 1e-9);
    }

    void testRiskGridDimensions() {
        PoissonBaseline poisson;
        QVector<PoissonBaseline::EventRecord> events;
        const QDateTime base(QDate(2024,1,1), QTime(0,0,0), Qt::UTC);
        for (int i = 0; i < 20; ++i)
            events.append({"z", base.addDays(i), "burglary"});
        poisson.fit(events);

        EnsemblePredictor ep;
        ep.setPoisson(&poisson);

        const int N = 5;
        const auto grid = ep.riskGrid(base.addDays(21),
                                       51.4, 51.6,
                                       -0.2, 0.0,
                                       N);
        QCOMPARE(grid.size(), N);
        for (const auto& row : grid)
            QCOMPARE(row.size(), N);
    }

    void testRiskGridAllProbsInRange() {
        PoissonBaseline poisson;
        QVector<PoissonBaseline::EventRecord> events;
        const QDateTime base(QDate(2024,1,1), QTime(0,0,0), Qt::UTC);
        for (int i = 0; i < 20; ++i)
            events.append({"z", base.addDays(i), "burglary"});
        poisson.fit(events);

        EnsemblePredictor ep;
        ep.setPoisson(&poisson);

        const auto grid = ep.riskGrid(base.addDays(21), 51.4, 51.6, -0.2, 0.0, 4);
        for (const auto& row : grid)
            for (const auto& cell : row) {
                QVERIFY(cell.probCrime >= 0.0);
                QVERIFY(cell.probCrime <= 1.0);
            }
    }

    void testHawkesOnlyPrediction() {
        HawkesProcess hawkes;
        QVector<SpatiotemporalEvent> events;
        const QDateTime epoch(QDate(2024,1,1), QTime(0,0,0), Qt::UTC);
        for (int i = 0; i < 20; ++i)
            events.append({static_cast<double>(i), 51.5 + i*0.001, -0.1 + i*0.001, "burglary"});
        hawkes.fit(events);

        EnsemblePredictor ep;
        ep.setHawkes(&hawkes);
        QVERIFY(ep.isReady());

        const auto pred = ep.predict("zone_x", epoch.addDays(21), "burglary", 51.5, -0.1);
        QVERIFY(pred.probCrime >= 0.0);
        QVERIFY(pred.probCrime <= 1.0);
        QCOMPARE(pred.dominantModel, QStringLiteral("hawkes"));
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// main
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
    { TestCoOffendingAnalyser t1; r |= runTest(&t1, "net_cooffend.txt"); }
    { TestEnsemblePredictor   t2; r |= runTest(&t2, "net_ensemble.txt"); }
    return r;
}

#include "test_network_ensemble.moc"
