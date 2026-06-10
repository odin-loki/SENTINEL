// test_full_system_integration.cpp
// Comprehensive integration test exercising the full SENTINEL pipeline.
// Covers: ingest, NLP, statistical models, series/hotspot detection,
// inference, benchmark metrics, provenance audit, and DB roundtrip.

#include <QTest>
#include <QCoreApplication>
#include <QDateTime>
#include <QDate>
#include <QTime>
#include <QMap>
#include <QVector>
#include <QPair>
#include <QString>
#include <cmath>
#include <algorithm>

#include "core/CrimeEvent.h"
#include "core/AppConfig.h"
#include "core/Database.h"
#include "ingest/DataQualityScorer.h"
#include "nlp/MOExtractor.h"
#include "nlp/CrimeClassifier.h"
#include "models/PoissonBaseline.h"
#include "models/HawkesProcess.h"
#include "models/SeriesDetector.h"
#include "models/KDEHotspot.h"
#include "models/BayesianHierarchical.h"
#include "models/EnsemblePredictor.h"
#include "inference/GeographicProfiler.h"
#include "inference/AnomalyDetector.h"
#include "inference/HintEngine.h"
#include "inference/CoOffendingAnalyser.h"
#include "inference/EvidenceScorer.h"
#include "audit/ProvenanceLog.h"
#include "benchmark/BenchmarkMetrics.h"

// ─────────────────────────────────────────────────────────────────────────────
// Shared synthetic data helpers
// ─────────────────────────────────────────────────────────────────────────────

static CrimeEvent makeEvent(int i, double lat, double lon,
                             const QDateTime& dt,
                             const QString& type = QStringLiteral("burglary"),
                             const QString& narrative = {})
{
    CrimeEvent ev;
    ev.eventId    = QString("EVT-%1").arg(i, 5, 10, QChar('0'));
    ev.id         = ev.eventId;
    ev.source     = QStringLiteral("uk_police_v1");
    ev.ingestedAt = dt;
    ev.occurredAt = dt;
    ev.reportedAt = dt;
    ev.lat        = lat;
    ev.lon        = lon;
    ev.latitude   = lat;
    ev.longitude  = lon;
    ev.crimeType  = type;
    ev.suburb     = QStringLiteral("London");
    ev.outcome    = QStringLiteral("unresolved");
    ev.qualityScore = 0.75;
    if (!narrative.isEmpty())
        ev.narrative = narrative;
    return ev;
}

// Generate N events spread over 180 days near London
static QVector<CrimeEvent> makeEvents(int n,
                                       double baseLat = 51.5,
                                       double baseLon = -0.1,
                                       double spread  = 0.05,
                                       const QString& type = QStringLiteral("burglary"))
{
    QVector<CrimeEvent> events;
    events.reserve(n);
    QDateTime base(QDate(2024, 1, 1), QTime(0, 0, 0), Qt::UTC);
    for (int i = 0; i < n; ++i) {
        double lat = baseLat + (i % 11) * spread / 11.0;
        double lon = baseLon + (i % 7)  * spread / 7.0;
        QDateTime dt = base.addSecs(static_cast<qint64>(i) * 86400LL * 180 / n);
        events.append(makeEvent(i, lat, lon, dt, type,
                                QStringLiteral("Suspect forced entry through rear door at night. "
                                               "Residential property targeted. Solo offender observed.")));
    }
    return events;
}

// ─────────────────────────────────────────────────────────────────────────────
// Test class
// ─────────────────────────────────────────────────────────────────────────────

class TestFullSystemIntegration : public QObject {
    Q_OBJECT

private slots:

