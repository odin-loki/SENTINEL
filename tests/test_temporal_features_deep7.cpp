// test_temporal_features_deep7.cpp — Deep audit iteration 28: TemporalFeatures
// weekOfMonth, isDark, doy encoding, hourRaw, sun altitude.
#include <QTest>
#include <cmath>
#include "models/TemporalFeatures.h"

class TestTemporalFeaturesDeep7 : public QObject
{
    Q_OBJECT

private slots:

    void testWeekOfMonthInRange()
    {
        const QDateTime dt(QDate(2024, 3, 18), QTime(12, 0), Qt::UTC);
        const auto f = TemporalFeatures::compute(dt);
        QVERIFY(f.weekOfMonth >= 1 && f.weekOfMonth <= 5);
    }

    void testIsDarkAtMidnight()
    {
        const QDateTime dt(QDate(2024, 1, 15), QTime(2, 0), Qt::UTC);
        const auto f = TemporalFeatures::compute(dt);
        QVERIFY(f.isDark);
    }

    void testDayOfYearEncodingBounded()
    {
        const QDateTime dt(QDate(2024, 7, 4), QTime(6, 0), Qt::UTC);
        const auto f = TemporalFeatures::compute(dt);
        QVERIFY2(f.doySin >= -1.0 && f.doySin <= 1.0, "doySin");
        QVERIFY2(f.doyCos >= -1.0 && f.doyCos <= 1.0, "doyCos");
    }

    void testHourRawMatchesInput()
    {
        const QDateTime dt(QDate(2024, 5, 5), QTime(17, 45), Qt::UTC);
        const auto f = TemporalFeatures::compute(dt);
        QCOMPARE(f.hourRaw, 17);
    }

    void testSunAltitudeMiddayPositiveSummer()
    {
        const QDateTime summer(QDate(2024, 6, 21), QTime(12, 0), Qt::UTC);
        const auto f = TemporalFeatures::compute(summer);
        QVERIFY(f.sunAltitudeDeg > 0.0);
    }

    void testDaytimeNotNight()
    {
        const QDateTime dt(QDate(2024, 6, 15), QTime(12, 0), Qt::UTC);
        const auto f = TemporalFeatures::compute(dt);
        QVERIFY(!f.isNight);
    }
};

QTEST_GUILESS_MAIN(TestTemporalFeaturesDeep7)
#include "test_temporal_features_deep7.moc"
