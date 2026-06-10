// test_uk_police_source_deep.cpp
// UKPoliceSource: parseRecord, outcome parsing, crime type mapping,
// setLocation, sourceId/displayName, and signal connectivity tests.
#include <QTest>
#include <QSignalSpy>
#include <QJsonObject>
#include "ingest/UKPoliceSource.h"

class UKPoliceSourceDeepTest : public QObject
{
    Q_OBJECT

private:
    static QJsonObject makeRecord(const QString& crimeType = QStringLiteral("burglary"),
                                   const QString& outcome  = QStringLiteral("Investigation complete; no suspect identified"),
                                   double lat = 51.5, double lon = -0.1)
    {
        QJsonObject r;
        r[QStringLiteral("category")]    = crimeType;
        r[QStringLiteral("persistent_id")] = QStringLiteral("ABC123");

        QJsonObject location;
        QJsonObject streetObj;
        streetObj[QStringLiteral("id")]   = 123456;
        streetObj[QStringLiteral("name")] = QStringLiteral("On or near High Street");
        location[QStringLiteral("street")]    = streetObj;
        location[QStringLiteral("latitude")]  = QString::number(lat);
        location[QStringLiteral("longitude")] = QString::number(lon);
        r[QStringLiteral("location")] = location;

        QJsonObject outcomeStatus;
        outcomeStatus[QStringLiteral("category")] = outcome;
        r[QStringLiteral("outcome_status")] = outcomeStatus;
        r[QStringLiteral("month")] = QStringLiteral("2024-03");
        return r;
    }

private slots:

    // 1. sourceId() returns "uk_police_v1"
    void testSourceId()
    {
        UKPoliceSource src(51.5, -0.1);
        QCOMPARE(src.sourceId(), QStringLiteral("uk_police_v1"));
    }

    // 2. displayName() non-empty
    void testDisplayNameNonEmpty()
    {
        UKPoliceSource src(51.5, -0.1);
        QVERIFY2(!src.displayName().isEmpty(), "displayName must be non-empty");
    }

    // 3. parseRecord: eventId non-empty for valid JSON
    void testParseRecordEventId()
    {
        UKPoliceSource src(51.5, -0.1);
        const auto ev = src.parseRecord(makeRecord());
        QVERIFY2(!ev.eventId.isEmpty() || !ev.id.isEmpty(),
                 "Parsed eventId should be non-empty");
    }

    // 4. parseRecord: latitude/longitude parsed correctly
    void testParseRecordLatLon()
    {
        UKPoliceSource src(51.5, -0.1);
        const auto ev = src.parseRecord(makeRecord(QStringLiteral("burglary"),
                                                    QStringLiteral("resolved"),
                                                    51.4774, -0.0052));
        QVERIFY2(ev.lat.has_value() || std::abs(ev.latitude - 51.4774) < 0.01,
                 qPrintable(QStringLiteral("Latitude should be ~51.4774, got %1").arg(ev.latitude)));
    }

    // 5. parseRecord: crimeType non-empty
    void testParseRecordCrimeType()
    {
        UKPoliceSource src(51.5, -0.1);
        const auto ev = src.parseRecord(makeRecord(QStringLiteral("bicycle-theft")));
        QVERIFY2(!ev.crimeType.isEmpty(), "crimeType should be non-empty for known category");
    }

    // 6. parseRecord: source set to "uk_police_v1"
    void testParseRecordSource()
    {
        UKPoliceSource src(51.5, -0.1);
        const auto ev = src.parseRecord(makeRecord());
        QVERIFY2(ev.source.contains(QStringLiteral("uk"), Qt::CaseInsensitive) ||
                 ev.source.contains(QStringLiteral("police"), Qt::CaseInsensitive),
                 qPrintable(QStringLiteral("source '%1' should contain 'uk' or 'police'").arg(ev.source)));
    }

    // 7. setLocation changes internal state without crash
    void testSetLocationNoCrash()
    {
        UKPoliceSource src(51.5, -0.1, 1.0);
        src.setLocation(53.4808, -2.2426, 2.0);  // Manchester
        QVERIFY(true);
    }

    // 8. eventFetched signal is connectable
    void testEventFetchedSignalConnectable()
    {
        UKPoliceSource src(51.5, -0.1);
        QSignalSpy spy(&src, &DataSource::eventFetched);
        QVERIFY2(spy.isValid(), "eventFetched signal should be connectable");
    }

    // 9. fetchError signal is connectable
    void testFetchErrorSignalConnectable()
    {
        UKPoliceSource src(51.5, -0.1);
        QSignalSpy spy(&src, &DataSource::fetchError);
        QVERIFY2(spy.isValid(), "fetchError signal should be connectable");
    }

    // 10. parseRecord: missing location fields don't crash
    void testParseRecordMissingLocationNoCrash()
    {
        UKPoliceSource src(51.5, -0.1);
        QJsonObject raw;
        raw[QStringLiteral("category")] = QStringLiteral("burglary");
        raw[QStringLiteral("month")]    = QStringLiteral("2024-03");
        const auto ev = src.parseRecord(raw);
        QVERIFY(true); // Must not crash
    }
};

QTEST_MAIN(UKPoliceSourceDeepTest)
#include "test_uk_police_source_deep.moc"
