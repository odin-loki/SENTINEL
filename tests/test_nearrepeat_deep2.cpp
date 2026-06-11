#include <QTest>
#include "models/NearRepeatVictimisation.h"
#include "models/SeriesDetector.h"
#include <cmath>

class TestNearRepeatDeep2 : public QObject
{
    Q_OBJECT

    static SeriesEvent ev(const QString& id, double lat, double lon, double tDays)
    {
        SeriesEvent e;
        e.eventId   = id;
        e.lat       = lat;
        e.lon       = lon;
        e.tDays     = tDays;
        e.crimeType = QString();
        return e;
    }

private slots:

    // Knox ratio with 2 near events: 1 near pair out of 1 total.
    // The study area (minimum 5×bw square) is much larger than the near-pair
    // area (π×bw²), so expected << 1 and the ratio > 1.
    void testKnoxKnownData()
    {
        NearRepeatVictimisation nrv(200.0, 14.0);
        QVector<SeriesEvent> evs;
        evs << ev("A", 0.0, 0.0,     0.0)
            << ev("B", 0.0, 0.001,   1.0);  // ~111 m east, 1 day later

        // Manually: nearPairs=1, totalPairs=1
        // study area = max(1000,1000)m * max(111,1000)m = 1e6 m²
        // nearArea = π * 200² ≈ 125664 m²
        // pSpace = 125664 / 1e6 ≈ 0.1257
        // expected = 1 * 0.1257 * 1.0 ≈ 0.1257
        // ratio = 1 / 0.1257 ≈ 7.96
        double knox = nrv.knoxStatistic(evs);
        QVERIFY2(knox > 1.0,
                 qPrintable(QString("Knox ratio %1 should be > 1.0 for a clustered pair")
                            .arg(knox)));
        QVERIFY2(knox > 7.0 && knox < 9.0,
                 qPrintable(QString("Knox ratio %1 should be near 7.96").arg(knox)));
    }

    // 3 collinear events 111 m apart: pairs AB and BC are near, AC (~222 m) is not.
    // nearPairs = 2; both directions of each near pair are captured by the
    // unordered loop (j < i), consistent with totalPairs = n*(n-1)/2.
    void testKnoxSymmetricPairsCountedCorrectly()
    {
        NearRepeatVictimisation nrv(200.0, 14.0);
        QVector<SeriesEvent> evs;
        evs << ev("A", 0.0, 0.000, 0.0)
            << ev("B", 0.0, 0.001, 1.0)   // ~111 m from A
            << ev("C", 0.0, 0.002, 2.0);  // ~111 m from B, ~222 m from A

        // AC: distance ≈ 222 m > 200 m → not counted
        // AB and BC: distance ≈ 111 m < 200 m, dt < 14 → both counted
        // nearPairs = 2, totalPairs = 3
        double knox = nrv.knoxStatistic(evs);
        QVERIFY2(knox > 1.0,
                 qPrintable(QString("Knox ratio %1 should be > 1 with 2 near pairs out of 3").arg(knox)));

        // Wider bandwidth should include AC → nearPairs = 3
        NearRepeatVictimisation nrvWide(300.0, 14.0);
        double knoxWide = nrvWide.knoxStatistic(evs);
        QVERIFY2(knoxWide >= knox,
                 "Wider bandwidth should yield >= Knox ratio (more or equal near pairs)");
    }

    // Temporal bands: same spatial cluster, only the narrow window excludes the pair.
    void testTemporalBandsProduceDifferentValues()
    {
        QVector<SeriesEvent> evs;
        evs << ev("A", 0.0, 0.0,   0.0)
            << ev("B", 0.0, 0.001, 7.0);  // 7 days apart, ~111 m

        NearRepeatVictimisation nrv5(200.0, 5.0);   // 5-day window: 7 d is outside
        NearRepeatVictimisation nrv14(200.0, 14.0); // 14-day window: 7 d is inside

        QCOMPARE(nrv5.knoxStatistic(evs), 0.0);
        QVERIFY2(nrv14.knoxStatistic(evs) > 1.0,
                 "14-day window should include the 7-day pair");
    }

    // Empty dataset → 1.0 (sentinel value meaning "no data")
    void testKnoxEmpty()
    {
        NearRepeatVictimisation nrv;
        QCOMPARE(nrv.knoxStatistic({}), 1.0);
    }

    // Single event → 1.0
    void testKnoxSingleEvent()
    {
        NearRepeatVictimisation nrv;
        QVector<SeriesEvent> evs;
        evs << ev("A", 0.0, 0.0, 0.0);
        QCOMPARE(nrv.knoxStatistic(evs), 1.0);
    }

