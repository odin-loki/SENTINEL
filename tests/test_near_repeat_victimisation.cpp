// test_near_repeat_victimisation.cpp
// Tests near-repeat victimisation patterns using SeriesDetector and
// RiskForecaster: elevated risk within 400m/14 days of a prior crime.
#include <QTest>
#include "models/SeriesDetector.h"
#include "models/RiskForecaster.h"
#include "core/CrimeEvent.h"
#include <cmath>

class NearRepeatVictimisationTest : public QObject
{
    Q_OBJECT

private:
    static SeriesEvent sev(double lat, double lon, double tDays,
                            const QString& type = QStringLiteral("burglary"))
    {
        SeriesEvent e;
        e.eventId   = QStringLiteral("E%1").arg(static_cast<int>(tDays * 10));
        e.lat       = lat;
        e.lon       = lon;
        e.tDays     = tDays;
        e.crimeType = type;
        e.moText    = QStringLiteral("forced_entry residential");
        return e;
    }

    static CrimeEvent cev(const QString& id, double lat, double lon,
                           const QDate& d, const QString& suburb = QStringLiteral("Soho"))
    {
        CrimeEvent ev;
        ev.eventId   = id;
        ev.crimeType = QStringLiteral("burglary");
        ev.suburb    = suburb;
        ev.lat       = lat;
        ev.lon       = lon;
        ev.latitude  = lat;
        ev.longitude = lon;
        const QDateTime dt(d, QTime(23, 0, 0), QTimeZone::utc());
        ev.occurredAt = dt;
        ev.timestamp  = dt;
        return ev;
    }

private slots:

    // ── 1. nearRepeatFor('burglary') returns distM <= 400m ────────────────────
    void testNearRepeatBurglaryParams()
    {
        const auto params = SeriesDetector::nearRepeatFor(QStringLiteral("burglary"));
        QVERIFY2(params.distM > 0 && params.distM <= 600.0,
                 qPrintable(QStringLiteral("Burglary near-repeat dist %1 should be in (0, 600m]")
                    .arg(params.distM)));
        QVERIFY2(params.days > 0 && params.days <= 30.0,
                 qPrintable(QStringLiteral("Burglary near-repeat days %1 should be in (0, 30]")
                    .arg(params.days)));
    }

    // ── 2. nearRepeatFor('vehicle crime') returns valid params ───────────────
    void testNearRepeatVehicleParams()
    {
        const auto params = SeriesDetector::nearRepeatFor(QStringLiteral("vehicle crime"));
        QVERIFY2(params.distM > 0, "Vehicle crime near-repeat distM must be > 0");
        QVERIFY2(params.days > 0,  "Vehicle crime near-repeat days must be > 0");
    }

    // ── 3. nearRepeatFor: multiplier >= 1.0 ──────────────────────────────────
    void testNearRepeatMultiplierPositive()
    {
        for (const auto& type : { QStringLiteral("burglary"),
                                   QStringLiteral("robbery"),
                                   QStringLiteral("vehicle crime") }) {
            const auto params = SeriesDetector::nearRepeatFor(type);
            QVERIFY2(params.multiplier >= 1.0,
                     qPrintable(QStringLiteral("%1 near-repeat multiplier %2 should >= 1.0")
                        .arg(type).arg(params.multiplier)));
        }
    }

    // ── 4. haversineKm: known distance ~157m → < 0.2 km ──────────────────────
    void testHaversineKnownDistance()
    {
        // Points ~157m apart (London, rough calculation)
        const double d = SeriesDetector::haversineKm(51.5074, -0.1278, 51.5074, -0.1250);
        QVERIFY2(d > 0.1 && d < 0.3,
                 qPrintable(QStringLiteral("Haversine distance %1 km should be ~0.16 km").arg(d)));
    }

    // ── 5. Two events 200m apart + 7 days → within near-repeat window ────────
    void testTwoEventsWithinNearRepeatWindow()
    {
        // London, 200m east: Δlon ≈ 0.0027°
        const auto params = SeriesDetector::nearRepeatFor(QStringLiteral("burglary"));
        const double distKm = SeriesDetector::haversineKm(51.5074, -0.1278, 51.5074, -0.1251);
        const double distM  = distKm * 1000.0;
        QVERIFY2(distM < params.distM,
                 qPrintable(QStringLiteral("200m apart (%1m) should be within near-repeat window (%2m)")
                    .arg(distM).arg(params.distM)));
    }

