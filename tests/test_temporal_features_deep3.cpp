#include <QTest>
#include <QDateTime>
#include <QDate>
#include <QTime>
#include <cmath>
#include "models/TemporalFeatures.h"

static constexpr double EPS = 1e-9;

class TemporalFeaturesDeep3Test : public QObject
{
    Q_OBJECT

private:
    // Helper: build QDateTime from a date + hour (UTC)
    static QDateTime dt(int year, int month, int day, int hour, int min = 0)
    {
        return QDateTime(QDate(year, month, day), QTime(hour, min, 0), Qt::UTC);
    }

private slots:

    // ── Hour cyclical encoding ────────────────────────────────────────────────

    void testHourZeroSinZeroCosOne()
    {
        // sin(2π * 0 / 24) = 0,  cos(2π * 0 / 24) = 1
        auto fv = TemporalFeatures::compute(dt(2024, 6, 15, 0));
        QVERIFY2(std::abs(fv.hourSin) < EPS,
                 qPrintable(QStringLiteral("hourOfDay=0: sinHour expected 0, got %1").arg(fv.hourSin)));
        QVERIFY2(std::abs(fv.hourCos - 1.0) < EPS,
                 qPrintable(QStringLiteral("hourOfDay=0: cosHour expected 1, got %1").arg(fv.hourCos)));
        QCOMPARE(fv.hourRaw, 0);
    }

    void testHourSixSinOneCosZero()
    {
        // sin(2π * 6 / 24) = sin(π/2) = 1,  cos(π/2) = 0
        auto fv = TemporalFeatures::compute(dt(2024, 6, 15, 6));
        QVERIFY2(std::abs(fv.hourSin - 1.0) < EPS,
                 qPrintable(QStringLiteral("hourOfDay=6: sinHour expected 1, got %1").arg(fv.hourSin)));
        QVERIFY2(std::abs(fv.hourCos) < EPS,
                 qPrintable(QStringLiteral("hourOfDay=6: cosHour expected 0, got %1").arg(fv.hourCos)));
        QCOMPARE(fv.hourRaw, 6);
    }

    void testHourTwelveSinZeroCosMinusOne()
    {
        // sin(2π * 12 / 24) = sin(π) ≈ 0,  cos(π) = -1
        auto fv = TemporalFeatures::compute(dt(2024, 6, 15, 12));
        QVERIFY2(std::abs(fv.hourSin) < EPS,
                 qPrintable(QStringLiteral("hourOfDay=12: sinHour expected 0, got %1").arg(fv.hourSin)));
        QVERIFY2(std::abs(fv.hourCos - (-1.0)) < EPS,
                 qPrintable(QStringLiteral("hourOfDay=12: cosHour expected -1, got %1").arg(fv.hourCos)));
        QCOMPARE(fv.hourRaw, 12);
    }

    void testHourEighteenSinMinusOneCosZero()
    {
        // sin(2π * 18 / 24) = sin(3π/2) = -1,  cos(3π/2) = 0
        auto fv = TemporalFeatures::compute(dt(2024, 6, 15, 18));
        QVERIFY2(std::abs(fv.hourSin - (-1.0)) < EPS,
                 qPrintable(QStringLiteral("hourOfDay=18: sinHour expected -1, got %1").arg(fv.hourSin)));
        QVERIFY2(std::abs(fv.hourCos) < EPS,
                 qPrintable(QStringLiteral("hourOfDay=18: cosHour expected 0, got %1").arg(fv.hourCos)));
    }

    void testAllHourEncodingsInRange()
    {
        for (int h = 0; h < 24; ++h) {
            auto fv = TemporalFeatures::compute(dt(2024, 3, 10, h));
            QVERIFY2(fv.hourSin >= -1.0 - EPS && fv.hourSin <= 1.0 + EPS,
                     qPrintable(QStringLiteral("hourSin out of [-1,1] at hour=%1: %2").arg(h).arg(fv.hourSin)));
            QVERIFY2(fv.hourCos >= -1.0 - EPS && fv.hourCos <= 1.0 + EPS,
                     qPrintable(QStringLiteral("hourCos out of [-1,1] at hour=%1: %2").arg(h).arg(fv.hourCos)));
            QCOMPARE(fv.hourRaw, h);
        }
    }

    // ── Day-of-week cyclical encoding ─────────────────────────────────────────

