// test_e2e_integration.cpp
// End-to-end integration tests for the full SENTINEL investigative workflow.
// Each test exercises multiple pipeline components in combination.

#include <QtTest>
#include <QCoreApplication>
#include <QVector>
#include <QPair>
#include <QDateTime>
#include <QString>
#include <algorithm>

#include "core/CrimeEvent.h"
#include "core/Database.h"
#include "core/AppConfig.h"
#include "ingest/DataQualityScorer.h"
#include "models/PoissonBaseline.h"
#include "models/HawkesProcess.h"
#include "models/KDEHotspot.h"
#include "models/SeriesDetector.h"
#include "inference/GeographicProfiler.h"
#include "inference/AnomalyDetector.h"
#include "audit/ProvenanceLog.h"

// ─── Helper ──────────────────────────────────────────────────────────────────

static CrimeEvent makeEvent(int i, double lat, double lon, int daysAgo,
                             const QString& type, const QString& zone)
{
    CrimeEvent e;
    e.eventId        = QString("ev%1").arg(i);
    e.id             = e.eventId;
    double elat      = lat + (i % 5) * 0.001;
    double elon      = lon + (i % 7) * 0.001;
    e.lat            = elat;
    e.lon            = elon;
    e.latitude       = elat;
    e.longitude      = elon;
    QDateTime dt     = QDateTime::currentDateTimeUtc().addDays(-daysAgo);
    e.occurredAt     = dt;
    e.timestamp      = dt;
    e.crimeType      = type;
    e.suburb         = zone;
    e.qualityScore   = 0.8;
    e.source         = "test_source";
    e.ingestedAt     = QDateTime::currentDateTimeUtc();
    return e;
}

static AnomalyFeatureVector toFeatureVec(const CrimeEvent& ev,
                                          const QDateTime& epoch,
                                          int typeCode = 0)
{
    AnomalyFeatureVector fv;
    fv.eventId       = ev.eventId;
    fv.lat           = ev.lat.value_or(0.0);
    fv.lon           = ev.lon.value_or(0.0);
    fv.tDays         = epoch.daysTo(ev.occurredAt.value_or(epoch));
    fv.hourNorm      = ev.occurredAt.value_or(epoch).time().hour() / 23.0;
    fv.crimeTypeCode = typeCode;
    return fv;
}

// ─── Test class ──────────────────────────────────────────────────────────────

class TestE2EIntegration : public QObject
{
    Q_OBJECT

private slots:
    void testFullInvestigativeWorkflow();
    void testDataQualityPipelineFiltering();
    void testDatabasePersistenceAndRetrieval();
    void testAnomalyAndLeadCorrelation();
};

// ─── 1. Full investigative workflow ──────────────────────────────────────────

