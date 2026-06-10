// test_temporal_features.cpp — TemporalFeatures unit tests
#include <QTest>
#include <QCoreApplication>
#include "models/TemporalFeatures.h"
#include <cmath>

class TestTemporalFeatures : public QObject
{
    Q_OBJECT

private slots:
    void testMidnightHourEncoding()
    {
        QDateTime dt = QDateTime(QDate(2024,1,15), QTime(0,0,0), Qt::UTC);
        auto f = TemporalFeatures::compute(dt);
        QCOMPARE(f.hourRaw, 0);
        QVERIFY(f.isNight);
        QVERIFY(std::abs(f.hourSin - 0.0) < 1e-6);
        QVERIFY(std::abs(f.hourCos - 1.0) < 1e-6);
    }

    void testNoonHourEncoding()
    {
        QDateTime dt = QDateTime(QDate(2024,1,15), QTime(12,0,0), Qt::UTC);
        auto f = TemporalFeatures::compute(dt);
        QCOMPARE(f.hourRaw, 12);
        QVERIFY(!f.isNight);
        QVERIFY(std::abs(f.hourSin) < 1e-6);
        QVERIFY(std::abs(f.hourCos - (-1.0)) < 1e-6);
    }

    void testNightHours()
    {
        QDateTime dt22 = QDateTime(QDate(2024,1,15), QTime(22,0,0), Qt::UTC);
        QVERIFY(TemporalFeatures::compute(dt22).isNight);
        QDateTime dt5  = QDateTime(QDate(2024,1,15), QTime(5,0,0), Qt::UTC);
        QVERIFY(TemporalFeatures::compute(dt5).isNight);
        QDateTime dt6  = QDateTime(QDate(2024,1,15), QTime(6,0,0), Qt::UTC);
        QVERIFY(!TemporalFeatures::compute(dt6).isNight);
    }

    void testWeekendDetection()
    {
        // 2024-01-13 is Saturday
        QDateTime sat = QDateTime(QDate(2024,1,13), QTime(12,0,0), Qt::UTC);
        QVERIFY(TemporalFeatures::compute(sat).isWeekend);
        // 2024-01-14 is Sunday
        QDateTime sun = QDateTime(QDate(2024,1,14), QTime(12,0,0), Qt::UTC);
        QVERIFY(TemporalFeatures::compute(sun).isWeekend);
        // 2024-01-15 is Monday
        QDateTime mon = QDateTime(QDate(2024,1,15), QTime(12,0,0), Qt::UTC);
        QVERIFY(!TemporalFeatures::compute(mon).isWeekend);
    }

    void testDowEncoding()
    {
        QDateTime mon = QDateTime(QDate(2024,1,15), QTime(12,0,0), Qt::UTC);
        QCOMPARE(TemporalFeatures::compute(mon).dowRaw, 0);
        QDateTime sun = QDateTime(QDate(2024,1,21), QTime(12,0,0), Qt::UTC);
        QCOMPARE(TemporalFeatures::compute(sun).dowRaw, 6);
    }

    void testCyclicFeaturesAreNormalised()
    {
        QDateTime dt = QDateTime(QDate(2024,6,15), QTime(9,30,0), Qt::UTC);
        auto f = TemporalFeatures::compute(dt);
        QVERIFY(f.hourSin >= -1.0 && f.hourSin <= 1.0);
        QVERIFY(f.hourCos >= -1.0 && f.hourCos <= 1.0);
        QVERIFY(f.dowSin >= -1.0 && f.dowSin <= 1.0);
        QVERIFY(f.monthSin >= -1.0 && f.monthSin <= 1.0);
        QVERIFY(f.doySin >= -1.0 && f.doySin <= 1.0);
    }

    void testLunarPhaseRange()
    {
        QDateTime dt = QDateTime(QDate(2024,1,15), QTime(0,0,0), Qt::UTC);
        auto f = TemporalFeatures::compute(dt);
        QVERIFY(f.lunarPhase >= 0.0 && f.lunarPhase <= 1.0);
    }

    void testWeekOfMonth()
    {
        QDateTime dt1 = QDateTime(QDate(2024,1,1), QTime(12,0,0), Qt::UTC);
        QVERIFY(TemporalFeatures::compute(dt1).weekOfMonth >= 1);
        QDateTime dt28 = QDateTime(QDate(2024,1,28), QTime(12,0,0), Qt::UTC);
        QVERIFY(TemporalFeatures::compute(dt28).weekOfMonth >= 4);
    }

    void testSunAltitudeDarkAtNight()
    {
        QDateTime midnight = QDateTime(QDate(2024,1,15), QTime(0,0,0), Qt::UTC);
        auto f = TemporalFeatures::compute(midnight);
        QVERIFY(f.isDark);
        QVERIFY(f.sunAltitudeDeg < -6.0);
    }

    void testSunAltitudeBrightAtNoon()
    {
        QDateTime noon = QDateTime(QDate(2024,6,21), QTime(12,0,0), Qt::UTC);
        auto f = TemporalFeatures::compute(noon);
        QVERIFY(!f.isDark);
    }

    void testDaysFromPayday()
    {
        QDateTime dt1 = QDateTime(QDate(2024,1,15), QTime(12,0,0), Qt::UTC);
        QDateTime dt2 = QDateTime(QDate(2024,1,16), QTime(12,0,0), Qt::UTC);
        auto f1 = TemporalFeatures::compute(dt1);
        auto f2 = TemporalFeatures::compute(dt2);
        QVERIFY(f1.daysFromPayday >= 0);
        QVERIFY(f2.daysFromPayday >= 0);
        QVERIFY(std::abs(f1.daysFromPayday - f2.daysFromPayday) <= 1);
    }

    void testAllMonthsCovered()
    {
        for (int month = 1; month <= 12; ++month) {
            QDateTime dt = QDateTime(QDate(2024, month, 15), QTime(12, 0, 0), Qt::UTC);
            auto f = TemporalFeatures::compute(dt);
            QVERIFY(f.lunarPhase >= 0.0 && f.lunarPhase <= 1.0);
            QVERIFY(f.weekOfMonth >= 1 && f.weekOfMonth <= 6);
        }
    }

    void testHourSinCosCompleteCycle()
    {
        // Over 24 hours, hourSin/hourCos should complete a full cycle
        double sumSin = 0.0, sumCos = 0.0;
        for (int h = 0; h < 24; ++h) {
            QDateTime dt = QDateTime(QDate(2024,1,15), QTime(h,0,0), Qt::UTC);
            auto f = TemporalFeatures::compute(dt);
            sumSin += f.hourSin;
            sumCos += f.hourCos;
        }
        // Sum of sin/cos over a full cycle ≈ 0
        QVERIFY(std::abs(sumSin) < 1e-4);
        QVERIFY(std::abs(sumCos) < 1e-4);
    }

    void testDowSinCosCompleteCycle()
    {
        double sumSin = 0.0, sumCos = 0.0;
        // Iterate over 7 consecutive days
        for (int d = 0; d < 7; ++d) {
            QDateTime dt = QDateTime(QDate(2024,1,1).addDays(d), QTime(12,0,0), Qt::UTC);
            auto f = TemporalFeatures::compute(dt);
            sumSin += f.dowSin;
            sumCos += f.dowCos;
        }
        QVERIFY(std::abs(sumSin) < 1e-4);
        QVERIFY(std::abs(sumCos) < 1e-4);
    }
};

QTEST_MAIN(TestTemporalFeatures)
#include "test_temporal_features.moc"
