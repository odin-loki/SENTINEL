// test_full_pipeline.cpp — End-to-end pipeline integration tests
// Covers: DB import → model → inference → leads → export → provenance
#include <QTest>
#include <QCoreApplication>
#include <QTemporaryFile>
#include <QTextStream>
#include <cmath>
#include <algorithm>

#include "core/Database.h"
#include "core/CrimeEvent.h"
#include "core/DataExporter.h"
#include "core/AppConfig.h"
#include "models/PoissonBaseline.h"
#include "models/HawkesProcess.h"
#include "models/EnsemblePredictor.h"
#include "models/RiskForecaster.h"
#include "models/BayesianHierarchical.h"
#include "models/SeriesDetector.h"
#include "models/KDEHotspot.h"
#include "inference/GeographicProfiler.h"
#include "inference/HintEngine.h"
#include "inference/MOAnalyser.h"
#include "inference/LeadReportGenerator.h"
#include "inference/AnomalyDetector.h"
#include "inference/EvidenceScorer.h"
#include "benchmark/BenchmarkMetrics.h"
#include "benchmark/CalibrationAnalyser.h"
#include "benchmark/BiasAuditor.h"
#include "audit/ProvenanceLog.h"
#include "nlp/MOExtractor.h"
#include "nlp/CrimeClassifier.h"

namespace {

// Build a minimal in-memory database
std::shared_ptr<Database> makeInMemDb() {
    AppConfig cfg;
    cfg.databasePath = ":memory:";
    auto db = std::make_shared<Database>(cfg);
    db->open();
    return db;
}

// Create a CrimeEvent for a specific zone
CrimeEvent makeEvent(const QString& id, const QString& zone,
                      double lat, double lon,
                      int daysAgo = 0,
                      const QString& type = "burglary")
{
    CrimeEvent ev;
    ev.eventId = ev.id = id;
    ev.suburb    = zone;
    ev.crimeType = type;
    ev.lat = lat; ev.longitude = lon;
    ev.lon = lon; ev.latitude  = lat;
    ev.qualityScore = 0.8;
    const QDateTime ts = QDateTime::currentDateTimeUtc().addDays(-daysAgo);
    ev.occurredAt = ts;
    ev.ingestedAt = ts;
    ev.timestamp  = ts;
    return ev;
}

// Generate events for a zone: n events uniformly over `days` days
QVector<CrimeEvent> makeZoneData(const QString& prefix, const QString& zone,
                                  int n, double lat, double lon,
                                  int days = 60,
                                  const QString& type = "burglary")
{
    QVector<CrimeEvent> evs;
    for (int i = 0; i < n; ++i) {
        const int daysAgo = days * i / std::max(n - 1, 1);
        const double jitter = (i % 3) * 0.001;
        evs.append(makeEvent(
            QString("%1_%2").arg(prefix).arg(i),
            zone,
            lat + jitter, lon + jitter,
            daysAgo, type));
    }
    return evs;
}

} // namespace

class TestFullPipeline : public QObject {
    Q_OBJECT
private slots:

    void testDatabaseInsertAndQuery() {
        auto db = makeInMemDb();
        QVector<CrimeEvent> events;
        events += makeZoneData("A", "NorthLondon", 30, 51.6, -0.1);
        events += makeZoneData("B", "SouthLondon", 15, 51.4, -0.1);

        for (const auto& ev : events) db->insertEvent(ev);
        QCOMPARE(db->eventCount(), 45);

        const auto all = db->getAllEvents();
        QCOMPARE(all.size(), 45);
    }

