// tests/test_series_detector.cpp
// Comprehensive Qt Test unit tests for SeriesDetector:
//   DBSCAN-based crime series detection, MO Jaccard similarity, Haversine
//   distance, near-repeat calibration table, and linkProbability scoring.

#include <QTest>
#include <QCoreApplication>
#include <QDate>
#include <QDateTime>
#include <QVector>
#include <QStringList>
#include <QtMath>
#include <cmath>

#include "models/SeriesDetector.h"
#include "core/CrimeEvent.h"

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// Build a CrimeEvent with the fields SeriesDetector::detect() reads.
// moKeywords is stored as a space-joined string in narrative (which maps
// to SeriesEvent::moText via the detect() convenience overload).
static CrimeEvent makeEvent(const QString& id,
                             double lat, double lon,
                             const QString& crimeType,
                             const QDateTime& occurredAt,
                             const QStringList& moKeywords = {})
{
    CrimeEvent ev;
    ev.eventId    = id;
    ev.source     = "test";
    ev.ingestedAt = occurredAt;
    ev.occurredAt = occurredAt;
    ev.lat        = lat;
    ev.lon        = lon;
    ev.crimeType  = crimeType;
    if (!moKeywords.isEmpty())
        ev.narrative = moKeywords.join(QLatin1Char(' '));
    return ev;
}

// Parse an ISO-8601 date-time string using Qt::ISODate.
static QDateTime iso(const char* s)
{
    return QDateTime::fromString(QString::fromLatin1(s), Qt::ISODate);
}

// ---------------------------------------------------------------------------
// Test class
// ---------------------------------------------------------------------------

class TestSeriesDetector : public QObject
{
    Q_OBJECT

private slots:

    // ── 1. Constructor with various parameters ───────────────────────────────

    void testConstructorDefaults()
    {
        // Default construction should not crash; detect on empty input returns empty.
        SeriesDetector det;
        QVERIFY(det.detect({}).isEmpty());
    }

    void testConstructorCustomParams()
    {
        // Verify that varying epsilon / minPts values change clustering behaviour.
        // A tight epsilon with high minPts should not form a series from 3 events.
        SeriesDetector strictDet(0.001, 1.0, 10);

        const QDateTime base = iso("2024-01-01T10:00:00");
        QVector<CrimeEvent> events;
        for (int i = 0; i < 5; ++i)
            events << makeEvent(QStringLiteral("s%1").arg(i),
                                51.5000 + i * 0.00001, -0.1000,
                                "burglary", base.addDays(i));

        // With minPts=10 and only 5 events, no cluster should form.
        QVERIFY(strictDet.detect(events).isEmpty());
    }

    void testConstructorLooseParams()
    {
        // Very loose epsilon and low minPts should eagerly cluster nearby events.
        SeriesDetector looseDet(5.0, 365.0, 2);

        const QDateTime base = iso("2024-03-01T08:00:00");
        QVector<CrimeEvent> events;
        events << makeEvent("a1", 51.5000, -0.1000, "robbery", base)
               << makeEvent("a2", 51.5010, -0.1010, "robbery", base.addDays(30))
               << makeEvent("a3", 51.5020, -0.1020, "robbery", base.addDays(60));

        QVERIFY(!looseDet.detect(events).isEmpty());
    }

    void testConstructorNearRepeatTable()
    {
        // Verify the calibration table is accessible with the constructor-set state.
        auto p = SeriesDetector::nearRepeatFor("burglary");
        QCOMPARE(p.distM,      200.0);
        QCOMPARE(p.days,        14.0);
        QCOMPARE(p.multiplier,   4.5);

        auto q = SeriesDetector::nearRepeatFor("robbery");
        QCOMPARE(q.distM,      400.0);
        QCOMPARE(q.days,         7.0);
        QCOMPARE(q.multiplier,   3.2);
    }

    // ── 2. detect() with an empty vector ────────────────────────────────────

    void testDetectEmptyVector()
    {
        SeriesDetector det(0.5, 14.0, 3);
        QVector<CrimeEvent> empty;
        const auto result = det.detect(empty);
        QVERIFY(result.isEmpty());
        QCOMPARE(result.size(), 0);
    }

    void testDetectSingleEvent()
    {
        // One event can never form a cluster (minSamples = 3 by default).
        SeriesDetector det(0.5, 14.0, 3);
        QVector<CrimeEvent> one;
        one << makeEvent("x1", 51.5, -0.1, "burglary", iso("2024-06-01T12:00:00"));
        QVERIFY(det.detect(one).isEmpty());
    }

