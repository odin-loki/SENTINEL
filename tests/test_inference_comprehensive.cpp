// test_inference_comprehensive.cpp
// Comprehensive tests for inference engine (AnomalyDetector, HintEngine,
// LeadReportGenerator, ProvenanceLog) and benchmarking layer (BiasAuditor,
// DataExporter).

#include <QTest>
#include <QJsonDocument>
#include <QJsonArray>
#include <QDateTime>
#include <QTimeZone>
#include <QSet>
#include <cmath>
#include <limits>

#include "inference/AnomalyDetector.h"
#include "inference/HintEngine.h"
#include "inference/LeadReportGenerator.h"
#include "audit/ProvenanceLog.h"
#include "benchmark/BiasAuditor.h"
#include "core/DataExporter.h"
#include "core/CrimeEvent.h"

namespace {

using AFV = AnomalyFeatureVector;

AFV makeAFV(const QString& id, double lat, double lon, double tDays,
            double hour = 0.5, int typeCode = 0)
{
    return { id, lat, lon, tDays, hour, typeCode };
}

QVector<AFV> cluster20()
{
    QVector<AFV> data;
    for (int i = 0; i < 20; ++i) {
        data.append(makeAFV(QStringLiteral("E%1").arg(i),
                            51.5 + i * 0.001,
                            -0.1 + i * 0.001,
                            static_cast<double>(i)));
    }
    return data;
}

CrimeEvent makeEvent(const QString& id = QStringLiteral("EVT-001"))
{
    CrimeEvent ev;
    ev.eventId    = id;
    ev.id         = id;
    ev.crimeType  = QStringLiteral("burglary");
    ev.suburb     = QStringLiteral("Shoreditch");
    ev.occurredAt = QDateTime(QDate(2024, 6, 1), QTime(14, 0, 0), QTimeZone::utc());
    ev.ingestedAt = QDateTime(QDate(2024, 6, 2), QTime(8, 0, 0), QTimeZone::utc());
    ev.lat        = 51.505;
    ev.lon        = -0.095;
    ev.latitude   = 51.505;
    ev.longitude  = -0.095;
    ev.source     = QStringLiteral("test");
    ev.qualityScore = 0.9;
    return ev;
}

SeriesMatch makeSeriesMatch(const QString& seriesId, double linkProb)
{
    SeriesMatch m;
    m.seriesId             = seriesId;
    m.memberCount          = 3;
    m.linkProbability      = linkProb;
    m.spatialDistanceM     = 150.0;
    m.temporalDistanceDays = 2.0;
    m.moSimilarity         = 0.7;
    m.compositeScore       = linkProb;
    m.method               = QStringLiteral("near_repeat");
    return m;
}

MOMatch makeMOMatch(const QString& caseId, double sim,
                    const QStringList& features)
{
    MOMatch m;
    m.caseId          = caseId;
    m.similarityScore = sim;
    m.sharedFeatures  = features;
    m.resolved        = false;
    return m;
}

InvestigativeLead makeLead(int rank, const QString& category,
                           const QString& headline, double confidence,
                           const std::vector<QString>& provenance = {})
{
    InvestigativeLead l;
    l.rank              = rank;
    l.category          = category;
    l.headline          = headline;
    l.detail            = QStringLiteral("Test detail for %1").arg(headline);
    l.confidence        = confidence;
    l.confidenceMethod  = QStringLiteral("test_method");
    l.provenance        = provenance.empty()
        ? std::vector<QString>{ QStringLiteral("HintEngine"), QStringLiteral("TestRule") }
        : provenance;
    l.generatedAt       = QDateTime(QDate(2024, 6, 10), QTime(12, 0, 0), QTimeZone::utc());
    return l;
}

} // namespace

class InferenceComprehensiveTest : public QObject
{
    Q_OBJECT

private slots:

    // ── AnomalyDetector ──────────────────────────────────────────────────────

