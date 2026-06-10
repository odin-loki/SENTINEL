// test_quality_and_data.cpp — SENTINEL unit + integration tests
// Covers: DataQualityScorer, real London CSV pipeline, model fitting
// Build via: sentinel_test(test_quality_and_data test_quality_and_data.cpp)

#include <QTest>
#include <QCoreApplication>
#include <QFile>
#include <QDateTime>
#include <QDate>
#include <QTime>
#include <QMap>
#include <QVector>

#include "core/CrimeEvent.h"
#include "ingest/DataQualityScorer.h"
#include "ingest/CsvImporter.h"
#include "nlp/MOExtractor.h"
#include "nlp/CrimeClassifier.h"
#include "models/PoissonBaseline.h"
#include "models/HawkesProcess.h"
#include "models/SeriesDetector.h"
#include "inference/AnomalyDetector.h"
#include "inference/HintEngine.h"
#include "audit/ProvenanceLog.h"

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

static CrimeEvent makeFullEvent(const QString& id)
{
    CrimeEvent ev;
    ev.eventId   = id;
    ev.source    = QStringLiteral("uk_police_v1");
    ev.crimeType = QStringLiteral("burglary");
    ev.occurredAt = QDateTime(QDate(2024, 6, 15), QTime(14, 30, 0), Qt::UTC);
    ev.lat       = 51.5074;
    ev.lon       = -0.1278;
    ev.locationRaw = QStringLiteral("On or near High Street");
    ev.ingestedAt = QDateTime::currentDateTimeUtc();
    return ev;
}

// ─────────────────────────────────────────────────────────────────────────────
// TestDataQualityScorer
// ─────────────────────────────────────────────────────────────────────────────

class TestDataQualityScorer : public QObject
{
    Q_OBJECT

private slots:

    void testPerfectEvent()
    {
        QMap<QString, double> relMap;
        relMap[QStringLiteral("uk_police_v1")] = 1.0;
        DataQualityScorer scorer(relMap);

        CrimeEvent ev = makeFullEvent(QStringLiteral("perfect-001"));
        QualityReport r = scorer.score(ev);

        QVERIFY(qAbs(r.completeness - 1.0) < 1e-9);
        QVERIFY(qAbs(r.sourceReliability - 1.0) < 1e-9);
        QVERIFY(qAbs(r.compositeScore - 1.0) < 1e-9);
        QVERIFY(!r.quarantined);
    }

    void testMissingLatLon()
    {
        DataQualityScorer scorer;

        CrimeEvent ev;
        ev.eventId   = QStringLiteral("no-latlon-001");
        ev.crimeType = QStringLiteral("theft");
        ev.occurredAt = QDateTime(QDate(2024, 3, 10), QTime(9, 0, 0), Qt::UTC);
        ev.locationRaw = QStringLiteral("On or near Church Road");
        // lat / lon intentionally not set (nullopt)

        QualityReport r = scorer.score(ev);
        // 3 of 4 fields present (no lat/lon) → completeness = 0.75
        QVERIFY(r.completeness < 1.0);
        QVERIFY(r.completeness > 0.0);
        QVERIFY(qAbs(r.completeness - 0.75) < 1e-9);
    }

    void testMissingCrimeType()
    {
        DataQualityScorer scorer;

        CrimeEvent ev;
        ev.eventId   = QStringLiteral("no-ct-001");
        // crimeType empty
        ev.occurredAt = QDateTime(QDate(2024, 5, 20), QTime(11, 0, 0), Qt::UTC);
        ev.lat       = 51.5074;
        ev.lon       = -0.1278;
        ev.locationRaw = QStringLiteral("On or near Baker Street");

        QualityReport r = scorer.score(ev);
        // 3 of 4 fields present (no crimeType) → completeness = 0.75
        QVERIFY(r.completeness < 1.0);
        QVERIFY(qAbs(r.completeness - 0.75) < 1e-9);
    }