    void testDowMondayEncodings()
    {
        // 2024-01-01 is a Monday (Qt dayOfWeek=1, our dow=0)
        // sin(2π * 0 / 7) = 0,  cos(2π * 0 / 7) = 1
        auto fv = TemporalFeatures::compute(dt(2024, 1, 1, 0));
        QVERIFY2(std::abs(fv.dowSin) < EPS,
                 qPrintable(QStringLiteral("Monday: dowSin expected 0, got %1").arg(fv.dowSin)));
        QVERIFY2(std::abs(fv.dowCos - 1.0) < EPS,
                 qPrintable(QStringLiteral("Monday: dowCos expected 1, got %1").arg(fv.dowCos)));
        QCOMPARE(fv.dowRaw, 0);
    }

    void testDowModularSameEncoding()
    {
        // Two Mondays exactly one week apart must produce identical dow encodings
        auto fv1 = TemporalFeatures::compute(dt(2024, 1, 1, 10));   // Monday
        auto fv2 = TemporalFeatures::compute(dt(2024, 1, 8, 10));   // next Monday
        QVERIFY2(std::abs(fv1.dowSin - fv2.dowSin) < EPS, "Mondays 1 week apart: dowSin should match");
        QVERIFY2(std::abs(fv1.dowCos - fv2.dowCos) < EPS, "Mondays 1 week apart: dowCos should match");
        QCOMPARE(fv1.dowRaw, fv2.dowRaw);
    }

    void testDowSundayIndex6()
    {
        // 2024-01-07 is a Sunday (Qt dayOfWeek=7, our dow=6)
        auto fv = TemporalFeatures::compute(dt(2024, 1, 7, 0));
        QCOMPARE(fv.dowRaw, 6);
        QVERIFY(fv.isWeekend);
    }

    void testAllDowEncodingsInRange()
    {
        // 2024-01-01 is Monday. Check all 7 days.
        for (int d = 0; d < 7; ++d) {
            auto fv = TemporalFeatures::compute(dt(2024, 1, 1 + d, 12));
            QVERIFY2(fv.dowSin >= -1.0 - EPS && fv.dowSin <= 1.0 + EPS,
                     qPrintable(QStringLiteral("dowSin out of range at d=%1").arg(d)));
            QVERIFY2(fv.dowCos >= -1.0 - EPS && fv.dowCos <= 1.0 + EPS,
                     qPrintable(QStringLiteral("dowCos out of range at d=%1").arg(d)));
            QCOMPARE(fv.dowRaw, d);
        }
    }

    // ── Month cyclical encoding ───────────────────────────────────────────────

    void testMonthJanuaryIndex0()
    {
        // month=1, index=0 → sin(0)=0, cos(0)=1
        auto fv = TemporalFeatures::compute(dt(2024, 1, 15, 12));
        QVERIFY2(std::abs(fv.monthSin) < EPS,
                 qPrintable(QStringLiteral("January monthSin expected 0, got %1").arg(fv.monthSin)));
        QVERIFY2(std::abs(fv.monthCos - 1.0) < EPS,
                 qPrintable(QStringLiteral("January monthCos expected 1, got %1").arg(fv.monthCos)));
    }

    void testMonthJulyIndex6()
    {
        // month=7, index=6 → sin(2π*6/12)=sin(π)≈0, cos(π)=-1
        auto fv = TemporalFeatures::compute(dt(2024, 7, 15, 12));
        QVERIFY2(std::abs(fv.monthSin) < EPS,
                 qPrintable(QStringLiteral("July monthSin expected ≈0, got %1").arg(fv.monthSin)));
        QVERIFY2(std::abs(fv.monthCos - (-1.0)) < EPS,
                 qPrintable(QStringLiteral("July monthCos expected -1, got %1").arg(fv.monthCos)));
    }

    void testAllMonthEncodingsInRange()
    {
        for (int m = 1; m <= 12; ++m) {
            auto fv = TemporalFeatures::compute(dt(2024, m, 15, 12));
            QVERIFY2(fv.monthSin >= -1.0 - EPS && fv.monthSin <= 1.0 + EPS,
                     qPrintable(QStringLiteral("monthSin out of range at month=%1: %2").arg(m).arg(fv.monthSin)));
            QVERIFY2(fv.monthCos >= -1.0 - EPS && fv.monthCos <= 1.0 + EPS,
                     qPrintable(QStringLiteral("monthCos out of range at month=%1: %2").arg(m).arg(fv.monthCos)));
        }
    }

    // ── Boolean feature correctness ───────────────────────────────────────────