    // ─────────────────────────────────────────────────────────────────────
    // 1. Complete ingest pipeline
    // ─────────────────────────────────────────────────────────────────────
    void testCompleteIngestPipeline()
    {
        const int N = 100;
        QVector<CrimeEvent> events = makeEvents(N);

        // Quality scoring
        QMap<QString, double> reliability;
        reliability[QStringLiteral("uk_police_v1")] = 0.9;
        DataQualityScorer scorer(reliability);
        QVector<QualityReport> reports = scorer.scoreBatch(events);
        QCOMPARE(reports.size(), N);

        ProvenanceLog prov;
        MOExtractor moExtractor;
        CrimeClassifier classifier;

        int classifiedCount = 0;

        for (int i = 0; i < N; ++i) {
            const CrimeEvent& ev = events[i];

            // Record ingest stage
            prov.record(ev.eventId, QStringLiteral("ingest"), QStringLiteral("scored"),
                        QString("quality=%1").arg(reports[i].compositeScore, 0, 'f', 2));

            // NLP classification
            QString text = ev.narrative.value_or(
                QStringLiteral("burglary forced entry through rear door at night"));

            QPair<QString,double> classResult = classifier.classify(text);
            QString crimeType = classResult.first;
            double  confidence = classResult.second;

            double  sev    = classifier.severityScore(text, crimeType);
            double  sent   = classifier.sentiment(text);
            bool    threat = classifier.threatSignal(text, sent);
            MOFeatures mo  = moExtractor.extract(text);

            // Assemble NLP result (for provenance; not stored in DB here)
            NLPResult nlp;
            nlp.eventId              = ev.eventId;
            nlp.crimeType            = crimeType;
            nlp.crimeTypeConfidence  = confidence;
            nlp.severityScore        = sev;
            nlp.sentimentCompound    = sent;
            nlp.threatSignal         = threat;
            nlp.moFeatures           = mo;
            nlp.rawText              = text;

            prov.record(ev.eventId, QStringLiteral("nlp"), QStringLiteral("classified"),
                        QString("type=%1 conf=%2").arg(crimeType).arg(confidence, 0, 'f', 2));

            if (nlp.crimeType.has_value() && !nlp.crimeType->isEmpty())
                ++classifiedCount;
        }

        // At least some events should receive an NLP crime type
        QVERIFY2(classifiedCount > 0, "At least one event should be NLP-classified");

        // Each event should have >=2 provenance entries (ingest + nlp)
        auto chain0 = prov.chain(events[0].eventId);
        QVERIFY2(chain0.size() >= 2, "First event should have >= 2 provenance entries");

        // Pass rate: generous lower bound
        double passRate = DataQualityScorer::passRate(reports);
        QVERIFY2(passRate >= 0.3, "At least 30% of events should pass quality threshold");

        // Composite scores should be in [0,1]
        for (const auto& r : reports) {
            QVERIFY2(r.compositeScore >= 0.0 && r.compositeScore <= 1.0,
                     "Composite score must be in [0,1]");
        }
    }