    void testEnsemblePredictionPipeline() {
        // High-crime zone (30 events) vs low-crime zone (5 events) over 60 days
        QVector<CrimeEvent> events;
        events += makeZoneData("H", "HotZone",  30, 51.5, -0.1);
        events += makeZoneData("C", "ColdZone",  5, 51.6, -0.2);

        // Build PoissonBaseline EventRecords
        QVector<PoissonBaseline::EventRecord> poissonRecs;
        for (const auto& ev : events) {
            PoissonBaseline::EventRecord rec;
            rec.zoneId     = ev.suburb;
            rec.occurredAt = ev.timestamp;
            rec.crimeType  = ev.crimeType;
            poissonRecs.append(rec);
        }
        PoissonBaseline poisson;
        poisson.fit(poissonRecs);
        HawkesProcess hawkes;
        QVector<SpatiotemporalEvent> stEvs;
        for (const auto& ev : events) {
            SpatiotemporalEvent se;
            se.tDays     = ev.timestamp.daysTo(QDateTime::currentDateTimeUtc());
            se.lat       = ev.lat.value_or(0.0);
            se.lon       = ev.lon.value_or(0.0);
            se.crimeType = ev.crimeType;
            stEvs.append(se);
        }
        hawkes.fit(stEvs);

        EnsemblePredictor ens;
        ens.setPoisson(&poisson);
        ens.setHawkes(&hawkes);
        QVERIFY(ens.isReady());

        const QDateTime now = QDateTime::currentDateTimeUtc();
        const auto hotPred  = ens.predict("HotZone",  now, "burglary", 51.5, -0.1);
        const auto coldPred = ens.predict("ColdZone", now, "burglary", 51.6, -0.2);

        qDebug() << "Hot zone probCrime:" << hotPred.probCrime
                 << "Cold zone probCrime:" << coldPred.probCrime;

        QVERIFY(hotPred.probCrime  >= 0.0 && hotPred.probCrime  <= 1.0);
        QVERIFY(coldPred.probCrime >= 0.0 && coldPred.probCrime <= 1.0);
    }

    void testRiskForecasterOutput() {
        QVector<CrimeEvent> events;
        events += makeZoneData("H", "HotZone",  20, 51.5, -0.1);
        events += makeZoneData("C", "ColdZone",  3, 51.6, -0.2);

        RiskForecaster forecaster(7);
        forecaster.fit(events);
        QVERIFY(forecaster.isFitted());

        const auto forecasts = forecaster.forecast(QDateTime::currentDateTimeUtc());
        QVERIFY(!forecasts.isEmpty());

        for (const auto& zf : forecasts) {
            QVERIFY(!zf.zoneId.isEmpty());
            QVERIFY(zf.weeklyRisk >= 0.0 && zf.weeklyRisk <= 1.0);
            QCOMPARE(static_cast<int>(zf.days.size()), 7);
            for (const auto& d : zf.days) {
                QVERIFY(d.riskScore >= 0.0 && d.riskScore <= 1.0);
                QVERIFY(d.date.isValid());
            }
        }
    }

    void testBayesianHierarchicalPipeline() {
        QVector<CrimeEvent> events;
        events += makeZoneData("H", "HotZone",   40, 51.5, -0.1);
        events += makeZoneData("M", "MedZone",   15, 51.55, -0.15);
        events += makeZoneData("C", "ColdZone",   3, 51.6, -0.2);

        BayesianHierarchical bh;
        bh.fit(events, 60.0);
        QVERIFY(bh.isFitted());
        QCOMPARE(bh.zoneCount(), 3);

        const auto posts = bh.allPosteriors();
        QCOMPARE(posts.size(), 3);
        // First entry should have highest posterior mean
        QVERIFY(posts[0].posteriorMean >= posts[1].posteriorMean);

        // Hot zone should have highest expected count
        const double hotMean = bh.predictMean("HotZone", 7.0);
        const double coldMean = bh.predictMean("ColdZone", 7.0);
        QVERIFY(hotMean > coldMean);
    }

    void testSeriesDetectionPipeline() {
        // Create 10 events forming 2 spatial-temporal clusters
        QVector<SeriesEvent> stEvents;
        for (int i = 0; i < 5; ++i) {
            SeriesEvent e;
            e.eventId  = QString("s1_%1").arg(i);
            e.lat      = 51.5 + i * 0.001;
            e.lon      = -0.1 + i * 0.001;
            e.tDays    = static_cast<double>(i);
            e.crimeType = "burglary";
            stEvents.append(e);
        }
        for (int i = 0; i < 5; ++i) {
            SeriesEvent e;
            e.eventId  = QString("s2_%1").arg(i);
            e.lat      = 51.6 + i * 0.001;  // different location
            e.lon      = -0.2 + i * 0.001;
            e.tDays    = static_cast<double>(i + 30);  // different time
            e.crimeType = "burglary";
            stEvents.append(e);
        }

        SeriesDetector det(0.5, 7.0, 2);  // 500m, 7 days, min 2 events
        const auto series = det.detectSeries(stEvents);
        qDebug() << "Detected series count:" << series.size();
        // Should detect at least one series
        QVERIFY(!series.isEmpty());
        for (const auto& s : series) {
            QVERIFY(!s.seriesId.isEmpty());
            QVERIFY(s.members.size() >= 2);
        }
    }