    void testIsNightHours()
    {
        // isNight: hour >= 22 or hour <= 5
        for (int h : {22, 23, 0, 1, 2, 3, 4, 5}) {
            auto fv = TemporalFeatures::compute(dt(2024, 6, 15, h));
            QVERIFY2(fv.isNight, qPrintable(QStringLiteral("hour=%1 should be isNight=true").arg(h)));
        }
        for (int h : {6, 12, 18, 21}) {
            auto fv = TemporalFeatures::compute(dt(2024, 6, 15, h));
            QVERIFY2(!fv.isNight, qPrintable(QStringLiteral("hour=%1 should be isNight=false").arg(h)));
        }
    }

    void testIsWeekendSatSun()
    {
        // 2024-01-06 = Saturday, 2024-01-07 = Sunday
        auto fvSat = TemporalFeatures::compute(dt(2024, 1, 6, 12));
        auto fvSun = TemporalFeatures::compute(dt(2024, 1, 7, 12));
        QVERIFY2(fvSat.isWeekend, "Saturday should be weekend");
        QVERIFY2(fvSun.isWeekend, "Sunday should be weekend");

        // 2024-01-01 = Monday … 2024-01-05 = Friday should NOT be weekend
        for (int d = 1; d <= 5; ++d) {
            auto fv = TemporalFeatures::compute(dt(2024, 1, d, 12));
            QVERIFY2(!fv.isWeekend,
                     qPrintable(QStringLiteral("2024-01-%1 should not be weekend").arg(d, 2, 10, QChar('0'))));
        }
    }

    // ── compute() returns consistent struct (all fields populated) ────────────

    void testComputeConsistentStruct()
    {
        // Any valid QDateTime should produce finite values for all float fields
        const QList<QDateTime> samples = {
            dt(2024,  1,  1,  0),
            dt(2024,  6, 21, 12),
            dt(2024, 12, 31, 23),
            dt(2000,  2, 29,  4),  // leap year
        };
        for (const auto& d : samples) {
            auto fv = TemporalFeatures::compute(d);
            QVERIFY(std::isfinite(fv.hourSin));
            QVERIFY(std::isfinite(fv.hourCos));
            QVERIFY(std::isfinite(fv.dowSin));
            QVERIFY(std::isfinite(fv.dowCos));
            QVERIFY(std::isfinite(fv.monthSin));
            QVERIFY(std::isfinite(fv.monthCos));
            QVERIFY(std::isfinite(fv.doySin));
            QVERIFY(std::isfinite(fv.doyCos));
            QVERIFY(std::isfinite(fv.lunarPhase));
            QVERIFY(std::isfinite(fv.sunAltitudeDeg));
            // Lunar phase in [0, 1]
            QVERIFY(fv.lunarPhase >= 0.0 && fv.lunarPhase <= 1.0);
        }
    }

    void testAllCyclicalValuesInNegOneToOne()
    {
        // Sample a range of datetimes and verify all sin/cos values are in [-1, 1]
        for (int month = 1; month <= 12; ++month) {
            for (int hour : {0, 6, 12, 18, 23}) {
                auto fv = TemporalFeatures::compute(dt(2024, month, 15, hour));
                const double vals[] = { fv.hourSin, fv.hourCos, fv.dowSin, fv.dowCos,
                                        fv.monthSin, fv.monthCos, fv.doySin, fv.doyCos };
                for (double v : vals) {
                    QVERIFY2(v >= -1.0 - EPS && v <= 1.0 + EPS,
                             qPrintable(QStringLiteral("Cyclical value %1 outside [-1,1]").arg(v)));
                }
            }
        }
    }

    // ── Week-of-month ─────────────────────────────────────────────────────────

    void testWeekOfMonth()
    {
        // day  1-7  → week 1
        // day  8-14 → week 2
        // day 15-21 → week 3
        // day 22-28 → week 4
        auto fv1 = TemporalFeatures::compute(dt(2024, 6,  1, 0));
        auto fv2 = TemporalFeatures::compute(dt(2024, 6,  8, 0));
        auto fv3 = TemporalFeatures::compute(dt(2024, 6, 15, 0));
        auto fv4 = TemporalFeatures::compute(dt(2024, 6, 22, 0));
        QCOMPARE(fv1.weekOfMonth, 1);
        QCOMPARE(fv2.weekOfMonth, 2);
        QCOMPARE(fv3.weekOfMonth, 3);
        QCOMPARE(fv4.weekOfMonth, 4);
    }
};

QTEST_GUILESS_MAIN(TemporalFeaturesDeep3Test)
#include "test_temporal_features_deep3.moc"
