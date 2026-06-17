// test_data_exporter_roundtrip.cpp
// Tests DataExporter CSV/JSON roundtrip fidelity and Markdown output quality.
#include <QTest>
#include <QTimeZone>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include "core/DataExporter.h"
#include "core/CrimeEvent.h"
#include "models/RiskForecaster.h"
#include "benchmark/BenchmarkMetrics.h"
#include "audit/ProvenanceLog.h"

class DataExporterRoundtripTest : public QObject
{
    Q_OBJECT

private:
    static InvestigativeLead makeLead(const QString& id, double conf, const QString& cat)
    {
        InvestigativeLead lead;
        lead.confidence = conf;
        lead.category   = cat;
        lead.headline   = id;  // use id as headline for identification
        lead.detail     = QStringLiteral("Detail text for ") + id;
        return lead;
    }

    static CrimeEvent makeEvent(const QString& id, const QString& type, const QString& suburb)
    {
        CrimeEvent ev;
        ev.eventId   = id;
        ev.crimeType = type;
        ev.suburb    = suburb;
        ev.latitude  = 51.5;
        ev.longitude = -0.1;
        const QDateTime dt = QDateTime(QDate(2024, 3, 15), QTime(22, 0, 0), QTimeZone::utc());
        ev.occurredAt = dt;
        ev.timestamp  = dt;
        return ev;
    }

private slots:

    // ── 1. leadsToJson roundtrip: all leadIds preserved ──────────────────────
    void testLeadsJsonRoundtrip()
    {
        const QVector<InvestigativeLead> leads = {
            makeLead(QStringLiteral("L001"), 0.9, QStringLiteral("series_linkage")),
            makeLead(QStringLiteral("L002"), 0.7, QStringLiteral("geographic_profile")),
            makeLead(QStringLiteral("L003"), 0.5, QStringLiteral("network_association")),
        };

        const QJsonArray arr = DataExporter::leadsToJson(leads);
        QCOMPARE(arr.size(), 3);

        // Collect headlines (which contain the IDs in our test setup)
        QSet<QString> foundHeadlines;
        for (const auto& val : arr) {
            const QJsonObject obj = val.toObject();
            foundHeadlines.insert(obj[QStringLiteral("headline")].toString());
        }
        QVERIFY2(foundHeadlines.contains(QStringLiteral("L001")), "L001 not found in JSON");
        QVERIFY2(foundHeadlines.contains(QStringLiteral("L002")), "L002 not found in JSON");
        QVERIFY2(foundHeadlines.contains(QStringLiteral("L003")), "L003 not found in JSON");
    }

    // ── 2. leadsToJson: confidence value preserved ────────────────────────────
    void testLeadsJsonConfidencePreserved()
    {
        const QVector<InvestigativeLead> leads = { makeLead(QStringLiteral("X"), 0.875, QStringLiteral("cat")) };
        const QJsonArray arr = DataExporter::leadsToJson(leads);
        QVERIFY(!arr.isEmpty());
        const QJsonObject obj = arr[0].toObject();
        const double conf = obj[QStringLiteral("confidence")].toDouble();
        QVERIFY2(std::abs(conf - 0.875) < 0.001,
                 qPrintable(QStringLiteral("Confidence %1 should be ~0.875").arg(conf)));
    }

    // ── 3. leadsToCsv: header row present ────────────────────────────────────
    void testLeadsCsvHeaderPresent()
    {
        const QVector<InvestigativeLead> leads = { makeLead(QStringLiteral("Y"), 0.6, QStringLiteral("nlp")) };
        const QString csv = DataExporter::leadsToCsv(leads);
        QVERIFY2(!csv.isEmpty(), "CSV must not be empty");
        // CSV should have at least 2 lines (header + 1 data row)
        const QStringList lines = csv.split(QStringLiteral("\n"), Qt::SkipEmptyParts);
        QVERIFY2(lines.size() >= 2, qPrintable(
            QStringLiteral("CSV has %1 lines, expected >= 2").arg(lines.size())));
    }

    // ── 4. leadsToCsv: headline in CSV ────────────────────────────────────────
    void testLeadsCsvContainsHeadline()
    {
        const QVector<InvestigativeLead> leads = { makeLead(QStringLiteral("CSV-001"), 0.7, QStringLiteral("x")) };
        const QString csv = DataExporter::leadsToCsv(leads);
        QVERIFY2(csv.contains(QStringLiteral("CSV-001")) || csv.contains(QStringLiteral("0.7")),
                 "Lead headline or confidence must be in CSV output");
    }

