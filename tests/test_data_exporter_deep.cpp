#include <QtTest>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include "core/DataExporter.h"
#include "core/CrimeEvent.h"
#include "audit/ProvenanceLog.h"

class TestDataExporterDeep : public QObject
{
    Q_OBJECT

private:
    static CrimeEvent makeEvent(const QString& id, const QString& type,
                                 const QString& suburb = "TestSuburb",
                                 double lat = 51.5, double lon = -0.1)
    {
        CrimeEvent ev;
        ev.eventId   = id;
        ev.crimeType = type;
        ev.suburb    = suburb;
        ev.lat       = lat;
        ev.lon       = lon;
        ev.occurredAt = QDateTime(QDate(2024, 1, 15), QTime(10, 30), Qt::UTC);
        ev.outcome    = "Under investigation";
        ev.qualityScore = 0.95;
        return ev;
    }

    static InvestigativeLead makeLead(int rank, const QString& cat,
                                       const QString& headline,
                                       double confidence = 0.85,
                                       const QString& method = "Bayesian")
    {
        InvestigativeLead lead;
        lead.rank             = rank;
        lead.category         = cat;
        lead.headline         = headline;
        lead.detail           = "Detail text for " + headline;
        lead.confidence       = confidence;
        lead.confidenceMethod = method;
        lead.generatedAt      = QDateTime::currentDateTimeUtc();
        return lead;
    }

private slots:
    // ── JSON export ────────────────────────────────────────────────────────────

    void testEventsToJsonIsValidJson()
    {
        QVector<CrimeEvent> events;
        events.append(makeEvent("E001", "Burglary"));
        events.append(makeEvent("E002", "Theft", "Suburb B", 52.0, -1.0));

        QJsonArray arr = DataExporter::eventsToJson(events);
        QCOMPARE(arr.size(), 2);

        QJsonDocument doc(arr);
        QVERIFY(!doc.isNull());
        QVERIFY(doc.isArray());

        QJsonObject obj0 = arr[0].toObject();
        QCOMPARE(obj0["eventId"].toString(), QStringLiteral("E001"));
        QCOMPARE(obj0["crimeType"].toString(), QStringLiteral("Burglary"));
        QVERIFY(obj0.contains("lat"));
        QVERIFY(obj0.contains("lon"));
    }

    void testEventsToJsonEmptyList()
    {
        QJsonArray arr = DataExporter::eventsToJson({});
        QCOMPARE(arr.size(), 0);
        QJsonDocument doc(arr);
        QVERIFY(doc.isArray());
        QCOMPARE(doc.array().size(), 0);
    }

    void testLeadsToJsonContainsAllFields()
    {
        QVector<InvestigativeLead> leads;
        leads.append(makeLead(1, "Geographic", "Suspect near crime scene"));

        QJsonArray arr = DataExporter::leadsToJson(leads);
        QCOMPARE(arr.size(), 1);

        QJsonObject obj = arr[0].toObject();
        QVERIFY(obj.contains("rank"));
        QVERIFY(obj.contains("category"));
        QVERIFY(obj.contains("headline"));
        QVERIFY(obj.contains("confidence"));
        QVERIFY(obj.contains("confidenceMethod"));
        QVERIFY(obj.contains("provenance"));
        QVERIFY(obj.contains("contradictions"));
    }

    // ── CSV export ─────────────────────────────────────────────────────────────

    void testEventsToCsvHasHeaderRow()
    {
        QString csv = DataExporter::eventsToCsv({});
        QVERIFY(csv.startsWith("event_id,crime_type,suburb,lat,lon,occurred_at,outcome,quality_score"));
    }

    void testEventsToCsvEscapesCommasInFields()
    {
        CrimeEvent ev = makeEvent("E001", "Theft, Burglary");
        QString csv = DataExporter::eventsToCsv({ev});
        QVERIFY2(csv.contains("\"Theft, Burglary\""),
                 "comma in crimeType must be quoted in CSV");
    }

    void testEventsToCsvEscapesQuotesInFields()
    {
        CrimeEvent ev = makeEvent("E001", "Assault");
        ev.outcome = "Outcome \"A\"";
        QString csv = DataExporter::eventsToCsv({ev});
        QVERIFY2(csv.contains("\"Outcome \"\"A\"\"\""),
                 "embedded quotes must be doubled in CSV");
    }

    void testLeadsToCsvHasHeaderRow()
    {
        QString csv = DataExporter::leadsToCsv({});
        QVERIFY(csv.startsWith("rank,category,headline,detail,confidence,confidence_method,generated_at"));
    }

    // ── Markdown export ─────────────────────────────────────────────────────────

    void testLeadsToMarkdownHasCorrectColumnCount()
    {
        QVector<InvestigativeLead> leads;
        leads.append(makeLead(1, "Geographic", "Suspect identified"));

        QString md = DataExporter::leadsToMarkdown(leads);
        // Find the header separator line and count columns
        QStringList lines = md.split('\n');
        bool foundSep = false;
        for (const QString& line : lines) {
            if (line.contains("---")) {
                // Count the number of | separators in the separator row
                int pipes = line.count('|');
                QVERIFY2(pipes >= 6, "Expected at least 6 pipe characters in separator (5 columns)");
                foundSep = true;
                break;
            }
        }
        QVERIFY2(foundSep, "Markdown table separator line not found");
    }