    void testDetectTwoEventsNoClusters()
    {
        SeriesDetector det(0.5, 14.0, 3);
        QVector<CrimeEvent> two;
        two << makeEvent("x1", 51.5000, -0.1000, "burglary", iso("2024-06-01T10:00:00"))
            << makeEvent("x2", 51.5002, -0.1001, "burglary", iso("2024-06-02T10:00:00"));
        QVERIFY(det.detect(two).isEmpty());
    }

    // ── 3. Five near-identical events → exactly 1 series ────────────────────

    void testFiveNearIdenticalEventsOneSeries()
    {
        // Five burglaries within ~50 m of each other, on consecutive days —
        // well inside epsKm=0.5 km and epsDays=14.
        SeriesDetector det(0.5, 14.0, 3);

        const QStringList mo = { "forced_entry", "residential", "rear_door", "nighttime" };
        const QDateTime base = iso("2024-04-15T22:00:00");

        QVector<CrimeEvent> events;
        events << makeEvent("b1", 51.50000, -0.10000, "burglary", base,           mo)
               << makeEvent("b2", 51.50010, -0.10005, "burglary", base.addDays(1), mo)
               << makeEvent("b3", 51.50020, -0.10010, "burglary", base.addDays(2), mo)
               << makeEvent("b4", 51.50005, -0.10003, "burglary", base.addDays(3), mo)
               << makeEvent("b5", 51.50015, -0.10008, "burglary", base.addDays(4), mo);

        const auto series = det.detect(events);
        QCOMPARE(series.size(), 1);
    }

    void testFiveNearIdenticalSeriesEventCount()
    {
        SeriesDetector det(0.5, 14.0, 3);

        const QDateTime base = iso("2024-04-15T22:00:00");
        QVector<CrimeEvent> events;
        for (int i = 0; i < 5; ++i)
            events << makeEvent(QStringLiteral("b%1").arg(i),
                                51.5000 + i * 0.0001, -0.1000 + i * 0.00005,
                                "burglary",
                                base.addDays(i));

        const auto series = det.detect(events);
        QCOMPARE(series.size(), 1);
        QCOMPARE(series.first().members.size(), 5);
    }

    void testFiveNearIdenticalSeriesDominantType()
    {
        SeriesDetector det(0.5, 14.0, 3);
        const QDateTime base = iso("2024-07-01T20:00:00");

        QVector<CrimeEvent> events;
        for (int i = 0; i < 5; ++i)
            events << makeEvent(QStringLiteral("r%1").arg(i),
                                -33.8700 + i * 0.0001, 151.2100 + i * 0.00005,
                                "robbery",
                                base.addDays(i));

        const auto series = det.detect(events);
        QCOMPARE(series.size(), 1);
        QCOMPARE(series.first().dominantCrimeType, QString("robbery"));
    }

    // ── 4. Spatially separate clusters → separate series ────────────────────

    void testTwoSeparateClusters()
    {
        // Cluster A: London (~51.50, -0.10)
        // Cluster B: Manchester (~53.48, -2.24)  — ~263 km away
        SeriesDetector det(0.5, 30.0, 3);
        const QDateTime base = iso("2024-05-01T10:00:00");

        QVector<CrimeEvent> events;
        // London cluster — 4 events
        for (int i = 0; i < 4; ++i)
            events << makeEvent(QStringLiteral("lon%1").arg(i),
                                51.5000 + i * 0.0002, -0.1000 + i * 0.0001,
                                "burglary", base.addDays(i));

        // Manchester cluster — 4 events
        for (int i = 0; i < 4; ++i)
            events << makeEvent(QStringLiteral("man%1").arg(i),
                                53.4800 + i * 0.0002, -2.2400 + i * 0.0001,
                                "burglary", base.addDays(i));

        const auto series = det.detect(events);
        QCOMPARE(series.size(), 2);
    }

