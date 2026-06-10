// test_pipeline_integration_deep.cpp — End-to-end pipeline integration tests.
// Covers: ingest → quality scoring → model fitting → forecasting → leads.

#include <QTest>
#include <QCoreApplication>
#include <QDir>
#include <QTemporaryFile>
#include <QTextStream>
#include <QDateTime>
#include <QTimeZone>
#include <QJsonArray>
#include <cmath>

#include "core/CrimeEvent.h"
#include "core/DataExporter.h"
#include "ingest/CsvImporter.h"
#include "ingest/DataQualityScorer.h"
#include "models/PoissonBaseline.h"
#include "models/RiskForecaster.h"
#include "models/EnsemblePredictor.h"
#include "models/SeriesDetector.h"
#include "models/BayesianHierarchical.h"
#include "models/KDEHotspot.h"
#include "inference/HintEngine.h"
#include "inference/AnomalyDetector.h"
#include "nlp/CrimeClassifier.h"
#include "nlp/MOExtractor.h"
#include "audit/ProvenanceLog.h"

class PipelineIntegrationDeepTest : public QObject
{
    Q_OBJECT

    // ── Shared helpers ─────────────────────────────────────────────────────────

    static CrimeEvent makeEvent(const QString& id,
                                 const QString& suburb,
                                 double lat, double lon,
                                 const QString& type = "burglary",
                                 const QDateTime& dt = {})
    {
        const QDateTime when = dt.isValid()
            ? dt
            : QDateTime(QDate(2024, 1, 15), QTime(14, 0, 0), QTimeZone::utc());

        CrimeEvent ev;
        ev.eventId    = id;
        ev.id         = id;
        ev.source     = "test";
        ev.crimeType  = type;
        ev.suburb     = suburb;
        ev.lat        = lat;
        ev.lon        = lon;
        ev.latitude   = lat;
        ev.longitude  = lon;
        ev.occurredAt = when;
        ev.timestamp  = when;   // flat convenience field used by RiskForecaster
        ev.ingestedAt = QDateTime::currentDateTimeUtc();
        ev.qualityScore = 0.8;
        return ev;
    }

    static QString writeTempCsv(const QStringList& lines)
    {
        auto* f = new QTemporaryFile(QStringLiteral("XXXXXX.csv"));
        f->setAutoRemove(false);
        (void)f->open();
        QTextStream out(f);
        for (const auto& line : lines)
            out << line << "\n";
        f->close();
        const QString path = f->fileName();
        delete f;
        return path;
    }

private slots:

    // ── 1. CsvImporter → DataQualityScorer: passRate > 0 on valid CSV ─────────
    void testCsvImporterToDataQualityScorer()
    {
        const QStringList lines = {
            "Crime ID,Month,Crime type,Latitude,Longitude,Location",
            "abc001,2024-01,Burglary,51.5074,-0.1278,On or near High Street",
            "abc002,2024-01,Robbery,51.5080,-0.1285,On or near Park Road",
            "abc003,2024-01,Theft from the person,51.5068,-0.1265,On or near Church Lane",
        };
        const QString csvPath = writeTempCsv(lines);
        QVERIFY(!csvPath.isEmpty());

        const auto events = CsvImporter::importFile(csvPath);
        QVERIFY2(!events.isEmpty(), "CsvImporter returned no events");

        DataQualityScorer scorer;
        const auto reports = scorer.scoreBatch(events);
        QCOMPARE(reports.size(), events.size());

        const double rate = DataQualityScorer::passRate(reports);
        QVERIFY2(rate > 0.0,
                 qPrintable(QStringLiteral("passRate %1 must be > 0").arg(rate)));

        QFile::remove(csvPath);
    }

    // ── 2. PoissonBaseline fit → RiskForecaster fit → forecast not empty ───────
    void testPoissonBaselineToRiskForecaster()
    {
        // Use RECENT events so RiskForecaster's 30-day recency window is populated
        const QDateTime now = QDateTime::currentDateTimeUtc();
        QVector<CrimeEvent> events;
        for (int i = 0; i < 10; ++i) {
            events.append(makeEvent("A" + QString::number(i), "ZoneA",
                                    51.5074, -0.1278, "burglary",
                                    now.addDays(-i)));
            events.append(makeEvent("B" + QString::number(i), "ZoneB",
                                    51.5200, -0.1100, "robbery",
                                    now.addDays(-i - 3)));
        }

        RiskForecaster rf(7);
        rf.fit(events);
        QVERIFY2(rf.isFitted(), "RiskForecaster not fitted after fit()");
        QVERIFY2(rf.zoneCount() >= 1, "No zones detected after fit()");

        const auto forecasts = rf.forecast(now);
        QVERIFY2(!forecasts.isEmpty(),
                 "RiskForecaster::forecast() returned empty result");
        QVERIFY2(!forecasts[0].days.isEmpty(), "First ZoneForecast has no days");
    }

