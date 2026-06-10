// test_export_completeness.cpp
// Completeness tests for DataExporter: verifies every field, column,
// and section is present across all export formats.
#include <QTest>
#include <QCoreApplication>
#include <QTemporaryFile>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include "core/DataExporter.h"
#include "core/CrimeEvent.h"
#include "models/RiskForecaster.h"
#include "benchmark/BenchmarkMetrics.h"
#include "audit/ProvenanceLog.h"

// ─────────────────────────────────────────────────────────────────────────────
// Test data helpers
// ─────────────────────────────────────────────────────────────────────────────
namespace {

static InvestigativeLead makeFullLead(int rank = 1,
                                       const QString& headline = "Test Headline")
{
    InvestigativeLead l;
    l.rank             = rank;
    l.category         = QStringLiteral("geo");
    l.headline         = headline;
    l.detail           = QStringLiteral("Test detail text");
    l.confidence       = 0.85;
    l.confidenceMethod = QStringLiteral("bayesian");
    l.generatedAt      = QDateTime(QDate(2024, 6, 1), QTime(12, 0, 0), Qt::UTC);
    QJsonObject sd;
    sd["key"] = QStringLiteral("value");
    l.supportingData   = sd;
    l.provenance       = {"source_A", "model_B"};
    l.contradictions   = {"contra_1"};
    return l;
}

static CrimeEvent makeFullEvent(const QString& id = "evt_1")
{
    CrimeEvent e;
    e.eventId      = e.id = id;
    e.crimeType    = QStringLiteral("burglary");
    e.suburb       = QStringLiteral("Melbourne CBD");
    e.lat          = -37.8136;
    e.lon          = 144.9631;
    e.latitude     = -37.8136;
    e.longitude    = 144.9631;
    e.occurredAt   = QDateTime(QDate(2024, 1, 15), QTime(14, 30, 0), Qt::UTC);
    e.outcome      = QStringLiteral("unresolved");
    e.qualityScore = 0.92;
    return e;
}

static ZoneForecast makeZoneForecast(const QString& id,
                                      int nDays,
                                      int alertLevel = 1)
{
    ZoneForecast zf;
    zf.zoneId     = id;
    zf.weeklyRisk = 0.5;
    zf.alertLevel = alertLevel;
    for (int i = 0; i < nDays; ++i) {
        ForecastDay d;
        d.zoneId           = id;
        d.date             = QDate(2024, 1, 1).addDays(i);
        d.riskScore        = 0.3 + 0.05 * i;
        d.baselineProb     = 0.25;
        d.escalationFactor = 1.1;
        d.temporalFactor   = 1.0;
        d.expectedCount    = 0.5;
        d.explanation      = QStringLiteral("test explanation");
        zf.days.append(d);
    }
    return zf;
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// TestExportCompleteness
// ─────────────────────────────────────────────────────────────────────────────
class TestExportCompleteness : public QObject {
    Q_OBJECT

private slots:

    // 1 ── JSON for a lead with all fields set has every required key
    void testLeadsJsonHasAllFields()
    {
        const auto arr = DataExporter::leadsToJson({makeFullLead()});
        QCOMPARE(arr.size(), 1);
        const auto obj = arr[0].toObject();
        QVERIFY(obj.contains("rank"));
        QVERIFY(obj.contains("category"));
        QVERIFY(obj.contains("headline"));
        QVERIFY(obj.contains("detail"));
        QVERIFY(obj.contains("confidence"));
        QVERIFY(obj.contains("confidenceMethod"));
        QVERIFY(obj.contains("generatedAt"));
        QVERIFY(obj.contains("supportingData"));
        QVERIFY(obj.contains("provenance"));
        QVERIFY(obj.contains("contradictions"));
    }

    // 2 ── CSV header starts with the canonical column names
    void testLeadsCsvHasHeader()
    {
        const auto csv = DataExporter::leadsToCsv({});
        QVERIFY(csv.startsWith("rank,category,headline,detail,confidence"));
    }

    // 3 ── 5 leads produce header + 5 data rows = 6 newlines
    void testLeadsCsvRowCount()
    {
        QVector<InvestigativeLead> leads;
        for (int i = 0; i < 5; ++i)
            leads.append(makeFullLead(i + 1));
        const auto csv = DataExporter::leadsToCsv(leads);
        QCOMPARE(csv.count('\n'), 6);
    }

    // 4 ── Headline containing a comma is quoted in CSV
    void testLeadsCsvEscapesCommas()
    {
        auto l = makeFullLead(1, "Lead, with comma");
        const auto csv = DataExporter::leadsToCsv({l});
        QVERIFY(csv.contains(QStringLiteral("\"Lead, with comma\"")));
    }

    // 5 ── Markdown contains the table header row and lead content
    void testLeadsMarkdownTableFormat()
    {
        QVector<InvestigativeLead> leads;
        leads.append(makeFullLead(1, "Alpha Lead"));
        leads.append(makeFullLead(2, "Beta Lead"));
        const auto md = DataExporter::leadsToMarkdown(leads);
        QVERIFY(md.contains("| Rank |"));
        QVERIFY(md.contains("Alpha Lead"));
        QVERIFY(md.contains("Beta Lead"));
    }

    // 6 ── Markdown has a "## Details" section listing lead headlines
    void testLeadsMarkdownDetailsSection()
    {
        const auto md = DataExporter::leadsToMarkdown({makeFullLead()});
        QVERIFY(md.contains("## Details"));
        QVERIFY(md.contains("Test Headline"));
    }

    // 7 ── ZoneForecast with 3 days → JSON "days" array has 3 elements
    void testForecastsJsonHasDaysArray()
    {
        const auto arr = DataExporter::forecastsToJson({makeZoneForecast("z", 3)});
        QCOMPARE(arr.size(), 1);
        QCOMPARE(arr[0].toObject()["days"].toArray().size(), 3);
    }

    // 8 ── 2 zones × 3 days = 6 data rows + 1 header = 7 newlines
    void testForecastsCsvRowPerDay()
    {
        QVector<ZoneForecast> forecasts;
        forecasts.append(makeZoneForecast("zone_A", 3));
        forecasts.append(makeZoneForecast("zone_B", 3));
        const auto csv = DataExporter::forecastsToCsv(forecasts);
        QCOMPARE(csv.count('\n'), 7);
    }

    // 9 ── forecast CSV includes "alert_level" column
    void testForecastsCsvHasAlertLevel()
    {
        const auto csv = DataExporter::forecastsToCsv({});
        QVERIFY(csv.contains("alert_level"));
    }

    // 10 ── Benchmark JSON has all expected metric keys
    void testBenchmarkJsonHasAllMetrics()
    {
        BenchmarkReport rep;
        rep.pai5pct    = 7.0; rep.pai10pct  = 5.0; rep.pai20pct  = 3.5;
        rep.pei10pct   = 0.7; rep.ser       = 0.5;
        rep.aucRoc     = 0.88; rep.aucPr    = 0.75;
        rep.mae        = 0.1;  rep.rmse     = 0.15;
        rep.brierScore = 0.08; rep.nSamples = 100;
        const auto obj = DataExporter::benchmarkToJson(rep);
        const QStringList required = {
            "pai05", "pai10", "pai20", "pei10", "ser",
            "aucRoc", "aucPr", "mae", "rmse", "brierScore", "summary"
        };
        for (const auto& key : required)
            QVERIFY2(obj.contains(key), qPrintable("Missing key: " + key));
    }

    // 11 ── Markdown shows "PASS" for a PAI value above target
    void testBenchmarkMarkdownPassFailStatus()
    {
        BenchmarkReport rep;
        rep.pai5pct    = 8.0;   // target ≥6.0 → PASS
        rep.pai10pct   = 5.0;
        rep.pai20pct   = 3.5;
        rep.pei10pct   = 0.7;
        rep.ser        = 0.5;
        rep.aucRoc     = 0.90;
        rep.brierScore = 0.07;  // target ≤0.10 → PASS
        rep.nSamples   = 100;
        const auto md  = DataExporter::benchmarkToMarkdown(rep);
        QVERIFY(md.contains("PASS"));
    }

    // 12 ── Markdown shows "WARN" when Brier score exceeds 0.10
    void testBenchmarkMarkdownBrierFail()
    {
        BenchmarkReport rep;
        rep.pai5pct    = 3.0;   // below target → WARN on PAI rows too
        rep.brierScore = 0.20;  // above 0.10 → WARN
        rep.nSamples   = 50;
        const auto md  = DataExporter::benchmarkToMarkdown(rep);
        QVERIFY(md.contains("WARN"));
    }

    // 13 ── Provenance JSON has all six required fields
    void testProvenanceJsonHasAllFields()
    {
        ProvenanceEntry e;
        e.timestamp = QDateTime(QDate(2024, 3, 1), QTime(9, 0, 0), Qt::UTC);
        e.eventId   = QStringLiteral("evt_abc");
        e.stage     = QStringLiteral("ingest");
        e.action    = QStringLiteral("import");
        e.detail    = QStringLiteral("CSV row 42");
        e.dataHash  = QStringLiteral("deadbeef1234");
        const auto arr = DataExporter::provenanceToJson({e});
        QCOMPARE(arr.size(), 1);
        const auto obj = arr[0].toObject();
        QVERIFY(obj.contains("timestamp"));
        QVERIFY(obj.contains("eventId"));
        QVERIFY(obj.contains("stage"));
        QVERIFY(obj.contains("action"));
        QVERIFY(obj.contains("detail"));
        QVERIFY(obj.contains("dataHash"));
    }

    // 14 ── Event JSON has all eight required fields
    void testEventsJsonHasAllFields()
    {
        const auto arr = DataExporter::eventsToJson({makeFullEvent()});
        QCOMPARE(arr.size(), 1);
        const auto obj = arr[0].toObject();
        QVERIFY(obj.contains("eventId"));
        QVERIFY(obj.contains("crimeType"));
        QVERIFY(obj.contains("suburb"));
        QVERIFY(obj.contains("lat"));
        QVERIFY(obj.contains("lon"));
        QVERIFY(obj.contains("occurredAt"));
        QVERIFY(obj.contains("outcome"));
        QVERIFY(obj.contains("quality"));
    }

    // 15 ── Events CSV header starts with canonical column names
    void testEventsCsvHasHeader()
    {
        const auto csv = DataExporter::eventsToCsv({});
        QVERIFY(csv.startsWith("event_id,crime_type,suburb,lat,lon"));
    }

    // 16 ── Values survive a JSON serialise → parse round-trip
    void testEventsCsvRoundtrip()
    {
        const auto ev  = makeFullEvent("round_1");
        const auto arr = DataExporter::eventsToJson({ev});
        const auto reparsed =
            QJsonDocument::fromJson(QJsonDocument(arr).toJson()).array();
        QCOMPARE(reparsed.size(), 1);
        const auto obj = reparsed[0].toObject();
        QCOMPARE(obj["eventId"].toString(),   QStringLiteral("round_1"));
        QCOMPARE(obj["crimeType"].toString(), QStringLiteral("burglary"));
        QVERIFY(std::abs(obj["lat"].toDouble() - (-37.8136)) < 1e-4);
        QCOMPARE(obj["outcome"].toString(),   QStringLiteral("unresolved"));
    }

    // 17 ── saveJson(array, path) returns true and writes a non-empty file
    void testSaveJsonToFile()
    {
        QTemporaryFile tmp;
        QVERIFY(tmp.open());
        const QString path = tmp.fileName();
        tmp.close();

        QJsonArray arr;
        QJsonObject item; item["key"] = QStringLiteral("val");
        arr.append(item);
        QVERIFY(DataExporter::saveJson(arr, path));

        QFile f(path);
        QVERIFY(f.open(QIODevice::ReadOnly));
        QVERIFY(f.size() > 0);
    }

    // 18 ── saveText("hello", path) returns true and file content is "hello"
    void testSaveTextToFile()
    {
        QTemporaryFile tmp;
        QVERIFY(tmp.open());
        const QString path = tmp.fileName();
        tmp.close();

        QVERIFY(DataExporter::saveText(QStringLiteral("hello"), path));

        QFile f(path);
        QVERIFY(f.open(QIODevice::ReadOnly));
        QCOMPARE(QString::fromUtf8(f.readAll()), QStringLiteral("hello"));
    }

    // 19 ── Saving to a non-existent deep path returns false
    void testSaveToInvalidPath()
    {
        QJsonArray arr;
        arr.append(1);
        const bool ok = DataExporter::saveJson(
            arr,
            QStringLiteral("/this/path/does/not/exist/sentinel_completeness_test.json"));
        QVERIFY(!ok);
    }

    // 20 ── Empty collections produce valid (empty) outputs rather than crashes
    void testEmptyCollections()
    {
        QVERIFY(DataExporter::leadsToJson({}).isEmpty());
        QVERIFY(!DataExporter::leadsToCsv({}).isEmpty());
        QVERIFY(DataExporter::leadsToCsv({}).startsWith("rank,"));

        QVERIFY(DataExporter::forecastsToJson({}).isEmpty());
        QVERIFY(!DataExporter::forecastsToCsv({}).isEmpty());

        QVERIFY(DataExporter::eventsToJson({}).isEmpty());
        QVERIFY(!DataExporter::eventsToCsv({}).isEmpty());
        QVERIFY(DataExporter::eventsToCsv({}).startsWith("event_id,"));

        QVERIFY(DataExporter::provenanceToJson({}).isEmpty());
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// main
// ─────────────────────────────────────────────────────────────────────────────
int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    TestExportCompleteness t;
    return QTest::qExec(&t, argc, argv);
}

#include "test_export_completeness.moc"