    void testThreeSeparateClusters()
    {
        SeriesDetector det(0.5, 20.0, 3);
        const QDateTime base = iso("2024-05-01T10:00:00");

        // Three anchor points > 500 km apart
        const double lats[3] = { 51.50, 53.48, 55.86 };  // London, Manchester, Glasgow
        const double lons[3] = { -0.10, -2.24, -4.25 };

        QVector<CrimeEvent> events;
        for (int c = 0; c < 3; ++c) {
            for (int i = 0; i < 4; ++i) {
                events << makeEvent(
                    QStringLiteral("c%1e%2").arg(c).arg(i),
                    lats[c] + i * 0.0002, lons[c] + i * 0.0001,
                    "burglary", base.addDays(i));
            }
        }

        const auto series = det.detect(events);
        QCOMPARE(series.size(), 3);
    }

    void testSeparateClustersHaveDistinctSeriesIds()
    {
        SeriesDetector det(0.5, 30.0, 3);
        const QDateTime base = iso("2024-06-01T08:00:00");

        QVector<CrimeEvent> events;
        for (int i = 0; i < 4; ++i)
            events << makeEvent(QStringLiteral("lon%1").arg(i),
                                51.5000 + i * 0.0002, -0.1000,
                                "burglary", base.addDays(i));
        for (int i = 0; i < 4; ++i)
            events << makeEvent(QStringLiteral("man%1").arg(i),
                                53.4800 + i * 0.0002, -2.2400,
                                "burglary", base.addDays(i));

        const auto series = det.detect(events);
        QCOMPARE(series.size(), 2);
        QVERIFY(series[0].seriesId != series[1].seriesId);
    }

    // ── 5. MO dissimilarity (Jaccard) test ──────────────────────────────────

    void testMoJaccardIdentical()
    {
        QCOMPARE(SeriesDetector::moJaccard("forced_entry residential nighttime",
                                           "forced_entry residential nighttime"), 1.0);
    }

    void testMoJaccardZeroDisjoint()
    {
        QCOMPARE(SeriesDetector::moJaccard("forced_entry residential",
                                           "pickpocket vehicle daytime"), 0.0);
    }

    void testMoJaccardPartialOverlap()
    {
        // "a b c" ∩ "b c d" = {b,c}, union = {a,b,c,d} → 0.5
        double j = SeriesDetector::moJaccard("a b c", "b c d");
        QVERIFY(std::abs(j - 0.5) < 1e-9);
    }

    void testMoJaccardEmptyStrings()
    {
        QCOMPARE(SeriesDetector::moJaccard("", ""), 0.0);
    }

    void testMoJaccardOneEmpty()
    {
        QCOMPARE(SeriesDetector::moJaccard("forced_entry", ""), 0.0);
        QCOMPARE(SeriesDetector::moJaccard("", "vehicle"), 0.0);
    }

    void testMoJaccardCaseInsensitive()
    {
        // moJaccard normalises to lowercase
        QCOMPARE(SeriesDetector::moJaccard("Forced_Entry RESIDENTIAL",
                                           "forced_entry residential"), 1.0);
    }

    void testLinkProbabilityLowForDissimilarMO()
    {
        // Construct a series close in space and time to a new event, but
        // with very different MO → linkProbability composite should be low.
        SeriesDetector det(0.5, 14.0, 3);

        // Build a minimal 3-event series near St. Paul's Cathedral, London
        const QDateTime base = iso("2024-08-01T20:00:00");
        SeriesEvent se1; se1.eventId="s1"; se1.lat=51.514; se1.lon=-0.098;
            se1.tDays=8975; se1.crimeType="burglary"; se1.moText="forced_entry residential";
        SeriesEvent se2; se2.eventId="s2"; se2.lat=51.514; se2.lon=-0.098;
            se2.tDays=8976; se2.crimeType="burglary"; se2.moText="forced_entry residential";
        SeriesEvent se3; se3.eventId="s3"; se3.lat=51.514; se3.lon=-0.098;
            se3.tDays=8977; se3.crimeType="burglary"; se3.moText="forced_entry residential";

        CrimeSeries series;
        series.seriesId = "TEST-SERIES";
        series.members = { se1, se2, se3 };
        series.dominantCrimeType = "burglary";
        series.centroidLat = 51.514;
        series.centroidLon = -0.098;
        series.firstDays = 8975;
        series.lastDays = 8977;

        // New event: same location but completely different MO
        SeriesEvent newEv;
        newEv.eventId = "new";
        newEv.lat = 51.514; newEv.lon = -0.098;
        newEv.tDays = 8978;  // 1 day after last
        newEv.crimeType = "burglary";
        newEv.moText = "pickpocket_vehicle daytime open_wallet";

        double dissimilarMO = SeriesDetector::moJaccard(newEv.moText, se1.moText);
        QVERIFY(dissimilarMO < 0.15);   // very little overlap

        SeriesMatch match = det.linkProbability(newEv, series, dissimilarMO);
        // Even though spatially/temporally close, low MO similarity reduces composite
        QVERIFY(match.compositeScore < 0.8);
        QVERIFY(match.linkProbability >= 0.0);
        QVERIFY(match.linkProbability <= 1.0);
    }