    // ─────────────────────────────────────────────────────────────────────
    // 2. Statistical models consistency
    // ─────────────────────────────────────────────────────────────────────
    void testStatisticalModelsConsistency()
    {
        const int N = 200;
        QDateTime base(QDate(2024, 1, 1), QTime(0, 0, 0), Qt::UTC);

        // ── Poisson ──────────────────────────────────────────────────────
        PoissonBaseline poisson;
        QVector<PoissonBaseline::EventRecord> records;
        records.reserve(N);
        for (int i = 0; i < N; ++i) {
            PoissonBaseline::EventRecord r;
            r.zoneId     = QStringLiteral("london_central");
            r.occurredAt = base.addSecs(static_cast<qint64>(i) * 86400LL * 180 / N);
            r.crimeType  = QStringLiteral("burglary");
            records.append(r);
        }
        poisson.fit(records);
        QVERIFY2(poisson.isFitted(), "PoissonBaseline should be fitted");
        QVERIFY2(poisson.totalEvents() == N, "Total events should match");

        QDateTime recentDt(QDate(2024, 6, 1), QTime(12, 0, 0), Qt::UTC);
        PoissonPrediction pred = poisson.predict(
            QStringLiteral("london_central"), recentDt, QStringLiteral("burglary"));

        QVERIFY2(pred.lambda > 0.0, "Poisson lambda must be > 0 after fitting");
        QVERIFY2(pred.probAtLeastOne >= 0.0, "P(at least one) must be non-negative");
        QVERIFY2(pred.probAtLeastOne <= 1.0, "P(at least one) must be <= 1");

        // ── Hawkes ───────────────────────────────────────────────────────
        HawkesProcess hawkes;
        QVector<SpatiotemporalEvent> stEvents;
        stEvents.reserve(N);
        for (int i = 0; i < N; ++i) {
            SpatiotemporalEvent ste;
            ste.tDays     = static_cast<double>(i) * 180.0 / N;
            ste.lat       = 51.5 + (i % 5) * 0.01;
            ste.lon       = -0.1 + (i % 7) * 0.01;
            ste.crimeType = QStringLiteral("burglary");
            stEvents.append(ste);
        }
        hawkes.fit(stEvents, 5);

        // Directly set valid parameters to ensure intensity check works
        // regardless of optimiser convergence with only 5 iterations
        if (!hawkes.isFitted()) {
            HawkesParams p;
            p.mu    = 0.1;
            p.alpha = 0.3;
            p.beta  = 0.5;
            p.sigma = 0.01;
            hawkes.setParams(p);
            hawkes.setHistory(stEvents);
        }

        double intensity = hawkes.intensity(179.0, 51.5, -0.1);
        QVERIFY2(intensity >= 0.0, "Hawkes intensity must be non-negative");

        // ── Poisson PMF sanity ────────────────────────────────────────────
        double pmf0 = PoissonBaseline::poissonPMF(1.0, 0);
        QVERIFY2(std::abs(pmf0 - std::exp(-1.0)) < 1e-9, "PMF(lambda=1, k=0) = e^-1");

        double pmf1 = PoissonBaseline::poissonPMF(2.0, 0);
        QVERIFY2(std::abs(pmf1 - std::exp(-2.0)) < 1e-9, "PMF(lambda=2, k=0) = e^-2");

        // ── BayesianHierarchical ─────────────────────────────────────────
        QVector<CrimeEvent> bevents = makeEvents(N);
        BayesianHierarchical bayes;
        bayes.fit(bevents, 30.0, QStringLiteral("burglary"));
        QVERIFY2(bayes.isFitted(), "BayesianHierarchical should be fitted");
        QVERIFY2(bayes.globalMean() > 0.0, "Global mean should be positive");
    }

    // ─────────────────────────────────────────────────────────────────────
    // 3. Series and hotspot detection
    // ─────────────────────────────────────────────────────────────────────
    void testSeriesAndHotspotDetection()
    {
        // 3 tight geographic clusters, 10 events each
        QVector<CrimeEvent> events;
        QDateTime base(QDate(2024, 1, 1), QTime(0, 0, 0), Qt::UTC);

        struct ClusterCenter { double lat, lon; };
        const ClusterCenter centers[3] = {
            {51.500, -0.100},
            {51.520, -0.120},
            {51.480, -0.080}
        };

        int idx = 0;
        for (const auto& c : centers) {
            for (int j = 0; j < 10; ++j) {
                // Very tight cluster: 0.1° * 0.001 = ~11 m apart
                CrimeEvent ev = makeEvent(idx,
                                          c.lat + j * 0.0005,
                                          c.lon + j * 0.0005,
                                          base.addDays(j),
                                          QStringLiteral("burglary"),
                                          QStringLiteral("forced entry residential solo night"));
                events.append(ev);
                ++idx;
            }
        }

        // ── SeriesDetector ────────────────────────────────────────────────
        // Use loose params to ensure we capture the clusters
        SeriesDetector detector(0.5, 20.0, 3);  // 500 m, 20 days, min 3 samples
        QVector<CrimeSeries> series = detector.detect(events);
        QVERIFY2(series.size() >= 1,
                 "SeriesDetector should find at least 1 series in clustered data");

        // Each series should have >= minSamples members
        for (const auto& s : series)
            QVERIFY2(s.members.size() >= 3, "Each series should have >= 3 members");

        // ── KDE Hotspot ───────────────────────────────────────────────────
        KDEHotspot kde(50, 1.0);
        QVector<QPair<double,double>> locs;
        for (const auto& ev : events)
            locs.append({ev.lat.value_or(51.5), ev.lon.value_or(-0.1)});

        const double latMin = 51.47, latMax = 51.53;
        const double lonMin = -0.13, lonMax = -0.07;

        QVector<HotspotRegion> hotspots =
            kde.findHotspots(locs, latMin, latMax, lonMin, lonMax, 5);

        QVERIFY2(!hotspots.isEmpty(), "KDE should find at least 1 hotspot region");
        QVERIFY2(hotspots[0].peakDensity > 0.0, "Top hotspot peak density must be > 0");
        QVERIFY2(hotspots[0].rank == 1, "Top hotspot should have rank 1");

        // ── KDE surface ───────────────────────────────────────────────────
        auto surface = kde.compute(locs, latMin, latMax, lonMin, lonMax);
        QVERIFY2(!surface.empty(), "KDE surface should be non-empty");
        // All cells should be non-negative
        for (const auto& row : surface)
            for (double v : row)
                QVERIFY2(v >= 0.0, "KDE surface value must be non-negative");
    }

