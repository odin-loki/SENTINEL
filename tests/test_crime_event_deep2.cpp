// test_crime_event_deep2.cpp — Iteration 13: CrimeEvent defaults, optional lat/lon,
// and quality score range.
#include <QTest>
#include <cmath>
#include "core/CrimeEvent.h"

class TestCrimeEventDeep2 : public QObject
{
    Q_OBJECT

private slots:

    void testDefaultConstruction()
    {
        const CrimeEvent ev;

        QVERIFY(ev.eventId.isEmpty());
        QVERIFY(ev.source.isEmpty());
        QVERIFY(ev.crimeType.isEmpty());
        QVERIFY(ev.suburb.isEmpty());
        QVERIFY(ev.outcome.isEmpty());
        QVERIFY(!ev.occurredAt.has_value());
        QVERIFY(!ev.reportedAt.has_value());
        QVERIFY(!ev.lat.has_value());
        QVERIFY(!ev.lon.has_value());
        QVERIFY(!ev.conviction.has_value());
        QVERIFY(ev.meta.isEmpty());

        QCOMPARE(ev.qualityScore, 0.5);
        QCOMPARE(ev.latitude,  0.0);
        QCOMPARE(ev.longitude, 0.0);
        QVERIFY(!ev.timestamp.isValid());
    }

    void testOptionalLatLonAbsentByDefault()
    {
        CrimeEvent ev;
        ev.eventId = QStringLiteral("GEO-ABSENT");

        QVERIFY(!ev.lat.has_value());
        QVERIFY(!ev.lon.has_value());
        QCOMPARE(ev.latitude,  0.0);
        QCOMPARE(ev.longitude, 0.0);
    }

    void testOptionalLatLonPresent()
    {
        CrimeEvent ev;
        ev.lat = 51.5074;
        ev.lon = -0.1278;
        ev.latitude  = ev.lat.value();
        ev.longitude = ev.lon.value();

        QVERIFY(ev.lat.has_value());
        QVERIFY(ev.lon.has_value());
        QVERIFY(qAbs(ev.lat.value() - 51.5074) < 1e-9);
        QVERIFY(qAbs(ev.lon.value() + 0.1278) < 1e-9);
    }

    void testQualityScoreDefaultInRange()
    {
        const CrimeEvent ev;
        QVERIFY(ev.qualityScore >= 0.0);
        QVERIFY(ev.qualityScore <= 1.0);
        QCOMPARE(ev.qualityScore, 0.5);
    }

    void testQualityScoreBoundaryValues()
    {
        CrimeEvent low;
        low.qualityScore = 0.0;
        QVERIFY(low.qualityScore >= 0.0);
        QVERIFY(low.qualityScore <= 1.0);

        CrimeEvent high;
        high.qualityScore = 1.0;
        QVERIFY(high.qualityScore >= 0.0);
        QVERIFY(high.qualityScore <= 1.0);

        CrimeEvent mid;
        mid.qualityScore = 0.73;
        QVERIFY(mid.qualityScore >= 0.0);
        QVERIFY(mid.qualityScore <= 1.0);
    }

    void testFlatFieldsMirrorOptionals()
    {
        CrimeEvent ev;
        const QDateTime dt = QDateTime(QDate(2024, 1, 15), QTime(12, 0, 0), QTimeZone::utc());
        ev.occurredAt = dt;
        ev.timestamp  = dt;
        ev.locationRaw = QStringLiteral("14 Baker St");
        ev.locationDescription = ev.locationRaw.value();
        ev.lat = 51.52;
        ev.lon = -0.16;
        ev.latitude  = ev.lat.value();
        ev.longitude = ev.lon.value();

        QCOMPARE(ev.timestamp, dt);
        QCOMPARE(ev.locationDescription, QStringLiteral("14 Baker St"));
        QVERIFY(qAbs(ev.latitude  - 51.52) < 1e-9);
        QVERIFY(qAbs(ev.longitude + 0.16)  < 1e-9);
    }
};

QTEST_GUILESS_MAIN(TestCrimeEventDeep2)
#include "test_crime_event_deep2.moc"
