// Iteration 12 — DataExporter deep tests
#include <QtTest/QtTest>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <cmath>
#include "core/DataExporter.h"
#include "core/CrimeEvent.h"
#include "models/RiskForecaster.h"
#include "benchmark/BenchmarkMetrics.h"
#include "audit/ProvenanceLog.h"

class DataExporterDeep3Test : public QObject
{
    Q_OBJECT

    static InvestigativeLead makeLead(int rank, const QString& category,
                                      const QString& headline, double confidence)
    {
        InvestigativeLead l;
        l.rank             = rank;
        l.category         = category;
        l.headline         = headline;
        l.confidence       = confidence;
        l.confidenceMethod = QStringLiteral("Bayes");
        l.generatedAt      = QDateTime(QDate(2024, 1, 1), QTime(12, 0), Qt::UTC);
        return l;
    }

    static CrimeEvent makeEvent(const QString& id, const QString& type)
    {
        CrimeEvent e;
        e.eventId      = id;
        e.crimeType    = type;
        e.suburb       = QStringLiteral("TestSuburb");
        e.lat          = 51.5;
        e.lon          = -0.1;
        e.occurredAt   = QDateTime(QDate(2024, 1, 1), QTime(12, 0), Qt::UTC);
        e.qualityScore = 0.85;
        return e;
    }

private slots:

    // ── escapeCsv edge cases (observed via leadsToCsv) ───────────────────

    void testEscapeCsvComma()
    {
        const QString csv = DataExporter::leadsToCsv({ makeLead(1, "cat,comma", "h", 0.9) });
        QVERIFY2(csv.contains(QStringLiteral("\"cat,comma\"")),
                 "Expected comma-containing field to be quoted");
    }

    void testEscapeCsvDoubleQuote()
    {
        const QString csv = DataExporter::leadsToCsv({ makeLead(1, "cat", "he said \"hello\"", 0.9) });
        QVERIFY2(csv.contains('"'), "Expected double-quote-containing field to be quoted");
    }

    void testEscapeCsvNewline()
    {
        const QString csv = DataExporter::leadsToCsv({ makeLead(1, "cat", "line1\nline2", 0.9) });
        QVERIFY2(csv.contains('"'), "Expected newline-containing field to be quoted");
    }

    // ── leadsToCsv ───────────────────────────────────────────────────────

    void testLeadsCsvHeader()
    {
        const QString csv = DataExporter::leadsToCsv({});
        QVERIFY(csv.startsWith("rank,category,headline,detail,confidence"));
    }

    void testLeadsCsvRowCount()
    {
        QVector<InvestigativeLead> leads;
        for (int i = 1; i <= 5; ++i)
            leads.append(makeLead(i, "test", QStringLiteral("lead%1").arg(i), 0.5));
        QCOMPARE(DataExporter::leadsToCsv(leads).count('\n'), 6);  // header + 5 rows
    }

    void testLeadsCsvEmpty()
    {
        QCOMPARE(DataExporter::leadsToCsv({}).count('\n'), 1);  // header only
    }

    // ── leadsToJson ──────────────────────────────────────────────────────

    void testLeadsJsonCount()
    {
        QVector<InvestigativeLead> leads;
        leads.append(makeLead(1, "geo", "h1", 0.8));
        leads.append(makeLead(2, "mo",  "h2", 0.6));
        QCOMPARE(DataExporter::leadsToJson(leads).size(), 2);
    }

    void testLeadsJsonRankField()
    {
        const QJsonArray arr = DataExporter::leadsToJson({ makeLead(3, "net", "hub", 0.75) });
        QCOMPARE(arr.first().toObject()["rank"].toInt(), 3);
    }

    void testLeadsJsonConfidenceRoundTrip()
    {
        const QJsonArray arr = DataExporter::leadsToJson({ makeLead(1, "geo", "h", 0.912) });
        QVERIFY2(std::abs(arr.first().toObject()["confidence"].toDouble() - 0.912) < 1e-9,
                 "confidence not round-tripped correctly");
    }

    void testLeadsJsonEmpty()
    {
        QVERIFY(DataExporter::leadsToJson({}).isEmpty());
    }

    // ── leadsToMarkdown ──────────────────────────────────────────────────

    void testLeadsMarkdownContainsHeader()
    {
        const QString md = DataExporter::leadsToMarkdown({ makeLead(1, "geo", "test", 0.8) }, "MyReport");
        QVERIFY(md.contains("# MyReport"));
        QVERIFY(md.contains("| Rank |"));
    }

    void testLeadsMarkdownContainsData()
    {
        const QString md = DataExporter::leadsToMarkdown({ makeLead(1, "GeoCategory", "Headline", 0.8) });
        QVERIFY(md.contains("GeoCategory"));
        QVERIFY(md.contains("Headline"));
    }

    void testLeadsMarkdownEscapesPipeChar()
    {
        const QString md = DataExporter::leadsToMarkdown({ makeLead(1, "cat|sub", "h", 0.8) });
        QVERIFY(md.contains("cat/sub"));
    }

    // ── leadsToHtml ──────────────────────────────────────────────────────

    void testLeadsHtmlDoctype()
    {
        const QString html = DataExporter::leadsToHtml({ makeLead(1, "geo", "h", 0.9) }, "Report");
        QVERIFY(html.startsWith("<!DOCTYPE html>"));
        QVERIFY(html.contains("<table>"));
        QVERIFY(html.contains("</table>"));
    }

    void testLeadsHtmlXssEscape()
    {
        const QString html = DataExporter::leadsToHtml({}, "<script>xss</script>");
        QVERIFY(!html.contains("<script>xss</script>"));
        QVERIFY(html.contains("&lt;script&gt;"));
    }