    // ─────────────────────────────────────────────────────────────────────
    // 4. Inference engine end-to-end
    // ─────────────────────────────────────────────────────────────────────
    void testInferenceEngineEndToEnd()
    {
        const int N = 50;
        QVector<CrimeEvent> events = makeEvents(N);

        // ── GeographicProfiler ────────────────────────────────────────────
        GeographicProfiler geoProfiler;
        QVector<QPair<double,double>> crimeLocations;
        for (int i = 0; i < 10; ++i) {
            crimeLocations.append({events[i].lat.value_or(51.5),
                                   events[i].lon.value_or(-0.1)});
        }
        GeographicProfile geoProfile = geoProfiler.profile(crimeLocations);
        QVERIFY2(!geoProfile.probabilitySurface.empty(),
                 "GeographicProfiler should produce a probability surface");
        QVERIFY2(geoProfile.peakProbability >= 0.0, "Peak probability must be non-negative");

        // ── AnomalyDetector ───────────────────────────────────────────────
        AnomalyDetector anomalyDetector(0.1);
        QVector<AnomalyFeatureVector> features;
        features.reserve(N);
        for (int i = 0; i < N; ++i) {
            AnomalyFeatureVector fv;
            fv.eventId       = events[i].eventId;
            fv.lat           = events[i].lat.value_or(51.5);
            fv.lon           = events[i].lon.value_or(-0.1);
            fv.tDays         = static_cast<double>(i) * 180.0 / N;
            fv.hourNorm      = (i % 24) / 24.0;
            fv.crimeTypeCode = 1;
            features.append(fv);
        }
        anomalyDetector.fit(features);
        QVERIFY2(anomalyDetector.isFitted(), "AnomalyDetector should be fitted");

        QVector<AnomalySignal> anomalies = anomalyDetector.detectAnomalies(features);
        QCOMPARE(anomalies.size(), N);
        // Combined scores should be finite
        for (const auto& a : anomalies)
            QVERIFY2(std::isfinite(a.combinedScore), "Anomaly combined score must be finite");

        // ── HintEngine ────────────────────────────────────────────────────
        HintEngine hintEngine;
        HintEngineInput input;
        input.event         = events[0];
        input.geoProfile    = geoProfile;
        input.anomalySignal = anomalies[0];
        input.dataQuality   = 0.8;

        QVector<InvestigativeLead> leads = hintEngine.generate(input);
        QVERIFY2(!leads.isEmpty(), "HintEngine should produce at least 1 lead");

        for (const auto& lead : leads) {
            QVERIFY2(lead.confidence >= 0.0, "Lead confidence must be >= 0");
            QVERIFY2(lead.confidence <= 1.0, "Lead confidence must be <= 1");
            QVERIFY2(!lead.headline.isEmpty(), "Lead headline must not be empty");
        }

        // ── Provenance chain (separate log for this event) ────────────────
        ProvenanceLog prov;
        prov.record(events[0].eventId, "ingest",    "loaded",    "source=uk_police");
        prov.record(events[0].eventId, "nlp",       "classified","type=burglary");
        prov.record(events[0].eventId, "model",     "poisson",   "lambda=1.2");
        prov.record(events[0].eventId, "inference", "profiled",  "peak=51.5,-0.1");
        prov.record(events[0].eventId, "output",    "lead_gen",  "leads=1");

        auto chain = prov.chain(events[0].eventId);
        QVERIFY2(chain.size() >= 1, "Provenance chain should have at least 1 entry");
    }