    void testQuarantineThreshold()
    {
        // All fields missing → completeness = 0.0, sourceReliability = 0.5 (default)
        // compositeScore = 0.5*0 + 0.5*0.5 = 0.25 < 0.3 → quarantined
        DataQualityScorer scorer;

        CrimeEvent ev;
        ev.eventId = QStringLiteral("quarantine-001");
        // All optional fields absent, crimeType empty, locationRaw absent

        QualityReport r = scorer.score(ev);
        QVERIFY(qAbs(r.completeness - 0.0) < 1e-9);
        QVERIFY(r.compositeScore < 0.3);
        QVERIFY(r.quarantined);
    }

    void testTemporalPrecisionHour()
    {
        DataQualityScorer scorer;
        CrimeEvent ev = makeFullEvent(QStringLiteral("tp-hour"));
        // occurredAt has time 14:30:00
        QualityReport r = scorer.score(ev);
        QCOMPARE(r.temporalPrecision, QStringLiteral("hour"));
    }

    void testTemporalPrecisionUnknown()
    {
        DataQualityScorer scorer;
        CrimeEvent ev;
        ev.eventId = QStringLiteral("tp-unknown");
        // occurredAt is nullopt (default)

        QualityReport r = scorer.score(ev);
        QCOMPARE(r.temporalPrecision, QStringLiteral("unknown"));
    }

    void testSpatialPrecisionExact()
    {
        DataQualityScorer scorer;
        CrimeEvent ev;
        ev.eventId = QStringLiteral("sp-exact");
        ev.lat     = 51.5074;   // 4 decimal places
        ev.lon     = -0.1278;   // 4 decimal places

        QualityReport r = scorer.score(ev);
        QCOMPARE(r.spatialPrecision, QStringLiteral("exact"));
    }

    void testSpatialPrecisionUnknown()
    {
        DataQualityScorer scorer;
        CrimeEvent ev;
        ev.eventId = QStringLiteral("sp-unknown");
        ev.lat     = 0.0;
        ev.lon     = 0.0;

        QualityReport r = scorer.score(ev);
        QCOMPARE(r.spatialPrecision, QStringLiteral("unknown"));
    }

    void testSourceReliabilityMap()
    {
        QMap<QString, double> relMap;
        relMap[QStringLiteral("my_source")] = 0.9;
        DataQualityScorer scorer(relMap);

        CrimeEvent ev = makeFullEvent(QStringLiteral("src-rel-001"));
        ev.source = QStringLiteral("my_source");

        QualityReport r = scorer.score(ev);
        QVERIFY(qAbs(r.sourceReliability - 0.9) < 1e-9);
    }

    void testSourceReliabilityDefault()
    {
        DataQualityScorer scorer; // no map → default 0.5
        CrimeEvent ev = makeFullEvent(QStringLiteral("src-def-001"));
        ev.source = QStringLiteral("unknown_source");

        QualityReport r = scorer.score(ev);
        QVERIFY(qAbs(r.sourceReliability - 0.5) < 1e-9);
    }

    void testBatchScoring()
    {
        DataQualityScorer scorer;
        QVector<CrimeEvent> events;
        for (int i = 0; i < 5; ++i)
            events.append(makeFullEvent(QStringLiteral("batch-%1").arg(i)));

        QVector<QualityReport> reports = scorer.scoreBatch(events);
        QCOMPARE(reports.size(), 5);

        for (int i = 0; i < 5; ++i)
            QCOMPARE(reports[i].eventId, events[i].eventId);
    }

    void testPassRate()
    {
        QVector<QualityReport> reports;

        QualityReport r1; r1.quarantined = false;
        QualityReport r2; r2.quarantined = false;
        QualityReport r3; r3.quarantined = true;
        reports << r1 << r2 << r3;

        const double rate = DataQualityScorer::passRate(reports);
        QVERIFY(qAbs(rate - (2.0 / 3.0)) < 1e-9);
    }

    void testPassRateAllPass()
    {
        QVector<QualityReport> reports;
        for (int i = 0; i < 4; ++i) {
            QualityReport r; r.quarantined = false;
            reports << r;
        }
        QVERIFY(qAbs(DataQualityScorer::passRate(reports) - 1.0) < 1e-9);
    }

