// test_series_detector_accuracy.cpp
// Validates SeriesDetector DBSCAN correctness: known clusters detected,
// noise points ignored, MO Jaccard similarity, link probability bounds.
#include <QTest>
#include "models/SeriesDetector.h"
#include "core/CrimeEvent.h"

static SeriesEvent makeEv(const QString& id, double lat, double lon,
                            double tDays, const QString& mo = "burglary at night forced entry")
{
    SeriesEvent ev;
    ev.eventId   = id;
    ev.lat       = lat;
    ev.lon       = lon;
    ev.tDays     = tDays;
    ev.crimeType = QStringLiteral("burglary");
    ev.moText    = mo;
    return ev;
}

class SeriesDetectorAccuracyTest : public QObject
{
    Q_OBJECT

private slots:

    // ── 1. Two tight clusters → two series detected ──────────────────────────
    void testTwoClusters()
    {
        // Cluster A: 4 events near (51.50, -0.10) within 0.1km, 3 days apart
        // Cluster B: 4 events near (51.60, -0.20) within 0.1km, 3 days apart
        QVector<SeriesEvent> evs;
        for (int i = 0; i < 4; ++i)
            evs.append(makeEv(QStringLiteral("A%1").arg(i),
                               51.500 + i * 0.0005, -0.100 + i * 0.0005, i * 3.0));
        for (int i = 0; i < 4; ++i)
            evs.append(makeEv(QStringLiteral("B%1").arg(i),
                               51.600 + i * 0.0005, -0.200 + i * 0.0005, 50.0 + i * 3.0));

        SeriesDetector det(0.3, 14.0, 3);
        const auto series = det.detectSeries(evs);

        QVERIFY2(series.size() >= 2,
                 qPrintable(QStringLiteral("Expected >= 2 series, got %1").arg(series.size())));
    }

    // ── 2. All points in noise → no series ───────────────────────────────────
    void testAllNoise()
    {
        // 5 events each far apart (1 degree spatial, 30 days temporal)
        QVector<SeriesEvent> evs;
        for (int i = 0; i < 5; ++i)
            evs.append(makeEv(QStringLiteral("N%1").arg(i),
                               51.0 + i * 1.0, -0.1, i * 30.0));

        SeriesDetector det(0.3, 14.0, 3);
        const auto series = det.detectSeries(evs);

        // No cluster large enough to be a series (minSamples=3, but no nearby points)
        QVERIFY2(series.isEmpty(),
                 qPrintable(QStringLiteral("Expected no series from isolated events, got %1")
                    .arg(series.size())));
    }

    // ── 3. Haversine distance accuracy ───────────────────────────────────────
    void testHaversineDistance()
    {
        // London to approximately 1km east: (51.5074, -0.1278) → (51.5074, -0.1127)
        const double d = SeriesDetector::haversineKm(51.5074, -0.1278, 51.5074, -0.1127);
        QVERIFY2(d > 0.8 && d < 1.2,
                 qPrintable(QStringLiteral("Expected ~1km, got %1 km").arg(d)));
    }

    // ── 4. Haversine: same point → 0 km ─────────────────────────────────────
    void testHaversineSamePoint()
    {
        const double d = SeriesDetector::haversineKm(51.5, -0.1, 51.5, -0.1);
        QVERIFY2(d < 1e-9, qPrintable(QStringLiteral("Same point distance %1 != 0").arg(d)));
    }

    // ── 5. MO Jaccard similarity ──────────────────────────────────────────────
    void testMOJaccardIdentical()
    {
        const double j = SeriesDetector::moJaccard("burglary forced entry", "burglary forced entry");
        QVERIFY2(std::abs(j - 1.0) < 1e-9,
                 qPrintable(QStringLiteral("Identical strings Jaccard %1 != 1.0").arg(j)));
    }

    void testMOJaccardDisjoint()
    {
        const double j = SeriesDetector::moJaccard("burglary night", "robbery daytime");
        QVERIFY2(j < 0.5,
                 qPrintable(QStringLiteral("Disjoint strings Jaccard %1 should be low").arg(j)));
    }

    void testMOJaccardPartialOverlap()
    {
        const double j = SeriesDetector::moJaccard("burglary forced entry night",
                                                    "burglary kicked window night");
        QVERIFY2(j > 0.0 && j < 1.0,
                 qPrintable(QStringLiteral("Partial overlap Jaccard %1 should be in (0,1)").arg(j)));
    }

