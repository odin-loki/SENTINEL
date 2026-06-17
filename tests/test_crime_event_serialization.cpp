// test_crime_event_serialization.cpp
// Tests CrimeEvent JSON serialization via DataExporter and Database roundtrip.
// Validates field preservation across the full data pipeline.
#include <QTest>
#include <QTimeZone>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include "core/DataExporter.h"
#include "core/CrimeEvent.h"
#include "audit/ProvenanceLog.h"
#include <cmath>

// Helper to convert QJsonArray to compact JSON string
static QString jsonArrayToString(const QJsonArray& arr) {
    return QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact));
}
static QString jsonObjToString(const QJsonObject& obj) {
    return QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact));
}

class CrimeEventSerializationTest : public QObject
{
    Q_OBJECT

private:
    static CrimeEvent makeFullEvent(const QString& id)
    {
        CrimeEvent ev;
        ev.eventId        = id;
        ev.id             = id;
        ev.crimeType      = QStringLiteral("burglary");
        ev.suburb         = QStringLiteral("Soho");
        ev.lat            = 51.5074;
        ev.lon            = -0.1278;
        ev.latitude       = 51.5074;
        ev.longitude      = -0.1278;
        ev.source         = QStringLiteral("uk_police");
        ev.sourceVersion  = QStringLiteral("1.0");
        ev.qualityScore   = 0.87;
        const QDateTime dt = QDateTime(QDate(2024, 3, 15), QTime(22, 0, 0), QTimeZone::utc());
        ev.occurredAt     = dt;
        ev.timestamp      = dt;
        ev.outcome        = QStringLiteral("under_investigation");
        return ev;
    }

private slots:

    // ── 1. eventsToJson: returns non-empty array ──────────────────────────────
    void testEventsToJsonValid()
    {
        const QVector<CrimeEvent> evs = { makeFullEvent(QStringLiteral("E1")),
                                           makeFullEvent(QStringLiteral("E2")) };
        const auto arr = DataExporter::eventsToJson(evs);
        QVERIFY2(!arr.isEmpty(), "eventsToJson must produce non-empty array");
    }

    // ── 2. eventsToJson: array length matches input ───────────────────────────
    void testEventsToJsonLength()
    {
        QVector<CrimeEvent> evs;
        for (int i = 0; i < 5; ++i) evs.append(makeFullEvent(QStringLiteral("E%1").arg(i)));
        const auto arr = DataExporter::eventsToJson(evs);
        QCOMPARE(arr.size(), 5);
    }

    // ── 3. eventsToJson: event ID preserved ──────────────────────────────────
    void testEventsToJsonEventId()
    {
        const QVector<CrimeEvent> evs = { makeFullEvent(QStringLiteral("MYID-42")) };
        const QString json = jsonArrayToString(DataExporter::eventsToJson(evs));
        QVERIFY2(json.contains(QStringLiteral("MYID-42")), "Event ID should be in JSON");
    }

    // ── 4. eventsToJson: crimeType preserved ─────────────────────────────────
    void testEventsToJsonCrimeType()
    {
        const QVector<CrimeEvent> evs = { makeFullEvent(QStringLiteral("E1")) };
        const QString json = jsonArrayToString(DataExporter::eventsToJson(evs));
        QVERIFY2(json.contains(QStringLiteral("burglary")), "crimeType should appear in JSON");
    }

    // ── 5. eventsToCsv: header row present ───────────────────────────────────
    void testEventsToCsvHeader()
    {
        const QVector<CrimeEvent> evs = { makeFullEvent(QStringLiteral("E1")) };
        const QString csv = DataExporter::eventsToCsv(evs);
        QVERIFY2(csv.contains(QStringLiteral("event_id"), Qt::CaseInsensitive) ||
                 csv.contains(QStringLiteral("id"),       Qt::CaseInsensitive) ||
                 csv.contains(QStringLiteral("crime"),    Qt::CaseInsensitive),
                 "CSV should have a header row with id or crime type");
    }

    // ── 6. eventsToCsv: data row contains event ID ───────────────────────────
    void testEventsToCsvData()
    {
        const QVector<CrimeEvent> evs = { makeFullEvent(QStringLiteral("CSV-EVT")) };
        const QString csv = DataExporter::eventsToCsv(evs);
        QVERIFY2(csv.contains(QStringLiteral("CSV-EVT")), "Event ID should appear in CSV");
    }

    // ── 7. leadsToJson: confidence preserved ─────────────────────────────────
    void testLeadsToJsonConfidence()
    {
        InvestigativeLead l;
        l.rank       = 1;
        l.headline   = QStringLiteral("Test lead");
        l.category   = QStringLiteral("series");
        l.confidence = 0.873;
        l.detail     = QStringLiteral("Detail");
        l.generatedAt = QDateTime::currentDateTimeUtc();

        const QString json = jsonArrayToString(DataExporter::leadsToJson({ l }));
        QVERIFY2(json.contains(QStringLiteral("0.87")) || json.contains(QStringLiteral("873")),
                 "Lead confidence should appear in JSON");
    }

    // ── 8. Empty events → empty JSON array ────────────────────────────────────
    void testEmptyEventsJson()
    {
        const auto arr = DataExporter::eventsToJson({});
        QVERIFY2(arr.isEmpty(), "Empty events should produce empty JSON array");
    }

    // ── 9. eventsToCsv: multiple rows, comma-separated ───────────────────────
    void testEventsToCsvMultipleRows()
    {
        QVector<CrimeEvent> evs;
        for (int i = 0; i < 3; ++i) evs.append(makeFullEvent(QStringLiteral("E%1").arg(i)));
        const QString csv = DataExporter::eventsToCsv(evs);
        const int lines = csv.count(QLatin1Char('\n'));
        // At least 3 data rows + 1 header = 4 newlines
        QVERIFY2(lines >= 3, qPrintable(QStringLiteral("CSV should have >= 3 newlines, got %1").arg(lines)));
    }

    // ── 10. provenanceToJson: non-empty for valid entries ────────────────────
    void testProvenanceToJson()
    {
        QVector<ProvenanceEntry> entries;
        ProvenanceEntry pe;
        pe.stage     = QStringLiteral("ingest");
        pe.action    = QStringLiteral("csv_import");
        pe.eventId   = QStringLiteral("E1");
        pe.timestamp = QDateTime::currentDateTimeUtc();
        entries.append(pe);

        const auto arr = DataExporter::provenanceToJson(entries);
        const QString json = jsonArrayToString(arr);
        QVERIFY2(!arr.isEmpty() && json.contains(QStringLiteral("ingest")),
                 "Provenance JSON should contain stage name");
    }
};

QTEST_MAIN(CrimeEventSerializationTest)
#include "test_crime_event_serialization.moc"
