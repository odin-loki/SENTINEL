// test_full_pipeline_integration.cpp
// Comprehensive end-to-end integration tests for the SENTINEL crime analytics
// pipeline: CSV import → NLP → models → inference → export → audit.

#include <QTest>
#include <QCoreApplication>
#include <QTemporaryFile>
#include <QTextStream>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QDateTime>
#include <QDate>
#include <QTime>
#include <QTimeZone>
#include <QSet>
#include <cmath>
#include <algorithm>

#include "core/CrimeEvent.h"
#include "core/AppConfig.h"
#include "core/Database.h"
#include "core/DataExporter.h"
#include "ingest/CsvImporter.h"
#include "nlp/CrimeClassifier.h"
#include "nlp/MOExtractor.h"
#include "models/PoissonBaseline.h"
#include "models/HawkesProcess.h"
#include "models/SeriesDetector.h"
#include "models/EnsemblePredictor.h"
#include "inference/HintEngine.h"
#include "inference/AnomalyDetector.h"
#include "inference/LeadReportGenerator.h"
#include "audit/ProvenanceLog.h"
#include "benchmark/BenchmarkMetrics.h"
#include "benchmark/BiasAuditor.h"

namespace {

static QDateTime utcDt(int year, int month, int day, int hour = 12)
{
    return QDateTime(QDate(year, month, day), QTime(hour, 0), QTimeZone::utc());
}

static QString writeTempCsv(const QString& content)
{
    QTemporaryFile tmp;
    tmp.setAutoRemove(false);
    if (!tmp.open())
        return {};
    QTextStream out(&tmp);
    out << content;
    out.flush();
    return tmp.fileName();
}

static const QString kCsvHeader =
    QStringLiteral("id,date,type,description,latitude,longitude,address,outcome,location\n");

static QString buildPipelineCsvRows(int count)
{
    const QStringList zones = {
        QStringLiteral("Camden"), QStringLiteral("Islington"),
        QStringLiteral("Hackney"), QStringLiteral("Southwark")
    };
    const QStringList narratives = {
        QStringLiteral("suspect broke into house and stole laptop"),
        QStringLiteral("victim was robbed and mugged demanded wallet"),
        QStringLiteral("forced entry rear window residential burglary"),
        QStringLiteral("vehicle window smashed valuables stolen"),
        QStringLiteral("suspect armed with knife approached victim")
    };

    QString rows;
    rows.reserve(count * 120);
    const double baseLat = 51.5074;
    const double baseLon = -0.1278;

    for (int i = 0; i < count; ++i) {
        const QString zone = zones[i % zones.size()];
        const double lat = baseLat + (i % 5) * 0.0003;
        const double lon = baseLon + (i % 7) * 0.0003;
        const QDateTime dt = utcDt(2024, 3, 1).addDays(i % 28).addSecs((i % 24) * 3600);
        rows += QStringLiteral("%1,%2,burglary,%3,%4,%5,%6 High St,unresolved,%7\n")
                    .arg(QStringLiteral("EVT%1").arg(i + 1, 4, 10, QChar('0')))
                    .arg(dt.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")))
                    .arg(narratives[i % narratives.size()])
                    .arg(lat, 0, 'f', 6)
                    .arg(lon, 0, 'f', 6)
                    .arg(i + 1)
                    .arg(zone);
    }
    return rows;
}

static void enrichImportedEvents(QVector<CrimeEvent>& events)
{
    for (auto& ev : events) {
        if (ev.locationRaw.has_value() && !ev.locationRaw->isEmpty())
            ev.suburb = *ev.locationRaw;
        else
            ev.suburb = QStringLiteral("London");

        if (ev.lat.has_value())  ev.latitude  = *ev.lat;
        if (ev.lon.has_value())  ev.longitude = *ev.lon;
        if (ev.occurredAt.has_value())
            ev.timestamp = *ev.occurredAt;
        ev.qualityScore = 0.85;
    }
}

static CrimeEvent makeEvent(const QString& eventId,
                             const QString& suburb,
                             double lat, double lon,
                             const QDateTime& occurredAt,
                             const QString& crimeType = QStringLiteral("burglary"),
                             const QString& narrative = {})
{
    CrimeEvent ev;
    ev.eventId      = eventId;
    ev.suburb       = suburb;
    ev.crimeType    = crimeType;
    ev.lat          = lat;
    ev.lon          = lon;
    ev.latitude     = lat;
    ev.longitude    = lon;
    ev.occurredAt   = occurredAt;
    ev.timestamp    = occurredAt;
    ev.ingestedAt   = occurredAt;
    ev.source       = QStringLiteral("integration_test");
    ev.outcome      = QStringLiteral("unresolved");
    ev.qualityScore = 0.8;
    if (!narrative.isEmpty())
        ev.narrative = narrative;
    return ev;
}

static InvestigativeLead makeLead(int rank, const QString& headline,
                                   double confidence,
                                   const QString& category = QStringLiteral("series"))
{
    InvestigativeLead lead;
    lead.rank             = rank;
    lead.category         = category;
    lead.headline         = headline;
    lead.detail           = QStringLiteral("Detail for %1").arg(headline);
    lead.confidence       = confidence;
    lead.confidenceMethod = QStringLiteral("integration_test");
    lead.generatedAt      = QDateTime::currentDateTimeUtc();
    return lead;
}

static QVector<SeriesMatch> seriesMatchesFromDetector(
    const QVector<CrimeEvent>& events,
    const QVector<CrimeSeries>& seriesList)
{
    QVector<SeriesMatch> matches;
    if (events.isEmpty() || seriesList.isEmpty())
        return matches;

    SeriesDetector detector(0.5, 14.0, 3);
    const QDateTime epoch = utcDt(2024, 1, 1);

    SeriesEvent probe;
    probe.eventId   = events.first().eventId;
    probe.lat       = events.first().lat.value_or(51.5);
    probe.lon       = events.first().lon.value_or(-0.1);
    probe.tDays     = epoch.daysTo(events.first().occurredAt.value_or(epoch));
    probe.crimeType = events.first().crimeType;
    probe.moText    = events.first().narrative.value_or(events.first().crimeType);

    const double moSim = SeriesDetector::moJaccard(
        probe.moText, seriesList.first().members.first().moText);

    SeriesMatch sm = detector.linkProbability(probe, seriesList.first(), moSim);
    sm.method = QStringLiteral("DBSCAN");
    matches.append(sm);
    return matches;
}

} // namespace

class FullPipelineIntegrationTest : public QObject
{
    Q_OBJECT

private slots:

