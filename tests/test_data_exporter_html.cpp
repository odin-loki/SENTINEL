// test_data_exporter_html.cpp
// Tests the HTML export methods of DataExporter.
#include <QTest>
#include <QTimeZone>
#include "core/DataExporter.h"
#include "core/CrimeEvent.h"

static InvestigativeLead makeLead(int rank, const QString& category,
                                   const QString& headline, double conf = 0.75)
{
    InvestigativeLead l;
    l.rank     = rank;
    l.category = category;
    l.headline = headline;
    l.confidence = conf;
    l.generatedAt = QDateTime(QDate(2024, 6, 10), QTime(12, 0, 0), QTimeZone::utc());
    return l;
}

static CrimeEvent makeEvent(const QString& id, const QString& type)
{
    CrimeEvent ev;
    ev.eventId   = id;
    ev.crimeType = type;
    ev.suburb    = QStringLiteral("Shoreditch");
    ev.lat       = 51.5074;
    ev.lon       = -0.1278;
    ev.occurredAt = QDateTime(QDate(2024, 3, 5), QTime(23, 0, 0), QTimeZone::utc());
    return ev;
}

class TestDataExporterHtml : public QObject
{
    Q_OBJECT

private slots:

    // ── 1. Empty leads → DOCTYPE + table present ──────────────────────────────
    void testLeadsToHtmlEmpty()
    {
        const QString html = DataExporter::leadsToHtml({});
        QVERIFY(html.contains(QStringLiteral("<!DOCTYPE html>")));
        QVERIFY(html.contains(QStringLiteral("<table")));
        QVERIFY(html.contains(QStringLiteral("</table>")));
    }

    // ── 2. Non-empty leads → categories appear in output ─────────────────────
    void testLeadsToHtmlNonEmpty()
    {
        QVector<InvestigativeLead> leads;
        leads.append(makeLead(1, QStringLiteral("series_linkage"),
                              QStringLiteral("Event linked to SER-001"), 0.80));
        leads.append(makeLead(2, QStringLiteral("mo_similarity"),
                              QStringLiteral("Case CASE-007 shares MO"), 0.65));
        leads.append(makeLead(3, QStringLiteral("network_association"),
                              QStringLiteral("Person PERSON-A linked"), 0.50));

        const QString html = DataExporter::leadsToHtml(leads);
        QVERIFY(html.contains(QStringLiteral("series_linkage")));
        QVERIFY(html.contains(QStringLiteral("mo_similarity")));
        QVERIFY(html.contains(QStringLiteral("network_association")));
    }

    // ── 3. HTML special chars in category are escaped ─────────────────────────
    void testLeadsToHtmlEscaping()
    {
        QVector<InvestigativeLead> leads;
        InvestigativeLead l;
        l.rank     = 1;
        l.category = QStringLiteral("<script>xss</script>");
        l.confidence = 0.5;
        l.generatedAt = QDateTime(QDate(2024, 6, 10), QTime(12, 0, 0), QTimeZone::utc());
        leads.append(l);
        const QString html = DataExporter::leadsToHtml(leads);
        QVERIFY2(!html.contains(QStringLiteral("<script>")),
                 "Script tags must be escaped in HTML output");
        QVERIFY(html.contains(QStringLiteral("&lt;script&gt;")));
    }

    // ── 4. Empty events → valid HTML ─────────────────────────────────────────
    void testEventsToHtmlEmpty()
    {
        const QString html = DataExporter::eventsToHtml({});
        QVERIFY(html.contains(QStringLiteral("<!DOCTYPE html>")));
        QVERIFY(html.contains(QStringLiteral("<table")));
    }

    // ── 5. Non-empty events → eventIds present ───────────────────────────────
    void testEventsToHtmlNonEmpty()
    {
        QVector<CrimeEvent> evs;
        evs.append(makeEvent(QStringLiteral("EV-001"), QStringLiteral("burglary")));
        evs.append(makeEvent(QStringLiteral("EV-002"), QStringLiteral("robbery")));
        evs.append(makeEvent(QStringLiteral("EV-003"), QStringLiteral("theft")));

        const QString html = DataExporter::eventsToHtml(evs);
        QVERIFY(html.contains(QStringLiteral("EV-001")));
        QVERIFY(html.contains(QStringLiteral("EV-002")));
        QVERIFY(html.contains(QStringLiteral("EV-003")));
    }

    // ── 6. Starts with DOCTYPE ────────────────────────────────────────────────
    void testEventsToHtmlHasDoctype()
    {
        const QString html = DataExporter::eventsToHtml({});
        QVERIFY2(html.startsWith(QStringLiteral("<!DOCTYPE html>")),
                 "HTML output must start with <!DOCTYPE html>");
    }

    // ── 7. Custom title appears in leads HTML ─────────────────────────────────
    void testLeadsToHtmlHasTitle()
    {
        const QString title = QStringLiteral("My Custom Report");
        const QString html  = DataExporter::leadsToHtml({}, title);
        QVERIFY2(html.contains(title),
                 "Custom title should appear in HTML output");
    }

    // ── 8. Custom title appears in events HTML ────────────────────────────────
    void testEventsToHtmlHasTitle()
    {
        const QString title = QStringLiteral("Events Export 2024");
        const QString html  = DataExporter::eventsToHtml({}, title);
        QVERIFY2(html.contains(title),
                 "Custom title should appear in events HTML output");
    }

    // ── 9. Confidence value appears in leads HTML ─────────────────────────────
    void testLeadsToHtmlConfidencePercent()
    {
        QVector<InvestigativeLead> leads;
        leads.append(makeLead(1, QStringLiteral("test"), QStringLiteral("Test headline"), 0.856));
        const QString html = DataExporter::leadsToHtml(leads);
        // The confidence value should appear in the HTML in some numeric form
        QVERIFY2(html.contains(QStringLiteral("0.856")) || html.contains(QStringLiteral("85.6")),
                 "Confidence should appear as a numeric value in HTML output");
    }

    // ── 10. Crime type appears in events HTML ─────────────────────────────────
    void testEventsToHtmlCrimeType()
    {
        QVector<CrimeEvent> evs;
        evs.append(makeEvent(QStringLiteral("EV-X"), QStringLiteral("violent_crime")));
        const QString html = DataExporter::eventsToHtml(evs);
        QVERIFY(html.contains(QStringLiteral("violent_crime")));
    }
};

QTEST_MAIN(TestDataExporterHtml)
#include "test_data_exporter_html.moc"