    void testKDEHotspotPipeline() {
        QVector<CrimeEvent> events;
        // 30 events clustered in NW area
        for (int i = 0; i < 30; ++i) {
            events.append(makeEvent(QString("k%1").arg(i), "NW",
                51.5 + (i % 5) * 0.002,
                -0.1 + (i / 5) * 0.002, i % 10));
        }
        // 5 background events scattered
        for (int i = 0; i < 5; ++i) {
            events.append(makeEvent(QString("bg%1").arg(i), "BG",
                51.55 + i * 0.01, -0.2 + i * 0.01, i));
        }

        KDEHotspot kde;
        // Compute bounding box
        double latMin=90, latMax=-90, lonMin=180, lonMax=-180;
        for (const auto& ev : events) {
            if (ev.lat.has_value()) {
                latMin = std::min(latMin, *ev.lat);
                latMax = std::max(latMax, *ev.lat);
                lonMin = std::min(lonMin, *ev.lon);
                lonMax = std::max(lonMax, *ev.lon);
            }
        }
        latMin -= 0.05; latMax += 0.05;
        lonMin -= 0.05; lonMax += 0.05;

        QVector<QPair<double,double>> kdeLocations;
        for (const auto& ev : events)
            kdeLocations.append({ev.lat.value_or(0.0), ev.lon.value_or(0.0)});
        const auto hotspots = kde.findHotspots(kdeLocations, latMin, latMax, lonMin, lonMax, 3);
        qDebug() << "KDE hotspots found:" << hotspots.size();
        QVERIFY(!hotspots.isEmpty());
        // Top hotspot should be in the NW cluster area
        QVERIFY(hotspots[0].peakDensity > 0.0);
    }

    void testHintEnginePipeline() {
        // Create an event with geographic profile and series matches
        CrimeEvent ev = makeEvent("central_ev", "West", 51.5, -0.1, 5, "robbery");
        ev.narrative = "Suspect broke rear window and stole electronics";

        HintEngineInput input;
        input.event       = ev;
        input.dataQuality = 0.85;

        // Add a series match
        SeriesMatch sm;
        sm.seriesId        = "series_001";
        sm.linkProbability = 0.82;   // must be >= 0.2 for HintEngine to create a lead
        sm.moSimilarity    = 0.82;
        sm.compositeScore  = 0.85;
        sm.memberCount     = 3;
        input.seriesMatches.append(sm);

        // Add a MO match
        MOMatch mo;
        mo.caseId          = "case_001";
        mo.similarityScore = 0.75;
        mo.sharedFeatures  = {"forced_entry", "rear_window"};
        input.moMatches.append(mo);

        HintEngine engine;
        const auto leads = engine.generate(input);
        qDebug() << "Leads generated:" << leads.size();
        QVERIFY(!leads.isEmpty());
        for (const auto& lead : leads) {
            QVERIFY(lead.confidence >= 0.0 && lead.confidence <= 1.0);
            QVERIFY(!lead.headline.isEmpty());
        }
    }

    void testLeadReportGeneration() {
        QVector<InvestigativeLead> leads;
        for (int i = 0; i < 5; ++i) {
            InvestigativeLead lead;
            lead.rank         = i + 1;
            lead.category     = (i % 2 == 0) ? "Geographic" : "Series";
            lead.headline     = QString("Lead %1: suspect likely from %2")
                                    .arg(i+1).arg(i % 2 == 0 ? "NW London" : "City");
            lead.detail       = "Based on Rossmo geographic profiling and series analysis.";
            lead.confidence   = 0.9 - i * 0.1;
            lead.confidenceMethod = "Bayesian";
            lead.generatedAt  = QDateTime::currentDateTimeUtc();
            leads.append(lead);
        }

        const LeadReport report = LeadReportGenerator::generate("CASE-2024-001", leads);
        QCOMPARE(report.totalLeads, 5);
        QCOMPARE(report.highConfidenceLeads, 3);  // confidence >= 0.7: 0.9,0.8,0.7 → 3
        QVERIFY(!report.markdownText.isEmpty());
        QVERIFY(report.markdownText.contains("CASE-2024-001"));

        // HTML export
        const QString html = LeadReportGenerator::generateHtml(report);
        QVERIFY(html.startsWith("<!DOCTYPE html>"));
        QVERIFY(html.contains("CASE-2024-001"));
        QVERIFY(html.contains("Lead 1"));
        for (const auto& lead : leads) {
            QVERIFY(html.contains(lead.headline));
        }
    }

