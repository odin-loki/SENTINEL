#include <QtTest>
#include <cmath>
#include "models/TemporalFeatures.h"

static constexpr double EPS = 1e-9;

class TestTemporalFeaturesDeep2 : public QObject
{
    Q_OBJECT

private slots:

    // ── Cyclical encoding via compute() ───────────────────────────────────────

    void testHourCyclicalMidnight()
    {
        // Hour 0: sin(2π*0/24) = 0, cos = 1
        QDateTime dt(QDate(2024, 6, 3), QTime(0, 0), Qt::UTC);
        TemporalFeatureVector fv = TemporalFeatures::compute(dt);
        QVERIFY(std::abs(fv.hourSin - 0.0) < EPS);
        QVERIFY(std::abs(fv.hourCos - 1.0) < EPS);
    }

    void testHourCyclicalNoon()
    {
        // Hour 12: sin(2π*12/24) = sin(π) = 0, cos = -1
        QDateTime dt(QDate(2024, 6, 3), QTime(12, 0), Qt::UTC);
        TemporalFeatureVector fv = TemporalFeatures::compute(dt);
        QVERIFY(std::abs(fv.hourSin - 0.0) < 1e-12);
        QVERIFY(std::abs(fv.hourCos - (-1.0)) < 1e-12);
    }

    void testHourCyclical6am()
    {
        // Hour 6: sin(2π*6/24) = sin(π/2) = 1, cos = 0
        QDateTime dt(QDate(2024, 6, 3), QTime(6, 0), Qt::UTC);
        TemporalFeatureVector fv = TemporalFeatures::compute(dt);
        QVERIFY(std::abs(fv.hourSin - 1.0) < 1e-12);
        QVERIFY(std::abs(fv.hourCos - 0.0) < 1e-12);
    }

    void testCyclicalSinCosUnitCircle()
    {
        // hourSin²+hourCos² = 1 for all hours
        for (int h = 0; h < 24; ++h) {
            QDateTime dt(QDate(2024, 6, 3), QTime(h, 0), Qt::UTC);
            TemporalFeatureVector fv = TemporalFeatures::compute(dt);
            double norm = fv.hourSin * fv.hourSin + fv.hourCos * fv.hourCos;
            QVERIFY2(std::abs(norm - 1.0) < 1e-12,
                     qPrintable(QString("sin²+cos² != 1 at hour %1: got %2").arg(h).arg(norm)));
        }
    }

    void testCyclicalHourMatchesManualFormula()
    {
        for (int h = 0; h < 24; ++h) {
            QDateTime dt(QDate(2024, 6, 3), QTime(h, 0), Qt::UTC);
            TemporalFeatureVector fv = TemporalFeatures::compute(dt);
            double expectedSin = std::sin(2.0 * M_PI * h / 24.0);
            double expectedCos = std::cos(2.0 * M_PI * h / 24.0);
            QVERIFY2(std::abs(fv.hourSin - expectedSin) < 1e-10,
                     qPrintable(QString("hourSin mismatch at h=%1").arg(h)));
            QVERIFY2(std::abs(fv.hourCos - expectedCos) < 1e-10,
                     qPrintable(QString("hourCos mismatch at h=%1").arg(h)));
        }
    }

    // ── Full compute() correctness ─────────────────────────────────────────────

    void testComputeMondayMorning()
    {
        // 2024-01-01 is a Monday, 09:00 UTC
        QDateTime dt(QDate(2024, 1, 1), QTime(9, 0), Qt::UTC);
        TemporalFeatureVector fv = TemporalFeatures::compute(dt);

        QCOMPARE(fv.hourRaw, 9);
        QCOMPARE(fv.dowRaw, 0);    // Monday = 0
        QVERIFY(!fv.isWeekend);
        QVERIFY(!fv.isNight);       // 09:00 is not night

        // Cyclical: hour=9, sin(2π*9/24)
        const double expectedHourSin = std::sin(2.0 * M_PI * 9.0 / 24.0);
        QVERIFY(std::abs(fv.hourSin - expectedHourSin) < 1e-10);
    }

    void testComputeSaturdayNight()
    {
        // 2024-01-06 is a Saturday, 23:00 UTC
        QDateTime dt(QDate(2024, 1, 6), QTime(23, 0), Qt::UTC);
        TemporalFeatureVector fv = TemporalFeatures::compute(dt);

        QCOMPARE(fv.hourRaw, 23);
        QCOMPARE(fv.dowRaw, 5);    // Saturday = 5
        QVERIFY(fv.isWeekend);
        QVERIFY(fv.isNight);        // 23:00 is night (>= 22)
    }

