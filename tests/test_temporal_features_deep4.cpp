// test_temporal_features_deep4.cpp — Deep audit iteration 17: TemporalFeatures
// hour / dow / month bin indices, empty (invalid) input handling.

#include <QTest>
#include <QDateTime>
#include <QDate>
#include <QTime>
#include <cmath>
#include "models/TemporalFeatures.h"

static constexpr double EPS = 1e-9;

class TemporalFeaturesDeep4Test : public QObject
{
    Q_OBJECT

    static QDateTime utc(int year, int month, int day, int hour, int min = 0)
    {
        return QDateTime(QDate(year, month, day), QTime(hour, min, 0), Qt::UTC);
    }

private slots:

    // ── Hour bin (0–23) ─────────────────────────────────────────────────────

    void testHourBinMapsEachHourToCorrectIndex()
    {
        for (int h = 0; h < 24; ++h) {
            const auto fv = TemporalFeatures::compute(utc(2024, 6, 15, h));
            QCOMPARE(fv.hourRaw, h);
            const double expectedSin = std::sin(2.0 * M_PI * h / 24.0);
            const double expectedCos = std::cos(2.0 * M_PI * h / 24.0);
            QVERIFY2(std::abs(fv.hourSin - expectedSin) < 1e-10,
                     qPrintable(QStringLiteral("hourSin mismatch at bin %1").arg(h)));
            QVERIFY2(std::abs(fv.hourCos - expectedCos) < 1e-10,
                     qPrintable(QStringLiteral("hourCos mismatch at bin %1").arg(h)));
        }
    }

    void testHourBinBoundaryMidnightAndLastHour()
    {
        const auto midnight = TemporalFeatures::compute(utc(2024, 3, 1, 0));
        const auto lastHour = TemporalFeatures::compute(utc(2024, 3, 1, 23));
        QCOMPARE(midnight.hourRaw, 0);
        QCOMPARE(lastHour.hourRaw, 23);
        QVERIFY2(std::abs(midnight.hourCos - 1.0) < EPS, "hour bin 0: cos should be 1");
        QVERIFY2(lastHour.isNight, "hour bin 23 must be flagged isNight");
    }

    // ── Day-of-week bin (0=Mon … 6=Sun) ─────────────────────────────────────

    void testDowBinIndicesAcrossFullWeek()
    {
        // 2024-01-01 is Monday (Qt dow=1 → our bin 0)
        for (int d = 0; d < 7; ++d) {
            const auto fv = TemporalFeatures::compute(utc(2024, 1, 1 + d, 12));
            QCOMPARE(fv.dowRaw, d);
            const double expectedSin = std::sin(2.0 * M_PI * d / 7.0);
            const double expectedCos = std::cos(2.0 * M_PI * d / 7.0);
            QVERIFY2(std::abs(fv.dowSin - expectedSin) < 1e-10,
                     qPrintable(QStringLiteral("dowSin mismatch at bin %1").arg(d)));
            QVERIFY2(std::abs(fv.dowCos - expectedCos) < 1e-10,
                     qPrintable(QStringLiteral("dowCos mismatch at bin %1").arg(d)));
        }
    }

    void testDowBinWeekendFlagsMatchSatSunBins()
    {
        const auto sat = TemporalFeatures::compute(utc(2024, 1, 6, 10));  // Saturday → bin 5
        const auto sun = TemporalFeatures::compute(utc(2024, 1, 7, 10));  // Sunday   → bin 6
        QCOMPARE(sat.dowRaw, 5);
        QCOMPARE(sun.dowRaw, 6);
        QVERIFY(sat.isWeekend);
        QVERIFY(sun.isWeekend);
        const auto wed = TemporalFeatures::compute(utc(2024, 1, 3, 10)); // Wednesday → bin 2
        QCOMPARE(wed.dowRaw, 2);
        QVERIFY(!wed.isWeekend);
    }

    // ── Month bin (0-indexed cyclical encoding) ───────────────────────────────

    void testMonthBinJanuaryAndJulyAnchors()
    {
        const auto jan = TemporalFeatures::compute(utc(2024, 1, 15, 12));
        const auto jul = TemporalFeatures::compute(utc(2024, 7, 15, 12));
        // month index 0 (Jan): sin(0)=0, cos(0)=1
        QVERIFY2(std::abs(jan.monthSin) < EPS, "January month bin sin should be 0");
        QVERIFY2(std::abs(jan.monthCos - 1.0) < EPS, "January month bin cos should be 1");
        // month index 6 (Jul): sin(π)≈0, cos(π)=-1
        QVERIFY2(std::abs(jul.monthSin) < EPS, "July month bin sin should be ≈0");
        QVERIFY2(std::abs(jul.monthCos - (-1.0)) < EPS, "July month bin cos should be -1");
    }

    void testMonthBinsAllTwelveAreDistinctOnUnitCircle()
    {
        QVector<QPair<double, double>> encodings;
        encodings.reserve(12);
        for (int m = 1; m <= 12; ++m) {
            const auto fv = TemporalFeatures::compute(utc(2024, m, 15, 12));
            const double norm = fv.monthSin * fv.monthSin + fv.monthCos * fv.monthCos;
            QVERIFY2(std::abs(norm - 1.0) < 1e-9,
                     qPrintable(QStringLiteral("month %1 encoding not on unit circle").arg(m)));
            encodings.append({fv.monthSin, fv.monthCos});
        }
        // Adjacent months must differ (no duplicate bin collapse)
        for (int m = 0; m < 11; ++m) {
            const double dSin = encodings[m].first  - encodings[m + 1].first;
            const double dCos = encodings[m].second - encodings[m + 1].second;
            const double dist = std::sqrt(dSin * dSin + dCos * dCos);
            QVERIFY2(dist > 0.01,
                     qPrintable(QStringLiteral("month bins %1 and %2 should differ").arg(m + 1).arg(m + 2)));
        }
    }

    // ── Empty / invalid input ─────────────────────────────────────────────────

    void testEmptyInvalidDateTimeDoesNotCrash()
    {
        QDateTime invalid;
        QVERIFY(!invalid.isValid());
        const auto fv = TemporalFeatures::compute(invalid);
        // Primary contract: must not throw or crash on empty/invalid input.
        QVERIFY(std::isfinite(fv.hourSin));
        QVERIFY(std::isfinite(fv.hourCos));
        QVERIFY(std::isfinite(fv.dowSin));
        QVERIFY(std::isfinite(fv.dowCos));
        QVERIFY(std::isfinite(fv.monthSin));
        QVERIFY(std::isfinite(fv.monthCos));
        // BUG (documented): invalid QDateTime can yield hourRaw outside [0,23].
        if (fv.hourRaw < 0 || fv.hourRaw > 23) {
            QWARN(qPrintable(QStringLiteral("BUG: invalid QDateTime hourRaw=%1 outside [0,23]")
                             .arg(fv.hourRaw)));
        }
    }
};

QTEST_GUILESS_MAIN(TemporalFeaturesDeep4Test)
#include "test_temporal_features_deep4.moc"