    void testDataExporterRoundtrip() {
        QVector<CrimeEvent> events;
        events += makeZoneData("E", "Zone1", 10, 51.5, -0.1);

        // JSON export
        const QJsonArray jsonArr = DataExporter::eventsToJson(events);
        QCOMPARE(jsonArr.size(), 10);

        // Verify first event's ID matches (DataExporter uses "eventId" key)
        const QString firstId = jsonArr[0].toObject()["eventId"].toString();
        QCOMPARE(firstId, events[0].eventId);

        // CSV export
        const QString csv = DataExporter::eventsToCsv(events);
        QVERIFY(!csv.isEmpty());
        const QStringList lines = csv.split('\n', Qt::SkipEmptyParts);
        QVERIFY(lines.size() >= 2);  // header + at least 1 data row
        // Header should contain known fields
        QVERIFY(lines[0].contains("event_id"));
    }

    void testBenchmarkMetricsPipeline() {
        // Generate 200 samples: 100 hot (1.0) and 100 cold (0.0)
        // Predictions: hot zones get 0.8, cold get 0.2
        QVector<double> yTrue, yPred;
        for (int i = 0; i < 100; ++i) {
            yTrue.append(1.0);
            yPred.append(0.8 + (i % 5) * 0.02);  // 0.8..0.88
        }
        for (int i = 0; i < 100; ++i) {
            yTrue.append(0.0);
            yPred.append(0.1 + (i % 5) * 0.02);  // 0.1..0.18
        }

        const BenchmarkReport rep = BenchmarkMetrics::fullReport(yTrue, yPred);
        QCOMPARE(rep.nSamples, 200);
        QVERIFY(std::isfinite(rep.aucRoc));
        QVERIFY(rep.aucRoc > 0.5);  // Should be much better than random
        QVERIFY(std::isfinite(rep.brierScore));
        QVERIFY(rep.brierScore >= 0.0 && rep.brierScore <= 1.0);

        qDebug() << "AUC-ROC:" << rep.aucRoc
                 << "Brier:" << rep.brierScore
                 << "PAI@5%:" << rep.pai5pct;
    }

    void testCalibrationAnalyserPipeline() {
        // Well-calibrated predictions: p=0.3 → ~30% actual
        QVector<QPair<double,double>> predActual;
        for (int i = 0; i < 100; ++i) {
            predActual.append({0.3, (i < 30) ? 1.0 : 0.0});
        }
        for (int i = 0; i < 100; ++i) {
            predActual.append({0.7, (i < 70) ? 1.0 : 0.0});
        }

        CalibrationAnalyser ca;
        const auto result = ca.analyse(predActual);
        QVERIFY(!result.bins.isEmpty());
        QVERIFY(result.ece >= 0.0);
        qDebug() << "ECE (well-calibrated):" << result.ece;
    }

    void testProvenanceLogPipeline() {
        ProvenanceLog log;
        const QString eventId = "ev_prov_001";

        log.record(eventId, "ingest",    "csv_import", "Imported from test CSV");
        log.record(eventId, "nlp",       "mo_extract", "Extracted 5 MO features");
        log.record(eventId, "model",     "ensemble",   "EnsemblePrediction: p=0.73");
        log.record(eventId, "inference", "hint_gen",   "Generated 3 leads");
        log.record(eventId, "output",    "export_json","Exported to leads.json");

        const auto chain = log.chain(eventId);
        QCOMPARE(chain.size(), 5);
        QCOMPARE(chain[0].stage, QStringLiteral("ingest"));
        QCOMPARE(chain[4].stage, QStringLiteral("output"));

        const QString formatted = log.formatChain(eventId);
        // formatChain output: "[timestamp] [eventId] [stage] action: detail (hash:-)"
        QVERIFY(formatted.contains(eventId));
        QVERIFY(formatted.contains("ingest"));
        QVERIFY(formatted.contains("output"));

        // Recent entries
        const auto recent = log.recent(3);
        QCOMPARE(recent.size(), 3);
    }

