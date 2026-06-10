// test_pipeline_e2e_stress.cpp — End-to-end stress tests for the SENTINEL
// C++23/Qt6 crime analytics pipeline.
//
// 10 test cases exercising all major subsystems at 1000-event scale.

#include <QTest>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QDateTime>
#include <QDate>
#include <cmath>

#include "core/CrimeEvent.h"
#include "core/AppConfig.h"
#include "core/Database.h"
#include "models/PoissonBaseline.h"
#include "models/SeriesDetector.h"
#include "models/RiskForecaster.h"
#include "inference/HintEngine.h"
#include "inference/CoOffendingAnalyser.h"
#include "benchmark/BenchmarkMetrics.h"

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

namespace {

static const QStringList kSuburbs    = { "Z1","Z2","Z3","Z4","Z5" };
static const QStringList kCrimeTypes = {
    "burglary","theft","assault","robbery","fraud","vandalism","drug"
};

// Base anchor: 30 days before epoch 2025-05-12 UTC
static const QDateTime kBase = QDateTime(QDate(2025,4,12), QTime(0,0,0), Qt::UTC);

// Deterministic pseudo-random: simple LCG
inline quint32 lcg(quint32& state) {
    state = state * 1664525u + 1013904223u;
    return state;
}

// Build N synthetic CrimeEvents deterministically.
// Coordinates cluster around London suburbs spread over 5 zones.
QVector<CrimeEvent> makeSyntheticEvents(int n, quint32 seed = 42u)
{
    quint32 rng = seed;
    const QVector<QPair<double,double>> zoneCentres = {
        {51.50, -0.12}, {51.52, -0.09}, {51.48, -0.10},
        {51.51,  -0.14}, {51.49, -0.07}
    };

    QVector<CrimeEvent> out;
    out.reserve(n);

    for (int i = 0; i < n; ++i) {
        CrimeEvent ev;
        ev.eventId    = QString("S-%1").arg(i, 6, 10, QChar('0'));
        ev.source     = QStringLiteral("stress_test");
        ev.ingestedAt = QDateTime::currentDateTimeUtc();

        // deterministic timestamp within last 30 days
        const int secondsInMonth = 30 * 24 * 3600;
        const int secOffset = static_cast<int>(lcg(rng) % static_cast<quint32>(secondsInMonth));
        const QDateTime ts = kBase.addSecs(secOffset);
        ev.occurredAt = ts;
        ev.timestamp  = ts;

        const int zi = static_cast<int>(lcg(rng) % 5u);
        ev.suburb = kSuburbs[zi];
        const auto [clat, clon] = zoneCentres[zi];

        // jitter ±0.005 degrees (≈ 500 m)
        const double jlat = (static_cast<double>(lcg(rng) % 1001u) - 500.0) * 0.00001;
        const double jlon = (static_cast<double>(lcg(rng) % 1001u) - 500.0) * 0.00001;
        ev.lat       = clat + jlat;
        ev.lon       = clon + jlon;
        ev.latitude  = ev.lat.value_or(0.0);
        ev.longitude = ev.lon.value_or(0.0);

        ev.crimeType     = kCrimeTypes[static_cast<int>(lcg(rng) % 7u)];
        ev.qualityScore  = 0.8;
        ev.outcome       = QStringLiteral("unresolved");
        out.append(ev);
    }
    return out;
}

// Build N events with a 50-event tight cluster injected at index 0..49
// (used to guarantee series detection).
QVector<CrimeEvent> makeSyntheticEventsWithCluster(int n)
{
    QVector<CrimeEvent> events = makeSyntheticEvents(n);

    // Overwrite first 50 events: same suburb, same crime type, same tiny area,
    // timestamps within 7 days (tight spatiotemporal cluster for DBSCAN)
    const QDateTime clusterBase = kBase.addDays(20);
    for (int i = 0; i < 50 && i < n; ++i) {
        events[i].suburb    = QStringLiteral("Z1");
        events[i].crimeType = QStringLiteral("burglary");
        events[i].lat       = 51.500 + i * 0.00005;
        events[i].lon       = -0.120 + i * 0.00005;
        events[i].latitude  = events[i].lat.value_or(0.0);
        events[i].longitude = events[i].lon.value_or(0.0);
        events[i].occurredAt = clusterBase.addSecs(i * 3600);  // hourly within cluster
        events[i].timestamp  = events[i].occurredAt.value_or(QDateTime{});
    }
    return events;
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────

class TestPipelineE2EStress : public QObject {
    Q_OBJECT
private slots:
    void testIngest1000Events();
    void testQueryPerformance();
    void testPoissonFitLargeDataset();
    void testSeriesDetection1000Events();
    void testRiskForecaster1000Events();
    void testHintEngine50Leads();
    void testCoOffendingLargeNetwork();
    void testBenchmarkMetrics1000();
    void testDatabaseBulkInsertPerformance();
    void testFullPipelineRoundtrip();
};

// ─────────────────────────────────────────────────────────────────────────────
// 1. testIngest1000Events
// ─────────────────────────────────────────────────────────────────────────────
void TestPipelineE2EStress::testIngest1000Events()
{
    AppConfig cfg;
    cfg.databasePath = QStringLiteral(":memory:");
    auto db = std::make_shared<Database>(cfg);
    QVERIFY(db->open());

    const auto events = makeSyntheticEvents(1000);
    for (const auto& ev : events)
        QVERIFY(db->insertEvent(ev));

    QCOMPARE(db->eventCount(), 1000);
}

// ─────────────────────────────────────────────────────────────────────────────
// 2. testQueryPerformance
// ─────────────────────────────────────────────────────────────────────────────
void TestPipelineE2EStress::testQueryPerformance()
{
    AppConfig cfg;
    cfg.databasePath = QStringLiteral(":memory:");
    auto db = std::make_shared<Database>(cfg);
    QVERIFY(db->open());

    const auto events = makeSyntheticEvents(1000);
    for (const auto& ev : events)
        db->insertEvent(ev);

    QElapsedTimer timer;
    timer.start();
    const auto results = db->getAllEvents();
    const qint64 elapsed = timer.elapsed();

    QVERIFY2(results.size() >= 1000,
             qPrintable(QString("Expected >= 1000 results, got %1").arg(results.size())));
    QVERIFY2(elapsed < 2000,
             qPrintable(QString("queryEvents took %1 ms, expected < 2000 ms").arg(elapsed)));
}

// ─────────────────────────────────────────────────────────────────────────────
// 3. testPoissonFitLargeDataset
// ─────────────────────────────────────────────────────────────────────────────
void TestPipelineE2EStress::testPoissonFitLargeDataset()
{
    const auto events = makeSyntheticEvents(1000);

    QVector<PoissonBaseline::EventRecord> recs;
    recs.reserve(events.size());
    for (const auto& ev : events) {
        PoissonBaseline::EventRecord r;
        r.zoneId     = ev.suburb;
        r.occurredAt = ev.occurredAt.value_or(ev.ingestedAt);
        r.crimeType  = ev.crimeType;
        recs.append(r);
    }

    PoissonBaseline baseline;
    baseline.fit(recs);

    QVERIFY2(baseline.isFitted(),
             "PoissonBaseline should be fitted after 1000 events");
    QVERIFY2(baseline.totalEvents() == 1000,
             qPrintable(QString("Expected totalEvents=1000, got %1")
                 .arg(baseline.totalEvents())));
}

// ─────────────────────────────────────────────────────────────────────────────
// 4. testSeriesDetection1000Events
// ─────────────────────────────────────────────────────────────────────────────
void TestPipelineE2EStress::testSeriesDetection1000Events()
{
    const auto crimeEvents = makeSyntheticEventsWithCluster(1000);

    // Convert to SeriesEvent
    const QDateTime epoch(QDate(2025,1,1), QTime(0,0,0), Qt::UTC);
    QVector<SeriesEvent> seriesEvents;
    seriesEvents.reserve(crimeEvents.size());
    for (const auto& ev : crimeEvents) {
        SeriesEvent se;
        se.eventId   = ev.eventId;
        se.lat       = ev.lat.value_or(0.0);
        se.lon       = ev.lon.value_or(0.0);
        const QDateTime dt = ev.occurredAt.value_or(ev.ingestedAt);
        se.tDays     = static_cast<double>(epoch.daysTo(dt));
        se.crimeType = ev.crimeType;
        se.moText    = ev.crimeType;
        seriesEvents.append(se);
    }

    // Use permissive epsilon to catch the injected 50-event tight cluster
    SeriesDetector detector(0.5, 14.0, 3);
    const auto series = detector.detectSeries(seriesEvents);

    QVERIFY2(series.size() >= 1,
             qPrintable(QString("Expected >= 1 series in 1000 events with 50-event cluster, got %1")
                 .arg(series.size())));
}

// ─────────────────────────────────────────────────────────────────────────────
// 5. testRiskForecaster1000Events
// ─────────────────────────────────────────────────────────────────────────────
void TestPipelineE2EStress::testRiskForecaster1000Events()
{
    const auto events = makeSyntheticEvents(1000);

    RiskForecaster forecaster(7);
    forecaster.fit(events);

    QVERIFY2(forecaster.isFitted(),
             "RiskForecaster should be fitted after 1000 events");
    QVERIFY2(forecaster.zoneCount() >= 1,
             qPrintable(QString("Expected >= 1 zone, got %1").arg(forecaster.zoneCount())));

    const auto forecasts = forecaster.forecast(QDateTime::currentDateTimeUtc());
    QVERIFY2(forecasts.size() >= 1,
             qPrintable(QString("Expected >= 1 zone forecast, got %1").arg(forecasts.size())));
}

// ─────────────────────────────────────────────────────────────────────────────
// 6. testHintEngine50Leads
// ─────────────────────────────────────────────────────────────────────────────
void TestPipelineE2EStress::testHintEngine50Leads()
{
    const auto events = makeSyntheticEvents(50, 99u);

    HintEngine engine;
    int eventsWithLeads = 0;

    for (int i = 0; i < 50; ++i) {
        HintEngineInput input;
        input.event       = events[i];
        input.dataQuality = 0.85;

        // Inject a series match so the engine always has something to work with
        SeriesMatch sm;
        sm.seriesId        = QString("SER-%1").arg(i);
        sm.memberCount     = 4;
        sm.linkProbability = 0.70;
        sm.compositeScore  = 0.65;
        sm.method          = "DBSCAN";
        input.seriesMatches.append(sm);

        const auto leads = engine.generate(input);

        // Verify all leads are structurally valid
        for (const auto& lead : leads) {
            QVERIFY(lead.confidence >= 0.0);
            QVERIFY(lead.confidence <= 1.0);
        }

        if (!leads.isEmpty())
            ++eventsWithLeads;
    }

    QVERIFY2(eventsWithLeads >= 50,
             qPrintable(QString("Expected all 50 inputs to produce leads, got %1")
                 .arg(eventsWithLeads)));
}

// ─────────────────────────────────────────────────────────────────────────────
// 7. testCoOffendingLargeNetwork
// ─────────────────────────────────────────────────────────────────────────────
void TestPipelineE2EStress::testCoOffendingLargeNetwork()
{
    // Build a 100-person co-offending network:
    // persons P0..P99, incidents INC0..INC49.
    // Each incident links 4 consecutive persons (rotating through 0..99).
    QVector<PersonIncidentRecord> records;
    records.reserve(200);

    for (int inc = 0; inc < 50; ++inc) {
        for (int k = 0; k < 4; ++k) {
            const int pid = (inc * 2 + k) % 100;
            PersonIncidentRecord r;
            r.personId   = QString("P%1").arg(pid);
            r.incidentId = QString("INC%1").arg(inc);
            r.role       = (k == 0) ? QStringLiteral("suspect") : QStringLiteral("associate");
            r.roleWeight = (k == 0) ? 1.0 : 0.5;
            records.append(r);
        }
    }

    CoOffendingAnalyser analyser;
    analyser.buildGraph(records);
    analyser.analyse();

    QVERIFY2(analyser.isBuilt(), "CoOffendingAnalyser should be built");

    const auto nodes = analyser.nodes();
    QVERIFY2(nodes.size() >= 1, "Network should have at least 1 node");

    bool allPageRankNonNeg = true;
    for (const auto& node : nodes) {
        if (node.pageRank < 0.0) {
            allPageRankNonNeg = false;
            break;
        }
    }
    QVERIFY2(allPageRankNonNeg, "All PageRank values should be >= 0");
}

// ─────────────────────────────────────────────────────────────────────────────
// 8. testBenchmarkMetrics1000
// ─────────────────────────────────────────────────────────────────────────────
void TestPipelineE2EStress::testBenchmarkMetrics1000()
{
    const int n = 1000;
    QVector<double> yTrue(n, 0.0);
    QVector<double> yPred(n, 0.0);

    // 200 crime cells in first 200 positions, graded predictor
    quint32 rng = 777u;
    for (int i = 0; i < n; ++i) {
        yTrue[i] = (i < 200) ? 1.0 : 0.0;
        // Predictor: higher scores for true crime cells, with noise
        const double base = (i < 200) ? 0.7 : 0.3;
        const double noise = (static_cast<double>(lcg(rng) % 201u) - 100.0) * 0.001;
        yPred[i] = std::clamp(base + noise, 0.0, 1.0);
    }

    const auto report = BenchmarkMetrics::fullReport(yTrue, yPred);

    QVERIFY2(report.pai5pct  >= 0.0, "PAI@5% should be >= 0");
    QVERIFY2(report.pai10pct >= 0.0, "PAI@10% should be >= 0");
    QVERIFY2(report.pai20pct >= 0.0, "PAI@20% should be >= 0");
    QVERIFY2(report.aucRoc   >= 0.0 && report.aucRoc   <= 1.0, "AUC-ROC in [0,1]");
    QVERIFY2(report.aucPr    >= 0.0 && report.aucPr    <= 1.0, "AUC-PR in [0,1]");
    QVERIFY2(report.brierScore >= 0.0 && report.brierScore <= 1.0, "Brier score in [0,1]");
    QVERIFY2(report.mae  >= 0.0, "MAE should be >= 0");
    QVERIFY2(report.rmse >= 0.0, "RMSE should be >= 0");
    QVERIFY2(report.nSamples == n,
             qPrintable(QString("Expected nSamples=%1, got %2").arg(n).arg(report.nSamples)));

    // Sanity: with 200/1000 = 20% base rate, PAI@5% > 1.0 means we beat random
    QVERIFY2(report.pai5pct > 1.0,
             qPrintable(QString("Expected PAI@5%% > 1.0 (informed predictor), got %1")
                 .arg(report.pai5pct, 0, 'f', 3)));
}

// ─────────────────────────────────────────────────────────────────────────────
// 9. testDatabaseBulkInsertPerformance
// ─────────────────────────────────────────────────────────────────────────────
void TestPipelineE2EStress::testDatabaseBulkInsertPerformance()
{
    AppConfig cfg;
    cfg.databasePath = QStringLiteral(":memory:");
    auto db = std::make_shared<Database>(cfg);
    QVERIFY(db->open());

    const auto events = makeSyntheticEvents(1000);

    QElapsedTimer timer;
    timer.start();
    for (const auto& ev : events)
        db->insertEvent(ev);
    const qint64 elapsed = timer.elapsed();

    QCOMPARE(db->eventCount(), 1000);
    QVERIFY2(elapsed < 5000,
             qPrintable(QString("1000 inserts took %1 ms, expected < 5000 ms").arg(elapsed)));
}

// ─────────────────────────────────────────────────────────────────────────────
// 10. testFullPipelineRoundtrip
// ─────────────────────────────────────────────────────────────────────────────
void TestPipelineE2EStress::testFullPipelineRoundtrip()
{
    // ── (a) Ingest 500 events ─────────────────────────────────────────────────
    AppConfig cfg;
    cfg.databasePath = QStringLiteral(":memory:");
    auto db = std::make_shared<Database>(cfg);
    QVERIFY(db->open());

    const auto events = makeSyntheticEventsWithCluster(500);
    for (const auto& ev : events)
        db->insertEvent(ev);
    QVERIFY(db->eventCount() == 500);

    // ── (b) Fit PoissonBaseline ───────────────────────────────────────────────
    QVector<PoissonBaseline::EventRecord> poissonRecs;
    poissonRecs.reserve(500);
    for (const auto& ev : events) {
        PoissonBaseline::EventRecord r;
        r.zoneId     = ev.suburb;
        r.occurredAt = ev.occurredAt.value_or(ev.ingestedAt);
        r.crimeType  = ev.crimeType;
        poissonRecs.append(r);
    }
    PoissonBaseline baseline;
    baseline.fit(poissonRecs);
    QVERIFY(baseline.isFitted());

    // ── (c) Fit RiskForecaster ───────────────────────────────────────────────
    RiskForecaster forecaster(7);
    forecaster.fit(events);
    QVERIFY(forecaster.isFitted());
    const auto zoneForecast = forecaster.forecast(QDateTime::currentDateTimeUtc());
    QVERIFY(zoneForecast.size() >= 1);

    // ── (d) Run SeriesDetector ────────────────────────────────────────────────
    SeriesDetector detector(0.5, 14.0, 3);
    const auto series = detector.detect(events);
    QVERIFY(series.size() >= 1);

    // ── (e) Build CoOffending network ─────────────────────────────────────────
    QVector<PersonIncidentRecord> records;
    for (int i = 0; i < 30; ++i) {
        for (int k = 0; k < 3; ++k) {
            PersonIncidentRecord r;
            r.personId   = QString("RP%1").arg((i * 2 + k) % 60);
            r.incidentId = events[i].eventId;
            r.role       = (k == 0) ? QStringLiteral("suspect") : QStringLiteral("associate");
            r.roleWeight = (k == 0) ? 1.0 : 0.5;
            records.append(r);
        }
    }
    CoOffendingAnalyser coAnalyser;
    coAnalyser.buildGraph(records);
    coAnalyser.analyse();
    QVERIFY(coAnalyser.isBuilt());
    const auto networkLeads = coAnalyser.findLeads(events[0].eventId, 5);

    // ── (f) Generate leads via HintEngine ────────────────────────────────────
    HintEngine engine;
    HintEngineInput input;
    input.event        = events[0];
    input.dataQuality  = 0.9;
    input.networkLeads = networkLeads;

    if (!series.isEmpty()) {
        SeriesMatch sm;
        sm.seriesId        = series[0].seriesId;
        sm.memberCount     = series[0].members.size();
        sm.linkProbability = 0.75;
        sm.compositeScore  = 0.70;
        sm.method          = "DBSCAN";
        input.seriesMatches.append(sm);
    }

    const auto leads = engine.generate(input);

    // ── (g) Verify non-zero outputs ───────────────────────────────────────────
    QVERIFY2(db->eventCount() == 500, "DB should still have 500 events");
    QVERIFY2(forecaster.zoneCount() >= 1, "RiskForecaster should have >= 1 zone");
    QVERIFY2(series.size() >= 1, "SeriesDetector should find >= 1 series");
    QVERIFY2(leads.size() >= 1,
             qPrintable(QString("HintEngine should produce >= 1 lead, got %1")
                 .arg(leads.size())));

    // All leads must have valid confidence
    for (const auto& lead : leads) {
        QVERIFY(lead.confidence >= 0.0);
        QVERIFY(lead.confidence <= 1.0);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// main
// ─────────────────────────────────────────────────────────────────────────────

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    TestPipelineE2EStress test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_pipeline_e2e_stress.moc"