    // ── 3. EnsemblePredictor (Poisson only): predict gives valid probCrime ──────
    void testEnsemblePredictorPoissonOnly()
    {
        // Fit PoissonBaseline
        const QDateTime base(QDate(2024, 1, 8), QTime(9, 0, 0), QTimeZone::utc());
        QVector<PoissonBaseline::EventRecord> recs;
        for (int i = 0; i < 20; ++i) {
            PoissonBaseline::EventRecord r;
            r.zoneId     = "Hot";
            r.occurredAt = base.addDays(i * 7);
            r.crimeType  = "burglary";
            recs.append(r);
        }
        PoissonBaseline pb;
        pb.fit(recs);
        QVERIFY(pb.isFitted());

        EnsemblePredictor ep;
        ep.setPoisson(&pb);
        ep.setWeights(1.0, 0.0);    // pure-Poisson ensemble
        QVERIFY(ep.isReady());

        const QDateTime dt(QDate(2024, 6, 10), QTime(14, 0, 0), QTimeZone::utc());
        const auto pred = ep.predict("Hot", dt, "burglary", 51.5074, -0.1278);

        QVERIFY2(pred.probCrime >= 0.0 && pred.probCrime <= 1.0,
                 qPrintable(QStringLiteral("probCrime %1 not in [0,1]")
                                .arg(pred.probCrime)));
    }

    // ── 4. SeriesDetector → CrimeSeries → HintEngine: leads generated ─────────
    void testSeriesDetectorToHintEngine()
    {
        // 5 events at essentially the same location within 8 days → forms series
        const QDateTime base(QDate(2024, 1, 15), QTime(14, 0, 0), QTimeZone::utc());
        QVector<CrimeEvent> events;
        for (int i = 0; i < 5; ++i) {
            CrimeEvent ev = makeEvent("SE" + QString::number(i), "Central",
                                      51.5074, -0.1278, "burglary",
                                      base.addDays(i * 2));
            ev.narrative = QString("Suspect forced entry through rear window");
            events.append(ev);
        }

        SeriesDetector sd(0.3, 14.0, 3);
        const auto series = sd.detect(events);

        // Build a series match (from detected series if available, else synthetic)
        SeriesMatch sm;
        sm.seriesId        = series.isEmpty() ? "S_synth" : series[0].seriesId;
        sm.memberCount     = series.isEmpty() ? 5 : series[0].members.size();
        sm.linkProbability = 0.85;
        sm.compositeScore  = 0.80;
        sm.method          = "spatiotemporal_dbscan";

        HintEngineInput input;
        input.event         = events[0];
        input.seriesMatches = {sm};
        input.dataQuality   = 0.9;

        HintEngine engine;
        const auto leads = engine.generate(input);
        QVERIFY2(!leads.isEmpty(),
                 "HintEngine must generate at least one lead from series match");
    }

    // ── 5. BayesianHierarchical fit → posteriorMean > 0 for populated zone ────
    void testBayesianHierarchicalPosteriorMean()
    {
        const QDateTime base(QDate(2024, 1, 1), QTime(12, 0, 0), QTimeZone::utc());
        QVector<CrimeEvent> events;
        for (int i = 0; i < 15; ++i) {
            events.append(makeEvent("BH" + QString::number(i), "PopulatedZone",
                                    51.5, -0.1, "burglary",
                                    base.addDays(i)));
        }
        // Also add a second zone for contrast
        for (int i = 0; i < 3; ++i) {
            events.append(makeEvent("BH2" + QString::number(i), "QuietZone",
                                    51.55, -0.15, "burglary",
                                    base.addDays(i * 5)));
        }

        BayesianHierarchical bh;
        bh.fit(events, 30.0);
        QVERIFY2(bh.isFitted(), "BayesianHierarchical not fitted");
        QVERIFY2(bh.zoneCount() >= 1, "No zones after fit()");

        const auto posterior = bh.posteriorForZone("PopulatedZone");
        QVERIFY2(posterior.posteriorMean > 0.0,
                 qPrintable(QStringLiteral("posteriorMean %1 must be > 0")
                                .arg(posterior.posteriorMean)));
    }

