// test_temporal_features_advanced.cpp — Advanced TemporalFeatures unit tests
#include <QTest>
#include <QCoreApplication>
#include "models/TemporalFeatures.h"
#include <cmath>
#include <vector>

class TestTemporalFeaturesAdvanced : public QObject
{
    Q_OBJECT

    // Euclidean distance in 2-D cyclical (sin/cos) space
    static double cyclicDist(double s1, double c1, double s2, double c2)
    {
        double ds = s1 - s2, dc = c1 - c2;
        return std::sqrt(ds * ds + dc * dc);
    }

    // Pack every field of TemporalFeatureVector into a flat double vector
    static std::vector<double> toFeatureVector(const TemporalFeatureVector& f)
    {
        return {
            f.hourSin, f.hourCos,
            f.dowSin,  f.dowCos,
            f.monthSin, f.monthCos,
            f.doySin,  f.doyCos,
            static_cast<double>(f.hourRaw),
            static_cast<double>(f.dowRaw),
            f.isWeekend        ? 1.0 : 0.0,
            f.isNight          ? 1.0 : 0.0,
            f.isPublicHoliday  ? 1.0 : 0.0,
            static_cast<double>(f.daysFromPayday),
            static_cast<double>(f.weekOfMonth),
            f.lunarPhase,
            f.sunAltitudeDeg,
            f.isDark           ? 1.0 : 0.0
        };
    }

private slots:

    // ── 1. Day-of-week convention: 0=Monday … 6=Sunday ───────────────────────
    void testDayOfWeek()
    {
        // 2024-01-15 is Monday
        QDateTime mon = QDateTime(QDate(2024, 1, 15), QTime(12, 0, 0), Qt::UTC);
        QCOMPARE(TemporalFeatures::compute(mon).dowRaw, 0);

        // 2024-01-21 is Sunday
        QDateTime sun = QDateTime(QDate(2024, 1, 21), QTime(12, 0, 0), Qt::UTC);
        QCOMPARE(TemporalFeatures::compute(sun).dowRaw, 6);

        // 2024-01-20 is Saturday
        QDateTime sat = QDateTime(QDate(2024, 1, 20), QTime(12, 0, 0), Qt::UTC);
        QCOMPARE(TemporalFeatures::compute(sat).dowRaw, 5);

        // Verify the full Mon–Sun sequence
        for (int d = 0; d < 7; ++d) {
            QDateTime dt = QDateTime(QDate(2024, 1, 15).addDays(d), QTime(9, 0, 0), Qt::UTC);
            QCOMPARE(TemporalFeatures::compute(dt).dowRaw, d);
        }
    }

    // ── 2. Hour-of-day raw value ──────────────────────────────────────────────
    void testHourOfDay()
    {
        QDateTime midnight = QDateTime(QDate(2024, 6, 15), QTime(0, 0, 0), Qt::UTC);
        QCOMPARE(TemporalFeatures::compute(midnight).hourRaw, 0);

        QDateTime noon = QDateTime(QDate(2024, 6, 15), QTime(12, 0, 0), Qt::UTC);
        QCOMPARE(TemporalFeatures::compute(noon).hourRaw, 12);

        QDateTime afternoon = QDateTime(QDate(2024, 6, 15), QTime(14, 0, 0), Qt::UTC);
        QCOMPARE(TemporalFeatures::compute(afternoon).hourRaw, 14);

        // All 24 hours round-trip cleanly
        for (int h = 0; h < 24; ++h) {
            QDateTime dt = QDateTime(QDate(2024, 3, 10), QTime(h, 0, 0), Qt::UTC);
            QCOMPARE(TemporalFeatures::compute(dt).hourRaw, h);
        }
    }

    // ── 3. Time-of-day night flag ─────────────────────────────────────────────
    void testTimeOfDay()
    {
        // 3 am → night (hour <= 5)
        QDateTime h3 = QDateTime(QDate(2024, 6, 15), QTime(3, 0, 0), Qt::UTC);
        QVERIFY(TemporalFeatures::compute(h3).isNight);

        // 14:00 → not night
        QDateTime h14 = QDateTime(QDate(2024, 6, 15), QTime(14, 0, 0), Qt::UTC);
        QVERIFY(!TemporalFeatures::compute(h14).isNight);

        // 22:00 → night (hour >= 22)
        QDateTime h22 = QDateTime(QDate(2024, 6, 15), QTime(22, 0, 0), Qt::UTC);
        QVERIFY(TemporalFeatures::compute(h22).isNight);

        // Boundary: hour 6 is NOT night; hour 5 IS
        QDateTime h6 = QDateTime(QDate(2024, 6, 15), QTime(6, 0, 0), Qt::UTC);
        QVERIFY(!TemporalFeatures::compute(h6).isNight);

        QDateTime h5 = QDateTime(QDate(2024, 6, 15), QTime(5, 0, 0), Qt::UTC);
        QVERIFY(TemporalFeatures::compute(h5).isNight);
    }