    // Two events well beyond both thresholds → 0 near pairs → ratio = 0.0
    void testKnoxFarApartReturnsZero()
    {
        NearRepeatVictimisation nrv(200.0, 14.0);
        QVector<SeriesEvent> evs;
        evs << ev("A",  0.0,  0.0,  0.0)
            << ev("B", 10.0, 10.0,  1.0);  // ~1566 km, well beyond 200 m
        QCOMPARE(nrv.knoxStatistic(evs), 0.0);
    }

    // Pair exactly at the temporal threshold (dt == windowDays): the implementation
    // uses <=, so the pair IS counted.  Using same-location events (dist = 0) to
    // isolate the time-boundary behaviour from haversine rounding.
    void testExactlyAtThreshold()
    {
        NearRepeatVictimisation nrv(200.0, 14.0);

        // dist = 0 <= 200m, dt = 14.0 <= 14.0 → counted
        QVector<SeriesEvent> at;
        at << ev("A", 0.0, 0.0,  0.0)
           << ev("B", 0.0, 0.0, 14.0);
        double knoxAt = nrv.knoxStatistic(at);
        QVERIFY2(knoxAt > 0.0,
                 "Same-location pair at exactly the temporal threshold must be counted (<=)");

        // dist = 0 <= 200m, dt = 14.001 > 14.0 → NOT counted
        QVector<SeriesEvent> beyond;
        beyond << ev("A", 0.0, 0.0,     0.0)
               << ev("B", 0.0, 0.0, 14.001);
        QCOMPARE(nrv.knoxStatistic(beyond), 0.0);
    }

    // analyse() returns empty for < 2 events
    void testAnalyseEmpty()
    {
        NearRepeatVictimisation nrv;
        QVERIFY(nrv.analyse({}).isEmpty());

        QVector<SeriesEvent> one;
        one << ev("A", 0.0, 0.0, 0.0);
        QVERIFY(nrv.analyse(one).isEmpty());
    }

    // analyse() with two near events produces a valid alert
    void testAnalyseTwoNearEvents()
    {
        NearRepeatVictimisation nrv(200.0, 14.0);
        QVector<SeriesEvent> evs;
        evs << ev("A", 0.0, 0.0,   0.0)
            << ev("B", 0.0, 0.001, 1.0);  // ~111 m, 1 day

        auto alerts = nrv.analyse(evs);
        QVERIFY2(!alerts.isEmpty(), "Near pair must produce at least one alert");

        const NearRepeatAlert& a = alerts.first();
        QVERIFY2(a.alertScore > 0.0 && a.alertScore <= 1.0,
                 qPrintable(QString("Alert score %1 must be in (0, 1]").arg(a.alertScore)));
        QVERIFY2(a.spatialDistanceM < 200.0,
                 qPrintable(QString("Spatial distance %1 m must be < 200 m bandwidth")
                            .arg(a.spatialDistanceM)));
        QVERIFY2(a.temporalDistanceDays < 14.0,
                 qPrintable(QString("Temporal distance %1 d must be < 14 d window")
                            .arg(a.temporalDistanceDays)));
    }

    // alertScore at zero distance and zero time should be 1.0
    void testAlertScoreMaxAtOrigin()
    {
        NearRepeatVictimisation nrv(200.0, 14.0);
        QCOMPARE(nrv.alertScore(0.0, 0.0), 1.0);
    }

    // alertScore at exactly the bandwidth boundary: spatialDecay = exp(-1)
    void testAlertScoreZeroAtBandwidthBoundary()
    {
        NearRepeatVictimisation nrv(200.0, 14.0);
        const double expected = std::exp(-1.0);
        QVERIFY2(std::abs(nrv.alertScore(200.0, 0.0) - expected) < 1e-9,
                 qPrintable(QString("Expected exp(-1)=%1 at bandwidth boundary")
                            .arg(expected)));
    }

    // alertScore at exactly the window boundary: temporalDecay = exp(-1)
    void testAlertScoreZeroAtWindowBoundary()
    {
        NearRepeatVictimisation nrv(200.0, 14.0);
        const double expected = std::exp(-1.0);
        QVERIFY2(std::abs(nrv.alertScore(0.0, 14.0) - expected) < 1e-9,
                 qPrintable(QString("Expected exp(-1)=%1 at window boundary")
                            .arg(expected)));
    }

    // alertScore strictly decreases with distance and with time
    void testAlertScoreMonotonicallyDecreasing()
    {
        NearRepeatVictimisation nrv(200.0, 14.0);
        const double s0  = nrv.alertScore(0.0,  0.0);
        const double s50 = nrv.alertScore(50.0, 1.0);
        const double s150 = nrv.alertScore(150.0, 7.0);
        QVERIFY2(s0 > s50,  "Score must decrease with increasing distance");
        QVERIFY2(s50 > s150, "Score must decrease as distance/time grow");
    }
};

QTEST_GUILESS_MAIN(TestNearRepeatDeep2)
#include "test_nearrepeat_deep2.moc"