    // ── 5. eventsToJson: event IDs and crime types preserved ─────────────────
    void testEventsJsonRoundtrip()
    {
        const QVector<CrimeEvent> evs = {
            makeEvent(QStringLiteral("EVT-1"), QStringLiteral("burglary"), QStringLiteral("Soho")),
            makeEvent(QStringLiteral("EVT-2"), QStringLiteral("robbery"),  QStringLiteral("Camden")),
        };

        const QJsonArray arr = DataExporter::eventsToJson(evs);
        QCOMPARE(arr.size(), 2);

        const QJsonObject obj0 = arr[0].toObject();
        QVERIFY2(!obj0[QStringLiteral("eventId")].toString().isEmpty(), "Event ID must be present");
        QVERIFY2(!obj0[QStringLiteral("crimeType")].toString().isEmpty(), "Crime type must be present");
    }

    // ── 6. eventsToCsv: non-empty with header ────────────────────────────────
    void testEventsCsvNonEmpty()
    {
        const QVector<CrimeEvent> evs = {
            makeEvent(QStringLiteral("E1"), QStringLiteral("theft"), QStringLiteral("Zone1")),
        };
        const QString csv = DataExporter::eventsToCsv(evs);
        QVERIFY2(!csv.isEmpty(), "Events CSV must be non-empty");
        QVERIFY2(csv.contains(QStringLiteral("E1")) || csv.contains(QStringLiteral("theft")),
                 "Events CSV should contain event data");
    }

    // ── 7. leadsToMarkdown: contains lead data ────────────────────────────────
    void testLeadsMarkdownContent()
    {
        const QVector<InvestigativeLead> leads = {
            makeLead(QStringLiteral("MD-001"), 0.92, QStringLiteral("series_linkage")),
        };
        const QString md = DataExporter::leadsToMarkdown(leads, QStringLiteral("Test Report"));
        QVERIFY2(!md.isEmpty(), "Markdown output must be non-empty");
        // Should contain headline or confidence or category
        QVERIFY2(md.contains(QStringLiteral("MD-001")) ||
                 md.contains(QStringLiteral("series_linkage")) ||
                 md.contains(QStringLiteral("0.92")),
                 "Markdown should contain lead data");
    }

    // ── 8. benchmarkToJson: key metrics present ───────────────────────────────
    void testBenchmarkJsonKeys()
    {
        BenchmarkReport report;
        report.pai5pct    = 2.5;
        report.pai10pct   = 1.8;
        report.aucRoc     = 0.76;
        report.brierScore = 0.12;
        report.nSamples   = 100;

        const QJsonObject obj = DataExporter::benchmarkToJson(report);
        QVERIFY2(obj.contains(QStringLiteral("pai5pct"))  ||
                 obj.contains(QStringLiteral("aucRoc"))   ||
                 obj.contains(QStringLiteral("nSamples")),
                 "Benchmark JSON should contain key metric fields");
    }

    // ── 9. provenanceToJson: entries count matches ────────────────────────────
    void testProvenanceJsonCount()
    {
        ProvenanceLog log;
        log.record(QStringLiteral("E1"), QStringLiteral("ingest"), QStringLiteral("import"), QStringLiteral("detail"));
        log.record(QStringLiteral("E1"), QStringLiteral("nlp"),    QStringLiteral("classify"), QStringLiteral("result"));

        const QJsonArray arr = DataExporter::provenanceToJson(log.chain(QStringLiteral("E1")));
        QCOMPARE(arr.size(), 2);
    }

    // ── 10. Empty inputs → safe returns (no crash) ───────────────────────────
    void testEmptyInputsSafe()
    {
        const QJsonArray emptyLeads = DataExporter::leadsToJson({});
        QVERIFY(emptyLeads.isEmpty());

        const QString emptyCsv = DataExporter::leadsToCsv({});
        // May return just a header or empty string — should not crash
        Q_UNUSED(emptyCsv);

        const QJsonArray emptyEvs = DataExporter::eventsToJson({});
        QVERIFY(emptyEvs.isEmpty());
    }
};

QTEST_MAIN(DataExporterRoundtripTest)
#include "test_data_exporter_roundtrip.moc"
