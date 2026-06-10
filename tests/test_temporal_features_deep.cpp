// test_temporal_features_deep.cpp
// Validates TemporalFeatures cyclical encoding, lunar phase, night/dark detection,
// payday distance, and edge cases.
#include <QTest>
#include "models/TemporalFeatures.h"
#include <cmath>

class TemporalFeaturesDeepTest : public QObject
{
    Q_OBJECT

private:
    static QDateTime makeUTC(int year, int month, int day, int hour, int minute = 0)
    {
        return QDateTime(QDate(year, month, day), QTime(hour, minute, 0), QTimeZone::utc());
    }

private slots:

    // ── 1. Cyclical hour encoding: sin²+cos²=1 ────────────────────────────────
    void testHourCyclicalUnitCircle()
    {
        for (int h = 0; h < 24; ++h) {
            const auto fv = TemporalFeatures::compute(makeUTC(2024, 6, 1, h));
            const double r2 = fv.hourSin * fv.hourSin + fv.hourCos * fv.hourCos;
            QVERIFY2(std::abs(r2 - 1.0) < 1e-9,
                     qPrintable(QStringLiteral("Hour %1: sin²+cos²=%2 should be 1.0")
                        .arg(h).arg(r2)));
        }
    }

    // ── 2. Cyclical DOW encoding: sin²+cos²=1 ────────────────────────────────
    void testDowCyclicalUnitCircle()
    {
        const QDate monday(2024, 1, 8);
        for (int d = 0; d < 7; ++d) {
            const QDateTime dt(monday.addDays(d), QTime(12, 0, 0), QTimeZone::utc());
            const auto fv = TemporalFeatures::compute(dt);
            const double r2 = fv.dowSin * fv.dowSin + fv.dowCos * fv.dowCos;
            QVERIFY2(std::abs(r2 - 1.0) < 1e-9,
                     qPrintable(QStringLiteral("DOW day=%1: sin²+cos²=%2 should be 1.0")
                        .arg(d).arg(r2)));
        }
    }

    // ── 3. isNight: hour 23 is night ─────────────────────────────────────────
    void testIsNightLateHour()
    {
        const auto fv = TemporalFeatures::compute(makeUTC(2024, 6, 1, 23));
        QVERIFY2(fv.isNight, "Hour 23 should be night");
    }

    // ── 4. isNight: hour 3 is night ──────────────────────────────────────────
    void testIsNightEarlyMorning()
    {
        const auto fv = TemporalFeatures::compute(makeUTC(2024, 6, 1, 3));
        QVERIFY2(fv.isNight, "Hour 3 should be night");
    }

    // ── 5. isNight: hour 12 is not night ─────────────────────────────────────
    void testIsNotNightMidday()
    {
        const auto fv = TemporalFeatures::compute(makeUTC(2024, 6, 1, 12));
        QVERIFY2(!fv.isNight, "Hour 12 should not be night");
    }

    // ── 6. isWeekend: Saturday is weekend ────────────────────────────────────
    void testIsWeekendSaturday()
    {
        // 2024-01-06 = Saturday
        const QDateTime saturday(QDate(2024, 1, 6), QTime(12, 0, 0), QTimeZone::utc());
        const auto fv = TemporalFeatures::compute(saturday);
        QVERIFY2(fv.isWeekend, "Saturday should be weekend");
    }

    // ── 7. isWeekend: Monday is not weekend ──────────────────────────────────
    void testIsNotWeekendMonday()
    {
        // 2024-01-08 = Monday
        const QDateTime monday(QDate(2024, 1, 8), QTime(10, 0, 0), QTimeZone::utc());
        const auto fv = TemporalFeatures::compute(monday);
        QVERIFY2(!fv.isWeekend, "Monday should not be weekend");
    }

    // ── 8. lunarPhase is in [0, 1] ───────────────────────────────────────────
    void testLunarPhaseRange()
    {
        for (int month = 1; month <= 12; ++month) {
            const auto fv = TemporalFeatures::compute(makeUTC(2024, month, 15, 12));
            QVERIFY2(fv.lunarPhase >= 0.0 && fv.lunarPhase <= 1.0,
                     qPrintable(QStringLiteral("Lunar phase %1 must be in [0,1] for month %2")
                        .arg(fv.lunarPhase).arg(month)));
        }
    }

    // ── 9. daysFromPayday is non-negative ────────────────────────────────────
    void testDaysFromPaydayNonNegative()
    {
        for (int day = 1; day <= 28; ++day) {
            const auto fv = TemporalFeatures::compute(makeUTC(2024, 3, day, 12));
            QVERIFY2(fv.daysFromPayday >= 0,
                     qPrintable(QStringLiteral("daysFromPayday %1 must be >= 0 for day %2")
                        .arg(fv.daysFromPayday).arg(day)));
        }
    }

    // ── 10. hourRaw and dowRaw have correct range ─────────────────────────────
    void testRawFieldsRange()
    {
        for (int h = 0; h < 24; ++h) {
            const auto fv = TemporalFeatures::compute(makeUTC(2024, 6, 1, h));
            QVERIFY2(fv.hourRaw >= 0 && fv.hourRaw <= 23,
                     qPrintable(QStringLiteral("hourRaw %1 must be in [0,23]").arg(fv.hourRaw)));
            QVERIFY2(fv.dowRaw >= 0 && fv.dowRaw <= 6,
                     qPrintable(QStringLiteral("dowRaw %1 must be in [0,6]").arg(fv.dowRaw)));
        }
    }
};

QTEST_MAIN(TemporalFeaturesDeepTest)
#include "test_temporal_features_deep.moc"