    void testAnomalyDetectorFitDetect()
    {
        AnomalyDetector detector;
        const auto data = cluster20();
        detector.fit(data);
        QVERIFY(detector.isFitted());

        const auto results = detector.detectAnomalies(data);
        QCOMPARE(results.size(), data.size());
        for (const auto& sig : results)
            QVERIFY2(!sig.eventId.isEmpty(), "Each signal must carry an eventId");
    }

    void testAnomalyDetectorZScoresComputed()
    {
        AnomalyDetector detector;
        const auto data = cluster20();
        detector.fit(data);
        const auto results = detector.detectAnomalies(data);

        QVERIFY(!results.isEmpty());
        for (const auto& sig : results) {
            QVERIFY2(std::isfinite(sig.zScoreTemporal),
                     qPrintable(QStringLiteral("zScoreTemporal not finite for %1")
                                    .arg(sig.eventId)));
            QVERIFY2(std::isfinite(sig.zScoreSpatial),
                     qPrintable(QStringLiteral("zScoreSpatial not finite for %1")
                                    .arg(sig.eventId)));
        }
    }

    void testAnomalyDetectorCombinedScoreInRange()
    {
        AnomalyDetector detector;
        const auto data = cluster20();
        detector.fit(data);
        const auto results = detector.detectAnomalies(data);

        for (const auto& sig : results) {
            QVERIFY2(sig.combinedScore >= 0.0 && sig.combinedScore <= 1.0,
                     qPrintable(QStringLiteral("combinedScore %1 out of [0,1] for %2")
                                    .arg(sig.combinedScore).arg(sig.eventId)));
        }
    }

    void testAnomalyDetectorUnfittedReturnsEmpty()
    {
        AnomalyDetector detector;
        QVERIFY(!detector.isFitted());
        const auto results = detector.detectAnomalies({});
        QVERIFY2(results.isEmpty(),
                 "detectAnomalies on empty input before fit must return empty");
    }

    void testAnomalyDetectorOutlierHigherScore()
    {
        AnomalyDetector detector;
        auto data = cluster20();
        data.append(makeAFV(QStringLiteral("OUTLIER"), 55.0, 10.0, 500.0, 0.99, 99));

        detector.fit(data);
        const auto results = detector.detectAnomalies(data);

        double clusterMax = 0.0;
        double outlierScore = 0.0;
        for (const auto& sig : results) {
            if (sig.eventId == QStringLiteral("OUTLIER"))
                outlierScore = sig.combinedScore;
            else
                clusterMax = std::max(clusterMax, sig.combinedScore);
        }

        QVERIFY2(outlierScore > clusterMax,
                 qPrintable(QStringLiteral("Outlier score %1 should exceed cluster max %2")
                                .arg(outlierScore).arg(clusterMax)));
    }

    // ── HintEngine ───────────────────────────────────────────────────────────

    void testHintEngineContradictionDetected()
    {
        HintEngine engine;
        HintEngineInput input;
        input.event = makeEvent();

        input.moMatches.append(makeMOMatch(QStringLiteral("CASE-SOLO"), 0.75,
                                         { QStringLiteral("solo") }));
        input.moMatches.append(makeMOMatch(QStringLiteral("CASE-GROUP"), 0.70,
                                         { QStringLiteral("group") }));

        const auto leads = engine.generate(input);
        QVERIFY2(leads.size() >= 2, "Need at least two MO leads for contradiction test");

        bool foundContradiction = false;
        for (const auto& lead : leads) {
            if (!lead.contradictions.empty()) {
                foundContradiction = true;
                break;
            }
        }
        QVERIFY2(foundContradiction,
                 "Contradiction between solo and group leads should be detected");
    }