    void testBiasAuditorPipeline() {
        // Create two groups with different crime event rates to test bias detection
        QVector<CrimeEvent> groupAEvents, groupBEvents;
        for (int i = 0; i < 20; ++i) {
            CrimeEvent ev = makeEvent(QString("a%1").arg(i), "GroupA", 51.5, -0.1, i, "burglary");
            ev.meta["demographic"] = QJsonValue("GroupA");
            groupAEvents.append(ev);
        }
        for (int i = 0; i < 20; ++i) {
            CrimeEvent ev = makeEvent(QString("b%1").arg(i), "GroupB", 51.6, -0.2, i, "burglary");
            ev.meta["demographic"] = QJsonValue("GroupB");
            groupBEvents.append(ev);
        }

        BiasAuditor auditor;
        // Run bias audit on predictions for each group
        // Both groups need >=1 positive prediction to avoid division by zero
        QVector<double> predsA(20, 0.8);   // High positive rate for group A (all ≥0.5)
        QVector<double> predsB(20, 0.4);   // Lower positive rate for group B
        predsB[0] = 0.6; predsB[1] = 0.6; // Give group B a few positives (~10%)
        QVector<double> actualA(20, 1.0);
        QVector<double> actualB(20, 0.0);

        // Build group labels vectors
        QVector<QString> groups(40);
        QVector<double> preds(40), actuals(40);
        for (int i = 0; i < 20; ++i) {
            groups[i] = "GroupA";
            preds[i] = predsA[i]; actuals[i] = actualA[i];
        }
        for (int i = 0; i < 20; ++i) {
            groups[20+i] = "GroupB";
            preds[20+i] = predsB[i]; actuals[20+i] = actualB[i];
        }
        const auto reports = BiasAuditor::disparateImpact(groups, preds);
        QVERIFY(!reports.isEmpty());
        const auto& r = reports[0];
        QVERIFY(!r.groupA.isEmpty());
        QVERIFY(!r.groupB.isEmpty());
        qDebug() << "Bias ratio:" << r.ratio;
        QVERIFY(std::isfinite(r.ratio));
    }

    void testNLPPipelineIntegration() {
        const QString narrative =
            "Suspect broke rear window of vehicle and stole GPS unit. "
            "Victim returned to find car unlocked and radio missing.";

        // Extract MO features
        MOExtractor extractor;
        const auto features = extractor.extract(narrative);
        // MOFeatures is a struct; check at least one optional field is set
        QVERIFY(features.entryMethod.has_value() || features.targetType.has_value()
                || !features.itemsTaken.empty());
        qDebug() << "Extracted MO entry:" << features.entryMethod.value_or("none");

        // Classify crime — returns QPair<crimeType, confidence>
        CrimeClassifier classifier;
        const auto classification = classifier.classify(narrative);
        QVERIFY(!classification.first.isEmpty());
        QVERIFY(classification.second >= 0.0 && classification.second <= 1.0);

        // Sentiment from the classifier
        const double sentScore = classifier.sentiment(narrative);
        QVERIFY(sentScore >= -1.0 && sentScore <= 1.0);

        qDebug() << "Crime type:" << classification.first
                 << "Confidence:" << classification.second
                 << "Sentiment:" << sentScore;
    }

