#include <QtTest>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonParseError>
#include "core/DataExporter.h"
#include "core/CrimeEvent.h"

static CrimeEvent makeEvent(const QString& id, const QString& type,
                             const QString& outcome = {})
{
    CrimeEvent ev;
    ev.eventId    = id;
    ev.crimeType  = type;
    ev.suburb     = QStringLiteral("Shoreditch");
    ev.lat        = 51.5074;
    ev.lon        = -0.1278;
    ev.occurredAt = QDateTime(QDate(2024, 3, 5), QTime(23, 0, 0), QTimeZone::utc());
    ev.outcome    = outcome;
    ev.qualityScore = 0.75;
    return ev;
}

static InvestigativeLead makeLead(int rank, const QString& cat,
                                   const QString& headline,
                                   double conf = 0.70,
                                   const QString& method = QStringLiteral("bayes"))
{
    InvestigativeLead l;
    l.rank              = rank;
    l.category          = cat;
    l.headline          = headline;
    l.confidence        = conf;
    l.confidenceMethod  = method;
    l.generatedAt       = QDateTime(QDate(2024, 6, 10), QTime(12, 0, 0), QTimeZone::utc());
    return l;
}

class TestExporterDeep2 : public QObject
{
    Q_OBJECT

private slots:

    // ── 1. HTML: < > & " are escaped in event ID and crime type ──────────────
    void testHtmlEscapesSpecialChars()
    {
        QVector<CrimeEvent> evs;
        CrimeEvent ev = makeEvent(QStringLiteral("EV<1>&\"2\""), QStringLiteral("rob&ber<y>"));
        evs.append(ev);

        const QString html = DataExporter::eventsToHtml(evs);

        QVERIFY2(!html.contains(QStringLiteral("<1>")),
                 "Raw < > must be escaped in HTML output");
        QVERIFY2(!html.contains(QStringLiteral("rob&ber<y>")),
                 "Raw & and < must be escaped in crime type");
        QVERIFY2(html.contains(QStringLiteral("&lt;")),
                 "< must be encoded as &lt;");
        QVERIFY2(html.contains(QStringLiteral("&gt;")),
                 "> must be encoded as &gt;");
        QVERIFY2(html.contains(QStringLiteral("&amp;")),
                 "& must be encoded as &amp;");
    }

    // ── 2. HTML leads: & in category is escaped ───────────────────────────────
    void testHtmlLeadsAmpersandEscaped()
    {
        QVector<InvestigativeLead> leads;
        leads.append(makeLead(1, QStringLiteral("A&B"), QStringLiteral("Link A&B found")));
        const QString html = DataExporter::leadsToHtml(leads);
        QVERIFY2(!html.contains(QStringLiteral("A&B")),
                 "Raw & must not appear literally in leads HTML");
        QVERIFY2(html.contains(QStringLiteral("A&amp;B")),
                 "& must be encoded as &amp; in leads HTML");
    }

    // ── 3. HTML leads: " is escaped ───────────────────────────────────────────
    void testHtmlLeadsQuoteEscaped()
    {
        QVector<InvestigativeLead> leads;
        leads.append(makeLead(1, QStringLiteral("cat"), QStringLiteral("He said \"hello\"")));
        const QString html = DataExporter::leadsToHtml(leads);
        QVERIFY2(html.contains(QStringLiteral("&quot;")),
                 "Double-quote must be encoded as &quot; in HTML");
    }

    // ── 4. JSON: eventsToJson produces valid, parseable JSON ─────────────────
    void testJsonEventsParseable()
    {
        QVector<CrimeEvent> evs;
        evs.append(makeEvent(QStringLiteral("J1"), QStringLiteral("burglary")));
        evs.append(makeEvent(QStringLiteral("J2"), QStringLiteral("theft")));

        const QJsonArray arr = DataExporter::eventsToJson(evs);
        QVERIFY2(!arr.isEmpty(), "eventsToJson should return non-empty array");

        const QByteArray bytes = QJsonDocument(arr).toJson(QJsonDocument::Compact);
        QJsonParseError err;
        const QJsonDocument parsed = QJsonDocument::fromJson(bytes, &err);
        QVERIFY2(err.error == QJsonParseError::NoError,
                 qPrintable(QStringLiteral("JSON parse error: %1").arg(err.errorString())));
        QVERIFY2(parsed.isArray(), "Parsed JSON must be an array");
        QCOMPARE(parsed.array().size(), 2);
    }

    // ── 5. JSON: each event object has expected keys ──────────────────────────
    void testJsonEventsHaveKeys()
    {
        QVector<CrimeEvent> evs;
        evs.append(makeEvent(QStringLiteral("K1"), QStringLiteral("assault")));
        const QJsonArray arr = DataExporter::eventsToJson(evs);
        QVERIFY2(arr.size() == 1, "Expected exactly 1 element");
        const QJsonObject obj = arr[0].toObject();
        QVERIFY(obj.contains(QStringLiteral("eventId")));
        QVERIFY(obj.contains(QStringLiteral("crimeType")));
        QVERIFY(obj.contains(QStringLiteral("lat")));
        QVERIFY(obj.contains(QStringLiteral("lon")));
        QVERIFY(obj.contains(QStringLiteral("occurredAt")));
        QVERIFY(obj.contains(QStringLiteral("quality")));
    }