void TestE2EIntegration::testFullInvestigativeWorkflow()
{
    const double baseLat = 51.5074;
    const double baseLon = -0.1278;
    const int    N       = 50;

    // ── Step 1: generate 50 crime events over 6 months ─────────────────────
    QVector<CrimeEvent> events;
    events.reserve(N);
    for (int i = 0; i < N; ++i) {
        int daysAgo = (i * 180) / N;                 // spread over ~180 days
        events.append(makeEvent(i, baseLat, baseLon, daysAgo, "burglary", "Westminster"));
    }
    QCOMPARE(events.size(), N);

    // ── Step 2: score quality ───────────────────────────────────────────────
    DataQualityScorer scorer;
    QVector<QualityReport> reports = scorer.scoreBatch(events);
    QCOMPARE(reports.size(), N);
    for (const auto& r : reports) {
        QVERIFY(r.compositeScore >= 0.0);
        QVERIFY(r.compositeScore <= 1.0);
    }

    // ── Step 3: insert into in-memory database ──────────────────────────────
    AppConfig cfg;
    cfg.databasePath = ":memory:";
    Database db(cfg);
    QVERIFY2(db.open(), "Failed to open in-memory database");
    QVERIFY(db.isOpen());

    for (const auto& ev : events) {
        QVERIFY(db.insertEvent(ev));
    }
    QVERIFY(db.eventCount() == N);

    // ── Step 4: fit PoissonBaseline ─────────────────────────────────────────
    QVector<PoissonBaseline::EventRecord> poissonRecs;
    poissonRecs.reserve(N);
    for (const auto& ev : events) {
        PoissonBaseline::EventRecord r;
        r.zoneId      = ev.suburb;
        r.occurredAt  = ev.occurredAt.value_or(QDateTime::currentDateTimeUtc());
        r.crimeType   = ev.crimeType;
        poissonRecs.append(r);
    }

    PoissonBaseline poisson;
    poisson.fit(poissonRecs);
    QVERIFY(poisson.isFitted());
    QVERIFY(poisson.totalEvents() == N);

    PoissonPrediction pred = poisson.predict("Westminster",
                                              QDateTime::currentDateTimeUtc(),
                                              "burglary");
    QVERIFY(pred.lambda >= 0.0);
    QVERIFY(pred.expectedCount >= 0.0);

    // ── Step 5: fit HawkesProcess ───────────────────────────────────────────
    QDateTime epoch = QDateTime::currentDateTimeUtc().addDays(-180);
    QVector<SpatiotemporalEvent> stEvents;
    stEvents.reserve(N);
    for (const auto& ev : events) {
        SpatiotemporalEvent se;
        se.tDays     = epoch.daysTo(ev.occurredAt.value_or(epoch));
        se.lat       = ev.lat.value_or(baseLat);
        se.lon       = ev.lon.value_or(baseLon);
        se.crimeType = ev.crimeType;
        stEvents.append(se);
    }

    HawkesProcess hawkes;
    bool fitted = hawkes.fit(stEvents, 50);  // fewer iterations for speed
    QVERIFY(hawkes.isFitted() || !fitted);   // fit may or may not converge
    // Intensity query should work regardless of convergence after setHistory
    hawkes.setHistory(stEvents);
    double inten = hawkes.intensity(180.0, baseLat, baseLon);
    QVERIFY(inten >= 0.0);

    // ── Step 6: KDE hotspot detection ──────────────────────────────────────
    QVector<QPair<double,double>> locations;
    locations.reserve(N);
    for (const auto& ev : events) {
        locations.append({ev.lat.value_or(baseLat), ev.lon.value_or(baseLon)});
    }

    KDEHotspot kde(20);  // smaller grid for speed
    auto hotspots = kde.findHotspots(locations,
                                      baseLat - 0.05, baseLat + 0.05,
                                      baseLon - 0.05, baseLon + 0.05,
                                      3);
    QVERIFY(!hotspots.isEmpty());
    for (const auto& h : hotspots) {
        QVERIFY(h.rank > 0);
        QVERIFY(h.peakDensity >= 0.0);
    }

    // ── Step 7: series detection ────────────────────────────────────────────
    SeriesDetector detector(0.5, 30.0, 3);  // 500m, 30 days, min 3 events
    auto series = detector.detect(events);
    // May or may not find series depending on event spread; just check validity
    for (const auto& s : series) {
        QVERIFY(!s.seriesId.isEmpty());
        QVERIFY(s.members.size() >= 3);
        QVERIFY(static_cast<int>(s.members.size()) >= 3);
    }

    // ── Step 8: anomaly detection ───────────────────────────────────────────
    QVector<AnomalyFeatureVector> fvs;
    fvs.reserve(N);
    for (const auto& ev : events) {
        fvs.append(toFeatureVec(ev, epoch));
    }

    AnomalyDetector anomaly(0.1);
    anomaly.fit(fvs);
    QVERIFY(anomaly.isFitted());

    auto anomalyResults = anomaly.detectAnomalies(fvs);
    QCOMPARE(anomalyResults.size(), N);
    for (const auto& sig : anomalyResults) {
        QVERIFY(sig.combinedScore >= 0.0);
        QVERIFY(sig.combinedScore <= 1.0);
    }

    // ── Step 9: geographic profiling ────────────────────────────────────────
    // Use all event locations for profiling (requires >= 3)
    GeographicProfiler profiler;
    auto geoProfile = profiler.profile(locations);
    QVERIFY(!geoProfile.probabilitySurface.empty());
    QVERIFY(geoProfile.peakProbability >= 0.0);
    QVERIFY(geoProfile.searchArea50pct >= 0.0);

    // ── Step 10: log provenance for each step ───────────────────────────────
    ProvenanceLog plog;
    plog.record("workflow", "ingest",    "generate_events",  QString("Generated %1 events").arg(N));
    plog.record("workflow", "ingest",    "quality_score",    "Scored quality for all events");
    plog.record("workflow", "ingest",    "db_insert",        "Inserted events into database");
    plog.record("workflow", "model",     "poisson_fit",      "Fitted PoissonBaseline");
    plog.record("workflow", "model",     "hawkes_fit",       "Fitted HawkesProcess");
    plog.record("workflow", "model",     "kde_hotspot",      "Detected KDE hotspots");
    plog.record("workflow", "inference", "series_detect",    "Detected crime series");
    plog.record("workflow", "inference", "anomaly_detect",   "Ran AnomalyDetector");
    plog.record("workflow", "inference", "geo_profile",      "Ran GeographicProfiler");

    // ── Step 11: assert provenance chain ────────────────────────────────────
    auto chain = plog.chain("workflow");
    QVERIFY(chain.size() == 9);
    for (const auto& entry : chain) {
        QVERIFY(!entry.stage.isEmpty());
        QVERIFY(!entry.action.isEmpty());
        QVERIFY(entry.timestamp.isValid());
    }

    db.close();
}