    void testLinkProbabilityHighForIdenticalMO()
    {
        SeriesDetector det(0.5, 14.0, 3);

        SeriesEvent se1; se1.eventId="s1"; se1.lat=51.514; se1.lon=-0.098;
            se1.tDays=8975; se1.crimeType="burglary"; se1.moText="forced_entry residential";
        SeriesEvent se2; se2.eventId="s2"; se2.lat=51.5141; se2.lon=-0.0981;
            se2.tDays=8976; se2.crimeType="burglary"; se2.moText="forced_entry residential";
        SeriesEvent se3; se3.eventId="s3"; se3.lat=51.5142; se3.lon=-0.0982;
            se3.tDays=8977; se3.crimeType="burglary"; se3.moText="forced_entry residential";

        CrimeSeries series;
        series.seriesId = "TEST-SERIES";
        series.members = { se1, se2, se3 };
        series.dominantCrimeType = "burglary";
        series.centroidLat = 51.514; series.centroidLon = -0.098;
        series.firstDays = 8975; series.lastDays = 8977;

        SeriesEvent newEv;
        newEv.eventId = "new"; newEv.lat = 51.514; newEv.lon = -0.098;
        newEv.tDays = 8978; newEv.crimeType = "burglary";
        newEv.moText = "forced_entry residential";

        double similarMO = SeriesDetector::moJaccard(newEv.moText, se1.moText);
        QCOMPARE(similarMO, 1.0);

        SeriesMatch match = det.linkProbability(newEv, series, similarMO);
        QVERIFY(match.compositeScore > 0.5);
        QVERIFY(match.linkProbability > 0.0);
    }

    // ── 6. Returned CrimeSeries objects have correct event counts ───────────

    void testEventCountInSeries()
    {
        SeriesDetector det(0.5, 14.0, 3);
        const QDateTime base = iso("2024-09-01T18:00:00");

        QVector<CrimeEvent> events;
        for (int i = 0; i < 7; ++i)
            events << makeEvent(QStringLiteral("ev%1").arg(i),
                                51.5000 + i * 0.0001, -0.1000,
                                "robbery", base.addDays(i));

        const auto series = det.detect(events);
        QVERIFY(!series.isEmpty());

        int total = 0;
        for (const auto& s : series) {
            QVERIFY(s.members.size() >= 3);   // DBSCAN min-samples
            total += s.members.size();
        }
        QVERIFY(total <= events.size());
    }

    void testEventCountExact()
    {
        // Exactly 5 events all in the same tight cluster — expect exactly 5 members
        SeriesDetector det(0.5, 14.0, 3);
        const QDateTime base = iso("2024-10-01T06:00:00");

        QVector<CrimeEvent> events;
        for (int i = 0; i < 5; ++i)
            events << makeEvent(QStringLiteral("c%1").arg(i),
                                51.5000 + i * 0.00005, -0.1000 + i * 0.00005,
                                "assault", base.addDays(i));

        const auto series = det.detect(events);
        QCOMPARE(series.size(), 1);
        QCOMPARE(series.first().members.size(), 5);
    }

    void testCentroidWithinBounds()
    {
        SeriesDetector det(0.5, 14.0, 3);
        const QDateTime base = iso("2024-10-15T12:00:00");

        QVector<CrimeEvent> events;
        events << makeEvent("p1", 51.500, -0.100, "burglary", base)
               << makeEvent("p2", 51.501, -0.101, "burglary", base.addDays(1))
               << makeEvent("p3", 51.502, -0.102, "burglary", base.addDays(2))
               << makeEvent("p4", 51.503, -0.103, "burglary", base.addDays(3));

        const auto series = det.detect(events);
        QCOMPARE(series.size(), 1);

        double centLat = series.first().centroidLat;
        double centLon = series.first().centroidLon;

        // Centroid must be within bounding box of the cluster
        QVERIFY(centLat >= 51.500 && centLat <= 51.503);
        QVERIFY(centLon >= -0.103 && centLon <= -0.100);
    }