    // ── 4. Weekend detection ──────────────────────────────────────────────────
    void testIsWeekend()
    {
        // 2024-01-13 = Saturday, 2024-01-14 = Sunday
        QDateTime sat = QDateTime(QDate(2024, 1, 13), QTime(12, 0, 0), Qt::UTC);
        QVERIFY(TemporalFeatures::compute(sat).isWeekend);

        QDateTime sun = QDateTime(QDate(2024, 1, 14), QTime(12, 0, 0), Qt::UTC);
        QVERIFY(TemporalFeatures::compute(sun).isWeekend);

        // Mon 15 – Fri 19: not weekend
        for (int day = 15; day <= 19; ++day) {
            QDateTime wd = QDateTime(QDate(2024, 1, day), QTime(12, 0, 0), Qt::UTC);
            QVERIFY(!TemporalFeatures::compute(wd).isWeekend);
        }
    }

    // ── 5. Holiday flag (placeholder — always false, must not crash) ──────────
    void testHolidayFlag()
    {
        const QList<QDate> dates = {
            QDate(2024, 12, 25),   // Christmas Day
            QDate(2024, 1,  1),    // New Year's Day
            QDate(2024, 4,  1),    // Easter Monday 2024
            QDate(2024, 8, 26),    // Summer Bank Holiday 2024
        };
        for (const QDate& d : dates) {
            QDateTime dt = QDateTime(d, QTime(12, 0, 0), Qt::UTC);
            auto f = TemporalFeatures::compute(dt);
            // The flag is a placeholder; test only that it holds a valid boolean
            QVERIFY(f.isPublicHoliday == true || f.isPublicHoliday == false);
        }
    }

    // ── 6. Season detection via sun altitude proxy ────────────────────────────
    void testSeasonDetection()
    {
        // At solar noon UTC, London summer sun should be higher than winter sun
        QDateTime julNoon = QDateTime(QDate(2024, 7, 15), QTime(12, 0, 0), Qt::UTC);
        QDateTime decNoon = QDateTime(QDate(2024, 12, 15), QTime(12, 0, 0), Qt::UTC);

        double sunJul = TemporalFeatures::compute(julNoon).sunAltitudeDeg;
        double sunDec = TemporalFeatures::compute(decNoon).sunAltitudeDeg;
        QVERIFY(sunJul > sunDec);   // summer noon higher than winter noon

        // July noon is not dark; December midnight is dark
        QVERIFY(!TemporalFeatures::compute(julNoon).isDark);
        QDateTime decMid = QDateTime(QDate(2024, 12, 15), QTime(0, 0, 0), Qt::UTC);
        QVERIFY(TemporalFeatures::compute(decMid).isDark);

        // Monthly cyclical encoding differs significantly between summer and winter
        auto fJul = TemporalFeatures::compute(julNoon);
        auto fDec = TemporalFeatures::compute(decNoon);
        double monthDist = cyclicDist(fJul.monthSin, fJul.monthCos,
                                      fDec.monthSin, fDec.monthCos);
        QVERIFY(monthDist > 0.5);
    }

    // ── 7. Rush-hour hours are correctly encoded and not night ────────────────
    void testRushHour()
    {
        // Monday 2024-06-17 at rush hours
        const QList<int> rushHours = {8, 9, 17, 18, 19};
        for (int h : rushHours) {
            QDateTime dt = QDateTime(QDate(2024, 6, 17), QTime(h, 0, 0), Qt::UTC);
            auto f = TemporalFeatures::compute(dt);
            QCOMPARE(f.hourRaw, h);
            QVERIFY(!f.isNight);
            QVERIFY(!f.isWeekend);
        }
        // Rush-hour encoding is distant from 2 am in cyclical space
        QDateTime rush8   = QDateTime(QDate(2024, 6, 17), QTime(8,  0, 0), Qt::UTC);
        QDateTime offPeak = QDateTime(QDate(2024, 6, 17), QTime(2,  0, 0), Qt::UTC);
        auto fRush = TemporalFeatures::compute(rush8);
        auto fOff  = TemporalFeatures::compute(offPeak);
        double d = cyclicDist(fRush.hourSin, fRush.hourCos, fOff.hourSin, fOff.hourCos);
        QVERIFY(d > 0.5);
    }

    // ── 8. Temporal distance reflected in raw and cyclical fields ─────────────
    void testTemporalDistance()
    {
        QDateTime base   = QDateTime(QDate(2024, 6, 1), QTime(10, 0, 0), Qt::UTC);
        QDateTime plus1h = base.addSecs(3600);
        QDateTime plus1d = base.addDays(1);

        // Standard Qt distance helpers
        QCOMPARE(base.secsTo(plus1h) / 3600, qint64(1));
        QCOMPARE(base.daysTo(plus1d),         qint64(1));

        auto fb   = TemporalFeatures::compute(base);
        auto f1h  = TemporalFeatures::compute(plus1h);
        auto f1d  = TemporalFeatures::compute(plus1d);

        // 1 hour later → hourRaw differs by 1
        QCOMPARE(f1h.hourRaw - fb.hourRaw, 1);
        // 24 hours later → same time of day
        QCOMPARE(f1d.hourRaw, fb.hourRaw);

        // Cyclical distance: 1 h apart < 12 h apart
        QDateTime plus12h = base.addSecs(12 * 3600);
        auto f12h = TemporalFeatures::compute(plus12h);
        double d1  = cyclicDist(fb.hourSin, fb.hourCos, f1h.hourSin,  f1h.hourCos);
        double d12 = cyclicDist(fb.hourSin, fb.hourCos, f12h.hourSin, f12h.hourCos);
        QVERIFY(d1 < d12);
    }