    void testHintEngineLeadsRanked()
    {
        HintEngine engine;
        HintEngineInput input;
        input.event = makeEvent();
        input.seriesMatches.append(makeSeriesMatch(QStringLiteral("SER-HIGH"), 0.92));
        input.seriesMatches.append(makeSeriesMatch(QStringLiteral("SER-MED"), 0.55));
        input.networkLeads.append([] {
            NetworkLead nl;
            nl.personId        = QStringLiteral("P1");
            nl.connectionType  = QStringLiteral("co_offender");
            nl.sharedIncidents = 2;
            nl.centralityScore = 0.4;
            nl.communityId     = 1;
            nl.riskScore       = 0.35;
            nl.reasoning       = QStringLiteral("Low risk");
            return nl;
        }());

        const auto leads = engine.generate(input);
        QVERIFY2(leads.size() >= 2, "Need multiple leads to verify ranking");

        for (int i = 1; i < leads.size(); ++i) {
            QVERIFY2(leads[i - 1].confidence >= leads[i].confidence,
                     qPrintable(QStringLiteral("Leads not in confidence-descending order at %1")
                                    .arg(i)));
        }
    }

    void testHintEngineProvenance()
    {
        HintEngine engine;
        HintEngineInput input;
        input.event = makeEvent();
        input.seriesMatches.append(makeSeriesMatch(QStringLiteral("SER-PROV"), 0.80));

        const auto leads = engine.generate(input);
        QVERIFY(!leads.isEmpty());
        for (const auto& lead : leads)
            QVERIFY2(!lead.provenance.empty(),
                     "Every lead must have a non-empty provenance vector");
    }

    void testHintEngineDeduplication()
    {
        HintEngine engine;
        HintEngineInput input;
        input.event = makeEvent();

        const auto duplicate = makeSeriesMatch(QStringLiteral("SER-DUP"), 0.85);
        input.seriesMatches.append(duplicate);
        input.seriesMatches.append(duplicate);

        const auto leads = engine.generate(input);

        int dupCount = 0;
        for (const auto& lead : leads) {
            if (lead.category == QStringLiteral("series_linkage")
                && lead.headline.contains(QStringLiteral("SER-DUP")))
                ++dupCount;
        }
        QVERIFY2(dupCount <= 1,
                 qPrintable(QStringLiteral("Identical series leads should be deduplicated, got %1")
                                .arg(dupCount)));
    }

    void testHintEngineEmptyInput()
    {
        HintEngine engine;
        HintEngineInput input;
        const auto leads = engine.generate(input);
        QVERIFY2(leads.isEmpty(),
                 "generate() with empty HintEngineInput should return empty vector");
    }

    // ── LeadReportGenerator ──────────────────────────────────────────────────

    void testLeadReportMarkdownContainsHeadline()
    {
        QVector<InvestigativeLead> leads;
        leads.append(makeLead(1, QStringLiteral("series_linkage"),
                              QStringLiteral("Linked to series SER-99"), 0.88));

        const auto report = LeadReportGenerator::generate(QStringLiteral("CASE-MD"), leads);
        QVERIFY2(report.markdownText.contains(QStringLiteral("Linked to series SER-99")),
                 "Markdown report must contain lead headlines");
    }

    void testLeadReportHtmlIsDarkThemed()
    {
        QVector<InvestigativeLead> leads;
        leads.append(makeLead(1, QStringLiteral("mo_similarity"),
                              QStringLiteral("MO match headline"), 0.75));

        const auto report = LeadReportGenerator::generate(QStringLiteral("CASE-HTML"), leads);
        const QString html = LeadReportGenerator::generateHtml(report);
        QVERIFY2(html.contains(QStringLiteral("#0d1117")),
                 "HTML report must use dark background colour #0d1117");
    }

    void testLeadReportHighConfidenceBadge()
    {
        QVector<InvestigativeLead> leads;
        leads.append(makeLead(1, QStringLiteral("network_association"),
                              QStringLiteral("High confidence lead"), 0.85));

        const auto report = LeadReportGenerator::generate(QStringLiteral("CASE-BADGE"), leads);
        const QString html = LeadReportGenerator::generateHtml(report);
        QVERIFY2(html.contains(QStringLiteral("HIGH CONFIDENCE")),
                 "Lead with confidence >= 0.8 must receive HIGH CONFIDENCE badge");
    }

