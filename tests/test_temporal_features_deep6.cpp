// test_temporal_features_deep6.cpp — Deep audit iteration 25: TemporalFeatures
// cyclical encoding, lunar phase, payday, night/weekend flags.
#include <QTest>
#include <cmath>
#include "models/TemporalFeatures.h"

class TestTemporalFeaturesDeep6 : public QObject
{
    Q_OBJECT

private slots:

    void testCyclicalEncodingBounded()
    {
        const QDateTime dt(QDate(2024, 6, 15), QTime(14, 30), Qt::UTC);
        const auto f = TemporalFeatures::compute(dt);
        QVERIFY2(f.hourSin >= -1.0 && f.hourSin <= 1.0, "hourSin bounds");
        QVERIFY2(f.hourCos >= -1.0 && f.hourCos <= 1.0, "hourCos bounds");
        QVERIFY2(f.dowSin >= -1.0 && f.dowSin <= 1.0, "dowSin bounds");
        QVERIFY2(f.monthSin >= -1.0 && f.monthSin <= 1.0, "monthSin bounds");
    }

    void testNightFlagAtMidnight()
    {
        const QDateTime midnight(QDate(2024, 1, 1), QTime(2, 0), Qt::UTC);
        const auto f = TemporalFeatures::compute(midnight);
        QVERIFY(f.isNight);
    }

    void testWeekendSaturday()
    {
        const QDateTime sat(QDate(2024, 1, 6), QTime(12, 0), Qt::UTC);
        const auto f = TemporalFeatures::compute(sat);
        QVERIFY(f.isWeekend);
        QCOMPARE(f.dowRaw, 5);
    }

    void testLunarPhaseInRange()
    {
        const QDateTime dt(QDate(2024, 3, 15), QTime(0, 0), Qt::UTC);
        const auto f = TemporalFeatures::compute(dt);
        QVERIFY2(f.lunarPhase >= 0.0 && f.lunarPhase <= 1.0,
                 qPrintable(QStringLiteral("lunar=%1").arg(f.lunarPhase)));
    }

    void testPaydayDistanceNonNegative()
    {
        const QDateTime dt(QDate(2024, 7, 10), QTime(9, 0), Qt::UTC);
        const auto f = TemporalFeatures::compute(dt);
        QVERIFY(f.daysFromPayday >= 0);
    }

    void testSunAltitudeWinterLower()
    {
        const QDateTime summer(QDate(2024, 6, 21), QTime(12, 0), Qt::UTC);
        const QDateTime winter(QDate(2024, 12, 21), QTime(12, 0), Qt::UTC);
        const auto fs = TemporalFeatures::compute(summer);
        const auto fw = TemporalFeatures::compute(winter);
        QVERIFY2(fs.sunAltitudeDeg > fw.sunAltitudeDeg,
                 "summer noon altitude should exceed winter");
    }

    void testHourRawMatchesInput()
    {
        const QDateTime dt(QDate(2024, 5, 1), QTime(17, 0), Qt::UTC);
        const auto f = TemporalFeatures::compute(dt);
        QCOMPARE(f.hourRaw, 17);
    }
};

QTEST_GUILESS_MAIN(TestTemporalFeaturesDeep6)
#include "test_temporal_features_deep6.moc"