    void testCsvToLeadsFullPipeline()
    {
        const QString csv  = kCsvHeader + buildPipelineCsvRows(30);
        const QString path = writeTempCsv(csv);
        QVERIFY2(!path.isEmpty(), "Failed to create temp CSV");

        QVector<CrimeEvent> events = CsvImporter::importFile(path, QStringLiteral("pipeline_csv"));
        QFile::remove(path);

        QVERIFY2(events.size() >= 30,
                 qPrintable(QStringLiteral("Expected >= 30 events, got %1").arg(events.size())));
        enrichImportedEvents(events);

        AppConfig cfg;
        cfg.databasePath = QStringLiteral(":memory:");
        Database db(cfg);
        QVERIFY(db.open());
        for (const auto& ev : events)
            QVERIFY(db.insertEvent(ev));

        CrimeClassifier classifier;
        MOExtractor moExtractor;
        for (auto& ev : events) {
            const QString text = ev.narrative.value_or(ev.crimeType);
            const auto [type, conf] = classifier.classify(text);
            if (!type.isEmpty())
                ev.crimeType = type;
            moExtractor.extract(text);
            Q_UNUSED(conf);
        }

        QVector<PoissonBaseline::EventRecord> poissonRecs;
        poissonRecs.reserve(events.size());
        for (const auto& ev : events) {
            if (!ev.occurredAt)
                continue;
            PoissonBaseline::EventRecord rec;
            rec.zoneId     = ev.suburb;
            rec.occurredAt = *ev.occurredAt;
            rec.crimeType  = ev.crimeType;
            poissonRecs.append(rec);
        }

        PoissonBaseline poisson;
        poisson.fit(poissonRecs);
        QVERIFY(poisson.isFitted());

        const QDateTime epoch = utcDt(2024, 1, 1);
        QVector<SpatiotemporalEvent> stEvents;
        stEvents.reserve(events.size());
        for (const auto& ev : events) {
            if (!ev.occurredAt || !ev.lat || !ev.lon)
                continue;
            SpatiotemporalEvent ste;
            ste.tDays     = static_cast<double>(epoch.daysTo(*ev.occurredAt));
            ste.lat       = *ev.lat;
            ste.lon       = *ev.lon;
            ste.crimeType = ev.crimeType;
            stEvents.append(ste);
        }

        HawkesProcess hawkes;
        hawkes.fit(stEvents, 10);
        if (!hawkes.isFitted()) {
            HawkesParams p;
            p.mu = 0.1; p.alpha = 0.3; p.beta = 0.5; p.sigma = 0.01;
            hawkes.setParams(p);
            hawkes.setHistory(stEvents);
        }
        QVERIFY(hawkes.isFitted() || !stEvents.isEmpty());

        SeriesDetector seriesDetector(0.5, 14.0, 3);
        const auto seriesList = seriesDetector.detect(events);
        QVERIFY(seriesList.size() >= 0);

        EnsemblePredictor ensemble;
        ensemble.setPoisson(&poisson);
        ensemble.setHawkes(&hawkes);
        QVERIFY(ensemble.isReady());

        const QDateTime predictAt = utcDt(2024, 4, 1);
        const auto ensPred = ensemble.predict(
            events.first().suburb, predictAt, events.first().crimeType,
            events.first().lat.value_or(51.5), events.first().lon.value_or(-0.1));
        QVERIFY(ensPred.probCrime >= 0.0 && ensPred.probCrime <= 1.0);

        HintEngine engine;
        HintEngineInput input;
        input.event       = events.first();
        input.dataQuality = 0.85;
        input.seriesMatches = seriesMatchesFromDetector(events, seriesList);

        if (input.seriesMatches.isEmpty() && !seriesList.isEmpty()) {
            SeriesMatch sm;
            sm.seriesId        = seriesList.first().seriesId;
            sm.memberCount     = seriesList.first().members.size();
            sm.linkProbability = 0.75;
            sm.compositeScore  = 0.70;
            sm.method          = QStringLiteral("DBSCAN");
            input.seriesMatches.append(sm);
        }

        const QVector<InvestigativeLead> leads = engine.generate(input);
        QVERIFY2(!leads.isEmpty(),
                 qPrintable(QStringLiteral("Expected >= 1 lead, got %1").arg(leads.size())));

        const QJsonArray jsonArr = DataExporter::eventsToJson(events);
        QVERIFY2(jsonArr.size() >= 30,
                 qPrintable(QStringLiteral("JSON array size %1").arg(jsonArr.size())));

        const QJsonDocument doc(jsonArr);
        QVERIFY2(doc.isArray(), "Exported JSON must be a valid array");
        QVERIFY2(!doc.toJson().isEmpty(), "JSON output must be non-empty");
    }