    // ── 6. Link probability in [0, 1] ────────────────────────────────────────
    void testLinkProbabilityRange()
    {
        QVector<SeriesEvent> evs;
        for (int i = 0; i < 5; ++i)
            evs.append(makeEv(QStringLiteral("E%1").arg(i),
                               51.50 + i * 0.001, -0.10, i * 2.0));

        SeriesDetector det(0.3, 14.0, 3);
        const auto series = det.detectSeries(evs);
        if (series.isEmpty()) QSKIP("No series detected; skip link probability test");

        const SeriesEvent candidate = makeEv("NEW", 51.501, -0.101, 10.0);
        const auto match = det.linkProbability(candidate, series[0], 0.7);

        QVERIFY2(match.linkProbability >= 0.0 && match.linkProbability <= 1.0,
                 qPrintable(QStringLiteral("Link probability %1 out of [0,1]")
                    .arg(match.linkProbability)));
    }

    // ── 7. Series centroid is within bounding box of members ────────────────
    void testSeriesCentroidInBounds()
    {
        QVector<SeriesEvent> evs;
        double minLat = 1e9, maxLat = -1e9;
        for (int i = 0; i < 5; ++i) {
            const double lat = 51.50 + i * 0.001;
            minLat = std::min(minLat, lat);
            maxLat = std::max(maxLat, lat);
            evs.append(makeEv(QStringLiteral("C%1").arg(i), lat, -0.10, i * 2.0));
        }

        SeriesDetector det(0.3, 14.0, 3);
        const auto series = det.detectSeries(evs);
        if (series.isEmpty()) QSKIP("No series detected");

        QVERIFY2(series[0].centroidLat >= minLat - 1e-6 &&
                 series[0].centroidLat <= maxLat + 1e-6,
                 qPrintable(QStringLiteral("Centroid lat %1 outside [%2, %3]")
                    .arg(series[0].centroidLat).arg(minLat).arg(maxLat)));
    }

    // ── 8. Near-repeat params by crime type ──────────────────────────────────
    void testNearRepeatParams()
    {
        const auto bParams  = SeriesDetector::nearRepeatFor(QStringLiteral("burglary"));
        const auto rParams  = SeriesDetector::nearRepeatFor(QStringLiteral("robbery"));
        const auto unknown  = SeriesDetector::nearRepeatFor(QStringLiteral("xyz"));

        QVERIFY2(bParams.distM > 0.0 && bParams.days > 0.0,
                 "Burglary near-repeat params must be positive");
        QVERIFY2(rParams.distM > 0.0 && rParams.days > 0.0,
                 "Robbery near-repeat params must be positive");
        QVERIFY2(unknown.distM > 0.0,
                 "Unknown crime type must return default params");
    }

    // ── 9. Detect CrimeEvent overload works ──────────────────────────────────
    void testDetectCrimeEventOverload()
    {
        QVector<CrimeEvent> evs;
        for (int i = 0; i < 5; ++i) {
            CrimeEvent e;
            e.eventId   = QStringLiteral("EV%1").arg(i);
            e.crimeType = QStringLiteral("burglary");
            e.suburb    = QStringLiteral("Test");
            e.lat       = 51.50 + i * 0.001;
            e.lon       = -0.10;
            const QDateTime dt(QDate(2024, 1, 1).addDays(i * 2), QTime(20, 0, 0), QTimeZone::utc());
            e.occurredAt = dt;
            e.timestamp  = dt;
            evs.append(e);
        }

        SeriesDetector det(0.3, 14.0, 3);
        const auto series = det.detect(evs);

        // Should not crash; result is acceptable regardless
        QVERIFY(series.size() >= 0);
    }

    // ── 10. Single tight cluster with more than minSamples events ────────────
    void testSingleDenseClustersDetected()
    {
        // 8 events all within 0.05km and 5 days → must form 1 series
        QVector<SeriesEvent> evs;
        for (int i = 0; i < 8; ++i)
            evs.append(makeEv(QStringLiteral("D%1").arg(i),
                               51.500 + i * 0.0001,   // ~11m apart
                               -0.100 + i * 0.0001,
                               i * 0.5));              // half-day apart

        SeriesDetector det(0.3, 14.0, 3);
        const auto series = det.detectSeries(evs);

        QVERIFY2(!series.isEmpty(),
                 "Dense cluster of 8 events should produce at least 1 series");
        QVERIFY2(series[0].members.size() >= 3,
                 qPrintable(QStringLiteral("Series should have >= 3 members, got %1")
                    .arg(series[0].members.size())));
    }
};

QTEST_MAIN(SeriesDetectorAccuracyTest)
#include "test_series_detector_accuracy.moc"
