// test_export_roundtrip.cpp — Export round-trip tests for DataExporter
#include <QTest>
#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <cmath>

#include "core/DataExporter.h"
#include "core/CrimeEvent.h"
#include "models/RiskForecaster.h"
#include "benchmark/BenchmarkMetrics.h"
#include "audit/ProvenanceLog.h"

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

namespace {

// Parse a single CSV line, properly handling RFC-4180 quoted fields.
static QStringList parseCsvLine(const QString& line)
{
    QStringList fields;
    QString current;
    bool inQuotes = false;

    for (int i = 0; i < line.size(); ++i) {
        const QChar c = line.at(i);
        if (inQuotes) {
            if (c == QLatin1Char('"')) {
                if (i + 1 < line.size() && line.at(i + 1) == QLatin1Char('"')) {
                    current += QLatin1Char('"');
                    ++i;
                } else {
                    inQuotes = false;
                }
            } else {
                current += c;
            }
        } else {
            if (c == QLatin1Char('"')) {
                inQuotes = true;
            } else if (c == QLatin1Char(',')) {
                fields << current;
                current.clear();
            } else {
                current += c;
            }
        }
    }
    fields << current;
    return fields;
}

static CrimeEvent makeEvent(const QString& id, double lat, double lon,
                              const QString& type  = "burglary",
                              const QString& suburb = "Test Suburb")
{
    CrimeEvent e;
    e.eventId    = id;
    e.id         = id;
    e.crimeType  = type;
    e.suburb     = suburb;
    e.lat        = lat;
    e.lon        = lon;
    e.latitude   = lat;
    e.longitude  = lon;
    e.occurredAt = QDateTime(QDate(2024, 1, 15), QTime(12, 0, 0), Qt::UTC);
    e.timestamp  = *e.occurredAt;
    e.outcome    = "unresolved";
    e.qualityScore = 0.9;
    return e;
}

static InvestigativeLead makeLead(int rank, const QString& cat,
                                   const QString& headline, double conf)
{
    InvestigativeLead l;
    l.rank             = rank;
    l.category         = cat;
    l.headline         = headline;
    l.detail           = QStringLiteral("Detail for %1").arg(headline);
    l.confidence       = conf;
    l.confidenceMethod = "bayes";
    l.generatedAt      = QDateTime(QDate(2024, 1, 1), QTime(0, 0, 0), Qt::UTC);
    l.provenance       = {"source_A", "model_B"};
    l.contradictions   = {"contra_1"};
    return l;
}

static ZoneForecast makeForecast(const QString& zoneId, int alertLevel = 1)
{
    ZoneForecast zf;
    zf.zoneId     = zoneId;
    zf.weeklyRisk = 0.65;
    zf.alertLevel = alertLevel;
    for (int i = 0; i < 3; ++i) {
        ForecastDay day;
        day.date             = QDate(2024, 1, 1).addDays(i);
        day.riskScore        = 0.2 + 0.1 * i;
        day.baselineProb     = 0.15;
        day.escalationFactor = 1.1;
        day.temporalFactor   = 1.05;
        day.expectedCount    = 0.8;
        day.explanation      = QStringLiteral("Risk day %1").arg(i + 1);
        zf.days.append(day);
    }
    return zf;
}

static BenchmarkReport makeReport()
{
    BenchmarkReport r;
    r.nSamples  = 500;
    r.pai5pct   = 7.2;
    r.pai10pct  = 5.1;
    r.pai20pct  = 3.4;
    r.pei10pct  = 0.72;
    r.ser       = 0.55;
    r.aucRoc    = 0.89;
    r.aucPr     = 0.82;
    r.mae       = 0.12;
    r.rmse      = 0.18;
    r.brierScore = 0.07;
    return r;
}

static ProvenanceEntry makeProvEntry(const QString& eventId, const QString& stage)
{
    ProvenanceEntry e;
    e.timestamp = QDateTime(QDate(2024, 3, 10), QTime(8, 0, 0), Qt::UTC);
    e.eventId   = eventId;
    e.stage     = stage;
    e.action    = "process";
    e.detail    = QStringLiteral("Detail for %1 at %2").arg(eventId, stage);
    e.dataHash  = "abc123def456";
    return e;
}

// Re-serialise a QJsonArray through QJsonDocument to simulate
// what a real consumer would do (write → read).
static QJsonArray jsonRoundtrip(const QJsonArray& arr)
{
    const QByteArray bytes = QJsonDocument(arr).toJson(QJsonDocument::Compact);
    return QJsonDocument::fromJson(bytes).array();
}

static QJsonObject jsonRoundtrip(const QJsonObject& obj)
{
    const QByteArray bytes = QJsonDocument(obj).toJson(QJsonDocument::Compact);
    return QJsonDocument::fromJson(bytes).object();
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// TestExportRoundtrip
// ─────────────────────────────────────────────────────────────────────────────

class TestExportRoundtrip : public QObject {
    Q_OBJECT

private slots:

    // ── 1. Events → JSON round-trip ───────────────────────────────────────────
    void testEventsJsonRoundtrip_FieldsMatch()
    {
        const CrimeEvent e = makeEvent("EVT001", 51.5074, -0.1278, "theft", "Camden");

        const auto arr = jsonRoundtrip(DataExporter::eventsToJson({e}));
        QCOMPARE(arr.size(), 1);

        const auto obj = arr[0].toObject();
        QCOMPARE(obj["eventId"].toString(),   QStringLiteral("EVT001"));
        QCOMPARE(obj["crimeType"].toString(), QStringLiteral("theft"));
        QCOMPARE(obj["suburb"].toString(),    QStringLiteral("Camden"));
        QVERIFY(std::abs(obj["lat"].toDouble() - 51.5074) < 1e-6);
        QVERIFY(std::abs(obj["lon"].toDouble() - (-0.1278)) < 1e-6);
        QCOMPARE(obj["outcome"].toString(),   QStringLiteral("unresolved"));
        QVERIFY(std::abs(obj["quality"].toDouble() - 0.9) < 1e-6);
    }

    void testEventsJsonRoundtrip_OccurredAt()
    {
        const CrimeEvent e = makeEvent("EVT002", 0.0, 0.0);
        const QDateTime expectedDt(QDate(2024, 1, 15), QTime(12, 0, 0), Qt::UTC);

        const auto arr = DataExporter::eventsToJson({e});
        const auto obj = arr[0].toObject();
        const QDateTime parsed =
            QDateTime::fromString(obj["occurredAt"].toString(), Qt::ISODate);
        QCOMPARE(parsed, expectedDt);
    }

    void testEventsJsonRoundtrip_MultipleEvents()
    {
        QVector<CrimeEvent> events;
        for (int i = 0; i < 5; ++i)
            events << makeEvent(QStringLiteral("E%1").arg(i), i * 0.1, i * 0.2,
                                "robbery", QStringLiteral("Zone%1").arg(i));

        const auto arr = jsonRoundtrip(DataExporter::eventsToJson(events));
        QCOMPARE(arr.size(), 5);

        for (int i = 0; i < 5; ++i) {
            const auto obj = arr[i].toObject();
            QCOMPARE(obj["eventId"].toString(), QStringLiteral("E%1").arg(i));
            QCOMPARE(obj["suburb"].toString(),  QStringLiteral("Zone%1").arg(i));
        }
    }

    void testEventsJsonRoundtrip_EmptyList()
    {
        const auto arr = jsonRoundtrip(DataExporter::eventsToJson({}));
        QVERIFY(arr.isEmpty());
    }

    // ── 2. Events → CSV round-trip ────────────────────────────────────────────
    void testEventsCsvRoundtrip_Header()
    {
        const auto csv = DataExporter::eventsToCsv({});
        const QStringList lines = csv.split('\n', Qt::SkipEmptyParts);
        QCOMPARE(lines.size(), 1);  // just the header

        const auto fields = parseCsvLine(lines[0]);
        QCOMPARE(fields[0], QStringLiteral("event_id"));
        QCOMPARE(fields[1], QStringLiteral("crime_type"));
        QCOMPARE(fields[2], QStringLiteral("suburb"));
        QCOMPARE(fields[3], QStringLiteral("lat"));
        QCOMPARE(fields[4], QStringLiteral("lon"));
        QCOMPARE(fields[5], QStringLiteral("occurred_at"));
        QCOMPARE(fields[6], QStringLiteral("outcome"));
        QCOMPARE(fields[7], QStringLiteral("quality_score"));
    }

    void testEventsCsvRoundtrip_FieldValues()
    {
        const CrimeEvent e = makeEvent("EVT_CSV", 48.8566, 2.3522, "assault", "Paris");

        const auto csv = DataExporter::eventsToCsv({e});
        const QStringList lines = csv.split('\n', Qt::SkipEmptyParts);
        QCOMPARE(lines.size(), 2);  // header + 1 row

        const auto row = parseCsvLine(lines[1]);
        QCOMPARE(row[0], QStringLiteral("EVT_CSV"));
        QCOMPARE(row[1], QStringLiteral("assault"));
        QCOMPARE(row[2], QStringLiteral("Paris"));
        QVERIFY(std::abs(row[3].toDouble() - 48.8566) < 1e-4);
        QVERIFY(std::abs(row[4].toDouble() - 2.3522)  < 1e-4);
        QCOMPARE(row[6], QStringLiteral("unresolved"));
        QVERIFY(std::abs(row[7].toDouble() - 0.9) < 1e-2);
    }

    void testEventsCsvRoundtrip_RowCount()
    {
        QVector<CrimeEvent> events;
        for (int i = 0; i < 7; ++i)
            events << makeEvent(QStringLiteral("E%1").arg(i), 0.0, 0.0);

        const auto csv = DataExporter::eventsToCsv(events);
        // +1 for header, +1 because split SkipEmptyParts removes trailing newline
        const int dataLines = csv.count('\n') - 1;
        QCOMPARE(dataLines, 7);
    }

    // ── 3. Leads → JSON round-trip ────────────────────────────────────────────
    void testLeadsJsonRoundtrip_Fields()
    {
        const auto lead = makeLead(2, "geo_profile", "Suspect in Zone A", 0.78);

        const auto arr = jsonRoundtrip(DataExporter::leadsToJson({lead}));
        QCOMPARE(arr.size(), 1);

        const auto obj = arr[0].toObject();
        QCOMPARE(obj["rank"].toInt(),             2);
        QCOMPARE(obj["category"].toString(),      QStringLiteral("geo_profile"));
        QCOMPARE(obj["headline"].toString(),      QStringLiteral("Suspect in Zone A"));
        QVERIFY(std::abs(obj["confidence"].toDouble() - 0.78) < 1e-9);
        QCOMPARE(obj["confidenceMethod"].toString(), QStringLiteral("bayes"));
    }

    void testLeadsJsonRoundtrip_ProvenancePreserved()
    {
        const auto lead = makeLead(1, "series", "Series A lead", 0.9);

        const auto arr = DataExporter::leadsToJson({lead});
        const auto prov = arr[0].toObject()["provenance"].toArray();
        QCOMPARE(prov.size(), 2);
        QCOMPARE(prov[0].toString(), QStringLiteral("source_A"));
        QCOMPARE(prov[1].toString(), QStringLiteral("model_B"));
    }

    void testLeadsJsonRoundtrip_ContradictionsPreserved()
    {
        const auto lead = makeLead(1, "series", "Lead", 0.9);

        const auto arr = DataExporter::leadsToJson({lead});
        const auto contra = arr[0].toObject()["contradictions"].toArray();
        QCOMPARE(contra.size(), 1);
        QCOMPARE(contra[0].toString(), QStringLiteral("contra_1"));
    }

    void testLeadsJsonRoundtrip_GeneratedAt()
    {
        const auto lead = makeLead(1, "cat", "Headline", 0.5);
        const QDateTime expected(QDate(2024, 1, 1), QTime(0, 0, 0), Qt::UTC);

        const auto arr = DataExporter::leadsToJson({lead});
        const auto ts  = arr[0].toObject()["generatedAt"].toString();
        const QDateTime parsed = QDateTime::fromString(ts, Qt::ISODate);
        QCOMPARE(parsed, expected);
    }

    // ── 4. Leads → CSV round-trip ─────────────────────────────────────────────
    void testLeadsCsvRoundtrip_Header()
    {
        const auto csv = DataExporter::leadsToCsv({});
        QVERIFY(csv.startsWith("rank,category,headline,detail,confidence,"));
    }

    void testLeadsCsvRoundtrip_FieldValues()
    {
        const auto lead = makeLead(3, "series_linkage", "Top Series Lead", 0.92);

        const auto csv   = DataExporter::leadsToCsv({lead});
        const QStringList lines = csv.split('\n', Qt::SkipEmptyParts);
        QCOMPARE(lines.size(), 2);

        const auto row = parseCsvLine(lines[1]);
        QCOMPARE(row[0].toInt(),   3);
        QCOMPARE(row[1], QStringLiteral("series_linkage"));
        QCOMPARE(row[2], QStringLiteral("Top Series Lead"));
        QVERIFY(std::abs(row[4].toDouble() - 0.92) < 1e-3);
        QCOMPARE(row[5], QStringLiteral("bayes"));
    }

    void testLeadsCsvRoundtrip_CommaEscaping()
    {
        auto lead = makeLead(1, "geo", "Headline, with comma", 0.7);

        const auto csv = DataExporter::leadsToCsv({lead});
        QVERIFY(csv.contains("\"Headline, with comma\""));

        // Parse back: the headline field should survive the round-trip
        const QStringList lines = csv.split('\n', Qt::SkipEmptyParts);
        const auto row = parseCsvLine(lines[1]);
        QCOMPARE(row[2], QStringLiteral("Headline, with comma"));
    }

    // ── 5. Forecasts → JSON round-trip ────────────────────────────────────────
    void testForecastsJsonRoundtrip_ZoneFields()
    {
        const ZoneForecast zf = makeForecast("Zone_42", 2);

        const auto arr = jsonRoundtrip(DataExporter::forecastsToJson({zf}));
        QCOMPARE(arr.size(), 1);

        const auto obj = arr[0].toObject();
        QCOMPARE(obj["zoneId"].toString(),   QStringLiteral("Zone_42"));
        QVERIFY(std::abs(obj["weeklyRisk"].toDouble() - 0.65) < 1e-9);
        QCOMPARE(obj["alertLevel"].toInt(),  2);
        QVERIFY(!obj["alertLabel"].toString().isEmpty());
    }

    void testForecastsJsonRoundtrip_DayFields()
    {
        const ZoneForecast zf = makeForecast("Zone_A");

        const auto arr  = DataExporter::forecastsToJson({zf});
        const auto days = arr[0].toObject()["days"].toArray();
        QCOMPARE(days.size(), 3);

        const auto day0 = days[0].toObject();
        QCOMPARE(day0["date"].toString(), QStringLiteral("2024-01-01"));
        QVERIFY(std::abs(day0["riskScore"].toDouble()    - 0.2)  < 1e-6);
        QVERIFY(std::abs(day0["baselineProb"].toDouble() - 0.15) < 1e-6);
        QCOMPARE(day0["explanation"].toString(), QStringLiteral("Risk day 1"));
    }

    void testForecastsJsonRoundtrip_Empty()
    {
        const auto arr = jsonRoundtrip(DataExporter::forecastsToJson({}));
        QVERIFY(arr.isEmpty());
    }

    // ── 6. Forecasts → CSV round-trip ─────────────────────────────────────────
    void testForecastsCsvRoundtrip_Header()
    {
        const auto csv = DataExporter::forecastsToCsv({});
        QVERIFY(csv.startsWith("zone_id,date,risk_score,baseline_prob,"));
    }

    void testForecastsCsvRoundtrip_RowCount()
    {
        QVector<ZoneForecast> forecasts;
        forecasts << makeForecast("Z1");  // 3 days
        forecasts << makeForecast("Z2");  // 3 days

        const auto csv = DataExporter::forecastsToCsv(forecasts);
        // header + 6 data rows = 7 newlines
        QCOMPARE(csv.count('\n'), 7);
    }

    void testForecastsCsvRoundtrip_FieldValues()
    {
        const ZoneForecast zf = makeForecast("Zone_CSV", 1);

        const auto csv = DataExporter::forecastsToCsv({zf});
        const QStringList lines = csv.split('\n', Qt::SkipEmptyParts);
        QCOMPARE(lines.size(), 4);  // header + 3 data rows

        const auto row = parseCsvLine(lines[1]);
        QCOMPARE(row[0], QStringLiteral("Zone_CSV"));
        QCOMPARE(row[1], QStringLiteral("2024-01-01"));
        QVERIFY(std::abs(row[2].toDouble() - 0.2)  < 1e-4);
        QVERIFY(std::abs(row[3].toDouble() - 0.15) < 1e-4);
        QCOMPARE(row[7].toInt(), 1);  // alertLevel
    }

    // ── 7. Benchmark → JSON round-trip ────────────────────────────────────────
    void testBenchmarkJsonRoundtrip_KeyFields()
    {
        const BenchmarkReport rep = makeReport();

        const auto obj = jsonRoundtrip(DataExporter::benchmarkToJson(rep));
        QCOMPARE(obj["nSamples"].toInt(), 500);
        QVERIFY(std::abs(obj["pai05"].toDouble()    - 7.2)  < 1e-9);
        QVERIFY(std::abs(obj["pai10"].toDouble()    - 5.1)  < 1e-9);
        QVERIFY(std::abs(obj["pai20"].toDouble()    - 3.4)  < 1e-9);
        QVERIFY(std::abs(obj["aucRoc"].toDouble()   - 0.89) < 1e-9);
        QVERIFY(std::abs(obj["brierScore"].toDouble()- 0.07)< 1e-9);
    }

    void testBenchmarkJsonRoundtrip_SummaryPresent()
    {
        const auto obj = DataExporter::benchmarkToJson(makeReport());
        QVERIFY(!obj["summary"].toString().isEmpty());
    }

    void testBenchmarkJsonRoundtrip_AllMetrics()
    {
        const BenchmarkReport rep = makeReport();
        const auto obj = DataExporter::benchmarkToJson(rep);

        const QStringList expected = {
            "nSamples", "pai05", "pai10", "pai20",
            "pei10", "ser", "aucRoc", "aucPr",
            "mae", "rmse", "brierScore", "summary"
        };
        for (const auto& key : expected) {
            QVERIFY2(obj.contains(key),
                     qPrintable(QStringLiteral("Missing key: %1").arg(key)));
        }
    }

    // ── 8. Benchmark → Markdown round-trip ───────────────────────────────────
    void testBenchmarkMarkdownContainsKeyFields()
    {
        const auto md = DataExporter::benchmarkToMarkdown(makeReport());

        QVERIFY(md.startsWith("# SENTINEL Benchmark Report\n"));
        QVERIFY(md.contains("PAI @ 5%"));
        QVERIFY(md.contains("PAI @ 10%"));
        QVERIFY(md.contains("PAI @ 20%"));
        QVERIFY(md.contains("AUC-ROC"));
        QVERIFY(md.contains("Brier Score"));
        QVERIFY(md.contains("PASS") || md.contains("WARN"));
        QVERIFY(md.contains("n=500"));
    }

    void testBenchmarkMarkdownTableStructure()
    {
        const auto md = DataExporter::benchmarkToMarkdown(makeReport());
        QVERIFY(md.contains("| Metric | Value | Target | Status |"));
        QVERIFY(md.contains("|--------|-------|--------|--------|"));
        QVERIFY(md.contains("**Summary:**"));
    }

    // ── 9. Provenance → JSON round-trip ──────────────────────────────────────
    void testProvenanceJsonRoundtrip_AllEntries()
    {
        QVector<ProvenanceEntry> chain;
        chain << makeProvEntry("EVT001", "ingest");
        chain << makeProvEntry("EVT001", "nlp");
        chain << makeProvEntry("EVT001", "model");

        const auto arr = jsonRoundtrip(DataExporter::provenanceToJson(chain));
        QCOMPARE(arr.size(), 3);
    }

    void testProvenanceJsonRoundtrip_FieldValues()
    {
        const auto entry = makeProvEntry("EVT_PROV", "inference");

        const auto arr = DataExporter::provenanceToJson({entry});
        QCOMPARE(arr.size(), 1);

        const auto obj = arr[0].toObject();
        QCOMPARE(obj["eventId"].toString(),  QStringLiteral("EVT_PROV"));
        QCOMPARE(obj["stage"].toString(),    QStringLiteral("inference"));
        QCOMPARE(obj["action"].toString(),   QStringLiteral("process"));
        QCOMPARE(obj["dataHash"].toString(), QStringLiteral("abc123def456"));
    }

    void testProvenanceJsonRoundtrip_TimestampPreserved()
    {
        const auto entry = makeProvEntry("E1", "ingest");
        const QDateTime expected(QDate(2024, 3, 10), QTime(8, 0, 0), Qt::UTC);

        const auto arr = DataExporter::provenanceToJson({entry});
        const auto ts  = arr[0].toObject()["timestamp"].toString();
        const QDateTime parsed = QDateTime::fromString(ts, Qt::ISODate);
        QCOMPARE(parsed, expected);
    }

    void testProvenanceJsonRoundtrip_Empty()
    {
        const auto arr = jsonRoundtrip(DataExporter::provenanceToJson({}));
        QVERIFY(arr.isEmpty());
    }

    // ── 10. Large batch ───────────────────────────────────────────────────────
    void testLargeBatchEventsJson_Count()
    {
        QVector<CrimeEvent> events;
        events.reserve(1000);
        for (int i = 0; i < 1000; ++i)
            events << makeEvent(QStringLiteral("LARGE_%1").arg(i),
                                i * 0.001, i * 0.001);

        const auto arr = DataExporter::eventsToJson(events);
        QCOMPARE(arr.size(), 1000);
    }

    void testLargeBatchEventsCsv_RowCount()
    {
        QVector<CrimeEvent> events;
        events.reserve(1000);
        for (int i = 0; i < 1000; ++i)
            events << makeEvent(QStringLiteral("BATCH_%1").arg(i), 0.0, 0.0);

        const auto csv = DataExporter::eventsToCsv(events);
        // 1 header + 1000 data rows = 1001 newlines
        QCOMPARE(csv.count('\n'), 1001);
    }

    void testLargeBatchLeadsJson_Count()
    {
        QVector<InvestigativeLead> leads;
        leads.reserve(500);
        for (int i = 0; i < 500; ++i)
            leads << makeLead(i + 1, "geo",
                              QStringLiteral("Lead %1").arg(i), 0.5 + i * 0.001);

        const auto arr = DataExporter::leadsToJson(leads);
        QCOMPARE(arr.size(), 500);
    }

    // ── 11. Empty exports ─────────────────────────────────────────────────────
    void testEmptyEventListJson()
    {
        QVERIFY(DataExporter::eventsToJson({}).isEmpty());
    }

    void testEmptyEventListCsv()
    {
        const auto csv = DataExporter::eventsToCsv({});
        // Only the header line
        QVERIFY(csv.startsWith("event_id,"));
        QCOMPARE(csv.count('\n'), 1);
    }

    void testEmptyLeadsList()
    {
        QVERIFY(DataExporter::leadsToJson({}).isEmpty());
        const auto csv = DataExporter::leadsToCsv({});
        QCOMPARE(csv.count('\n'), 1);
    }

    void testEmptyForecastsList()
    {
        QVERIFY(DataExporter::forecastsToJson({}).isEmpty());
        const auto csv = DataExporter::forecastsToCsv({});
        QCOMPARE(csv.count('\n'), 1);
    }

    void testEmptyProvenanceChain()
    {
        QVERIFY(DataExporter::provenanceToJson({}).isEmpty());
    }

    // ── 12. Events with null / missing optional fields ────────────────────────
    void testEventsWithNullLat()
    {
        CrimeEvent e;
        e.eventId   = "NO_LOC";
        e.id        = "NO_LOC";
        e.crimeType = "theft";
        e.suburb    = "Unknown";
        e.outcome   = "unresolved";
        e.qualityScore = 0.5;
        // lat, lon not set; occurredAt not set

        const auto arr = DataExporter::eventsToJson({e});
        QCOMPARE(arr.size(), 1);

        const auto obj = arr[0].toObject();
        // value_or(0.0) should be used
        QVERIFY(std::abs(obj["lat"].toDouble()) < 1e-9);
        QVERIFY(std::abs(obj["lon"].toDouble()) < 1e-9);
        // occurredAt should be empty string
        QVERIFY(obj["occurredAt"].toString().isEmpty());
    }

    void testEventsWithNullLatCsv()
    {
        CrimeEvent e;
        e.eventId      = "NO_LOC_CSV";
        e.id           = "NO_LOC_CSV";
        e.crimeType    = "theft";
        e.suburb       = "Unknown";
        e.outcome      = "unresolved";
        e.qualityScore = 0.5;

        const auto csv = DataExporter::eventsToCsv({e});
        const QStringList lines = csv.split('\n', Qt::SkipEmptyParts);
        QCOMPARE(lines.size(), 2);

        const auto row = parseCsvLine(lines[1]);
        QVERIFY(std::abs(row[3].toDouble()) < 1e-9);  // lat = 0.0
        QVERIFY(std::abs(row[4].toDouble()) < 1e-9);  // lon = 0.0
        QVERIFY(row[5].isEmpty());                     // occurredAt = ""
    }

    // ── 13. Special characters in strings ────────────────────────────────────
    void testSpecialCharsJson()
    {
        CrimeEvent e = makeEvent("SPEC", 0.0, 0.0, "theft & robbery", "St. Peter's");

        const auto arr = DataExporter::eventsToJson({e});
        const auto obj = arr[0].toObject();

        // JSON handles these natively; values should be preserved verbatim
        QCOMPARE(obj["crimeType"].toString(), QStringLiteral("theft & robbery"));
        QCOMPARE(obj["suburb"].toString(),    QStringLiteral("St. Peter's"));
    }

    void testSpecialCharsCsvCommaInField()
    {
        CrimeEvent e = makeEvent("SPEC_CSV", 0.0, 0.0,
                                  "theft, robbery and assault",
                                  "O'Connor, Northside");

        const auto csv = DataExporter::eventsToCsv({e});
        const QStringList lines = csv.split('\n', Qt::SkipEmptyParts);
        const auto row = parseCsvLine(lines[1]);

        QCOMPARE(row[1], QStringLiteral("theft, robbery and assault"));
        QCOMPARE(row[2], QStringLiteral("O'Connor, Northside"));
    }

    void testSpecialCharsCsvQuoteInField()
    {
        CrimeEvent e = makeEvent("QUOTE_CSV", 0.0, 0.0,
                                  "assault \"grievous\"", "West");

        const auto csv = DataExporter::eventsToCsv({e});
        const QStringList lines = csv.split('\n', Qt::SkipEmptyParts);
        const auto row = parseCsvLine(lines[1]);

        QCOMPARE(row[1], QStringLiteral("assault \"grievous\""));
    }

    void testSpecialCharsLeadCsvComma()
    {
        auto lead = makeLead(1, "series", "Lead: Zone A, Zone B", 0.75);

        const auto csv = DataExporter::leadsToCsv({lead});
        QVERIFY(csv.contains("\"Lead: Zone A, Zone B\""));

        const QStringList lines = csv.split('\n', Qt::SkipEmptyParts);
        const auto row = parseCsvLine(lines[1]);
        QCOMPARE(row[2], QStringLiteral("Lead: Zone A, Zone B"));
    }

    // ── Consistency: JSON and CSV agree on key values ─────────────────────────
    void testEventsJsonAndCsvAgree()
    {
        QVector<CrimeEvent> events;
        for (int i = 0; i < 5; ++i)
            events << makeEvent(QStringLiteral("AGR_%1").arg(i),
                                10.0 + i, 20.0 + i,
                                "burglary", QStringLiteral("Suburb%1").arg(i));

        const auto arr  = DataExporter::eventsToJson(events);
        const auto csv  = DataExporter::eventsToCsv(events);
        const QStringList lines = csv.split('\n', Qt::SkipEmptyParts);

        // Skip header line; compare JSON fields with CSV fields
        for (int i = 0; i < 5; ++i) {
            const auto obj = arr[i].toObject();
            const auto row = parseCsvLine(lines[i + 1]);

            QCOMPARE(obj["eventId"].toString(), row[0]);
            QCOMPARE(obj["crimeType"].toString(), row[1]);
            QCOMPARE(obj["suburb"].toString(), row[2]);
            QVERIFY(std::abs(obj["lat"].toDouble() - row[3].toDouble()) < 1e-4);
        }
    }

    void testLeadsJsonAndCsvAgree()
    {
        QVector<InvestigativeLead> leads;
        for (int i = 0; i < 3; ++i)
            leads << makeLead(i + 1, "geo",
                              QStringLiteral("Headline%1").arg(i), 0.5 + i * 0.1);

        const auto arr = DataExporter::leadsToJson(leads);
        const auto csv = DataExporter::leadsToCsv(leads);
        const QStringList lines = csv.split('\n', Qt::SkipEmptyParts);

        for (int i = 0; i < 3; ++i) {
            const auto obj = arr[i].toObject();
            const auto row = parseCsvLine(lines[i + 1]);

            QCOMPARE(obj["rank"].toInt(), row[0].toInt());
            QCOMPARE(obj["headline"].toString(), row[2]);
        }
    }
};

QTEST_MAIN(TestExportRoundtrip)
#include "test_export_roundtrip.moc"