    void testLeadsToMarkdownEscapesPipeInMethod()
    {
        InvestigativeLead lead = makeLead(1, "MO Analysis", "Repeat offender",
                                           0.75, "Bayesian|TF-IDF");
        QString md = DataExporter::leadsToMarkdown({lead});

        // The confidenceMethod "Bayesian|TF-IDF" should have the pipe replaced with /
        QVERIFY2(!md.contains("Bayesian|TF-IDF"),
                 "Pipe in confidenceMethod must be escaped in Markdown table");
        QVERIFY2(md.contains("Bayesian/TF-IDF"),
                 "Pipe in confidenceMethod should be replaced with /");
    }

    void testLeadsToMarkdownEscapesPipeInCategory()
    {
        InvestigativeLead lead = makeLead(1, "Geographic|Temporal", "Cluster detected", 0.9);
        QString md = DataExporter::leadsToMarkdown({lead});
        QVERIFY2(!md.contains("Geographic|Temporal"),
                 "Pipe in category must be escaped");
        QVERIFY2(md.contains("Geographic/Temporal"),
                 "Pipe in category should become /");
    }

    void testLeadsToMarkdownEscapesPipeInHeadline()
    {
        InvestigativeLead lead = makeLead(1, "Suspect", "Zone A|Zone B overlap", 0.8);
        QString md = DataExporter::leadsToMarkdown({lead});

        // Find the table rows only (before the ## Details section)
        int detailsPos = md.indexOf(QStringLiteral("## Details"));
        QString tableSection = (detailsPos > 0) ? md.left(detailsPos) : md;

        QVERIFY2(!tableSection.contains("Zone A|Zone B"),
                 "Pipe in headline must be escaped in the Markdown table section");
        QVERIFY2(tableSection.contains("Zone A/Zone B"),
                 "Pipe in headline should become / in the table section");
    }

    void testLeadsToMarkdownEmptyProducesValidTable()
    {
        QString md = DataExporter::leadsToMarkdown({});
        QVERIFY(!md.isEmpty());
        QVERIFY(md.contains("|"));
    }

    // ── HTML export ─────────────────────────────────────────────────────────────

    void testEventsToHtmlEscapesLtGt()
    {
        CrimeEvent ev = makeEvent("<script>alert(1)</script>", "XSS Test");
        QString html = DataExporter::eventsToHtml({ev});
        QVERIFY2(!html.contains("<script>"), "< must be HTML-escaped");
        QVERIFY2(html.contains("&lt;script&gt;"), "< should become &lt;");
    }

    void testEventsToHtmlEscapesAmpersand()
    {
        CrimeEvent ev = makeEvent("E001&E002", "Theft");
        QString html = DataExporter::eventsToHtml({ev});
        QVERIFY2(!html.contains("E001&E002") || html.contains("E001&amp;E002"),
                 "& in event ID should be HTML-escaped");
    }

    void testEventsToHtmlIsWellFormed()
    {
        QVector<CrimeEvent> events;
        events.append(makeEvent("E001", "Burglary"));
        QString html = DataExporter::eventsToHtml(events);
        QVERIFY(html.startsWith("<!DOCTYPE html>") || html.startsWith("<!DOCTYPE HTML>"));
        QVERIFY(html.contains("</html>"));
        QVERIFY(html.contains("<table>"));
        QVERIFY(html.contains("</table>"));
    }

    void testLeadsToHtmlEscapesSpecialChars()
    {
        InvestigativeLead lead = makeLead(1, "Test <script>", "Alert & Notice", 0.9);
        QString html = DataExporter::leadsToHtml({lead});
        QVERIFY2(!html.contains("<script>"), "< in category must be HTML-escaped");
        QVERIFY2(!html.contains("Alert & Notice") || html.contains("Alert &amp; Notice"),
                 "& in headline must be HTML-escaped");
    }

    void testLeadsToHtmlEmptyProducesValidTable()
    {
        QString html = DataExporter::leadsToHtml({});
        QVERIFY(html.contains("<table>"));
        QVERIFY(html.contains("</table>"));
        QVERIFY(html.contains("<tbody>"));
    }

    // ── Benchmark markdown ─────────────────────────────────────────────────────

    void testBenchmarkToMarkdownHasPassWarnLabels()
    {
        BenchmarkReport report;
        report.nSamples  = 100;
        report.pai5pct   = 8.0;   // > 6.0, should PASS
        report.pai10pct  = 3.0;   // < 4.5, should WARN
        report.pai20pct  = 4.0;
        report.pei10pct  = 0.7;
        report.ser       = 0.5;
        report.aucRoc    = 0.9;
        report.brierScore = 0.05;

        QString md = DataExporter::benchmarkToMarkdown(report);
        QVERIFY(md.contains("PASS"));
        QVERIFY(md.contains("WARN"));
    }

    // ── Provenance JSON ───────────────────────────────────────────────────────

    void testProvenanceToJsonContainsRequiredFields()
    {
        ProvenanceEntry entry;
        entry.timestamp = QDateTime::currentDateTimeUtc();
        entry.eventId   = "E001";
        entry.stage     = "Ingestion";
        entry.action    = "Import";
        entry.detail    = "Loaded from CSV";
        entry.dataHash  = "abc123";

        QJsonArray arr = DataExporter::provenanceToJson({entry});
        QCOMPARE(arr.size(), 1);
        QJsonObject obj = arr[0].toObject();
        QVERIFY(obj.contains("timestamp"));
        QVERIFY(obj.contains("eventId"));
        QVERIFY(obj.contains("stage"));
        QVERIFY(obj.contains("action"));
        QVERIFY(obj.contains("detail"));
        QVERIFY(obj.contains("dataHash"));
    }
};

QTEST_GUILESS_MAIN(TestDataExporterDeep)
#include "test_data_exporter_deep.moc"