    void testLeadReportProvenanceChain()
    {
        QVector<InvestigativeLead> leads;
        leads.append(makeLead(1, QStringLiteral("geographic_profile"),
                              QStringLiteral("Peak anchor lead"), 0.72,
                              { QStringLiteral("GeographicProfiler.profile"),
                                QStringLiteral("rossmo_cgt") }));

        const auto report = LeadReportGenerator::generate(QStringLiteral("CASE-PROV"), leads);
        const QString html = LeadReportGenerator::generateHtml(report);
        QVERIFY2(html.contains(QStringLiteral("Provenance Chain")),
                 "HTML report must include provenance section");
        QVERIFY2(html.contains(QStringLiteral("GeographicProfiler.profile")),
                 "HTML report must include provenance data");
    }

    void testLeadReportEmptyLeads()
    {
        const auto report = LeadReportGenerator::generate(QStringLiteral("CASE-EMPTY"), {});
        const QString html = LeadReportGenerator::generateHtml(report);
        QVERIFY2(!html.isEmpty(), "Empty leads report HTML must not crash");
        QCOMPARE(report.totalLeads, 0);
    }

    // ── ProvenanceLog ────────────────────────────────────────────────────────

    void testProvenanceLogRecord()
    {
        ProvenanceLog log;
        log.record(QStringLiteral("EV-REC"), QStringLiteral("ingest"),
                   QStringLiteral("csv_import"), QStringLiteral("Imported row"),
                   QStringLiteral("hash1234"));

        const auto chain = log.chain(QStringLiteral("EV-REC"));
        QCOMPARE(chain.size(), 1);
        QCOMPARE(chain.first().eventId, QStringLiteral("EV-REC"));
        QCOMPARE(chain.first().action, QStringLiteral("csv_import"));
    }

    void testProvenanceLogFormatHtmlNonEmpty()
    {
        ProvenanceLog log;
        log.record(QStringLiteral("EV-HTML"), QStringLiteral("nlp"),
                   QStringLiteral("classify"), QStringLiteral("burglary 0.9"));

        const QString html = log.formatHtml(QStringLiteral("EV-HTML"));
        QVERIFY2(!html.isEmpty(), "formatHtml() must return non-empty HTML");
        QVERIFY2(html.contains(QStringLiteral("<table")),
                 "formatHtml() must contain an HTML table");
    }

    void testProvenanceLogFormatHtmlDarkTheme()
    {
        ProvenanceLog log;
        log.record(QStringLiteral("EV-DARK"), QStringLiteral("model"),
                   QStringLiteral("fit"), QStringLiteral("model trained"));

        const QString html = log.formatHtml(QStringLiteral("EV-DARK"));
        QVERIFY2(html.contains(QStringLiteral("#0d1117")),
                 "Provenance HTML must use dark background #0d1117");
    }

    void testProvenanceLogFormatChainMarkdown()
    {
        ProvenanceLog log;
        log.record(QStringLiteral("EV-CHAIN"), QStringLiteral("inference"),
                   QStringLiteral("hint_gen"), QStringLiteral("3 leads generated"));

        const QString md = log.formatChain(QStringLiteral("EV-CHAIN"));
        QVERIFY2(!md.isEmpty(), "formatChain() must return non-empty markdown");
        QVERIFY2(md.contains(QStringLiteral("EV-CHAIN")),
                 "formatChain() markdown must contain event ID");
    }

    void testProvenanceLogMultipleStages()
    {
        ProvenanceLog log;
        log.record(QStringLiteral("EV-MULTI"), QStringLiteral("ingest"),
                   QStringLiteral("import"), QStringLiteral("step 1"));
        log.record(QStringLiteral("EV-MULTI"), QStringLiteral("nlp"),
                   QStringLiteral("classify"), QStringLiteral("step 2"));
        log.record(QStringLiteral("EV-MULTI"), QStringLiteral("inference"),
                   QStringLiteral("detect"), QStringLiteral("step 3"));

        const auto chain = log.chain(QStringLiteral("EV-MULTI"));
        QCOMPARE(chain.size(), 3);

        QSet<QString> stages;
        for (const auto& entry : chain)
            stages.insert(entry.stage);

        QVERIFY(stages.contains(QStringLiteral("ingest")));
        QVERIFY(stages.contains(QStringLiteral("nlp")));
        QVERIFY(stages.contains(QStringLiteral("inference")));
    }