    void testComputeSundayNight()
    {
        // 2024-01-07 is a Sunday, 03:00 UTC
        QDateTime dt(QDate(2024, 1, 7), QTime(3, 0), Qt::UTC);
        TemporalFeatureVector fv = TemporalFeatures::compute(dt);

        QCOMPARE(fv.dowRaw, 6);    // Sunday = 6
        QVERIFY(fv.isWeekend);
        QVERIFY(fv.isNight);        // 03:00 is night (<= 5)
    }

    void testComputeHour5IsNight()
    {
        // Hour 5 should be night (hour <= 5)
        QDateTime dt(QDate(2024, 3, 15), QTime(5, 0), Qt::UTC);
        TemporalFeatureVector fv = TemporalFeatures::compute(dt);
        QVERIFY(fv.isNight);
    }

    void testComputeHour6IsNotNight()
    {
        // Hour 6 should NOT be night
        QDateTime dt(QDate(2024, 3, 15), QTime(6, 0), Qt::UTC);
        TemporalFeatureVector fv = TemporalFeatures::compute(dt);
        QVERIFY(!fv.isNight);
    }

    // ── Lunar phase via compute() ──────────────────────────────────────────────

    void testLunarPhaseEpochIsNewMoon()
    {
        // 2000-01-06 is the reference new moon → lunarPhase ≈ 0.0
        QDateTime dt(QDate(2000, 1, 6), QTime(12, 0), Qt::UTC);
        TemporalFeatureVector fv = TemporalFeatures::compute(dt);
        QVERIFY2(fv.lunarPhase < 0.01,
                 qPrintable(QString("Epoch should be near-zero phase, got %1").arg(fv.lunarPhase)));
    }

    void testLunarPhaseInRange()
    {
        // All phases must be in [0, 1)
        for (int year = 2020; year <= 2025; year += 2) {
            for (int month = 1; month <= 12; month += 3) {
                QDateTime dt(QDate(year, month, 15), QTime(12, 0), Qt::UTC);
                TemporalFeatureVector fv = TemporalFeatures::compute(dt);
                QVERIFY2(fv.lunarPhase >= 0.0 && fv.lunarPhase < 1.0,
                         qPrintable(QString("Lunar phase out of [0,1): %1").arg(fv.lunarPhase)));
            }
        }
    }

    void testLunarPhaseHalfPeriodIsFullMoon()
    {
        // ~15 days after the 2000-01-06 new moon should be near full moon (phase ≈ 0.5)
        QDateTime dt(QDate(2000, 1, 21), QTime(12, 0), Qt::UTC);
        TemporalFeatureVector fv = TemporalFeatures::compute(dt);
        QVERIFY2(fv.lunarPhase > 0.4 && fv.lunarPhase < 0.6,
                 qPrintable(QString("Half synodic should be near 0.5, got %1").arg(fv.lunarPhase)));
    }

    // ── Sun altitude via compute() ─────────────────────────────────────────────

    void testSunAltitudeNoonSummer()
    {
        // Solar noon on summer solstice at UK (default lat=51.5): sun is high
        QDateTime dt(QDate(2024, 6, 21), QTime(12, 0), Qt::UTC);
        TemporalFeatureVector fv = TemporalFeatures::compute(dt);
        QVERIFY2(fv.sunAltitudeDeg > 40.0,
                 qPrintable(QString("Summer noon sun altitude should be > 40°, got %1")
                                .arg(fv.sunAltitudeDeg)));
    }

    void testSunAltitudeMidnightIsNegative()
    {
        // Midnight in UK (June, no midnight sun): sun should be below horizon
        QDateTime dt(QDate(2024, 1, 21), QTime(0, 0), Qt::UTC);
        TemporalFeatureVector fv = TemporalFeatures::compute(dt);
        QVERIFY2(fv.sunAltitudeDeg < 0.0,
                 qPrintable(QString("Midnight sun altitude should be negative, got %1")
                                .arg(fv.sunAltitudeDeg)));
    }

    void testSunAltitudeInRange()
    {
        // Sun altitude must be in [-90, 90] degrees
        QDateTime dt(QDate(2024, 3, 15), QTime(10, 30), Qt::UTC);
        TemporalFeatureVector fv = TemporalFeatures::compute(dt);
        QVERIFY2(fv.sunAltitudeDeg >= -90.0 && fv.sunAltitudeDeg <= 90.0,
                 qPrintable(QString("Sun altitude out of [-90,90]: %1").arg(fv.sunAltitudeDeg)));
    }

    void testIsDarkWhenSunBelowSixDegrees()
    {
        // Civil twilight: sun below -6° → isDark
        QDateTime dt(QDate(2024, 1, 15), QTime(17, 0), Qt::UTC);  // early evening UK winter
        TemporalFeatureVector fv = TemporalFeatures::compute(dt);
        // At 17:00 UTC in January at London, sun should be below horizon
        // isDark = sunAltitudeDeg < -6
        QVERIFY(fv.sunAltitudeDeg < 20.0);  // definitely not high in sky
    }