    void testTemporalRangeInSeries()
    {
        SeriesDetector det(0.5, 30.0, 3);
        const QDateTime base = iso("2024-11-01T08:00:00");

        QVector<CrimeEvent> events;
        for (int i = 0; i < 5; ++i)
            events << makeEvent(QStringLiteral("t%1").arg(i),
                                51.5000 + i * 0.0001, -0.1000,
                                "vehicle_crime", base.addDays(i * 3));

        const auto series = det.detect(events);
        QCOMPARE(series.size(), 1);

        // lastDays >= firstDays
        QVERIFY(series.first().lastDays >= series.first().firstDays);
    }

    // ── 7. All events are noise (edge case) ──────────────────────────────────

    void testAllNoiseWidelySpread()
    {
        SeriesDetector det(0.5, 14.0, 3);
        const QDateTime base = iso("2024-12-01T10:00:00");

        QVector<CrimeEvent> events;
        // Each event ~hundreds of km from the others
        events << makeEvent("n1",  51.50,  -0.10, "burglary", base)
               << makeEvent("n2",  48.86,   2.35, "burglary", base.addDays(1))
               << makeEvent("n3",  52.52,  13.40, "burglary", base.addDays(2))
               << makeEvent("n4",  40.71, -74.01, "burglary", base.addDays(3))
               << makeEvent("n5", -33.87, 151.21, "burglary", base.addDays(4));

        QVERIFY(det.detect(events).isEmpty());
    }

    void testAllNoiseTemporallyDistant()
    {
        // Spatially close but 200 days apart — outside epsDays
        SeriesDetector det(0.5, 14.0, 3);
        const QDateTime base = iso("2024-01-01T10:00:00");

        QVector<CrimeEvent> events;
        events << makeEvent("t1", 51.5000, -0.1000, "burglary", base)
               << makeEvent("t2", 51.5001, -0.1001, "burglary", base.addDays(200))
               << makeEvent("t3", 51.5002, -0.1002, "burglary", base.addDays(400))
               << makeEvent("t4", 51.5003, -0.1003, "burglary", base.addDays(600));

        QVERIFY(det.detect(events).isEmpty());
    }

    void testNoiseEmptyResult()
    {
        SeriesDetector det(0.5, 14.0, 3);
        QVector<CrimeEvent> events;
        for (int i = 0; i < 3; ++i)
            events << makeEvent(QStringLiteral("iso%1").arg(i),
                                10.0 * (i + 1), 10.0 * (i + 1),
                                "assault",
                                iso("2024-06-01T10:00:00").addDays(i));
        // 3 events far apart (10° each) — no cluster
        QVERIFY(det.detect(events).isEmpty());
    }

    // ── 8. Realistic batch of 50 synthetic events ────────────────────────────
    //
    // Three pre-designed clusters (20+15+10 events) + 5 noise points.
    // The remaining 50 events are built deterministically.

    void testFiftySyntheticEvents()
    {
        SeriesDetector det(0.5, 21.0, 3);

        const QDateTime base = iso("2024-03-01T00:00:00");
        QVector<CrimeEvent> events;
        events.reserve(50);

        // --- Cluster A: 20 burglaries near London Bridge (51.505, -0.086) ---
        for (int i = 0; i < 20; ++i) {
            events << makeEvent(
                QStringLiteral("A%1").arg(i),
                51.5050 + (i % 5) * 0.0002,
                -0.0860 + (i % 4) * 0.0001,
                "burglary",
                base.addDays(i),
                { "forced_entry", "residential", "rear" });
        }

        // --- Cluster B: 15 robberies near Canary Wharf (51.505, 0.020) ---
        for (int i = 0; i < 15; ++i) {
            events << makeEvent(
                QStringLiteral("B%1").arg(i),
                51.5050 + (i % 4) * 0.0002,
                0.0200 + (i % 5) * 0.0001,
                "robbery",
                base.addDays(i),
                { "street", "weapon", "group" });
        }

        // --- Cluster C: 10 vehicle crimes near Heathrow (51.470, -0.450) ---
        for (int i = 0; i < 10; ++i) {
            events << makeEvent(
                QStringLiteral("C%1").arg(i),
                51.4700 + (i % 3) * 0.0002,
                -0.4500 + (i % 4) * 0.0001,
                "vehicle_crime",
                base.addDays(i),
                { "carjack", "vehicle", "forced" });
        }

        // --- 5 noise points spread globally ---
        events << makeEvent("N0",  48.86,   2.35, "assault", base.addDays(1))
               << makeEvent("N1",  52.52,  13.40, "assault", base.addDays(2))
               << makeEvent("N2",  40.71, -74.01, "assault", base.addDays(3))
               << makeEvent("N3", -33.87, 151.21, "assault", base.addDays(4))
               << makeEvent("N4",  35.68, 139.69, "assault", base.addDays(5));

        QCOMPARE(events.size(), 50);

        const auto series = det.detect(events);

        // Must detect between 1 and 3 series (may merge nearby clusters depending on eps)
        QVERIFY(series.size() >= 1);
        QVERIFY(series.size() <= 3);

        // All series must have at least minSamples=3 members
        for (const auto& s : series)
            QVERIFY(s.members.size() >= 3);

        // Series IDs must be non-empty and distinct
        QSet<QString> ids;
        for (const auto& s : series) {
            QVERIFY(!s.seriesId.isEmpty());
            ids.insert(s.seriesId);
        }
        QCOMPARE(ids.size(), series.size());

        // Dominant crime type must be non-empty for each series
        for (const auto& s : series)
            QVERIFY(!s.dominantCrimeType.isEmpty());
    }