    // ── BiasAuditor ──────────────────────────────────────────────────────────

    void testBiasAuditorDisparateImpact()
    {
        QVector<QString> groups;
        QVector<double> yPred;

        for (int i = 0; i < 5; ++i) {
            groups << QStringLiteral("Biased");
            yPred  << 1.0;
        }
        for (int i = 0; i < 5; ++i) {
            groups << QStringLiteral("Unbiased");
            yPred  << 0.0;
        }

        const auto reports = BiasAuditor::disparateImpact(groups, yPred);
        QVERIFY(!reports.isEmpty());

        bool foundFlagged = false;
        for (const auto& r : reports) {
            if (r.groupA == QStringLiteral("Biased")
                && r.groupB == QStringLiteral("Unbiased")) {
                QVERIFY2(r.valueA > r.valueB,
                         "Biased group should have higher positive rate");
                if (r.flagged)
                    foundFlagged = true;
            }
        }
        QVERIFY2(foundFlagged, "Disparate impact between biased/unbiased groups should be flagged");
    }

    void testBiasAuditorEqualizedOddsDiff()
    {
        GroupStats a;
        a.groupId    = QStringLiteral("A");
        a.nEvents    = 10;
        a.nFlagged   = 5;
        a.nActualPos = 4;
        a.nTP        = 3;

        GroupStats b;
        b.groupId    = QStringLiteral("B");
        b.nEvents    = 10;
        b.nFlagged   = 5;
        b.nActualPos = 4;
        b.nTP        = 3;

        BiasAuditor auditor;
        const double diff = auditor.equalizedOddsDiff({ a, b });
        QCOMPARE(diff, 0.0);
    }

    void testBiasAuditorFeedbackLoopCheck()
    {
        QMap<QString, QVector<QPair<double, double>>> trendData;
        trendData[QStringLiteral("OverPoliced")] = {
            { 0.10, 0.20 }, { 0.15, 0.19 }, { 0.25, 0.18 }, { 0.35, 0.17 }
        };
        trendData[QStringLiteral("Normal")] = {
            { 0.10, 0.10 }, { 0.11, 0.11 }, { 0.12, 0.12 }
        };

        const auto flagged = BiasAuditor::feedbackLoopCheck(trendData, 0.01);
        QVERIFY2(flagged.contains(QStringLiteral("OverPoliced")),
                 "feedbackLoopCheck must detect over-policed neighborhoods");
    }

    void testBiasAuditorGroupStats()
    {
        const QVector<QString> groups = {
            QStringLiteral("G1"), QStringLiteral("G1"),
            QStringLiteral("G1"), QStringLiteral("G1")
        };
        const QVector<double> yTrue = { 1.0, 1.0, 0.0, 0.0 };
        const QVector<double> yPred = { 0.9, 0.2, 0.8, 0.1 };

        const auto stats = BiasAuditor::groupStats(groups, yTrue, yPred);
        QVERIFY(stats.contains(QStringLiteral("G1")));

        const GroupStats& s = stats[QStringLiteral("G1")];
        QCOMPARE(s.nTP, 1);
        QCOMPARE(s.nActualPos, 2);
        QCOMPARE(s.nEvents, 4);
        QCOMPARE(s.nFlagged, 2);
    }

    void testBiasAuditorMaxDisparateImpact()
    {
        GroupStats a;
        a.groupId  = QStringLiteral("A");
        a.nEvents  = 10;
        a.nFlagged = 5;

        GroupStats b;
        b.groupId  = QStringLiteral("B");
        b.nEvents  = 10;
        b.nFlagged = 5;

        BiasAuditor auditor;
        const double ratio = auditor.maxDisparateImpact({ a, b });
        QCOMPARE(ratio, 1.0);
    }