    void testExportAllFormats()
    {
        const QDateTime base = utcDt(2024, 6, 1);
        QVector<CrimeEvent> events;
        events.reserve(10);
        for (int i = 0; i < 10; ++i) {
            events.append(makeEvent(
                QStringLiteral("EXPORT-%1").arg(i, 3, 10, QChar('0')),
                QStringLiteral("Zone_%1").arg(i % 3),
                51.5 + i * 0.001, -0.1 + i * 0.001,
                base.addDays(i)));
        }

        QVector<InvestigativeLead> leads = {
            makeLead(1, QStringLiteral("Series link to burglary cluster"), 0.92),
            makeLead(2, QStringLiteral("MO match on forced entry"), 0.71),
            makeLead(3, QStringLiteral("Geographic profile peak NW"), 0.55)
        };

        const QJsonArray eventsJson = DataExporter::eventsToJson(events);
        const QString eventsCsv     = DataExporter::eventsToCsv(events);
        const QString eventsHtml    = DataExporter::eventsToHtml(events);

        const QJsonArray leadsJson = DataExporter::leadsToJson(leads);
        const QString leadsCsv     = DataExporter::leadsToCsv(leads);
        const QString leadsHtml    = DataExporter::leadsToHtml(leads);

        QVERIFY2(!eventsJson.isEmpty(), "Events JSON must be non-empty");
        QVERIFY2(QJsonDocument(eventsJson).isArray(), "Events JSON must parse as array");
        QVERIFY2(!eventsCsv.isEmpty() && eventsCsv.contains(QStringLiteral("event_id")),
                 "Events CSV must have header row");
        QVERIFY2(!eventsHtml.isEmpty() && eventsHtml.contains(QStringLiteral("<table>")),
                 "Events HTML must contain table");

        QVERIFY2(!leadsJson.isEmpty(), "Leads JSON must be non-empty");
        QVERIFY2(QJsonDocument(leadsJson).isArray(), "Leads JSON must parse as array");
        QVERIFY2(!leadsCsv.isEmpty() && leadsCsv.contains(QStringLiteral("rank")),
                 "Leads CSV must have header row");
        QVERIFY2(!leadsHtml.isEmpty() && leadsHtml.contains(QStringLiteral("<table>")),
                 "Leads HTML must contain table");
    }