    void testFiftySyntheticEventsTotalMembership()
    {
        SeriesDetector det(0.5, 21.0, 3);
        const QDateTime base = iso("2024-03-01T00:00:00");
        QVector<CrimeEvent> events;
        events.reserve(50);

        for (int i = 0; i < 20; ++i)
            events << makeEvent(QStringLiteral("A%1").arg(i),
                                51.5050 + (i % 5) * 0.0002, -0.0860 + (i % 4) * 0.0001,
                                "burglary", base.addDays(i));
        for (int i = 0; i < 15; ++i)
            events << makeEvent(QStringLiteral("B%1").arg(i),
                                51.5050 + (i % 4) * 0.0002,  0.0200 + (i % 5) * 0.0001,
                                "robbery",  base.addDays(i));
        for (int i = 0; i < 10; ++i)
            events << makeEvent(QStringLiteral("C%1").arg(i),
                                51.4700 + (i % 3) * 0.0002, -0.4500 + (i % 4) * 0.0001,
                                "vehicle_crime", base.addDays(i));
        events << makeEvent("N0",  48.86,   2.35, "assault", base.addDays(1))
               << makeEvent("N1",  52.52,  13.40, "assault", base.addDays(2))
               << makeEvent("N2",  40.71, -74.01, "assault", base.addDays(3))
               << makeEvent("N3", -33.87, 151.21, "assault", base.addDays(4))
               << makeEvent("N4",  35.68, 139.69, "assault", base.addDays(5));

        QCOMPARE(events.size(), 50);

        const auto series = det.detect(events);

        // Total members across all series must not exceed total events
        int total = 0;
        for (const auto& s : series) total += s.members.size();
        QVERIFY(total <= 50);
        QVERIFY(total >= 3);   // at least one valid cluster
    }

    // ── 9. Haversine helper (sanity checks for space computation) ───────────

    void testHaversineSamePoint()
    {
        QCOMPARE(SeriesDetector::haversineKm(51.5, -0.1, 51.5, -0.1), 0.0);
    }

    void testHaversineLondonManchester()
    {
        // Well-known distance: London → Manchester ≈ 263 km
        const double d = SeriesDetector::haversineKm(51.5074, -0.1278,
                                                      53.4808, -2.2426);
        QVERIFY(std::abs(d - 263.0) < 10.0);
    }

    void testHaversineEquatorial()
    {
        // 1° longitude at the equator ≈ 111.195 km
        const double d = SeriesDetector::haversineKm(0.0, 0.0, 0.0, 1.0);
        QVERIFY(d >= 110.0 && d <= 112.0);
    }

    void testHaversineSymmetry()
    {
        double d1 = SeriesDetector::haversineKm(51.5, -0.1, 53.48, -2.24);
        double d2 = SeriesDetector::haversineKm(53.48, -2.24, 51.5, -0.1);
        QVERIFY(std::abs(d1 - d2) < 1e-9);
    }
};

// ---------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    TestSeriesDetector t;
    return QTest::qExec(&t, argc, argv);
}

#include "test_series_detector.moc"