    // ── DataExporter ─────────────────────────────────────────────────────────

    void testDataExporterEventsToHtml()
    {
        QVector<CrimeEvent> events;
        CrimeEvent ev = makeEvent(QStringLiteral("EXP-001"));
        events.append(ev);

        const QString html = DataExporter::eventsToHtml(events);
        QVERIFY2(html.contains(QStringLiteral("Event ID")),
                 "Events HTML must contain Event ID table header");
        QVERIFY2(html.contains(QStringLiteral("Crime Type")),
                 "Events HTML must contain Crime Type table header");
        QVERIFY2(html.contains(QStringLiteral("EXP-001")),
                 "Events HTML must contain event data");
    }

    void testDataExporterLeadsToHtml()
    {
        QVector<InvestigativeLead> leads;
        leads.append(makeLead(1, QStringLiteral("series_linkage"),
                              QStringLiteral("Series lead"), 0.82));

        const QString html = DataExporter::leadsToHtml(leads);
        QVERIFY2(html.contains(QStringLiteral("<table")),
                 "Leads HTML must contain a table");
        QVERIFY2(html.contains(QStringLiteral("0.8200"))
                     || html.contains(QStringLiteral("0.82")),
                 "Leads HTML must expose confidence score");
    }

    void testDataExporterEventsToJson()
    {
        QVector<CrimeEvent> events;
        events.append(makeEvent(QStringLiteral("JSON-001")));

        const QJsonArray arr = DataExporter::eventsToJson(events);
        QVERIFY2(!arr.isEmpty(), "eventsToJson must return non-empty array");

        const QJsonDocument doc(arr);
        QVERIFY2(!doc.isNull(), "JSON document must be valid");
        QCOMPARE(arr.size(), 1);
        QVERIFY2(arr.first().toObject().value(QStringLiteral("eventId")).toString()
                     == QStringLiteral("JSON-001"),
                 "JSON must contain eventId field");
    }

    void testDataExporterEventsToCsv()
    {
        QVector<CrimeEvent> events;
        events.append(makeEvent(QStringLiteral("CSV-001")));

        const QString csv = DataExporter::eventsToCsv(events);
        const QStringList lines = csv.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        QVERIFY2(lines.size() >= 2, "CSV must have header row plus at least one data row");
        QVERIFY2(lines.first().contains(QStringLiteral("event_id")),
                 "CSV header must include event_id");
        QVERIFY2(lines.at(1).contains(QStringLiteral("CSV-001")),
                 "CSV data row must contain event ID");
    }

    void testDataExporterXssEscaping()
    {
        QVector<CrimeEvent> events;
        CrimeEvent ev;
        ev.eventId   = QStringLiteral("XSS&001");
        ev.crimeType = QStringLiteral("Burglary & theft <armed>");
        ev.suburb    = QStringLiteral("Shoreditch");
        ev.lat       = 51.5;
        ev.lon       = -0.1;
        ev.occurredAt = QDateTime(QDate(2024, 1, 1), QTime(0, 0, 0), QTimeZone::utc());
        events.append(ev);

        const QString html = DataExporter::eventsToHtml(events);
        QVERIFY2(!html.contains(QStringLiteral("<armed>")),
                 "HTML must not contain raw angle-bracket tags");
        QVERIFY2(html.contains(QStringLiteral("&lt;")),
                 "HTML must escape < characters");
        QVERIFY2(html.contains(QStringLiteral("&gt;")),
                 "HTML must escape > characters");
        QVERIFY2(html.contains(QStringLiteral("&amp;")),
                 "HTML must escape & characters");
        QVERIFY2(html.contains(QStringLiteral("XSS&amp;001")),
                 "HTML must escape & in event ID");
    }
};

QTEST_MAIN(InferenceComprehensiveTest)
#include "test_inference_comprehensive.moc"