    void testFullEndToEndPipeline() {
        // ── Full pipeline: DB → Models → Inference → Leads → Export ──────────
        auto db = makeInMemDb();
        ProvenanceLog provLog;

        // 1. Ingest
        QVector<CrimeEvent> allEvents;
        allEvents += makeZoneData("N", "NorthZone",  25, 51.6, -0.1, 60);
        allEvents += makeZoneData("S", "SouthZone",  10, 51.4, -0.1, 60);
        allEvents += makeZoneData("E", "EastZone",    5, 51.5,  0.0, 60);

        for (const auto& ev : allEvents) {
            db->insertEvent(ev);
            provLog.record(ev.eventId, "ingest", "db_insert",
                           "Stored to in-memory database");
        }
        QCOMPARE(db->eventCount(), 40);

        // 2. Ensemble prediction
        QVector<PoissonBaseline::EventRecord> poissonRecs;
        for (const auto& ev : allEvents) {
            PoissonBaseline::EventRecord rec;
            rec.zoneId     = ev.suburb;
            rec.occurredAt = ev.timestamp;
            rec.crimeType  = ev.crimeType;
            poissonRecs.append(rec);
        }
        PoissonBaseline poisson;
        poisson.fit(poissonRecs);
        QVector<SpatiotemporalEvent> stEvs;
        for (const auto& ev : allEvents) {
            SpatiotemporalEvent se;
            se.tDays     = ev.timestamp.daysTo(QDateTime::currentDateTimeUtc());
            se.lat       = ev.lat.value_or(0.0);
            se.lon       = ev.lon.value_or(0.0);
            se.crimeType = ev.crimeType;
            stEvs.append(se);
        }
        HawkesProcess hawkes;
        hawkes.fit(stEvs);

        EnsemblePredictor ens;
        ens.setPoisson(&poisson);
        ens.setHawkes(&hawkes);
        QVERIFY(ens.isReady());

        const QDateTime now = QDateTime::currentDateTimeUtc();
        const auto northPred = ens.predict("NorthZone", now, "burglary", 51.6, -0.1);
        const auto southPred = ens.predict("SouthZone", now, "burglary", 51.4, -0.1);
        (void)southPred;
        provLog.record("NorthZone", "model", "ensemble",
                       QString("p=%1").arg(northPred.probCrime));

        // 3. Risk forecast
        RiskForecaster forecaster(7);
        forecaster.fit(allEvents);
        const auto forecasts = forecaster.forecast(QDateTime::currentDateTimeUtc());
        QVERIFY(!forecasts.isEmpty());

        // 4. Bayesian hierarchical
        BayesianHierarchical bh;
        bh.fit(allEvents, 60.0);
        QVERIFY(bh.isFitted());

        // 5. Hint engine for highest-risk zone
        HintEngineInput hintInput;
        hintInput.event = allEvents[0];
        hintInput.dataQuality = 0.8;

        SeriesMatch sm;
        sm.seriesId           = "series_auto_1";
        sm.linkProbability    = 0.75;  // must be >= 0.2 for HintEngine to create a lead
        sm.moSimilarity       = 0.75;
        sm.compositeScore     = 0.78;
        sm.memberCount        = 3;
        hintInput.seriesMatches.append(sm);

        HintEngine engine;
        const auto leads = engine.generate(hintInput);
        QVERIFY(!leads.isEmpty());
        for (const auto& lead : leads)
            provLog.record(allEvents[0].eventId, "output", "lead_gen", lead.headline);

        // 6. Lead report
        const auto report = LeadReportGenerator::generate("CASE-E2E-001", leads);
        QVERIFY(report.totalLeads > 0);
        QVERIFY(!report.markdownText.isEmpty());
        const QString html = LeadReportGenerator::generateHtml(report);
        QVERIFY(html.contains("CASE-E2E-001"));

        // 7. Export
        const QJsonArray jsonLeads = DataExporter::leadsToJson(leads);
        QVERIFY(!jsonLeads.isEmpty());
        const QString csvEvents = DataExporter::eventsToCsv(allEvents);
        QVERIFY(!csvEvents.isEmpty());

        // 8. Provenance chain
        const auto chain = provLog.chain(allEvents[0].eventId);
        QVERIFY(!chain.isEmpty());
        QVERIFY(chain.size() >= 2);  // at least ingest + lead_gen

        qDebug() << "Full pipeline: events=" << db->eventCount()
                 << "leads=" << leads.size()
                 << "forecast zones=" << forecasts.size()
                 << "BH zones=" << bh.zoneCount()
                 << "provenance entries=" << chain.size();
    }
};

// ─── main ─────────────────────────────────────────────────────────────────────

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    TestFullPipeline t;
    QStringList args = {"test", "-o", "full_pipeline.txt,txt"};
    return QTest::qExec(&t, args);
}

#include "test_full_pipeline.moc"