// ─── 2. Data quality pipeline filtering ──────────────────────────────────────

void TestE2EIntegration::testDataQualityPipelineFiltering()
{
    const int TOTAL = 100;
    QVector<CrimeEvent> events;
    events.reserve(TOTAL);

    // First 70 events: valid lat/lon, all fields set
    for (int i = 0; i < 70; ++i) {
        CrimeEvent e = makeEvent(i, 51.5074, -0.1278, i % 180, "robbery", "Camden");
        e.source = "reliable_source";
        events.append(e);
    }

    // Last 30 events: missing lat/lon, missing time, low-reliability source
    // Scoring: completeness = 1/4 (only crimeType set),
    // sourceReliability = 0.1 → compositeScore = 0.5*0.25 + 0.5*0.1 = 0.175 < 0.3
    for (int i = 70; i < TOTAL; ++i) {
        CrimeEvent e;
        e.eventId    = QString("ev%1").arg(i);
        e.id         = e.eventId;
        e.crimeType  = "robbery";
        e.suburb     = "Unknown";
        e.source     = "low_quality_source";   // mapped to 0.1 reliability below
        e.ingestedAt = QDateTime::currentDateTimeUtc();
        // lat, lon, occurredAt intentionally left as nullopt
        events.append(e);
    }

    QCOMPARE(events.size(), TOTAL);

    // Score all events — configure scorer so low_quality_source has 0.1 reliability
    DataQualityScorer scorer({{"low_quality_source", 0.1}});
    QVector<QualityReport> reports = scorer.scoreBatch(events);
    QCOMPARE(reports.size(), TOTAL);

    // Compute average scores for good (0-69) and bad (70-99) events
    double goodAvg = 0.0, badAvg = 0.0;
    for (int i = 0;  i < 70;    ++i) goodAvg += reports[i].compositeScore;
    for (int i = 70; i < TOTAL; ++i) badAvg  += reports[i].compositeScore;
    goodAvg /= 70.0;
    badAvg  /= 30.0;

    // Events with full coordinates/timestamps should score higher than events missing them
    QVERIFY2(goodAvg > badAvg,
             qPrintable(QString("Good events avg score %1 should exceed bad events avg %2")
                        .arg(goodAvg).arg(badAvg)));

    // All events with lat/lon should pass a moderate threshold
    QVector<CrimeEvent> passing;
    for (int i = 0; i < TOTAL; ++i) {
        if (reports[i].compositeScore >= 0.3) passing.append(events[i]);
    }
    // At least the 70 good events should pass the threshold
    QVERIFY2(passing.size() >= 70,
             qPrintable(QString("Expected at least 70 events to pass quality threshold, got %1")
                        .arg(passing.size())));
}

// ─── 3. Database persistence and retrieval ───────────────────────────────────