    void testPassRateEmpty()
    {
        QVERIFY(qAbs(DataQualityScorer::passRate({}) - 0.0) < 1e-9);
    }

    void testCompositeScoreFormula()
    {
        QMap<QString, double> relMap;
        relMap[QStringLiteral("src_x")] = 0.8;
        DataQualityScorer scorer(relMap);

        CrimeEvent ev = makeFullEvent(QStringLiteral("formula-001"));
        ev.source = QStringLiteral("src_x");
        // completeness = 1.0, sourceReliability = 0.8
        // compositeScore = 0.5*1.0 + 0.5*0.8 = 0.9

        // New formula: 0.30*completeness + 0.20*temporalPrecision + 0.20*spatialPrecision + 0.30*sourceReliability
        // makeFullEvent: completeness=1.0, temporal="hour"→1.0, spatial lat=51.5074 lon=-0.1278 → "exact"→1.0, reliability=0.8
        // composite = 0.30*1.0 + 0.20*1.0 + 0.20*1.0 + 0.30*0.8 = 0.94
        QualityReport r = scorer.score(ev);
        QVERIFY2(qAbs(r.compositeScore - 0.94) < 1e-6,
                 qPrintable(QStringLiteral("compositeScore expected 0.94, got %1").arg(r.compositeScore)));
        QVERIFY(!r.quarantined);
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// TestRealDataPipeline
// ─────────────────────────────────────────────────────────────────────────────

class TestRealDataPipeline : public QObject
{
    Q_OBJECT

private:
    QVector<CrimeEvent> m_events;
    QString             m_csvPath;

private slots:

    void initTestCase()
    {
        // Try relative path from the test binary location
        m_csvPath = QCoreApplication::applicationDirPath()
                  + QStringLiteral("/../tests/data/london_crimes_2024.csv");
        if (!QFile::exists(m_csvPath))
            m_csvPath = QStringLiteral("tests/data/london_crimes_2024.csv");
        if (!QFile::exists(m_csvPath))
            m_csvPath = QCoreApplication::applicationDirPath()
                      + QStringLiteral("/data/london_crimes_2024.csv");

        if (!QFile::exists(m_csvPath)) {
            qWarning("CSV not found at any expected path; real-data tests will skip.");
            return;
        }

        m_events = CsvImporter::importFile(m_csvPath, QStringLiteral("london_2024"));
    }

    void testLoadLondonCSV()
    {
        if (!QFile::exists(m_csvPath))
            QSKIP("CSV test data not found - skipping");

        QVERIFY2(m_events.size() >= 80,
                 qPrintable(QString("Expected >= 80 events, got %1").arg(m_events.size())));
    }

    void testAllEventsHaveCoords()
    {
        if (!QFile::exists(m_csvPath) || m_events.isEmpty())
            QSKIP("CSV test data not found or empty - skipping");

        for (const CrimeEvent& ev : m_events) {
            QVERIFY2(ev.lat.has_value(),
                     qPrintable(QString("Missing lat for event %1").arg(ev.eventId)));
            QVERIFY2(ev.lon.has_value(),
                     qPrintable(QString("Missing lon for event %1").arg(ev.eventId)));
        }
    }

    void testCrimeTypesSet()
    {
        if (!QFile::exists(m_csvPath) || m_events.isEmpty())
            QSKIP("CSV test data not found or empty - skipping");

        for (const CrimeEvent& ev : m_events) {
            QVERIFY2(!ev.crimeType.isEmpty(),
                     qPrintable(QString("Empty crimeType for event %1").arg(ev.eventId)));
        }
    }

    void testQualityScore()
    {
        if (!QFile::exists(m_csvPath) || m_events.isEmpty())
            QSKIP("CSV test data not found or empty - skipping");

        DataQualityScorer scorer;
        QVector<QualityReport> reports = scorer.scoreBatch(m_events);
        QCOMPARE(reports.size(), m_events.size());

        const double rate = DataQualityScorer::passRate(reports);
        QVERIFY2(rate >= 0.7,
                 qPrintable(QString("Pass rate %1 below 0.7").arg(rate)));
    }

    void testPoissonFit()
    {
        if (!QFile::exists(m_csvPath) || m_events.isEmpty())
            QSKIP("CSV test data not found or empty - skipping");

        const QDateTime epoch(QDate(2024, 1, 1), QTime(0, 0, 0), Qt::UTC);

        QVector<PoissonBaseline::EventRecord> records;
        records.reserve(m_events.size());
        for (const CrimeEvent& ev : m_events) {
            PoissonBaseline::EventRecord rec;
            rec.zoneId    = QStringLiteral("london");
            rec.occurredAt = ev.occurredAt.value_or(epoch);
            rec.crimeType = ev.crimeType;
            records.append(rec);
        }

        PoissonBaseline poisson;
        poisson.fit(records);

        QVERIFY(poisson.isFitted());
        QVERIFY(poisson.totalEvents() >= 80);

        // Predict using the datetime of the first event (known to be in training data)
        const QDateTime firstDt = m_events[0].occurredAt.value_or(epoch);
        PoissonPrediction pred = poisson.predict(
            QStringLiteral("london"), firstDt, m_events[0].crimeType);
        QVERIFY(pred.lambda >= 0.0);
        QVERIFY(poisson.totalEvents() > 0);
    }

    void testHawkesFit()
    {
        if (!QFile::exists(m_csvPath) || m_events.isEmpty())
            QSKIP("CSV test data not found or empty - skipping");

        const QDateTime epoch(QDate(2024, 1, 1), QTime(0, 0, 0), Qt::UTC);

        QVector<SpatiotemporalEvent> steEvents;
        steEvents.reserve(m_events.size());
        for (const CrimeEvent& ev : m_events) {
            if (!ev.lat.has_value() || !ev.lon.has_value()) continue;
            SpatiotemporalEvent ste;
            ste.tDays     = static_cast<double>(
                epoch.daysTo(ev.occurredAt.value_or(epoch)));
            ste.lat       = *ev.lat;
            ste.lon       = *ev.lon;
            ste.crimeType = ev.crimeType;
            steEvents.append(ste);
        }

        HawkesProcess hawkes;
        hawkes.fit(steEvents, 50); // limited iterations for speed

        QVERIFY(hawkes.isFitted());
        QVERIFY(hawkes.params().mu > 0.0);
    }

    void testSeriesDetection()
    {
        if (!QFile::exists(m_csvPath) || m_events.isEmpty())
            QSKIP("CSV test data not found or empty - skipping");

        SeriesDetector detector;
        QVector<CrimeSeries> series = detector.detect(m_events);
        // May find zero or more series — just verify no crash
        QVERIFY(series.size() >= 0);
    }

    void testFullInferencePipeline()
    {
        if (!QFile::exists(m_csvPath) || m_events.isEmpty())
            QSKIP("CSV test data not found or empty - skipping");

        const CrimeEvent& ev = m_events[0];

        // ── MOExtractor ────────────────────────────────────────────────────
        MOExtractor moEx;
        const QString text = ev.narrative.value_or(ev.crimeType);
        MOFeatures mo = moEx.extract(text);
        // Features may be empty for plain crime-type strings; just verify no crash

        // ── CrimeClassifier ────────────────────────────────────────────────
        CrimeClassifier clf;
        auto [classifiedType, conf] = clf.classify(ev.crimeType);
        QVERIFY(!classifiedType.isEmpty());
        QVERIFY(conf >= 0.0);

        // ── AnomalyDetector ────────────────────────────────────────────────
        const QDateTime epoch(QDate(2024, 1, 1), QTime(0, 0, 0), Qt::UTC);
        QMap<QString, int> ctCodes;
        int codeIdx = 0;

        QVector<AnomalyFeatureVector> fvs;
        fvs.reserve(m_events.size());
        for (const CrimeEvent& e : m_events) {
            if (!e.lat.has_value() || !e.lon.has_value()) continue;
            if (!ctCodes.contains(e.crimeType))
                ctCodes[e.crimeType] = codeIdx++;
            AnomalyFeatureVector fv;
            fv.eventId       = e.eventId;
            fv.lat           = *e.lat;
            fv.lon           = *e.lon;
            fv.tDays         = static_cast<double>(
                epoch.daysTo(e.occurredAt.value_or(epoch)));
            fv.hourNorm      = 0.0;
            fv.crimeTypeCode = ctCodes.value(e.crimeType, 0);
            fvs.append(fv);
        }

        AnomalyDetector detector;
        detector.fit(fvs);
        QVERIFY(detector.isFitted());

        AnomalyFeatureVector evFv;
        evFv.eventId       = ev.eventId;
        evFv.lat           = ev.lat.value_or(51.5074);
        evFv.lon           = ev.lon.value_or(-0.1278);
        evFv.tDays         = static_cast<double>(
            epoch.daysTo(ev.occurredAt.value_or(epoch)));
        evFv.hourNorm      = 0.0;
        evFv.crimeTypeCode = ctCodes.value(ev.crimeType, 0);

        QVector<AnomalyFeatureVector> single = { evFv };
        QVector<AnomalySignal> anomalies = detector.detectAnomalies(single);
        QCOMPARE(anomalies.size(), 1);

        // ── HintEngine ────────────────────────────────────────────────────
        HintEngine hints;
        HintEngineInput input;
        input.event       = ev;
        input.dataQuality = 0.75;
        QVector<InvestigativeLead> leads = hints.generate(input);
        // May return empty leads — just verify no crash
        QVERIFY(leads.size() >= 0);
    }

    void testProvenanceChain()
    {
        if (!QFile::exists(m_csvPath) || m_events.isEmpty())
            QSKIP("CSV test data not found or empty - skipping");

        const QString& eventId = m_events[0].eventId;

        ProvenanceLog log;
        log.record(eventId, QStringLiteral("ingest"),
                   QStringLiteral("CSV_LOAD"),
                   QStringLiteral("Loaded from London crimes 2024 CSV"));
        log.record(eventId, QStringLiteral("nlp"),
                   QStringLiteral("CLASSIFY"),
                   QStringLiteral("Crime type classified by CrimeClassifier"));
        log.record(eventId, QStringLiteral("model"),
                   QStringLiteral("POISSON_FIT"),
                   QStringLiteral("Poisson baseline model fitted"));
        log.record(eventId, QStringLiteral("inference"),
                   QStringLiteral("ANOMALY"),
                   QStringLiteral("Anomaly detection completed"));
        log.record(eventId, QStringLiteral("output"),
                   QStringLiteral("LEAD_GEN"),
                   QStringLiteral("Investigative leads generated by HintEngine"));

        const QVector<ProvenanceEntry> chain = log.chain(eventId);
        QVERIFY(chain.size() >= 5);

        for (const ProvenanceEntry& entry : chain) {
            QCOMPARE(entry.eventId, eventId);
            QVERIFY(entry.timestamp.isValid());
            QVERIFY(!entry.stage.isEmpty());
            QVERIFY(!entry.action.isEmpty());
        }

        QStringList stages;
        for (const ProvenanceEntry& e : chain) stages << e.stage;
        QVERIFY(stages.contains(QStringLiteral("ingest")));
        QVERIFY(stages.contains(QStringLiteral("nlp")));
        QVERIFY(stages.contains(QStringLiteral("inference")));
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// main
// ─────────────────────────────────────────────────────────────────────────────

static int runTest(QObject* obj, const char* logFile)
{
    QStringList args = { QStringLiteral("test"),
                         QStringLiteral("-o"),
                         QString(QStringLiteral("%1,txt")).arg(logFile) };
    return QTest::qExec(obj, args);
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    int r = 0;
    TestDataQualityScorer t1; r |= runTest(&t1, "qual_scorer.txt");
    TestRealDataPipeline  t2; r |= runTest(&t2, "real_data.txt");
    return r;
}

#include "test_quality_and_data.moc"