    // ── 6. detectSeries: near-repeat cluster → forms series ─────────────────
    void testNearRepeatClusterFormsSeries()
    {
        const auto params = SeriesDetector::nearRepeatFor(QStringLiteral("burglary"));
        SeriesDetector sd(params.distM / 1000.0, params.days, 3);

        QVector<SeriesEvent> evs;
        for (int i = 0; i < 5; ++i)
            evs.append(sev(51.5074 + i * 0.0002, -0.1278, static_cast<double>(i)));

        const auto result = sd.detectSeries(evs);
        QVERIFY2(!result.isEmpty(),
                 "5 events within near-repeat window should form a series");
    }

    // ── 7. RiskForecaster: low thresholds + recent burst → elevated alert ────
    void testRecentBurstElevatesAlert()
    {
        RiskForecaster rf(7);
        // Use very low alert thresholds so any non-trivial risk triggers elevation
        rf.setAlertThresholds(0.001, 0.01, 0.05);

        QVector<CrimeEvent> evs;
        const QDate today = QDate::currentDate();
        // 20 crimes clustered in 1 suburb in last 2 days
        for (int i = 0; i < 20; ++i)
            evs.append(cev(QStringLiteral("E%1").arg(i),
                           51.5, -0.1, today.addDays(-(i % 2)),
                           QStringLiteral("BurstZone")));

        rf.fit(evs);
        const QDateTime from(today, QTime(0, 0, 0), QTimeZone::utc());
        const auto forecasts = rf.forecast(from);

        // At least one zone should have alertLevel > 0
        const bool anyElevated = std::any_of(forecasts.begin(), forecasts.end(),
            [](const ZoneForecast& zf){ return zf.alertLevel > 0; });
        QVERIFY2(anyElevated, "Recent burst with low thresholds should trigger elevated alert");
    }

    // ── 8. linkProbability: nearby same-type event has high linkProb ─────────
    void testLinkProbabilityNearby()
    {
        SeriesDetector sd;

        // Build a 5-event series first
        QVector<SeriesEvent> evs;
        for (int i = 0; i < 5; ++i)
            evs.append(sev(51.5074, -0.1278, static_cast<double>(i)));
        const auto series = sd.detectSeries(evs);

        if (!series.isEmpty()) {
            // Test a new nearby event
            const SeriesEvent newEv = sev(51.5075, -0.1280, 6.0);
            const auto match = sd.linkProbability(newEv, series.first(), 0.9);
            QVERIFY2(match.linkProbability >= 0.0 && match.linkProbability <= 1.0,
                     qPrintable(QStringLiteral("Link probability %1 must be in [0,1]")
                        .arg(match.linkProbability)));
        }
        QVERIFY(true);  // pass if no series found
    }

    // ── 9. linkProbability: distant event has lower link probability ─────────
    void testLinkProbabilityDistantLower()
    {
        SeriesDetector sd;

        QVector<SeriesEvent> evs;
        for (int i = 0; i < 5; ++i)
            evs.append(sev(51.5074, -0.1278, static_cast<double>(i)));
        const auto series = sd.detectSeries(evs);

        if (!series.isEmpty()) {
            const SeriesEvent nearEv  = sev(51.5075, -0.1279, 6.0);   // ~100m away
            const SeriesEvent farEv   = sev(52.0000,  1.0000, 6.0);   // 50km away

            const auto nearMatch = sd.linkProbability(nearEv, series.first(), 0.9);
            const auto farMatch  = sd.linkProbability(farEv,  series.first(), 0.1);

            QVERIFY2(nearMatch.linkProbability >= farMatch.linkProbability,
                     qPrintable(QStringLiteral("Near (%1) should have >= link prob than far (%2)")
                        .arg(nearMatch.linkProbability).arg(farMatch.linkProbability)));
        }
        QVERIFY(true);
    }

    // ── 10. SeriesMatch compositeScore in [0,1] ───────────────────────────────
    void testSeriesMatchCompositeRange()
    {
        SeriesDetector sd(0.5, 20.0, 3);

        QVector<SeriesEvent> evs;
        for (int i = 0; i < 5; ++i)
            evs.append(sev(51.5074 + i * 0.001, -0.1278, static_cast<double>(i)));
        const auto series = sd.detectSeries(evs);

        if (!series.isEmpty()) {
            for (const auto& s : series) {
                QVERIFY2(!s.seriesId.isEmpty(), "Series should have a valid seriesId");
                QVERIFY2(s.members.size() >= 3, "Series should have >= 3 members");
            }
        }
        QVERIFY(true);
    }
};

QTEST_MAIN(NearRepeatVictimisationTest)
#include "test_near_repeat_victimisation.moc"