void TestE2EIntegration::testDatabasePersistenceAndRetrieval()
{
    AppConfig cfg;
    cfg.databasePath = ":memory:";
    Database db(cfg);
    QVERIFY(db.open());

    const int TOTAL = 200;

    // Insert 100 "theft" events in zone A (London centre)
    for (int i = 0; i < 100; ++i) {
        CrimeEvent e = makeEvent(i, 51.50, -0.12, i, "theft", "Southwark");
        QVERIFY(db.insertEvent(e));
    }
    // Insert 100 "assault" events in zone B (slightly north)
    for (int i = 0; i < 100; ++i) {
        CrimeEvent e = makeEvent(i + 100, 51.55, -0.10, i, "assault", "Islington");
        QVERIFY(db.insertEvent(e));
    }

    QCOMPARE(db.eventCount(), TOTAL);

    // ── Query by crime type ──────────────────────────────────────────────────
    auto thefts   = db.queryEvents("theft");
    auto assaults = db.queryEvents("assault");
    QVERIFY(thefts.size() > 0);
    QVERIFY(assaults.size() > 0);
    QVERIFY(thefts.size() <= TOTAL);
    QVERIFY(assaults.size() <= TOTAL);
    QVERIFY(thefts.size() + assaults.size() == TOTAL);

    // ── Query by spatial bounding box ────────────────────────────────────────
    // Southern box: captures only theft events (lat ~51.50)
    auto southBox = db.queryEvents(QString{}, QDateTime{}, QDateTime{},
                                    51.48, 51.52,    // latMin, latMax
                                    -0.15, -0.09);   // lonMin, lonMax
    QVERIFY(southBox.size() > 0);
    QVERIFY(southBox.size() < TOTAL);

    // ── Query by date range ──────────────────────────────────────────────────
    QDateTime from = QDateTime::currentDateTimeUtc().addDays(-30);
    QDateTime to   = QDateTime::currentDateTimeUtc();
    auto recent = db.queryEvents(QString{}, from, to);
    QVERIFY(recent.size() > 0);
    QVERIFY(recent.size() < TOTAL);  // only last 30 days of the 200-day spread

    db.close();
}

// ─── 4. Anomaly detection and outlier identification ─────────────────────────

void TestE2EIntegration::testAnomalyAndLeadCorrelation()
{
    const int  NORMAL = 80;
    QDateTime  epoch  = QDateTime::currentDateTimeUtc().addDays(-90);

    // Normal events: clustered around London
    QVector<AnomalyFeatureVector> fvs;
    fvs.reserve(NORMAL + 1);

    for (int i = 0; i < NORMAL; ++i) {
        AnomalyFeatureVector fv;
        fv.eventId       = QString("ev%1").arg(i);
        fv.lat           = 51.5074 + (i % 5) * 0.001;
        fv.lon           = -0.1278 + (i % 7) * 0.001;
        fv.tDays         = static_cast<double>(i);     // spread over 80 days
        fv.hourNorm      = (i % 24) / 23.0;
        fv.crimeTypeCode = 0;
        fvs.append(fv);
    }

    // Clear outlier: far from London, time far outside training range
    AnomalyFeatureVector outlier;
    outlier.eventId       = "outlier_event";
    outlier.lat           = 40.7128;   // New York latitude
    outlier.lon           = -74.0060;  // New York longitude
    outlier.tDays         = 5000.0;    // far in the future relative to training
    outlier.hourNorm      = 0.5;
    outlier.crimeTypeCode = 99;
    fvs.append(outlier);

    QCOMPARE(fvs.size(), NORMAL + 1);

    // Fit on all events (including the outlier)
    AnomalyDetector detector(0.05);
    detector.fit(fvs);
    QVERIFY(detector.isFitted());

    auto detResults = detector.detectAnomalies(fvs);
    QCOMPARE(detResults.size(), NORMAL + 1);

    // Find the signal for the outlier
    AnomalySignal outlierSig;
    bool found = false;
    for (const auto& s : detResults) {
        if (s.eventId == "outlier_event") {
            outlierSig = s;
            found = true;
            break;
        }
    }
    QVERIFY2(found, "Outlier event signal not found in results");

    // The outlier should have a higher combined score than the average normal event
    double normalSum = 0.0;
    int normalCount  = 0;
    for (const auto& s : detResults) {
        if (s.eventId != "outlier_event") {
            normalSum += s.combinedScore;
            ++normalCount;
        }
    }
    double normalAvg = (normalCount > 0) ? (normalSum / normalCount) : 0.0;

    QVERIFY2(outlierSig.combinedScore > normalAvg,
             qPrintable(QString("Outlier score %1 should exceed normal average %2")
                        .arg(outlierSig.combinedScore).arg(normalAvg)));

    // Verify that most normal events have low anomaly scores (< 0.8)
    int highScoreNormals = 0;
    for (const auto& s : detResults) {
        if (s.eventId != "outlier_event" && s.combinedScore > 0.8) {
            ++highScoreNormals;
        }
    }
    // Allow up to 20% false positives
    QVERIFY2(highScoreNormals <= NORMAL / 5,
             qPrintable(QString("Too many normal events flagged: %1").arg(highScoreNormals)));
}

// ─────────────────────────────────────────────────────────────────────────────

QTEST_MAIN(TestE2EIntegration)
#include "test_e2e_integration.moc"
