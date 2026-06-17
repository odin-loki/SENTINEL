// test_temporal_features_edge.cpp
// Edge case and seasonal pattern tests for TemporalFeatures.
#include <QTest>
#include <QTimeZone>
#include "models/TemporalFeatures.h"
#include <cmath>

class TemporalFeaturesEdgeTest : public QObject
{
    Q_OBJECT

private:
    static QDateTime utc(int year, int month, int day, int hour, int min = 0)
    {
        return QDateTime(QDate(year, month, day), QTime(hour, min, 0), QTimeZone::utc());
    }

private slots:

    // 1. compute: all cyclical values in [-1, +1]
    void testCyclicalValuesInRange()
    {
        const auto f = TemporalFeatures::compute(utc(2024, 6, 15, 14));
        QVERIFY2(std::abs(f.hourSin)  <= 1.0 + 1e-9, "hourSin must be in [-1,1]");
        QVERIFY2(std::abs(f.hourCos)  <= 1.0 + 1e-9, "hourCos must be in [-1,1]");
        QVERIFY2(std::abs(f.dowSin)   <= 1.0 + 1e-9, "dowSin must be in [-1,1]");
        QVERIFY2(std::abs(f.dowCos)   <= 1.0 + 1e-9, "dowCos must be in [-1,1]");
        QVERIFY2(std::abs(f.monthSin) <= 1.0 + 1e-9, "monthSin must be in [-1,1]");
        QVERIFY2(std::abs(f.monthCos) <= 1.0 + 1e-9, "monthCos must be in [-1,1]");
    }

    // 2. isNight: midnight is night
    void testMidnightIsNight()
    {
        const auto f = TemporalFeatures::compute(utc(2024, 3, 15, 0));
        QVERIFY2(f.isNight, "Midnight should be flagged as night");
    }

    // 3. isNight: midday is not night
    void testMiddayIsNotNight()
    {
        const auto f = TemporalFeatures::compute(utc(2024, 3, 15, 12));
        QVERIFY2(!f.isNight, "Midday should not be night");
    }

    // 4. isWeekend: Saturday is weekend
    void testSaturdayIsWeekend()
    {
        // 2024-01-06 is a Saturday
        const auto f = TemporalFeatures::compute(utc(2024, 1, 6, 10));
        QVERIFY2(f.isWeekend, "Saturday should be flagged as weekend");
    }

    // 5. isWeekend: Wednesday is not weekend
    void testWednesdayIsNotWeekend()
    {
        // 2024-01-03 is a Wednesday
        const auto f = TemporalFeatures::compute(utc(2024, 1, 3, 10));
        QVERIFY2(!f.isWeekend, "Wednesday should not be weekend");
    }

    // 6. hourRaw and dowRaw are in valid ranges
    void testRawFieldsInRange()
    {
        const auto f = TemporalFeatures::compute(utc(2024, 7, 20, 17));
        QVERIFY2(f.hourRaw >= 0 && f.hourRaw <= 23,
                 qPrintable(QStringLiteral("hourRaw %1 must be 0-23").arg(f.hourRaw)));
        QVERIFY2(f.dowRaw >= 0 && f.dowRaw <= 6,
                 qPrintable(QStringLiteral("dowRaw %1 must be 0-6").arg(f.dowRaw)));
    }

    // 7. lunarPhase in [0, 1]
    void testLunarPhaseRange()
    {
        const auto f = TemporalFeatures::compute(utc(2024, 5, 23, 20));
        QVERIFY2(f.lunarPhase >= 0.0 && f.lunarPhase <= 1.0,
                 qPrintable(QStringLiteral("lunarPhase %1 must be in [0,1]").arg(f.lunarPhase)));
    }

    // 8. weekOfMonth in [1, 5]
    void testWeekOfMonthRange()
    {
        for (int day = 1; day <= 28; day += 7) {
            const auto f = TemporalFeatures::compute(utc(2024, 2, day, 12));
            QVERIFY2(f.weekOfMonth >= 1 && f.weekOfMonth <= 5,
                     qPrintable(QStringLiteral("weekOfMonth %1 for day %2 must be 1-5")
                        .arg(f.weekOfMonth).arg(day)));
        }
    }

    // 9. Different hours produce different hourCos values (hour 0 vs hour 6)
    void testDifferentHoursDifferentCos()
    {
        const auto f0  = TemporalFeatures::compute(utc(2024, 1, 15, 0));
        const auto f6  = TemporalFeatures::compute(utc(2024, 1, 15, 6));
        QVERIFY2(std::abs(f0.hourCos - f6.hourCos) > 1e-6,
                 qPrintable(QStringLiteral("Hour 0 hourCos=%1, Hour 6 hourCos=%2 should differ")
                    .arg(f0.hourCos).arg(f6.hourCos)));
    }

    // 10. Hour 22 is night, hour 6 is not night
    void testNightBoundary()
    {
        const auto f22 = TemporalFeatures::compute(utc(2024, 8, 1, 22));
        const auto f6  = TemporalFeatures::compute(utc(2024, 8, 1, 6));
        QVERIFY2(f22.isNight, "Hour 22 should be night");
        QVERIFY2(!f6.isNight,  "Hour 6 should not be night (>5)");
    }
};

QTEST_MAIN(TemporalFeaturesEdgeTest)
#include "test_temporal_features_edge.moc"
