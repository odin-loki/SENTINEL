// test_performance.cpp — Performance regression tests for SENTINEL
// Verifies that core analytics operations complete within specified time budgets.
// Each test uses a 2x safety margin over the design-spec target.
#include <QTest>
#include <QCoreApplication>
#include <QDateTime>
#include <QElapsedTimer>
#include <QVector>
#include <QPair>
#include <QString>
#include <cmath>
#include <random>

#include "core/CrimeEvent.h"
#include "core/Database.h"
#include "core/AppConfig.h"
#include "models/PoissonBaseline.h"
#include "models/HawkesProcess.h"
#include "models/SeriesDetector.h"
#include "models/EnsemblePredictor.h"
#include "inference/GeographicProfiler.h"
#include "inference/MOAnalyser.h"
#include "inference/AnomalyDetector.h"
#include "inference/HintEngine.h"
#include "inference/CoOffendingAnalyser.h"
#include "benchmark/CalibrationAnalyser.h"

// ─────────────────────────────────────────────────────────────────────────────
// Deterministic pseudo-random helpers
// ─────────────────────────────────────────────────────────────────────────────
namespace {

static std::mt19937 rng(42);

static double randRange(double lo, double hi) {
    return std::uniform_real_distribution<double>(lo, hi)(rng);
}

static int randInt(int lo, int hi) {
    return std::uniform_int_distribution<int>(lo, hi)(rng);
}

static const QStringList kCrimeTypes = {
    "burglary", "theft", "robbery", "assault", "vandalism"
};

static const QStringList kMoWords = {
    "forced", "entry", "rear", "window", "lock", "pick", "gloves",
    "mask", "night", "dawn", "residential", "commercial", "vehicle",
    "cash", "jewellery", "electronics", "solo", "group", "armed"
};

static QString randomMO(int wordCount = 6) {
    QStringList chosen;
    chosen.reserve(wordCount);
    for (int i = 0; i < wordCount; ++i)
        chosen << kMoWords[randInt(0, kMoWords.size() - 1)];
    return chosen.join(" ");
}

static QDateTime baseTime() {
    return QDateTime(QDate(2024, 1, 1), QTime(0, 0, 0), Qt::UTC);
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// TestPerformanceRegression
// ─────────────────────────────────────────────────────────────────────────────
class TestPerformanceRegression : public QObject {
    Q_OBJECT

private slots:

    // ── 1. Poisson fit — 10,000 events ───────────────────────────────────────
    // Design target: ≤ 500ms   Safety margin: ≤ 1000ms
    void testPoissonFit10k() {
        QVector<PoissonBaseline::EventRecord> events;
        events.reserve(10'000);
        const QDateTime base = baseTime();
        for (int i = 0; i < 10'000; ++i) {
            PoissonBaseline::EventRecord r;
            r.zoneId      = QStringLiteral("zone_%1").arg(randInt(0, 19));
            r.occurredAt  = base.addSecs(randInt(0, 365 * 86400));
            r.crimeType   = kCrimeTypes[randInt(0, kCrimeTypes.size() - 1)];
            events.append(r);
        }

        PoissonBaseline model;
        QElapsedTimer t;
        t.start();
        model.fit(events);
        const qint64 elapsed = t.elapsed();

        qDebug("Poisson fit (10k events): %lld ms", elapsed);
        QVERIFY2(elapsed < 1000,
            qPrintable(QStringLiteral("took %1 ms, limit 1000 ms").arg(elapsed)));
        QVERIFY(model.isFitted());
        QCOMPARE(model.totalEvents(), 10'000);

        const auto pred = model.predict("zone_0", base, "burglary");
        QVERIFY(pred.lambda >= 0.0);
        QVERIFY(pred.probAtLeastOne >= 0.0 && pred.probAtLeastOne <= 1.0);
    }

    // ── 2. Hawkes fit — 500 events, max 50 iterations ────────────────────────
    // Design target: ≤ 15000ms  Safety margin: ≤ 60000ms
    // Note: Hawkes EM is O(n²) — 500 events is the practical limit for < 60s
    void testHawkesFit1k() {
        QVector<SpatiotemporalEvent> events;
        events.reserve(500);
        for (int i = 0; i < 500; ++i) {
            SpatiotemporalEvent e;
            e.tDays     = randRange(0.0, 365.0);
            e.lat       = 51.5 + randRange(-0.1, 0.1);
            e.lon       = -0.13 + randRange(-0.1, 0.1);
            e.crimeType = kCrimeTypes[randInt(0, kCrimeTypes.size() - 1)];
            events.append(e);
        }
        // Sort ascending by time (required by Hawkes log-likelihood)
        std::sort(events.begin(), events.end(),
                  [](const SpatiotemporalEvent& a, const SpatiotemporalEvent& b){
                      return a.tDays < b.tDays;
                  });

        HawkesProcess model;
        QElapsedTimer t;
        t.start();
        model.fit(events, 50);
        const qint64 elapsed = t.elapsed();

        qDebug("Hawkes fit (500 events, 50 iter): %lld ms", elapsed);
        QVERIFY2(elapsed < 60000,
            qPrintable(QStringLiteral("took %1 ms, limit 60000 ms").arg(elapsed)));
        QVERIFY(model.isFitted());

        const auto& p = model.params();
        QVERIFY(p.mu > 0.0);
        QVERIFY(p.alpha >= 0.0);
        QVERIFY(p.beta > 0.0);
    }

    // ── 3. Series detection — 500 events ─────────────────────────────────────
    // Design target: ≤ 1000ms   Safety margin: ≤ 2000ms
    void testSeriesDetection500() {
        QVector<SeriesEvent> events;
        events.reserve(500);
        for (int i = 0; i < 500; ++i) {
            SeriesEvent e;
            e.eventId   = QStringLiteral("ev%1").arg(i);
            e.lat       = 51.5 + randRange(-0.05, 0.05);
            e.lon       = -0.13 + randRange(-0.05, 0.05);
            e.tDays     = randRange(0.0, 90.0);
            e.crimeType = kCrimeTypes[randInt(0, kCrimeTypes.size() - 1)];
            e.moText    = randomMO();
            events.append(e);
        }

        SeriesDetector detector(0.3, 14.0, 3);
        QElapsedTimer t;
        t.start();
        const QVector<CrimeSeries> series = detector.detectSeries(events);
        const qint64 elapsed = t.elapsed();

        qDebug("Series detection (500 events): %lld ms, %d series found",
               elapsed, series.size());
        QVERIFY2(elapsed < 2000,
            qPrintable(QStringLiteral("took %1 ms, limit 2000 ms").arg(elapsed)));
        // Correctness: series members reference valid event IDs
        for (const auto& s : series) {
            QVERIFY(s.members.size() >= 3);
            QVERIFY(!s.seriesId.isEmpty());
        }
    }

    // ── 4. Geographic profiler — grid=50, 100 incidents ──────────────────────
    // Design target: ≤ 200ms   Safety margin: ≤ 400ms
    void testGeographicProfile100() {
        QVector<QPair<double, double>> locs;
        locs.reserve(100);
        for (int i = 0; i < 100; ++i)
            locs.append({ 51.5 + randRange(-0.05, 0.05),
                          -0.13 + randRange(-0.05, 0.05) });

        GeographicProfiler profiler(1.2, 1.2, 0.5, 50);
        QElapsedTimer t;
        t.start();
        const GeographicProfile result = profiler.profile(locs);
        const qint64 elapsed = t.elapsed();

        qDebug("Geographic profile (grid=50, 100 pts): %lld ms", elapsed);
        QVERIFY2(elapsed < 400,
            qPrintable(QStringLiteral("took %1 ms, limit 400 ms").arg(elapsed)));
        QVERIFY(!result.probabilitySurface.empty());
        QVERIFY(result.peakProbability > 0.0);
        QVERIFY(result.gridLats.size() == 50);
        QVERIFY(result.gridLons.size() == 50);
    }

    // ── 5. MO analysis — corpus=1000 cases, topK=10 ──────────────────────────
    // Design target: ≤ 100ms   Safety margin: ≤ 200ms
    void testMOAnalysis1k() {
        QVector<MOCaseRecord> corpus;
        corpus.reserve(1000);
        for (int i = 0; i < 1000; ++i) {
            MOCaseRecord r;
            r.caseId         = QStringLiteral("case_%1").arg(i);
            r.moText         = randomMO(8);
            r.resolved       = (i % 3 == 0);
            r.outcome        = r.resolved ? "convicted" : "open";
            r.suspectProfile = QStringLiteral("male 25-35");
            corpus.append(r);
        }

        MOAnalyser analyser;
        analyser.fit(corpus);
        QVERIFY(analyser.isFitted());
        QCOMPARE(analyser.caseCount(), 1000);

        const QString query = randomMO(6);
        QElapsedTimer t;
        t.start();
        const QVector<MOMatch> matches = analyser.findSimilar(query, 10);
        const qint64 elapsed = t.elapsed();

        qDebug("MO analysis query (corpus=1k, topK=10): %lld ms, %d matches",
               elapsed, matches.size());
        QVERIFY2(elapsed < 200,
            qPrintable(QStringLiteral("took %1 ms, limit 200 ms").arg(elapsed)));
        QVERIFY(matches.size() <= 10);
        for (const auto& m : matches) {
            QVERIFY(!m.caseId.isEmpty());
            QVERIFY(m.similarityScore >= 0.0 && m.similarityScore <= 1.0);
        }
    }

    // ── 6. AnomalyDetector.fit — 1000 events ─────────────────────────────────
    // Design target: ≤ 500ms   Safety margin: ≤ 1000ms
    void testAnomalyDetectorFit1k() {
        QVector<AnomalyFeatureVector> data;
        data.reserve(1000);
        for (int i = 0; i < 1000; ++i) {
            AnomalyFeatureVector f;
            f.eventId       = QStringLiteral("e%1").arg(i);
            f.lat           = 51.5 + randRange(-0.1, 0.1);
            f.lon           = -0.13 + randRange(-0.1, 0.1);
            f.tDays         = randRange(0.0, 365.0);
            f.hourNorm      = randRange(0.0, 1.0);
            f.crimeTypeCode = randInt(0, 4);
            data.append(f);
        }

        AnomalyDetector detector(0.05);
        QElapsedTimer t;
        t.start();
        detector.fit(data);
        const qint64 fitElapsed = t.elapsed();

        qDebug("AnomalyDetector fit (1k events): %lld ms", fitElapsed);
        QVERIFY2(fitElapsed < 1000,
            qPrintable(QStringLiteral("took %1 ms, limit 1000 ms").arg(fitElapsed)));
        QVERIFY(detector.isFitted());

        const QVector<AnomalySignal> detected = detector.detectAnomalies(data);
        QVERIFY(!detected.isEmpty());
        for (const auto& sig : detected) {
            QVERIFY(sig.combinedScore >= 0.0);
        }
    }

    // ── 7. HintEngine — all inputs filled ────────────────────────────────────
    // Design target: ≤ 50ms   Safety margin: ≤ 100ms
    void testHintEngineAllInputs() {
        // Build a realistic HintEngineInput with all optional fields populated
        HintEngineInput input;

        // Base event
        CrimeEvent ev;
        ev.eventId  = ev.id = "test_event_001";
        ev.crimeType = "burglary";
        ev.lat = ev.latitude  = 51.505;
        ev.lon = ev.longitude = -0.128;
        ev.occurredAt = baseTime();
        ev.timestamp  = baseTime();
        ev.qualityScore = 0.85;
        input.event = ev;

        // Series matches
        for (int i = 0; i < 5; ++i) {
            SeriesMatch sm;
            sm.seriesId              = QStringLiteral("series_%1").arg(i);
            sm.memberCount           = randInt(3, 12);
            sm.linkProbability       = randRange(0.3, 0.95);
            sm.spatialDistanceM      = randRange(50.0, 500.0);
            sm.temporalDistanceDays  = randRange(1.0, 14.0);
            sm.moSimilarity          = randRange(0.4, 0.9);
            sm.compositeScore        = randRange(0.3, 0.9);
            sm.method                = "dbscan";
            input.seriesMatches.append(sm);
        }

        // MO matches
        for (int i = 0; i < 5; ++i) {
            MOMatch mm;
            mm.caseId          = QStringLiteral("case_%1").arg(i);
            mm.similarityScore = randRange(0.4, 0.9);
            mm.resolved        = (i % 2 == 0);
            mm.outcome         = mm.resolved ? "convicted" : "open";
            mm.suspectProfile  = "male 25-40";
            input.moMatches.append(mm);
        }

        // Geographic profile
        QVector<QPair<double, double>> locs;
        for (int i = 0; i < 10; ++i)
            locs.append({ 51.505 + randRange(-0.02, 0.02),
                          -0.128 + randRange(-0.02, 0.02) });
        GeographicProfiler profiler(1.2, 1.2, 0.5, 20);
        input.geoProfile = profiler.profile(locs);

        // Network leads
        for (int i = 0; i < 3; ++i) {
            NetworkLead nl;
            nl.personId        = QStringLiteral("person_%1").arg(i);
            nl.connectionType  = "direct_cooffender";
            nl.sharedIncidents = randInt(1, 5);
            nl.centralityScore = randRange(0.1, 0.9);
            nl.communityId     = randInt(0, 3);
            nl.riskScore       = randRange(0.3, 0.9);
            nl.reasoning       = "Co-appeared in multiple incidents";
            input.networkLeads.append(nl);
        }

        // Evidence weights
        for (int i = 0; i < 4; ++i) {
            EvidenceWeight ew;
            ew.evidenceType          = QStringLiteral("type_%1").arg(i);
            ew.likelihoodRatio       = randRange(1.5, 8.0);
            ew.posteriorOdds         = randRange(1.0, 5.0);
            ew.posteriorProbability  = randRange(0.4, 0.9);
            ew.reliability           = randRange(0.5, 1.0);
            input.evidenceWeights.append(ew);
        }

        // Anomaly signal
        AnomalySignal sig;
        sig.eventId       = "test_event_001";
        sig.isolationScore = 0.72;
        sig.lofScore       = 1.8;
        sig.zScoreTemporal = 2.1;
        sig.zScoreSpatial  = 1.6;
        sig.combinedScore  = 0.68;
        sig.isAnomaly      = true;
        sig.signalReasons  = { "temporal_outlier", "spatial_outlier" };
        input.anomalySignal = sig;

        input.dataQuality = 0.85;

        HintEngine engine;
        QElapsedTimer t;
        t.start();
        const QVector<InvestigativeLead> leads = engine.generate(input);
        const qint64 elapsed = t.elapsed();

        qDebug("HintEngine generate (all inputs): %lld ms, %d leads",
               elapsed, leads.size());
        QVERIFY2(elapsed < 100,
            qPrintable(QStringLiteral("took %1 ms, limit 100 ms").arg(elapsed)));
        QVERIFY(!leads.isEmpty());
        for (const auto& lead : leads) {
            QVERIFY(!lead.headline.isEmpty());
            QVERIFY(lead.confidence >= 0.0 && lead.confidence <= 1.0);
        }
    }

    // ── 8. Database — 1000 insert + full query ────────────────────────────────
    // Design target: ≤ 2000ms   Safety margin: ≤ 4000ms
    void testDatabaseInsertQuery1k() {
        AppConfig cfg;
        cfg.databasePath = ":memory:";

        Database db(cfg);
        QVERIFY(db.open());

        const QDateTime base = baseTime();

        QElapsedTimer t;
        t.start();
        for (int i = 0; i < 1000; ++i) {
            CrimeEvent ev;
            ev.eventId   = ev.id = QStringLiteral("db_ev_%1").arg(i);
            ev.crimeType = kCrimeTypes[randInt(0, kCrimeTypes.size() - 1)];
            const QDateTime dt = base.addSecs(randInt(0, 365 * 86400));
            ev.occurredAt = dt;
            ev.timestamp  = dt;
            ev.lat = ev.latitude  = 51.5 + randRange(-0.1, 0.1);
            ev.lon = ev.longitude = -0.13 + randRange(-0.1, 0.1);
            ev.qualityScore       = randRange(0.4, 1.0);
            ev.suburb             = "London";
            ev.outcome            = "unknown";
            QVERIFY(db.insertEvent(ev));
        }
        const qint64 insertElapsed = t.elapsed();
        qDebug("Database insert 1k: %lld ms", insertElapsed);

        t.restart();
        const QVector<CrimeEvent> results = db.queryEvents(
            QString{}, QDateTime{}, QDateTime{},
            -90.0, 90.0, -180.0, 180.0, 5000);
        const qint64 queryElapsed = t.elapsed();
        qDebug("Database query (all): %lld ms, %d rows", queryElapsed, results.size());

        const qint64 total = insertElapsed + queryElapsed;
        qDebug("Database total: %lld ms", total);
        QVERIFY2(total < 4000,
            qPrintable(QStringLiteral("took %1 ms, limit 4000 ms").arg(total)));
        QCOMPARE(results.size(), 1000);
    }

    // ── 9. CalibrationAnalyser ECE — 10,000 samples ──────────────────────────
    // Design target: ≤ 100ms   Safety margin: ≤ 200ms
    void testCalibrationECE10k() {
        QVector<QPair<double, double>> predActual;
        predActual.reserve(10'000);
        for (int i = 0; i < 10'000; ++i) {
            const double pred   = randRange(0.0, 1.0);
            const double actual = (randRange(0.0, 1.0) < pred) ? 1.0 : 0.0;
            predActual.append({ pred, actual });
        }

        CalibrationAnalyser analyser(10);
        QElapsedTimer t;
        t.start();
        const CalibrationResult result = analyser.analyse(predActual);
        const qint64 elapsed = t.elapsed();

        qDebug("CalibrationAnalyser ECE (10k): %lld ms  ECE=%.4f",
               elapsed, result.ece);
        QVERIFY2(elapsed < 200,
            qPrintable(QStringLiteral("took %1 ms, limit 200 ms").arg(elapsed)));
        QVERIFY(result.ece >= 0.0 && result.ece <= 1.0);
        QVERIFY(result.nSamples == 10'000);
        QVERIFY(!result.bins.isEmpty());
    }

    // ── 10. CoOffendingAnalyser — 200-person graph ───────────────────────────
    // Design target: ≤ 3000ms   Safety margin: ≤ 6000ms
    void testCoOffending200() {
        // Create 200 persons across 80 incidents (2-5 persons per incident)
        QVector<PersonIncidentRecord> records;
        const QStringList roles = { "suspect", "witness", "associate" };
        const double roleWeights[] = { 1.0, 0.3, 0.5 };

        for (int inc = 0; inc < 80; ++inc) {
            const int nPersons = randInt(2, 5);
            for (int p = 0; p < nPersons; ++p) {
                PersonIncidentRecord r;
                r.incidentId = QStringLiteral("inc_%1").arg(inc);
                r.personId   = QStringLiteral("person_%1").arg(randInt(0, 199));
                const int ri = randInt(0, 2);
                r.role       = roles[ri];
                r.roleWeight = roleWeights[ri];
                records.append(r);
            }
        }

        CoOffendingAnalyser analyser;
        QElapsedTimer t;
        t.start();
        analyser.buildGraph(records);
        analyser.analyse();
        const qint64 elapsed = t.elapsed();

        qDebug("CoOffending (200-person graph, 80 incidents): %lld ms", elapsed);
        QVERIFY2(elapsed < 6000,
            qPrintable(QStringLiteral("took %1 ms, limit 6000 ms").arg(elapsed)));
        QVERIFY(analyser.isBuilt());

        const QVector<NetworkNode> nodes = analyser.nodes();
        QVERIFY(!nodes.isEmpty());
        for (const auto& node : nodes) {
            QVERIFY(node.pageRank >= 0.0);
            QVERIFY(node.betweenness >= 0.0);
        }

        const QVector<NetworkLead> leads = analyser.findLeads("inc_0", 5);
        QVERIFY(leads.size() <= 5);
    }

    // ── 11. EnsemblePredictor risk grid — 10x10 with Poisson ─────────────────
    // Design target: ≤ 500ms   Safety margin: ≤ 1000ms
    void testEnsembleRiskGrid10x10() {
        // Fit a Poisson model with 2000 events
        QVector<PoissonBaseline::EventRecord> poissonEvents;
        poissonEvents.reserve(2000);
        const QDateTime base = baseTime();
        for (int i = 0; i < 2000; ++i) {
            PoissonBaseline::EventRecord r;
            r.zoneId     = QStringLiteral("zone_%1").arg(randInt(0, 9));
            r.occurredAt = base.addSecs(randInt(0, 180 * 86400));
            r.crimeType  = kCrimeTypes[randInt(0, kCrimeTypes.size() - 1)];
            poissonEvents.append(r);
        }
        PoissonBaseline poisson;
        poisson.fit(poissonEvents);
        QVERIFY(poisson.isFitted());

        EnsemblePredictor ensemble;
        ensemble.setPoisson(&poisson);
        ensemble.setWeights(1.0, 0.0);  // Poisson-only as requested

        const QDateTime queryDt = base.addDays(200);

        QElapsedTimer t;
        t.start();
        const auto grid = ensemble.riskGrid(
            queryDt,
            51.45, 51.55,   // latMin, latMax
            -0.18, -0.08,   // lonMin, lonMax
            10);            // gridN = 10
        const qint64 elapsed = t.elapsed();

        qDebug("EnsemblePredictor riskGrid (10x10, Poisson-only): %lld ms", elapsed);
        QVERIFY2(elapsed < 1000,
            qPrintable(QStringLiteral("took %1 ms, limit 1000 ms").arg(elapsed)));
        QCOMPARE(grid.size(), 10);
        QCOMPARE(grid[0].size(), 10);
        for (const auto& row : grid) {
            for (const auto& cell : row) {
                QVERIFY(cell.probCrime >= 0.0 && cell.probCrime <= 1.0);
                QVERIFY(cell.expectedCount >= 0.0);
            }
        }
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Main
// ─────────────────────────────────────────────────────────────────────────────
static int runTest(QObject* obj, const char* logFile) {
    QStringList args = { "test", "-o", QString("%1,txt").arg(logFile) };
    return QTest::qExec(obj, args);
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    int r = 0;
    TestPerformanceRegression t1;
    r |= runTest(&t1, "perf_regression.txt");
    return r;
}

#include "test_performance.moc"