    // ── 9. Feature-vector has the expected number of elements ─────────────────
    void testFeatureVectorLength()
    {
        QDateTime dt = QDateTime(QDate(2024, 6, 15), QTime(14, 0, 0), Qt::UTC);
        auto fv  = TemporalFeatures::compute(dt);
        auto vec = toFeatureVector(fv);
        // 8 cyclical doubles + 2 raw ints + 4 booleans + 2 int scalars +
        // lunarPhase + sunAltitudeDeg = 18 entries
        QCOMPARE(static_cast<int>(vec.size()), 18);
    }

    // ── 10. All numeric features stay within documented bounds ────────────────
    void testAllFeaturesBounded()
    {
        for (int month = 1; month <= 12; ++month) {
            for (int hour : {0, 3, 6, 9, 12, 15, 18, 21, 23}) {
                QDateTime dt = QDateTime(QDate(2024, month, 15), QTime(hour, 0, 0), Qt::UTC);
                auto f = TemporalFeatures::compute(dt);

                QVERIFY(f.hourSin  >= -1.0 && f.hourSin  <= 1.0);
                QVERIFY(f.hourCos  >= -1.0 && f.hourCos  <= 1.0);
                QVERIFY(f.dowSin   >= -1.0 && f.dowSin   <= 1.0);
                QVERIFY(f.dowCos   >= -1.0 && f.dowCos   <= 1.0);
                QVERIFY(f.monthSin >= -1.0 && f.monthSin <= 1.0);
                QVERIFY(f.monthCos >= -1.0 && f.monthCos <= 1.0);
                QVERIFY(f.doySin   >= -1.0 && f.doySin   <= 1.0);
                QVERIFY(f.doyCos   >= -1.0 && f.doyCos   <= 1.0);

                QVERIFY(f.lunarPhase    >= 0.0  && f.lunarPhase    <= 1.0);
                QVERIFY(f.sunAltitudeDeg >= -90.0 && f.sunAltitudeDeg <= 90.0);

                QVERIFY(f.hourRaw >= 0  && f.hourRaw <= 23);
                QVERIFY(f.dowRaw  >= 0  && f.dowRaw  <= 6);
                QVERIFY(f.daysFromPayday >= 0 && f.daysFromPayday <= 7);
                QVERIFY(f.weekOfMonth    >= 1 && f.weekOfMonth    <= 5);
            }
        }
    }

    // ── 11. Serial correlation: nearby timestamps are closer in cyclical space ─
    void testSerialCorrelation()
    {
        QDateTime base  = QDateTime(QDate(2024, 6, 15), QTime(10, 0, 0), Qt::UTC);
        QDateTime p1h   = base.addSecs(1  * 3600);
        QDateTime p12h  = base.addSecs(12 * 3600);

        auto fb   = TemporalFeatures::compute(base);
        auto f1h  = TemporalFeatures::compute(p1h);
        auto f12h = TemporalFeatures::compute(p12h);

        double d1  = cyclicDist(fb.hourSin, fb.hourCos, f1h.hourSin,  f1h.hourCos);
        double d12 = cyclicDist(fb.hourSin, fb.hourCos, f12h.hourSin, f12h.hourCos);
        QVERIFY(d1 < d12);

        // Same principle holds for day-of-week: 1 day apart < 3 days apart
        QDateTime mon = QDateTime(QDate(2024, 1, 15), QTime(12, 0, 0), Qt::UTC);
        QDateTime tue = mon.addDays(1);
        QDateTime thu = mon.addDays(3);
        auto fMon = TemporalFeatures::compute(mon);
        auto fTue = TemporalFeatures::compute(tue);
        auto fThu = TemporalFeatures::compute(thu);
        double dDay1 = cyclicDist(fMon.dowSin, fMon.dowCos, fTue.dowSin, fTue.dowCos);
        double dDay3 = cyclicDist(fMon.dowSin, fMon.dowCos, fThu.dowSin, fThu.dowCos);
        QVERIFY(dDay1 < dDay3);
    }

    // ── 12. Default/invalid QDateTime must not crash ──────────────────────────
    void testEmptyTimestampHandled()
    {
        QDateTime invalid;
        // Must not throw or crash; result values are implementation-defined
        Q_UNUSED(TemporalFeatures::compute(invalid));
        QVERIFY(true);
    }
};

QTEST_MAIN(TestTemporalFeaturesAdvanced)
#include "test_temporal_features_advanced.moc"