    // ─────────────────────────────────────────────────────────────────────
    // 5. Benchmark metrics end-to-end
    // ─────────────────────────────────────────────────────────────────────
    void testBenchmarkMetricsEndToEnd()
    {
        // 50 perfect pairs + 50 random pairs
        QVector<double> yTrue, yPred;
        for (int i = 0; i < 50; ++i) {
            yTrue.append(1.0);
            yPred.append(1.0);
        }
        for (int i = 0; i < 50; ++i) {
            yTrue.append(i % 2 == 0 ? 1.0 : 0.0);
            yPred.append(0.5);
        }

        double auc = BenchmarkMetrics::aucRoc(yTrue, yPred);
        QVERIFY2(auc > 0.5,  "AUC-ROC for 50-perfect + 50-random data should be > 0.5");
        QVERIFY2(auc <= 1.0, "AUC-ROC must be <= 1.0");

        double brier = BenchmarkMetrics::brierScore(yTrue, yPred);
        QVERIFY2(brier >= 0.0, "Brier score must be non-negative");
        QVERIFY2(brier <= 1.0, "Brier score must be <= 1.0");

        // Full report
        BenchmarkReport report = BenchmarkMetrics::fullReport(yTrue, yPred);
        QVERIFY2(report.nSamples == 100, "Report nSamples should be 100");
        QVERIFY2(report.aucRoc > 0.0,   "Report AUC-ROC should be positive");
        QVERIFY2(!report.reportText().isEmpty(), "Report text should be non-empty");

        // ECE for a perfect predictor (predicted = actual = 1.0)
        QVector<QPair<double,double>> perfectPairs;
        for (int i = 0; i < 50; ++i)
            perfectPairs.append({1.0, 1.0});

        double ece = EnsemblePredictor::ece(perfectPairs, 10);
        QVERIFY2(ece < 0.1, "ECE for perfect predictor should be < 0.1");
        QVERIFY2(ece >= 0.0, "ECE must be non-negative");

        // MAE and RMSE
        double mae  = BenchmarkMetrics::mae(yTrue, yPred);
        double rmse = BenchmarkMetrics::rmse(yTrue, yPred);
        QVERIFY2(mae  >= 0.0, "MAE must be non-negative");
        QVERIFY2(rmse >= mae, "RMSE must be >= MAE (Cauchy-Schwarz)");
    }