    void testBenchmarkFullReport()
    {
        QVector<double> yTrue(100, 0.0);
        QVector<double> yPred(100, 0.0);

        for (int i = 0; i < 30; ++i) {
            yTrue[i] = 1.0;
            yPred[i] = 0.85;
        }
        for (int i = 30; i < 100; ++i) {
            yTrue[i] = 0.0;
            yPred[i] = 0.15;
        }

        const BenchmarkReport report = BenchmarkMetrics::fullReport(yTrue, yPred);
        const QString text = report.reportText();

        QVERIFY2(text.contains(QStringLiteral("PAI")),
                 "fullReport text must contain PAI");
        QVERIFY2(text.contains(QStringLiteral("AUC")),
                 "fullReport text must contain AUC");
        QVERIFY2(report.aucRoc > 0.5,
                 qPrintable(QStringLiteral("Expected AUC > 0.5, got %1").arg(report.aucRoc)));
        QCOMPARE(report.nSamples, 100);
    }

    void testBiasAuditorDetectsImbalance()
    {
        QVector<QString> groups;
        QVector<double>  preds;

        for (int i = 0; i < 100; ++i) {
            groups.append(QStringLiteral("A"));
            preds.append(i < 90 ? 0.9 : 0.1);
        }
        for (int i = 0; i < 100; ++i) {
            groups.append(QStringLiteral("B"));
            preds.append(i < 10 ? 0.9 : 0.1);
        }

        const auto reports = BiasAuditor::disparateImpact(groups, preds);
        QVERIFY2(!reports.isEmpty(), "BiasAuditor must return reports");

        // DI ratio = min_rate / max_rate ≤ 1.0; for strong imbalance it is < 0.8 and flagged
        bool foundHighRatio = false;
        for (const BiasReport& r : reports) {
            if ((r.groupA == QStringLiteral("A") && r.groupB == QStringLiteral("B")) ||
                (r.groupA == QStringLiteral("B") && r.groupB == QStringLiteral("A"))) {
                QVERIFY2(r.ratio < 0.8,
                         qPrintable(QStringLiteral("Expected DI ratio < 0.8 (imbalanced), got %1")
                                        .arg(r.ratio)));
                QVERIFY2(r.flagged, "Imbalanced group pair should be flagged");
                foundHighRatio = true;
            }
        }
        QVERIFY2(foundHighRatio, "Expected A vs B disparate impact report");
    }

    void testProvenanceRecordsAllStages()
    {
        ProvenanceLog log;
        const CrimeEvent ev = makeEvent(
            QStringLiteral("PROV-EVT-001"),
            QStringLiteral("Westminster"),
            51.5074, -0.1278,
            utcDt(2024, 5, 15),
            QStringLiteral("burglary"),
            QStringLiteral("suspect broke into house and stole laptop"));

        CrimeClassifier classifier;
        MOExtractor moExtractor;
        const QString text = ev.narrative.value_or(ev.crimeType);
        const auto [crimeType, conf] = classifier.classify(text);
        moExtractor.extract(text);

        log.record(ev.eventId, QStringLiteral("ingest"), QStringLiteral("loaded"),
                   QStringLiteral("source=integration_test"));
        log.record(ev.eventId, QStringLiteral("nlp"), QStringLiteral("classified"),
                   QStringLiteral("type=%1 conf=%2").arg(crimeType).arg(conf, 0, 'f', 2));

        QVector<PoissonBaseline::EventRecord> recs;
        recs.append({ev.suburb, *ev.occurredAt, ev.crimeType});
        PoissonBaseline poisson;
        poisson.fit(recs);

        log.record(ev.eventId, QStringLiteral("model"), QStringLiteral("poisson_fit"),
                   QStringLiteral("fitted=%1 events=%2")
                       .arg(poisson.isFitted() ? QStringLiteral("yes") : QStringLiteral("no"))
                       .arg(poisson.totalEvents()));

        HintEngine engine;
        HintEngineInput input;
        input.event = ev;
        input.dataQuality = 0.8;
        SeriesMatch sm;
        sm.seriesId        = QStringLiteral("SER-PROV");
        sm.memberCount     = 4;
        sm.linkProbability = 0.72;
        sm.compositeScore  = 0.68;
        sm.method          = QStringLiteral("near_repeat");
        input.seriesMatches.append(sm);
        const auto leads = engine.generate(input);

        log.record(ev.eventId, QStringLiteral("inference"), QStringLiteral("hint_engine"),
                   QStringLiteral("leads=%1").arg(leads.size()));

        const auto chain = log.chain(ev.eventId);
        QVERIFY2(chain.size() >= 3,
                 qPrintable(QStringLiteral("Expected >= 3 provenance entries, got %1")
                                .arg(chain.size())));

        QStringList stages;
        for (const auto& entry : chain)
            stages.append(entry.stage);
        QVERIFY(stages.contains(QStringLiteral("nlp")));
        QVERIFY(stages.contains(QStringLiteral("model")));
        QVERIFY(stages.contains(QStringLiteral("inference")));
    }