    void testLeadsHtmlHeadlineEscaped()
    {
        const QString html = DataExporter::leadsToHtml({ makeLead(1, "cat", "<b>bold</b>", 0.9) }, "T");
        QVERIFY(!html.contains("<b>bold</b>"));
        QVERIFY(html.contains("&lt;b&gt;"));
    }

    // ── eventsToJson ─────────────────────────────────────────────────────

    void testEventsJsonCount()
    {
        QVector<CrimeEvent> evs = { makeEvent("E1","burglary"), makeEvent("E2","theft") };
        QCOMPARE(DataExporter::eventsToJson(evs).size(), 2);
    }

    void testEventsJsonFields()
    {
        const QJsonObject obj = DataExporter::eventsToJson({ makeEvent("EVT_001","robbery") })
                                    .first().toObject();
        QCOMPARE(obj["eventId"].toString(),   QStringLiteral("EVT_001"));
        QCOMPARE(obj["crimeType"].toString(), QStringLiteral("robbery"));
        QVERIFY2(std::abs(obj["lat"].toDouble() - 51.5) < 1e-9, "lat mismatch");
        QVERIFY2(std::abs(obj["lon"].toDouble() - (-0.1)) < 1e-9, "lon mismatch");
    }

    // ── eventsToCsv ──────────────────────────────────────────────────────

    void testEventsCsvHeader()
    {
        QVERIFY(DataExporter::eventsToCsv({}).startsWith("event_id,crime_type,suburb,lat,lon"));
    }

    void testEventsCsvRowCount()
    {
        QVector<CrimeEvent> evs;
        for (int i = 1; i <= 3; ++i) evs.append(makeEvent(QStringLiteral("E%1").arg(i),"burglary"));
        QCOMPARE(DataExporter::eventsToCsv(evs).count('\n'), 4);  // header + 3 rows
    }

    // ── eventsToHtml ─────────────────────────────────────────────────────

    void testEventsHtmlContainsTable()
    {
        const QString html = DataExporter::eventsToHtml({ makeEvent("E1","theft") }, "Events");
        QVERIFY(html.contains("<table>"));
        QVERIFY(html.contains("E1"));
    }

    // ── forecastsToJson ──────────────────────────────────────────────────

    void testForecastsJsonZoneId()
    {
        ZoneForecast zf;
        zf.zoneId     = QStringLiteral("ZONE_ALPHA");
        zf.weeklyRisk = 0.3;
        zf.alertLevel = 1;
        const QJsonArray arr = DataExporter::forecastsToJson({zf});
        QCOMPARE(arr.size(), 1);
        QCOMPARE(arr.first().toObject()["zoneId"].toString(), QStringLiteral("ZONE_ALPHA"));
    }

    void testForecastsJsonAlertLabel()
    {
        ZoneForecast zf;
        zf.zoneId     = QStringLiteral("Z");
        zf.alertLevel = 2;
        const QJsonArray arr = DataExporter::forecastsToJson({zf});
        QCOMPARE(arr.first().toObject()["alertLabel"].toString(), QStringLiteral("HIGH"));
    }

    // ── benchmarkToJson ──────────────────────────────────────────────────

    void testBenchmarkJsonFields()
    {
        BenchmarkReport rep;
        rep.nSamples   = 100;
        rep.pai5pct    = 7.2;
        rep.aucRoc     = 0.87;
        rep.brierScore = 0.09;

        const QJsonObject obj = DataExporter::benchmarkToJson(rep);
        QCOMPARE(obj["nSamples"].toInt(), 100);
        QVERIFY2(std::abs(obj["aucRoc"].toDouble() - 0.87) < 1e-9, "aucRoc mismatch");
        QVERIFY2(std::abs(obj["brierScore"].toDouble() - 0.09) < 1e-9, "brierScore mismatch");
    }

    // ── benchmarkToMarkdown ───────────────────────────────────────────────

    void testBenchmarkMarkdownStructure()
    {
        BenchmarkReport rep;
        rep.nSamples = 50;
        rep.pai5pct  = 4.0;
        rep.aucRoc   = 0.82;
        const QString md = DataExporter::benchmarkToMarkdown(rep);
        QVERIFY(md.contains("# SENTINEL Benchmark Report"));
        QVERIFY(md.contains("| Metric |"));
        QVERIFY(md.contains("PAI @ 5%"));
        QVERIFY(md.contains("AUC-ROC"));
    }

    // ── provenanceToJson ─────────────────────────────────────────────────

    void testProvenanceJsonFields()
    {
        ProvenanceEntry e;
        e.timestamp = QDateTime(QDate(2024, 6, 15), QTime(12, 0), Qt::UTC);
        e.eventId   = QStringLiteral("PROV_EVT");
        e.stage     = QStringLiteral("model");
        e.action    = QStringLiteral("predict");
        e.detail    = QStringLiteral("ran GP");
        e.dataHash  = QStringLiteral("abc123");

        const QJsonArray arr = DataExporter::provenanceToJson({e});
        QCOMPARE(arr.size(), 1);
        const QJsonObject obj = arr.first().toObject();
        QCOMPARE(obj["eventId"].toString(),  QStringLiteral("PROV_EVT"));
        QCOMPARE(obj["stage"].toString(),    QStringLiteral("model"));
        QCOMPARE(obj["dataHash"].toString(), QStringLiteral("abc123"));
    }

    void testProvenanceJsonEmpty()
    {
        QVERIFY(DataExporter::provenanceToJson({}).isEmpty());
    }
};

QTEST_GUILESS_MAIN(DataExporterDeep3Test)
#include "test_data_exporter_deep3.moc"
