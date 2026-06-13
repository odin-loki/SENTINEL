// test_temporal_features_deep5.cpp — Iteration 22 deep audit: invalid input zero vector,
// cyclical bounds, lunar phase, sun altitude, payday distance, hour encoding,
// and weekend flags across Friday–Sunday.
#include <QTest>
#include <QDateTime>
#include <QDate>
#include <QTime>
#include <cmath>

#include "models/TemporalFeatures.h"

static constexpr double EPS = 1e-9;

class TestTemporalFeaturesDeep5 : public QObject
{
    Q_OBJECT

    static QDateTime utc(int year, int month, int day, int hour, int min = 0)
    {
        return QDateTime(QDate(year, month, day), QTime(hour, min, 0), Qt::UTC);
    }

private slots:
    void testInvalidDateTimeReturnsZeroVector();
    void testCyclicalSinCosBounds();
    void testLunarPhaseInUnitInterval();
    void testSunAltitudeNightNegative();
    void testDaysFromPaydayInRange();
    void testHourCyclicalEncoding();
    void testWeekendFlagFridayThroughSunday();
};

void TestTemporalFeaturesDeep5::testInvalidDateTimeReturnsZeroVector()
{
    QDateTime invalid;
    QVERIFY(!invalid.isValid());

    const auto fv = TemporalFeatures::compute(invalid);

    QCOMPARE(fv.hourRaw, 0);
    QCOMPARE(fv.dowRaw, 0);
    QVERIFY(!fv.isWeekend);
    QVERIFY(!fv.isNight);
    QVERIFY(!fv.isPublicHoliday);
    QCOMPARE(fv.daysFromPayday, 0);
    QCOMPARE(fv.weekOfMonth, 0);
    QVERIFY(std::abs(fv.hourSin) < EPS);
    QVERIFY(std::abs(fv.hourCos) < EPS);
    QVERIFY(std::abs(fv.dowSin) < EPS);
    QVERIFY(std::abs(fv.dowCos) < EPS);
    QVERIFY(std::abs(fv.monthSin) < EPS);
    QVERIFY(std::abs(fv.monthCos) < EPS);
    QVERIFY(std::abs(fv.lunarPhase) < EPS);
    QVERIFY(std::abs(fv.sunAltitudeDeg) < EPS);
    QVERIFY(!fv.isDark);
}

void TestTemporalFeaturesDeep5::testCyclicalSinCosBounds()
{
    const auto fv = TemporalFeatures::compute(utc(2024, 6, 15, 14));

    auto inUnitCircle = [](double sinVal, double cosVal, const char* label) {
        const double norm = sinVal * sinVal + cosVal * cosVal;
        QVERIFY2(std::abs(norm - 1.0) < 1e-6,
                 qPrintable(QStringLiteral("%1 encoding not on unit circle").arg(label)));
        QVERIFY2(sinVal >= -1.0 - EPS && sinVal <= 1.0 + EPS,
                 qPrintable(QStringLiteral("%1 sin out of bounds").arg(label)));
        QVERIFY2(cosVal >= -1.0 - EPS && cosVal <= 1.0 + EPS,
                 qPrintable(QStringLiteral("%1 cos out of bounds").arg(label)));
    };

    inUnitCircle(fv.hourSin, fv.hourCos, "hour");
    inUnitCircle(fv.dowSin, fv.dowCos, "dow");
    inUnitCircle(fv.monthSin, fv.monthCos, "month");
    inUnitCircle(fv.doySin, fv.doyCos, "doy");
}

void TestTemporalFeaturesDeep5::testLunarPhaseInUnitInterval()
{
    for (int month = 1; month <= 12; ++month) {
        const auto fv = TemporalFeatures::compute(utc(2024, month, 15, 12));
        QVERIFY2(fv.lunarPhase >= 0.0 && fv.lunarPhase <= 1.0,
                 qPrintable(QStringLiteral("lunarPhase=%1 out of [0,1] for month %2")
                                .arg(fv.lunarPhase).arg(month)));
    }
}

void TestTemporalFeaturesDeep5::testSunAltitudeNightNegative()
{
    const auto midnightWinter = TemporalFeatures::compute(utc(2024, 1, 15, 2));
    const auto midnightSummer = TemporalFeatures::compute(utc(2024, 7, 15, 2));

    QVERIFY2(midnightWinter.sunAltitudeDeg < 0.0,
             qPrintable(QStringLiteral("Winter night altitude expected negative, got %1")
                            .arg(midnightWinter.sunAltitudeDeg)));
    QVERIFY(midnightWinter.isDark);
    QVERIFY2(midnightSummer.sunAltitudeDeg < 0.0,
             "Early-morning summer UTC should still be below horizon at London latitude");
    QVERIFY(midnightSummer.isNight);
}

void TestTemporalFeaturesDeep5::testDaysFromPaydayInRange()
{
    for (int doy = 1; doy <= 366; ++doy) {
        const QDate date = QDate(2024, 1, 1).addDays(doy - 1);
        const auto fv = TemporalFeatures::compute(
            QDateTime(date, QTime(12, 0), Qt::UTC));
        QVERIFY2(fv.daysFromPayday >= 0 && fv.daysFromPayday <= 7,
                 qPrintable(QStringLiteral("daysFromPayday=%1 out of [0,7] on doy %2")
                                .arg(fv.daysFromPayday).arg(doy)));
    }
}

void TestTemporalFeaturesDeep5::testHourCyclicalEncoding()
{
    for (int h = 0; h < 24; ++h) {
        const auto fv = TemporalFeatures::compute(utc(2024, 6, 15, h));
        QCOMPARE(fv.hourRaw, h);

        const double expectedSin = std::sin(2.0 * M_PI * h / 24.0);
        const double expectedCos = std::cos(2.0 * M_PI * h / 24.0);
        QVERIFY2(std::abs(fv.hourSin - expectedSin) < 1e-10,
                 qPrintable(QStringLiteral("hourSin mismatch at hour %1").arg(h)));
        QVERIFY2(std::abs(fv.hourCos - expectedCos) < 1e-10,
                 qPrintable(QStringLiteral("hourCos mismatch at hour %1").arg(h)));
    }
}

void TestTemporalFeaturesDeep5::testWeekendFlagFridayThroughSunday()
{
    // 2024-01-05 Fri, 2024-01-06 Sat, 2024-01-07 Sun (Mon-based dowRaw 4,5,6)
    const auto fri = TemporalFeatures::compute(utc(2024, 1, 5, 12));
    const auto sat = TemporalFeatures::compute(utc(2024, 1, 6, 12));
    const auto sun = TemporalFeatures::compute(utc(2024, 1, 7, 12));

    QCOMPARE(fri.dowRaw, 4);
    QCOMPARE(sat.dowRaw, 5);
    QCOMPARE(sun.dowRaw, 6);

    // Current contract: isWeekend is true for Sat/Sun only (dow >= 5).
    QVERIFY(!fri.isWeekend);
    QVERIFY(sat.isWeekend);
    QVERIFY(sun.isWeekend);

    const auto mon = TemporalFeatures::compute(utc(2024, 1, 1, 12));
    QVERIFY(!mon.isWeekend);
}

QTEST_GUILESS_MAIN(TestTemporalFeaturesDeep5)
#include "test_temporal_features_deep5.moc"