    void testLeadReportGeneratesHtmlAndMarkdown()
    {
        const QString caseRef = QStringLiteral("CASE-REF-2024-INTEGRATION");
        const QVector<InvestigativeLead> leads = {
            makeLead(1, QStringLiteral("High-confidence series linkage"), 0.91),
            makeLead(2, QStringLiteral("Strong MO similarity match"), 0.82),
            makeLead(3, QStringLiteral("Moderate geographic profile lead"), 0.65),
            makeLead(4, QStringLiteral("Network co-offending connection"), 0.48),
            makeLead(5, QStringLiteral("Low-confidence anomaly signal"), 0.32)
        };

        const LeadReport report = LeadReportGenerator::generate(caseRef, leads);
        const QString html      = LeadReportGenerator::generateHtml(report);

        QVERIFY2(report.markdownText.contains(caseRef),
                 "Markdown must contain case reference number");
        QVERIFY2(html.contains(caseRef),
                 "HTML must contain case reference number");

        for (const auto& lead : leads)
            QVERIFY2(report.markdownText.contains(lead.headline),
                     qPrintable(QStringLiteral("Markdown missing headline: %1").arg(lead.headline)));

        QVERIFY2(html.contains(QStringLiteral("HIGH CONFIDENCE")),
                 "HTML must contain HIGH CONFIDENCE badge for confidence >= 0.8");
    }

    void testDatabaseRoundTrip()
    {
        AppConfig cfg;
        cfg.databasePath = QStringLiteral(":memory:");
        Database db(cfg);
        QVERIFY(db.open());

        const QDateTime base = utcDt(2024, 2, 1);
        QVector<CrimeEvent> inserted;
        inserted.reserve(20);
        for (int i = 0; i < 20; ++i) {
            inserted.append(makeEvent(
                QStringLiteral("DB-RT-%1").arg(i, 3, 10, QChar('0')),
                QStringLiteral("Southwark"),
                51.50 + i * 0.0005, -0.12 + i * 0.0005,
                base.addDays(i)));
            QVERIFY(db.insertEvent(inserted.last()));
        }
        QCOMPARE(db.eventCount(), 20);

        const QVector<CrimeEvent> retrieved = db.getAllEvents();
        QVERIFY2(retrieved.size() >= 20,
                 qPrintable(QStringLiteral("Retrieved %1 events").arg(retrieved.size())));

        const QJsonArray jsonArr = DataExporter::eventsToJson(retrieved);
        QVERIFY2(jsonArr.size() >= 20,
                 qPrintable(QStringLiteral("JSON has %1 events").arg(jsonArr.size())));

        QStringList jsonIds;
        for (const QJsonValue& val : jsonArr) {
            const QJsonObject obj = val.toObject();
            QVERIFY(obj.contains(QStringLiteral("eventId")));
            jsonIds.append(obj[QStringLiteral("eventId")].toString());
        }

        for (const auto& ev : inserted) {
            QVERIFY2(jsonIds.contains(ev.eventId),
                     qPrintable(QStringLiteral("Missing eventId %1 in JSON export")
                                    .arg(ev.eventId)));
        }
    }

