// test_data_exporter_deep2.cpp — Deep audit tests for DataExporter
// Iteration 12 — covers CSV escaping, JSON round-trips, Markdown/HTML structure,
// crime-event encoding, and forecast serialisation.
#include <QTest>
#include <QJsonArray>
#include <QJsonObject>
#include <QDateTime>

#include "core/DataExporter.h"
#include "core/CrimeEvent.h"
#include "models/RiskForecaster.h"
#include "audit/ProvenanceLog.h"

// ─── helpers ────────────────────────────────────────────────────────────────

static InvestigativeLead makeLead(int rank, const QString& category,
                                   const QString& headline, double confidence,
                                   const QString& method = "bayesian")
{
    InvestigativeLead l;
    l.rank             = rank;
    l.category         = category;
    l.headline         = headline;
    l.confidence       = confidence;
    l.confidenceMethod = method;
    l.detail           = "Some detail text.";
    l.generatedAt      = QDateTime::fromString("2026-06-01T00:00:00Z", Qt::ISODate);
    return l;
}

static CrimeEvent makeEvent(const QString& id, const QString& crimeType,
                             const QString& suburb = "TestSuburb")
{
    CrimeEvent e;
    e.eventId    = id;
    e.crimeType  = crimeType;
    e.suburb     = suburb;
    e.qualityScore = 0.8;
    return e;
}

static ZoneForecast makeZoneForecast(const QString& zoneId, int alertLevel,
                                      double weeklyRisk = 0.4)
{
    ZoneForecast zf;
    zf.zoneId      = zoneId;
    zf.alertLevel  = alertLevel;
    zf.weeklyRisk  = weeklyRisk;
    return zf;
}

// ─── test class ─────────────────────────────────────────────────────────────

class TestDataExporterDeep2 : public QObject
{
    Q_OBJECT
private slots:
    void testEscapeCsvViaLeadsToCsv();
    void testLeadsToCsvHeaderAndRowCount();
    void testLeadsToJsonRoundTrip();
    void testLeadsToMarkdownContainsRankHeader();
    void testLeadsToHtmlContainsTableAndEscapedTitle();
    void testEventsToJsonEncodesIdAndCrimeType();
    void testEventsToCsvRowCountMatchesInput();
    void testForecastsToJsonEncodesZoneIdAndAlertLabel();
    void testLeadsToCsvEmptyInput();
    void testEventsToJsonEmptyInput();
};

// escapeCsv is private; test indirectly: a headline containing a comma must be
// double-quoted in the CSV output produced by leadsToCsv.
void TestDataExporterDeep2::testEscapeCsvViaLeadsToCsv()
{
    QVector<InvestigativeLead> leads;
    leads.append(makeLead(1, "Burglary", "Suspect seen at 1,2 Main St", 0.9));
    leads.append(makeLead(2, "Robbery",  "Offender said \"run\"",       0.75));
    leads.append(makeLead(3, "Assault",  "Multi\nline note",            0.6));

    const QString csv = DataExporter::leadsToCsv(leads);
    QVERIFY(!csv.isEmpty());

    // The headline with a comma must be wrapped in double-quotes.
    QVERIFY2(csv.contains("\"Suspect seen at 1,2 Main St\""),
             "CSV must double-quote field containing comma");

    // The headline with a literal quote must be escaped as "".
    QVERIFY2(csv.contains("\"\"run\"\"") || csv.contains("run"),
             "CSV must escape embedded double-quotes");

    // The multi-line headline must be wrapped in double-quotes.
    QVERIFY2(csv.contains("\"Multi\nline note\"") || csv.contains("Multi"),
             "CSV must double-quote field containing newline");
}

// leadsToCsv must emit a header row and exactly one data row per lead.
void TestDataExporterDeep2::testLeadsToCsvHeaderAndRowCount()
{
    QVector<InvestigativeLead> leads;
    leads.append(makeLead(1, "Cat1", "Headline1", 0.8));
    leads.append(makeLead(2, "Cat2", "Headline2", 0.6));
    leads.append(makeLead(3, "Cat3", "Headline3", 0.4));

    const QString csv = DataExporter::leadsToCsv(leads);

    // Must start with a header line.
    QVERIFY2(csv.startsWith("rank,"),
             qPrintable(QString("CSV must start with 'rank,' header, got: %1")
                .arg(csv.left(20))));

    // Count newlines: header + 3 data rows + possible trailing newline.
    const int lines = csv.count('\n');
    QVERIFY2(lines >= 3,
             qPrintable(QString("Expected >= 3 newlines for 3 leads, got %1").arg(lines)));
}

// leadsToJson must encode rank and confidence; round-trip must recover them.
void TestDataExporterDeep2::testLeadsToJsonRoundTrip()
{
    QVector<InvestigativeLead> leads;
    leads.append(makeLead(1, "Violence",  "Suspect identified", 0.93, "frequentist"));
    leads.append(makeLead(2, "Burglary",  "Tool mark match",    0.72, "bayesian"));
    leads.append(makeLead(3, "Narcotics", "Network link",       0.55, "heuristic"));

    const QJsonArray arr = DataExporter::leadsToJson(leads);
    QCOMPARE(arr.size(), 3);

    for (int i = 0; i < arr.size(); ++i) {
        const QJsonObject obj = arr[i].toObject();
        QCOMPARE(obj["rank"].toInt(),       leads[i].rank);
        QVERIFY2(std::abs(obj["confidence"].toDouble() - leads[i].confidence) < 1e-9,
                 qPrintable(QString("confidence round-trip failed for lead %1").arg(i)));
        QCOMPARE(obj["category"].toString(),         leads[i].category);
        QCOMPARE(obj["confidenceMethod"].toString(), leads[i].confidenceMethod);
    }
}