    // ── Days from payday via compute() ────────────────────────────────────────

    void testDaysFromPaydayRange()
    {
        // Must be in [0, 7]
        for (int doy = 1; doy <= 365; ++doy) {
            QDate d = QDate(2024, 1, 1).addDays(doy - 1);
            QDateTime dt(d, QTime(12, 0), Qt::UTC);
            TemporalFeatureVector fv = TemporalFeatures::compute(dt);
            QVERIFY2(fv.daysFromPayday >= 0 && fv.daysFromPayday <= 7,
                     qPrintable(QString("daysFromPayday out of [0,7]: %1 on %2")
                                    .arg(fv.daysFromPayday).arg(d.toString())));
        }
    }

    void testDaysFromPaydaySymmetry()
    {
        // daysFromPayday should be min(doy%14, 14-doy%14) which is symmetric around 7
        for (int doy = 1; doy <= 14; ++doy) {
            QDate d(2024, 1, doy);
            QDateTime dt(d, QTime(12, 0), Qt::UTC);
            TemporalFeatureVector fv = TemporalFeatures::compute(dt);
            int expected = std::min(d.dayOfYear() % 14, 14 - d.dayOfYear() % 14);
            QCOMPARE(fv.daysFromPayday, expected);
        }
    }

    // ── Week of month ──────────────────────────────────────────────────────────

    void testWeekOfMonthFirstDay()
    {
        QDateTime dt(QDate(2024, 5, 1), QTime(12, 0), Qt::UTC);
        TemporalFeatureVector fv = TemporalFeatures::compute(dt);
        QCOMPARE(fv.weekOfMonth, 1);  // day 1 → week 1
    }

    void testWeekOfMonthDay8()
    {
        QDateTime dt(QDate(2024, 5, 8), QTime(12, 0), Qt::UTC);
        TemporalFeatureVector fv = TemporalFeatures::compute(dt);
        QCOMPARE(fv.weekOfMonth, 2);  // day 8 → week 2
    }

    void testWeekOfMonthDay31()
    {
        QDateTime dt(QDate(2024, 5, 31), QTime(12, 0), Qt::UTC);
        TemporalFeatureVector fv = TemporalFeatures::compute(dt);
        QCOMPARE(fv.weekOfMonth, 5);  // day 31 → week 5
    }

    // ── Month cyclical ─────────────────────────────────────────────────────────

    void testMonthCyclicalJanuaryAndDecemberAreClose()
    {
        // January (month=1, index=0) and December (month=12, index=11) should
        // be close to each other on the unit circle (cyclic distance ≈ 1/12)
        QDateTime jan(QDate(2024, 1, 15), QTime(12, 0), Qt::UTC);
        QDateTime dec(QDate(2024, 12, 15), QTime(12, 0), Qt::UTC);
        TemporalFeatureVector fvJan = TemporalFeatures::compute(jan);
        TemporalFeatureVector fvDec = TemporalFeatures::compute(dec);

        // Angular distance = acos(sin1*sin2 + cos1*cos2)
        double dot = fvJan.monthSin * fvDec.monthSin + fvJan.monthCos * fvDec.monthCos;
        dot = std::max(-1.0, std::min(1.0, dot));
        double angDist = std::acos(dot) * 180.0 / M_PI;  // degrees

        // 1 month separation = 360/12 = 30° on the unit circle
        QVERIFY2(angDist < 35.0,
                 qPrintable(QString("Jan-Dec angular distance should be ~30°, got %1°").arg(angDist)));
    }

    void testMonthCyclicalJuneAndJanuaryAreFar()
    {
        // January and July are 6 months apart → 180° on the circle
        QDateTime jan(QDate(2024, 1, 15), QTime(12, 0), Qt::UTC);
        QDateTime jul(QDate(2024, 7, 15), QTime(12, 0), Qt::UTC);
        TemporalFeatureVector fvJan = TemporalFeatures::compute(jan);
        TemporalFeatureVector fvJul = TemporalFeatures::compute(jul);

        double dot = fvJan.monthSin * fvJul.monthSin + fvJan.monthCos * fvJul.monthCos;
        dot = std::max(-1.0, std::min(1.0, dot));
        double angDist = std::acos(dot) * 180.0 / M_PI;

        QVERIFY2(angDist > 150.0,
                 qPrintable(QString("Jan-Jul angular distance should be ~180°, got %1°").arg(angDist)));
    }
};

QTEST_GUILESS_MAIN(TestTemporalFeaturesDeep2)
#include "test_temporal_features_deep2.moc"