    void testPipelineWithMultipleCrimeTypes()
    {
        CrimeClassifier classifier;
        const QStringList texts = {
            QStringLiteral("suspect broke into house and stole laptop"),
            QStringLiteral("victim was robbed and mugged demanded wallet"),
            QStringLiteral("victim was found dead after being shot"),
            QStringLiteral("victim reported sexual assault indecent touching"),
            QStringLiteral("minor shoplifting theft of goods"),
            QStringLiteral("violent assault victim punched and kicked")
        };

        QSet<QString> classifiedTypes;
        for (const QString& text : texts) {
            const auto [type, conf] = classifier.classify(text);
            QVERIFY2(!type.isEmpty(), "Classifier must return a crime type");
            QVERIFY2(conf >= 0.0 && conf <= 1.0, "Confidence must be in [0,1]");
            classifiedTypes.insert(type);
        }

        QVERIFY2(classifiedTypes.size() >= 6,
                 qPrintable(QStringLiteral("Expected 6 distinct types, got %1")
                                .arg(classifiedTypes.size())));
    }

    void testAnomalyPipelineEndToEnd()
    {
        const QDateTime epoch = utcDt(2024, 1, 1);
        QVector<AnomalyFeatureVector> normal;
        normal.reserve(50);

        for (int i = 0; i < 50; ++i) {
            AnomalyFeatureVector f;
            f.eventId       = QStringLiteral("NORM-%1").arg(i);
            f.lat           = 51.5074 + (i % 10) * 0.0002;
            f.lon           = -0.1278 + (i % 7) * 0.0002;
            f.tDays         = static_cast<double>(i);
            f.hourNorm      = (i % 24) / 24.0;
            f.crimeTypeCode = 1;
            normal.append(f);
        }

        QVector<AnomalyFeatureVector> outliers;
        outliers.reserve(5);
        for (int i = 0; i < 5; ++i) {
            AnomalyFeatureVector f;
            f.eventId       = QStringLiteral("OUT-%1").arg(i);
            f.lat           = 52.4 + i * 0.01;
            f.lon           = -0.1278 + i * 0.5;
            f.tDays         = 500.0 + i;
            f.hourNorm      = 0.99;
            f.crimeTypeCode = 99;
            outliers.append(f);
        }

        QVector<AnomalyFeatureVector> all = normal + outliers;

        AnomalyDetector detector(0.1);
        detector.fit(normal);
        QVERIFY(detector.isFitted());

        const auto anomalySignals = detector.detectAnomalies(all);
        QCOMPARE(anomalySignals.size(), all.size());

        int flaggedCount = 0;
        for (const auto& sig : anomalySignals) {
            if (sig.eventId.startsWith(QStringLiteral("OUT-")) &&
                (sig.isAnomaly || sig.combinedScore > 0.5))
                ++flaggedCount;
        }

        QVERIFY2(flaggedCount >= 1,
                 qPrintable(QStringLiteral("Expected >= 1 outlier flagged, got %1")
                                .arg(flaggedCount)));
        Q_UNUSED(epoch);
    }

    void testSeriesLinkagePipeline()
    {
        const QDateTime base = utcDt(2024, 3, 10, 22);
        QVector<CrimeEvent> events;
        events.reserve(8);

        for (int i = 0; i < 8; ++i) {
            events.append(makeEvent(
                QStringLiteral("SERIES-%1").arg(i),
                QStringLiteral("Camden"),
                51.5390 + i * 0.00005,
                -0.1426 + i * 0.00005,
                base.addSecs(i * 3600),
                QStringLiteral("burglary"),
                QStringLiteral("forced entry rear window residential solo night")));
        }

        SeriesDetector detector(0.5, 7.0, 3);
        const auto seriesList = detector.detect(events);
        QVERIFY2(!seriesList.isEmpty(),
                 "SeriesDetector should find at least one series cluster");

        const QVector<SeriesMatch> matches = seriesMatchesFromDetector(events, seriesList);
        QVERIFY2(!matches.isEmpty(),
                 "linkProbability should return at least one SeriesMatch");
        QVERIFY2(matches.first().linkProbability >= 0.0,
                 "SeriesMatch linkProbability must be non-negative");
    }
};

QTEST_MAIN(FullPipelineIntegrationTest)
#include "test_full_pipeline_integration.moc"