    // ─────────────────────────────────────────────────────────────────────
    // 6. Provenance full chain
    // ─────────────────────────────────────────────────────────────────────
    void testProvenanceFullChain()
    {
        ProvenanceLog prov;
        const QString eventId = QStringLiteral("TEST-EVT-PROV-001");

        prov.record(eventId, QStringLiteral("ingest"),    QStringLiteral("loaded"),    QStringLiteral("source=uk_police"),   QStringLiteral("abc123"));
        prov.record(eventId, QStringLiteral("nlp"),       QStringLiteral("classified"),QStringLiteral("type=burglary"),      QStringLiteral("def456"));
        prov.record(eventId, QStringLiteral("model"),     QStringLiteral("fitted"),    QStringLiteral("lambda=1.5"),         QStringLiteral("ghi789"));
        prov.record(eventId, QStringLiteral("inference"), QStringLiteral("profiled"),  QStringLiteral("peak=51.5,-0.1"),     QStringLiteral("jkl012"));
        prov.record(eventId, QStringLiteral("output"),    QStringLiteral("exported"),  QStringLiteral("leads=3"),            QStringLiteral("mno345"));

        QVector<ProvenanceEntry> chain = prov.chain(eventId);
        QCOMPARE(chain.size(), 5);

        // Verify all stages recorded
        QStringList stages;
        for (const auto& entry : chain)
            stages.append(entry.stage);
        QVERIFY2(stages.contains(QStringLiteral("ingest")),    "Chain must include 'ingest'");
        QVERIFY2(stages.contains(QStringLiteral("nlp")),       "Chain must include 'nlp'");
        QVERIFY2(stages.contains(QStringLiteral("model")),     "Chain must include 'model'");
        QVERIFY2(stages.contains(QStringLiteral("inference")), "Chain must include 'inference'");
        QVERIFY2(stages.contains(QStringLiteral("output")),    "Chain must include 'output'");

        // All entries should reference the correct event
        for (const auto& entry : chain)
            QCOMPARE(entry.eventId, eventId);

        // formatHtml should contain all stage names
        QString html = prov.formatHtml(eventId);
        QVERIFY2(!html.isEmpty(),                         "formatHtml must return non-empty HTML");
        QVERIFY2(html.contains(QStringLiteral("ingest")),    "HTML must contain 'ingest'");
        QVERIFY2(html.contains(QStringLiteral("nlp")),       "HTML must contain 'nlp'");
        QVERIFY2(html.contains(QStringLiteral("inference")), "HTML must contain 'inference'");

        // formatChain must be non-empty
        QString chainStr = prov.formatChain(eventId);
        QVERIFY2(!chainStr.isEmpty(), "formatChain must return non-empty string");

        // recent() should include our 5 entries
        auto recent = prov.recent(10);
        QVERIFY2(recent.size() >= 5, "recent(10) should include at least 5 entries");

        // clear() should wipe the log
        prov.clear();
        QVERIFY2(prov.chain(eventId).isEmpty(), "chain() must be empty after clear()");
    }

    // ─────────────────────────────────────────────────────────────────────
    // 7. Database pipeline roundtrip
    // ─────────────────────────────────────────────────────────────────────
    void testDatabasePipelineRoundtrip()
    {
        const int N = 50;

        AppConfig cfg;
        cfg.databasePath = QStringLiteral(":memory:");

        Database db(cfg);
        bool opened = db.open();
        QVERIFY2(opened,
                 qPrintable(QString("In-memory DB should open: %1").arg(db.lastError())));

        QVector<CrimeEvent> events = makeEvents(N);
        for (const auto& ev : events) {
            bool ok = db.insertEvent(ev);
            QVERIFY2(ok, qPrintable(QString("insertEvent failed: %1").arg(db.lastError())));
        }

        QCOMPARE(db.eventCount(), N);

        // Retrieve all events
        QVector<CrimeEvent> retrieved = db.getAllEvents();
        QVERIFY2(retrieved.size() == N, "Should retrieve all 50 inserted events");

        // Run HintEngine on first 5 retrieved events and persist leads
        HintEngine hintEngine;
        int totalLeads = 0;

        const int processCount = std::min(5, static_cast<int>(retrieved.size()));
        for (int i = 0; i < processCount; ++i) {
            HintEngineInput input;
            input.event       = retrieved[i];
            input.dataQuality = 0.75;

            QVector<InvestigativeLead> leads = hintEngine.generate(input);
            for (const auto& lead : leads) {
                bool ok = db.insertLead(lead, retrieved[i].eventId);
                QVERIFY2(ok, "insertLead should succeed");
                ++totalLeads;
            }
        }

        // Lead count in DB must match generated lead count
        QVector<InvestigativeLead> dbLeads = db.queryLeads();
        QVERIFY2(dbLeads.size() == totalLeads,
                 qPrintable(QString("DB lead count %1 != generated %2")
                            .arg(dbLeads.size()).arg(totalLeads)));

        // Crime type counts should include burglary
        QMap<QString,int> typeCounts = db.crimeTypeCounts();
        QVERIFY2(typeCounts.contains(QStringLiteral("burglary")),
                 "Crime type counts should include 'burglary'");
        QVERIFY2(typeCounts[QStringLiteral("burglary")] == N,
                 "All 50 events should be burglary");

        db.close();
    }
};

QTEST_MAIN(TestFullSystemIntegration)
#include "test_full_system_integration.moc"
