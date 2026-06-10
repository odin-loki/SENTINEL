// test_uk_police_source_parsing.cpp
// Tests UKPoliceSource JSON record parsing without network access.
#include <QTest>
#include <QJsonObject>
#include "ingest/UKPoliceSource.h"
#include "core/CrimeEvent.h"
#include <cmath>

class UKPoliceSourceParsingTest : public QObject
{
    Q_OBJECT

private:
    UKPoliceSource* m_src = nullptr;

    static QJsonObject fullRecord()
    {
        QJsonObject obj;
        obj[QStringLiteral("id")]         = QStringLiteral("38b01fe3abc123");
        obj[QStringLiteral("category")]   = QStringLiteral("burglary");
        obj[QStringLiteral("month")]      = QStringLiteral("2024-03");
        obj[QStringLiteral("persistent_id")] = QStringLiteral("abc123");

        QJsonObject loc;
        loc[QStringLiteral("latitude")]  = QStringLiteral("51.5074");
        loc[QStringLiteral("longitude")] = QStringLiteral("-0.1278");

        QJsonObject street;
        street[QStringLiteral("name")] = QStringLiteral("On or near Oxford Street");
        loc[QStringLiteral("street")] = street;
        obj[QStringLiteral("location")] = loc;

        QJsonObject outcome;
        outcome[QStringLiteral("category")] = QJsonObject{{QStringLiteral("name"), QStringLiteral("Under investigation")}};
        obj[QStringLiteral("outcome_status")] = outcome;

        return obj;
    }

private slots:

    void initTestCase()
    {
        m_src = new UKPoliceSource(51.5, -0.1, 1.0, this);
    }

    void cleanupTestCase()
    {
        delete m_src;
        m_src = nullptr;
    }

    // ── 1. parseRecord produces non-empty eventId ────────────────────────────
    void testParseRecordEventId()
    {
        const auto ev = m_src->parseRecord(fullRecord());
        QVERIFY2(!ev.eventId.isEmpty(), "parseRecord must set a non-empty eventId");
    }

    // ── 2. parseRecord sets crimeType from category ───────────────────────────
    void testParseRecordCrimeType()
    {
        const auto ev = m_src->parseRecord(fullRecord());
        QVERIFY2(!ev.crimeType.isEmpty(), "crimeType must be set from category");
    }

    // ── 3. parseRecord: lat/lon parsed ────────────────────────────────────────
    void testParseRecordLatLon()
    {
        const auto ev = m_src->parseRecord(fullRecord());
        const double lat = ev.lat.value_or(0.0);
        const double lon = ev.lon.value_or(0.0);
        QVERIFY2(std::abs(lat - 51.5074) < 0.01,
                 qPrintable(QStringLiteral("Latitude %1 should be ~51.5074").arg(lat)));
        QVERIFY2(std::abs(lon - (-0.1278)) < 0.01,
                 qPrintable(QStringLiteral("Longitude %1 should be ~-0.1278").arg(lon)));
    }

    // ── 4. parseRecord: timestamp is set from month ───────────────────────────
    void testParseRecordTimestamp()
    {
        const auto ev = m_src->parseRecord(fullRecord());
        const bool hasTimestamp = (ev.occurredAt.has_value() && ev.occurredAt->isValid())
                               || ev.timestamp.isValid();
        QVERIFY2(hasTimestamp, "Timestamp should be set from month field");
    }

    // ── 5. parseRecord: source set correctly ─────────────────────────────────
    void testParseRecordSource()
    {
        const auto ev = m_src->parseRecord(fullRecord());
        QVERIFY2(!ev.source.isEmpty(), "Source field must be set");
    }

    // ── 6. parseRecord: empty JSON → graceful fallback ────────────────────────
    void testParseRecordEmpty()
    {
        const auto ev = m_src->parseRecord(QJsonObject{});
        Q_UNUSED(ev);
        QVERIFY(true);  // no crash
    }

    // ── 7. sourceId returns expected string ──────────────────────────────────
    void testSourceId()
    {
        QCOMPARE(m_src->sourceId(), QStringLiteral("uk_police_v1"));
    }

    // ── 8. displayName is non-empty ───────────────────────────────────────────
    void testDisplayName()
    {
        QVERIFY2(!m_src->displayName().isEmpty(), "displayName must be non-empty");
    }

    // ── 9. parseRecord: suburb or locationRaw populated from street.name ──────
    void testParseRecordStreet()
    {
        const auto ev = m_src->parseRecord(fullRecord());
        const bool hasLocation = !ev.suburb.isEmpty() ||
                                 (ev.locationRaw.has_value() && !ev.locationRaw->isEmpty());
        QVERIFY2(hasLocation, "Street name should populate suburb or locationRaw");
    }

    // ── 10. setLocation doesn't crash ────────────────────────────────────────
    void testSetLocation()
    {
        m_src->setLocation(52.0, 0.0, 2.0);
        QVERIFY(true);  // no crash
    }
};

QTEST_MAIN(UKPoliceSourceParsingTest)
#include "test_uk_police_source_parsing.moc"