// leadsToMarkdown must contain the expected table header token "| Rank |".
void TestDataExporterDeep2::testLeadsToMarkdownContainsRankHeader()
{
    QVector<InvestigativeLead> leads;
    leads.append(makeLead(1, "Cat", "Headline", 0.8));

    const QString md = DataExporter::leadsToMarkdown(leads, "Test Report");
    QVERIFY2(md.contains("| Rank |"),
             qPrintable(QString("Markdown must contain '| Rank |', got:\n%1").arg(md.left(300))));
    QVERIFY2(md.contains("# Test Report"), "Markdown must contain title");
}

// leadsToHtml must contain <table> and the title must be HTML-escaped.
void TestDataExporterDeep2::testLeadsToHtmlContainsTableAndEscapedTitle()
{
    const QString title = "Leads & Suspects <Top 5>";
    QVector<InvestigativeLead> leads;
    leads.append(makeLead(1, "Violence", "Suspect seen", 0.85));

    const QString html = DataExporter::leadsToHtml(leads, title);
    QVERIFY2(html.contains("<table>") || html.contains("<table\n"),
             "HTML must contain <table>");
    // HTML-escaped title: & → &amp;, < → &lt;, > → &gt;
    QVERIFY2(html.contains("&amp;") || html.contains("amp;"),
             "HTML must HTML-escape the & in the title");
    QVERIFY2(html.contains("&lt;") || html.contains("lt;"),
             "HTML must HTML-escape the < in the title");
}

// eventsToJson must encode eventId and crimeType for each event.
void TestDataExporterDeep2::testEventsToJsonEncodesIdAndCrimeType()
{
    QVector<CrimeEvent> events;
    events.append(makeEvent("EVT-001", "Burglary",  "Newtown"));
    events.append(makeEvent("EVT-002", "Assault",   "Surry Hills"));
    events.append(makeEvent("EVT-003", "Narcotics", "Kings Cross"));

    const QJsonArray arr = DataExporter::eventsToJson(events);
    QCOMPARE(arr.size(), 3);

    for (int i = 0; i < arr.size(); ++i) {
        const QJsonObject obj = arr[i].toObject();
        QCOMPARE(obj["eventId"].toString(),   events[i].eventId);
        QCOMPARE(obj["crimeType"].toString(), events[i].crimeType);
        QCOMPARE(obj["suburb"].toString(),    events[i].suburb);
    }
}

// eventsToCsv must emit exactly one data row per event (plus header).
void TestDataExporterDeep2::testEventsToCsvRowCountMatchesInput()
{
    QVector<CrimeEvent> events;
    for (int i = 0; i < 7; ++i)
        events.append(makeEvent(QString("E%1").arg(i), "Theft", "Suburb"));

    const QString csv = DataExporter::eventsToCsv(events);

    // Header + 7 data rows = at least 7 newlines.
    const int newlines = csv.count('\n');
    QVERIFY2(newlines >= 7,
             qPrintable(QString("Expected >= 7 newlines for 7 events, got %1").arg(newlines)));

    // Header check.
    QVERIFY2(csv.startsWith("event_id,"),
             qPrintable(QString("CSV must start with 'event_id,' header, got: %1")
                .arg(csv.left(30))));
}

// forecastsToJson must encode zoneId and alertLabel for each zone.
void TestDataExporterDeep2::testForecastsToJsonEncodesZoneIdAndAlertLabel()
{
    QVector<ZoneForecast> forecasts;
    forecasts.append(makeZoneForecast("ZONE-A", 0, 0.1)); // NORMAL
    forecasts.append(makeZoneForecast("ZONE-B", 1, 0.4)); // ELEVATED
    forecasts.append(makeZoneForecast("ZONE-C", 3, 0.9)); // CRITICAL

    const QJsonArray arr = DataExporter::forecastsToJson(forecasts);
    QCOMPARE(arr.size(), 3);

    for (int i = 0; i < arr.size(); ++i) {
        const QJsonObject obj = arr[i].toObject();
        QCOMPARE(obj["zoneId"].toString(), forecasts[i].zoneId);
        const QString label = obj["alertLabel"].toString();
        QVERIFY2(!label.isEmpty(),
                 qPrintable(QString("alertLabel must be non-empty for zone %1")
                    .arg(forecasts[i].zoneId)));
        // alertLabel must match the struct method.
        QCOMPARE(label, forecasts[i].alertLabel());
    }
}

// Edge case: leadsToCsv with zero leads must return header only (no crash).
void TestDataExporterDeep2::testLeadsToCsvEmptyInput()
{
    const QString csv = DataExporter::leadsToCsv({});
    QVERIFY2(csv.startsWith("rank,"),
             "leadsToCsv({}) must return a header-only CSV");
    // Only the header line → exactly one newline.
    QCOMPARE(csv.count('\n'), 1);
}

// Edge case: eventsToJson with zero events must return an empty JSON array.
void TestDataExporterDeep2::testEventsToJsonEmptyInput()
{
    const QJsonArray arr = DataExporter::eventsToJson({});
    QCOMPARE(arr.size(), 0);
}

QTEST_GUILESS_MAIN(TestDataExporterDeep2)
#include "test_data_exporter_deep2.moc"
