// test_data_exporter.cpp — DataExporter unit tests
#include <QTest>
#include <QCoreApplication>
#include <QTemporaryFile>
#include <QFile>
#include <QJsonDocument>
#include "core/DataExporter.h"
#include "core/CrimeEvent.h"
#include "models/RiskForecaster.h"
#include "audit/ProvenanceLog.h"

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────
namespace {

static InvestigativeLead makeLead(int rank, const QString& cat,
                                   const QString& headline, double conf) {
    InvestigativeLead l;
    l.rank             = rank;
    l.category         = cat;
    l.headline         = headline;
    l.confidence       = conf;
    l.detail           = QStringLiteral("Test detail for %1").arg(headline);
    l.confidenceMethod = QStringLiteral("test_method");
    l.generatedAt      = QDateTime(QDate(2024,1,1), QTime(0,0,0), Qt::UTC);
    l.provenance       = {"source_A", "model_B"};
    return l;
}

static CrimeEvent makeEvent(const QString& id, double lat, double lon,
                              const QString& type = "burglary") {
    CrimeEvent e;
    e.eventId = e.id = id;
    e.crimeType = type;
    e.suburb = "Test Suburb";
    e.lat = lat; e.lon = lon;
    e.latitude = lat; e.longitude = lon;
    e.occurredAt = QDateTime(QDate(2024,1,15), QTime(12,0,0), Qt::UTC);
    e.qualityScore = 0.9;
    return e;
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// TestDataExporter
// ─────────────────────────────────────────────────────────────────────────────

class TestDataExporter : public QObject {
    Q_OBJECT

private slots:

    // ── Leads → JSON ─────────────────────────────────────────────────────────
    void testLeadsToJsonEmpty() {
        const auto arr = DataExporter::leadsToJson({});
        QVERIFY(arr.isEmpty());
    }

    void testLeadsToJsonSize() {
        QVector<InvestigativeLead> leads;
        for (int i = 0; i < 5; ++i) leads.append(makeLead(i+1, "geo", "Headline", 0.7));
        const auto arr = DataExporter::leadsToJson(leads);
        QCOMPARE(arr.size(), 5);
    }

    void testLeadsToJsonFields() {
        const auto lead = makeLead(1, "series_linkage", "Test Series Lead", 0.85);
        const auto arr  = DataExporter::leadsToJson({lead});
        QCOMPARE(arr.size(), 1);
        const auto obj = arr[0].toObject();
        QCOMPARE(obj["rank"].toInt(),           1);
        QCOMPARE(obj["category"].toString(),    QStringLiteral("series_linkage"));
        QCOMPARE(obj["headline"].toString(),    QStringLiteral("Test Series Lead"));
        QVERIFY(std::abs(obj["confidence"].toDouble() - 0.85) < 1e-9);
        QCOMPARE(obj["confidenceMethod"].toString(), QStringLiteral("test_method"));
    }

    void testLeadsToJsonProvenanceArray() {
        const auto lead = makeLead(1, "geo", "Headline", 0.5);
        const auto arr  = DataExporter::leadsToJson({lead});
        const auto prov = arr[0].toObject()["provenance"].toArray();
        QCOMPARE(prov.size(), 2);
        QCOMPARE(prov[0].toString(), QStringLiteral("source_A"));
    }

    // ── Leads → CSV ──────────────────────────────────────────────────────────
    void testLeadsToCsvHeader() {
        const auto csv = DataExporter::leadsToCsv({});
        QVERIFY(csv.startsWith("rank,category,headline,"));
    }

    void testLeadsToCsvRowCount() {
        QVector<InvestigativeLead> leads;
        for (int i = 0; i < 3; ++i) leads.append(makeLead(i+1, "geo", "Headline", 0.7));
        const auto csv = DataExporter::leadsToCsv(leads);
        const int lines = csv.count('\n');
        QCOMPARE(lines, 4);  // 1 header + 3 data rows
    }

    void testLeadsToCsvEscapesCommas() {
        auto lead = makeLead(1, "geo", "Headline with, comma", 0.7);
        const auto csv = DataExporter::leadsToCsv({lead});
        QVERIFY(csv.contains("\"Headline with, comma\""));
    }

    // ── Leads → Markdown ─────────────────────────────────────────────────────
    void testLeadsToMarkdownTitle() {
        const auto md = DataExporter::leadsToMarkdown({}, "My Report");
        QVERIFY(md.startsWith("# My Report\n"));
    }

    void testLeadsToMarkdownTable() {
        const auto lead = makeLead(1, "series_linkage", "Test Lead", 0.8);
        const auto md   = DataExporter::leadsToMarkdown({lead});
        QVERIFY(md.contains("| Rank |"));
        QVERIFY(md.contains("Test Lead"));
        QVERIFY(md.contains("80.0%"));
    }

    void testLeadsToMarkdownDetails() {
        const auto lead = makeLead(1, "geo", "Geo Lead", 0.9);
        const auto md   = DataExporter::leadsToMarkdown({lead});
        QVERIFY(md.contains("## Details"));
        QVERIFY(md.contains("Geo Lead"));
    }

    // ── Forecasts → JSON ─────────────────────────────────────────────────────
    void testForecastsToJsonEmpty() {
        QVERIFY(DataExporter::forecastsToJson({}).isEmpty());
    }

    void testForecastsToJsonFields() {
        ZoneForecast zf;
        zf.zoneId     = "zone_A";
        zf.weeklyRisk = 0.65;
        zf.alertLevel = 2;
        ForecastDay day;
        day.date            = QDate(2024,1,1);
        day.riskScore       = 0.65;
        day.baselineProb    = 0.5;
        day.escalationFactor= 1.3;
        day.temporalFactor  = 1.2;
        day.expectedCount   = 1.5;
        day.explanation     = "test";
        zf.days.append(day);

        const auto arr = DataExporter::forecastsToJson({zf});
        QCOMPARE(arr.size(), 1);
        const auto obj = arr[0].toObject();
        QCOMPARE(obj["zoneId"].toString(), QStringLiteral("zone_A"));
        QVERIFY(std::abs(obj["weeklyRisk"].toDouble() - 0.65) < 1e-9);
        QCOMPARE(obj["alertLevel"].toInt(), 2);
        QCOMPARE(obj["days"].toArray().size(), 1);
    }

    // ── Forecasts → CSV ──────────────────────────────────────────────────────
    void testForecastsToCsvHeader() {
        const auto csv = DataExporter::forecastsToCsv({});
        QVERIFY(csv.startsWith("zone_id,date,risk_score,"));
    }

    void testForecastsToCsvRows() {
        ZoneForecast zf;
        zf.zoneId = "z";
        zf.alertLevel = 0;
        for (int i = 0; i < 3; ++i) {
            ForecastDay d;
            d.date = QDate(2024,1,1).addDays(i);
            d.riskScore = 0.3 * i; d.baselineProb = 0.2;
            d.escalationFactor = 1.0; d.temporalFactor = 1.0;
            d.expectedCount = 0.5;
            zf.days.append(d);
        }
        const auto csv = DataExporter::forecastsToCsv({zf});
        QCOMPARE(csv.count('\n'), 4);  // 1 header + 3 rows
    }

    // ── Benchmark → JSON ─────────────────────────────────────────────────────
    void testBenchmarkToJsonFields() {
        BenchmarkReport rep;
        rep.nSamples  = 100;
        rep.pai5pct   = 7.2;
        rep.aucRoc    = 0.88;
        rep.brierScore= 0.08;

        const auto obj = DataExporter::benchmarkToJson(rep);
        QCOMPARE(obj["nSamples"].toInt(), 100);
        QVERIFY(std::abs(obj["pai05"].toDouble() - 7.2) < 1e-9);
        QVERIFY(!obj["summary"].toString().isEmpty());
    }

    // ── Benchmark → Markdown ─────────────────────────────────────────────────
    void testBenchmarkToMarkdownHeader() {
        BenchmarkReport rep;
        rep.nSamples = 200; rep.pai5pct = 6.5; rep.pai10pct = 4.8;
        rep.pai20pct = 3.2; rep.pei10pct = 0.7; rep.ser = 0.5;
        rep.aucRoc = 0.87; rep.brierScore = 0.09;
        const auto md = DataExporter::benchmarkToMarkdown(rep);
        QVERIFY(md.startsWith("# SENTINEL Benchmark Report\n"));
        QVERIFY(md.contains("PAI @ 5%"));
        QVERIFY(md.contains("PASS"));
    }

    // ── Provenance → JSON ─────────────────────────────────────────────────────
    void testProvenanceToJsonEmpty() {
        QVERIFY(DataExporter::provenanceToJson({}).isEmpty());
    }

    void testProvenanceToJsonFields() {
        ProvenanceEntry e;
        e.timestamp = QDateTime(QDate(2024,1,1), QTime(0,0,0), Qt::UTC);
        e.eventId   = "event_1";
        e.stage     = "ingest";
        e.action    = "import";
        e.detail    = "CSV row 1";
        e.dataHash  = "abc123";

        const auto arr = DataExporter::provenanceToJson({e});
        QCOMPARE(arr.size(), 1);
        const auto obj = arr[0].toObject();
        QCOMPARE(obj["eventId"].toString(), QStringLiteral("event_1"));
        QCOMPARE(obj["stage"].toString(),   QStringLiteral("ingest"));
        QCOMPARE(obj["dataHash"].toString(),QStringLiteral("abc123"));
    }

    // ── Events → JSON ─────────────────────────────────────────────────────────
    void testEventsToJsonEmpty() {
        QVERIFY(DataExporter::eventsToJson({}).isEmpty());
    }

    void testEventsToJsonFields() {
        const auto ev  = makeEvent("e1", 51.5, -0.1, "theft");
        const auto arr = DataExporter::eventsToJson({ev});
        QCOMPARE(arr.size(), 1);
        const auto obj = arr[0].toObject();
        QCOMPARE(obj["eventId"].toString(),   QStringLiteral("e1"));
        QCOMPARE(obj["crimeType"].toString(), QStringLiteral("theft"));
        QVERIFY(std::abs(obj["lat"].toDouble() - 51.5) < 1e-6);
    }

    // ── Events → CSV ─────────────────────────────────────────────────────────
    void testEventsToCsvHeader() {
        const auto csv = DataExporter::eventsToCsv({});
        QVERIFY(csv.startsWith("event_id,crime_type,suburb,lat,lon,"));
    }

    void testEventsToCsvRow() {
        const auto ev  = makeEvent("e1", 51.5, -0.1);
        const auto csv = DataExporter::eventsToCsv({ev});
        QVERIFY(csv.contains("e1"));
        QVERIFY(csv.contains("51.500000"));
        QVERIFY(csv.contains("burglary"));
    }

    // ── File save ─────────────────────────────────────────────────────────────
    void testSaveJsonObject() {
        QTemporaryFile tmp;
        tmp.open();
        const QString path = tmp.fileName();
        tmp.close();

        QJsonObject obj;
        obj["test"] = 42;
        QVERIFY(DataExporter::saveJson(obj, path));

        QFile f(path);
        f.open(QIODevice::ReadOnly);
        const auto doc = QJsonDocument::fromJson(f.readAll());
        QCOMPARE(doc.object()["test"].toInt(), 42);
    }

    void testSaveJsonArray() {
        QTemporaryFile tmp;
        tmp.open();
        const QString path = tmp.fileName();
        tmp.close();

        QJsonArray arr;
        arr.append(1); arr.append(2); arr.append(3);
        QVERIFY(DataExporter::saveJson(arr, path));

        QFile f(path);
        f.open(QIODevice::ReadOnly);
        const auto doc = QJsonDocument::fromJson(f.readAll());
        QCOMPARE(doc.array().size(), 3);
    }

    void testSaveTextContent() {
        QTemporaryFile tmp;
        tmp.open();
        const QString path = tmp.fileName();
        tmp.close();

        const QString content = "Hello SENTINEL!";
        QVERIFY(DataExporter::saveText(content, path));

        QFile f(path);
        f.open(QIODevice::ReadOnly);
        QCOMPARE(QString::fromUtf8(f.readAll()), content);
    }

    void testSaveToInvalidPath() {
        const bool ok = DataExporter::saveText("test", "/nonexistent/directory/file.txt");
        QVERIFY(!ok);
        QVERIFY(!DataExporter::lastError().isEmpty());
    }
};

// ─── main ─────────────────────────────────────────────────────────────────────

static int runTest(QObject* obj, const char* logFile) {
    QStringList args = { "test", "-o", QString("%1,txt").arg(logFile) };
    return QTest::qExec(obj, args);
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    int r = 0;
    TestDataExporter t1; r |= runTest(&t1, "data_exporter.txt");
    return r;
}

#include "test_data_exporter.moc"