    // ── 6. CSV: event with comma and quote in outcome is properly escaped ─────
    void testCsvEscapesCommaAndQuote()
    {
        QVector<CrimeEvent> evs;
        CrimeEvent ev = makeEvent(QStringLiteral("C1"), QStringLiteral("theft"));
        ev.outcome = QStringLiteral("pending, review \"officer notes\"");
        evs.append(ev);

        const QString csv = DataExporter::eventsToCsv(evs);

        QVERIFY2(csv.contains(QStringLiteral("\"pending, review")),
                 "Outcome with comma must be quoted in CSV");
        QVERIFY2(csv.contains(QStringLiteral("\"\"officer notes\"\"")),
                 "Double-quotes inside quoted field must be doubled");
    }

    // ── 7. CSV: header row present with correct columns ───────────────────────
    void testCsvHeaderRow()
    {
        const QString csv = DataExporter::eventsToCsv({});
        const QString firstLine = csv.split(QLatin1Char('\n')).first();
        QVERIFY(firstLine.contains(QStringLiteral("event_id")));
        QVERIFY(firstLine.contains(QStringLiteral("crime_type")));
        QVERIFY(firstLine.contains(QStringLiteral("lat")));
        QVERIFY(firstLine.contains(QStringLiteral("lon")));
        QVERIFY(firstLine.contains(QStringLiteral("quality_score")));
    }

    // ── 8. Markdown: header row has 5 columns (leads format) ─────────────────
    void testMarkdownLeadsColumnCount()
    {
        QVector<InvestigativeLead> leads;
        leads.append(makeLead(1, QStringLiteral("series"), QStringLiteral("Pattern found")));

        const QString md = DataExporter::leadsToMarkdown(leads);
        const QStringList lines = md.split(QLatin1Char('\n'));

        QString headerLine;
        for (const QString& line : lines) {
            if (line.trimmed().startsWith(QLatin1Char('|')) &&
                line.contains(QStringLiteral("Rank"))) {
                headerLine = line;
                break;
            }
        }
        QVERIFY2(!headerLine.isEmpty(), "Markdown must have a header row starting with |");

        // Count pipe separators: 5 columns → 6 pipes in "| A | B | C | D | E |"
        const int pipeCount = headerLine.count(QLatin1Char('|'));
        QVERIFY2(pipeCount == 6,
                 qPrintable(QStringLiteral("Expected 6 pipes for 5 columns, got %1 in: %2")
                     .arg(pipeCount).arg(headerLine)));
    }

    // ── 9. Markdown: separator row has correct dashes ─────────────────────────
    void testMarkdownHasSeparatorRow()
    {
        const QString md = DataExporter::leadsToMarkdown(
            { makeLead(1, QStringLiteral("c"), QStringLiteral("h")) });
        QVERIFY2(md.contains(QStringLiteral("|---")),
                 "Markdown must have a separator row with |---");
    }

    // ── 10. Empty events → valid HTML with DOCTYPE and table ──────────────────
    void testEmptyEventsHtmlValid()
    {
        const QString html = DataExporter::eventsToHtml({});
        QVERIFY2(!html.isEmpty(), "Empty events HTML must not be empty string");
        QVERIFY2(html.startsWith(QStringLiteral("<!DOCTYPE html>")),
                 "Must start with <!DOCTYPE html>");
        QVERIFY2(html.contains(QStringLiteral("<table")), "Must contain <table");
        QVERIFY2(html.contains(QStringLiteral("</table>")), "Must contain </table>");
        QVERIFY2(html.contains(QStringLiteral("</html>")), "Must close with </html>");
    }

    // ── 11. Empty events → valid JSON array (empty) ───────────────────────────
    void testEmptyEventsJsonIsArray()
    {
        const QJsonArray arr = DataExporter::eventsToJson({});
        QVERIFY2(arr.isEmpty(), "Empty events must produce empty JSON array");

        const QByteArray bytes = QJsonDocument(arr).toJson(QJsonDocument::Compact);
        QJsonParseError err;
        QJsonDocument::fromJson(bytes, &err);
        QVERIFY2(err.error == QJsonParseError::NoError, "Empty JSON array must be parseable");
    }

    // ── 12. Empty events → CSV has only header row ────────────────────────────
    void testEmptyEventsCsvHeaderOnly()
    {
        const QString csv = DataExporter::eventsToCsv({});
        QVERIFY2(!csv.isEmpty(), "Empty events CSV must not be empty");
        const QStringList lines = csv.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        QCOMPARE(lines.size(), 1);
    }

    // ── 13. Empty leads → Markdown has header row but no data rows ────────────
    void testEmptyLeadsMarkdownHeaderPresent()
    {
        const QString md = DataExporter::leadsToMarkdown({});
        QVERIFY2(md.contains(QStringLiteral("| Rank")),
                 "Empty leads Markdown must still have a header row");
        QVERIFY2(!md.contains(QStringLiteral("| 1 |")),
                 "Empty leads Markdown must not have data rows");
    }

    // ── 14. HTML: <script> injection is fully escaped ─────────────────────────
    void testHtmlXssScriptTagEscaped()
    {
        QVector<CrimeEvent> evs;
        CrimeEvent ev = makeEvent(QStringLiteral("<script>alert(1)</script>"),
                                   QStringLiteral("xss<test>"));
        evs.append(ev);

        const QString html = DataExporter::eventsToHtml(evs);
        QVERIFY2(!html.contains(QStringLiteral("<script>")),
                 "<script> tag must be escaped in HTML event output");
    }
};

QTEST_GUILESS_MAIN(TestExporterDeep2)
#include "test_exporter_deep2.moc"