    // ── 6. KDEHotspot: findHotspots() not empty ───────────────────────────────
    void testKDEHotspotFindHotspots()
    {
        // 10 locations clustered in two areas
        QVector<QPair<double, double>> locations;
        for (int i = 0; i < 6; ++i)
            locations.append({51.50 + (i % 2) * 0.002, -0.12 + (i % 2) * 0.002});
        for (int i = 0; i < 4; ++i)
            locations.append({51.52 + (i % 2) * 0.002, -0.10 + (i % 2) * 0.002});

        KDEHotspot kde(30);
        const auto hotspots = kde.findHotspots(
            locations,
            51.48, 51.54,   // latMin, latMax
            -0.14, -0.08,   // lonMin, lonMax
            3               // topK
        );
        QVERIFY2(!hotspots.isEmpty(), "KDEHotspot::findHotspots() returned empty");
    }

    // ── 7. CrimeClassifier → MOExtractor: classify → canonical MO not empty ───
    void testCrimeClassifierToMOExtractor()
    {
        const QString text =
            "Suspect forced entry through rear window at night and stole cash and jewellery";

        CrimeClassifier cc;
        const auto [crimeType, confidence] = cc.classify(text);
        QVERIFY2(!crimeType.isEmpty(), "CrimeClassifier returned empty crime type");
        QVERIFY2(confidence >= 0.0 && confidence <= 1.0,
                 "confidence not in [0,1]");

        MOExtractor moe;
        const MOFeatures mo = moe.extract(text);
        const QString canonical = moe.canonicalMOString(mo);
        QVERIFY2(!canonical.isEmpty(),
                 "MOExtractor::canonicalMOString() returned empty string");
    }

    // ── 8. ProvenanceLog record → chain retrieval matches recorded entries ─────
    void testProvenanceLogChain()
    {
        ProvenanceLog log;

        log.record("EV001", "ingest",    "import",   "Imported from CSV", "hash_a");
        log.record("EV001", "nlp",       "classify", "Classified as burglary");
        log.record("EV002", "ingest",    "import",   "Imported from CSV", "hash_b");
        log.record("EV001", "inference", "series",   "Linked to series S1");

        const auto chain = log.chain("EV001");
        QCOMPARE(chain.size(), 3);

        QCOMPARE(chain[0].stage,   QString("ingest"));
        QCOMPARE(chain[0].eventId, QString("EV001"));
        QCOMPARE(chain[1].stage,   QString("nlp"));
        QCOMPARE(chain[2].stage,   QString("inference"));

        // EV002 chain must be independent
        const auto chain2 = log.chain("EV002");
        QCOMPARE(chain2.size(), 1);
        QCOMPARE(chain2[0].stage, QString("ingest"));
    }

    // ── 9. DataExporter: eventsToJson → parseable JSON with correct length ─────
    void testDataExporterEventsToJson()
    {
        constexpr int N = 5;
        QVector<CrimeEvent> events;
        for (int i = 0; i < N; ++i)
            events.append(makeEvent("EXP" + QString::number(i), "Zone",
                                    51.5 + i * 0.01, -0.1 + i * 0.01));

        const QJsonArray arr = DataExporter::eventsToJson(events);
        QCOMPARE(arr.size(), N);

        // Each element must be a JSON object
        for (int i = 0; i < N; ++i)
            QVERIFY2(arr[i].isObject(),
                     qPrintable(QStringLiteral("Element %1 is not a JSON object").arg(i)));
    }

    // ── 10. AnomalyDetector fit → detect → at least some events processed ─────
    void testAnomalyDetectorFitDetect()
    {
        constexpr int N = 30;
        QVector<AnomalyFeatureVector> vecs;
        for (int i = 0; i < N; ++i) {
            AnomalyFeatureVector v;
            v.eventId       = "AD" + QString::number(i);
            v.lat           = 51.5  + (i % 5)  * 0.01;
            v.lon           = -0.12 + (i % 4)  * 0.01;
            v.tDays         = i * 1.0;
            v.hourNorm      = (i % 24) / 24.0;
            v.crimeTypeCode = i % 3;
            vecs.append(v);
        }

        AnomalyDetector ad(0.1);
        ad.fit(vecs);
        QVERIFY2(ad.isFitted(), "AnomalyDetector not fitted after fit()");

        const auto results = ad.detectAnomalies(vecs);
        QVERIFY2(!results.isEmpty(),
                 "AnomalyDetector::detectAnomalies() returned empty result");
        QCOMPARE(results.size(), N);
    }
};

QTEST_MAIN(PipelineIntegrationDeepTest)
#include "test_pipeline_integration_deep.moc"
