// test_series_detector_deep.cpp
// Deep tests for SeriesDetector: DBSCAN clustering, Jaccard, linkProbability,
// and the CrimeEvent convenience overload.
#include <QTest>
#include <QTimeZone>
#include "models/SeriesDetector.h"
#include "core/CrimeEvent.h"
#include <cmath>
#include <algorithm>

class SeriesDetectorDeepTest : public QObject
{
    Q_OBJECT

private:
    static SeriesEvent sev(double lat, double lon, double t,
                            const QString& mo = QStringLiteral("forced_entry residential night cash"))
    {
        SeriesEvent e;
        e.eventId    = QStringLiteral("E%1").arg(qRound(t * 100));
        e.lat        = lat;
        e.lon        = lon;
        e.tDays      = t;
        e.crimeType  = QStringLiteral("burglary");
        e.moText     = mo;
        return e;
    }

    static CrimeEvent cev(const QString& id, double lat, double lon,
                           const QDate& date, const QString& type = QStringLiteral("burglary"))
    {
        CrimeEvent ev;
        ev.eventId   = id;
        ev.crimeType = type;
        ev.lat       = lat;
        ev.lon       = lon;
        ev.latitude  = lat;
        ev.longitude = lon;
        const QDateTime dt(date, QTime(12, 0, 0), QTimeZone::utc());
        ev.occurredAt = dt;
        ev.timestamp  = dt;
        ev.suburb     = QStringLiteral("Zone1");
        return ev;
    }

private slots:

    // ── 1. Single event → no series ──────────────────────────────────────────
    void testSingleEventNoSeries()
    {
        SeriesDetector sd;
        const auto result = sd.detectSeries({ sev(51.5, -0.1, 0.0) });
        QVERIFY2(result.isEmpty(), "Single event should produce no series (< minSamples=3)");
    }

    // ── 2. Two events → no series (need minSamples=3) ────────────────────────
    void testTwoEventsNoSeries()
    {
        SeriesDetector sd;
        const auto result = sd.detectSeries({
            sev(51.5, -0.1, 0.0),
            sev(51.5, -0.1, 1.0),
        });
        QVERIFY2(result.isEmpty(), "Two events below minSamples=3 should produce no series");
    }

    // ── 3. Tight cluster of 5 events → forms series ──────────────────────────
    void testTightClusterFormsSeries()
    {
        SeriesDetector sd(0.5, 20.0, 3);  // 500m, 20 days, min 3 samples
        QVector<SeriesEvent> evs;
        for (int i = 0; i < 5; ++i)
            evs.append(sev(51.5 + i * 0.001, -0.1 + i * 0.001, static_cast<double>(i)));

        const auto result = sd.detectSeries(evs);
        QVERIFY2(!result.isEmpty(), "Tight cluster of 5 events should form at least one series");
    }

    // ── 4. Series has correct member count ───────────────────────────────────
    void testSeriesMemberCount()
    {
        SeriesDetector sd(0.5, 20.0, 3);
        QVector<SeriesEvent> evs;
        for (int i = 0; i < 6; ++i)
            evs.append(sev(51.5 + i * 0.001, -0.1, static_cast<double>(i)));

        const auto result = sd.detectSeries(evs);
        QVERIFY(!result.isEmpty());
        QVERIFY2(result.first().members.size() >= 3,
                 qPrintable(QStringLiteral("Series should have >= 3 members, got %1")
                    .arg(result.first().members.size())));
    }

    // ── 5. Centroid is within the cluster bounding box ───────────────────────
    void testCentroidBounds()
    {
        SeriesDetector sd(1.0, 30.0, 3);
        QVector<SeriesEvent> evs;
        for (int i = 0; i < 5; ++i)
            evs.append(sev(51.5 + i * 0.001, -0.1, static_cast<double>(i)));

        const auto result = sd.detectSeries(evs);
        QVERIFY(!result.isEmpty());
        const auto& s = result.first();
        QVERIFY2(s.centroidLat >= 51.4 && s.centroidLat <= 51.6,
                 qPrintable(QStringLiteral("Centroid lat %1 out of bounds").arg(s.centroidLat)));
    }

    // ── 6. MO Jaccard: identical strings → 1.0 ───────────────────────────────
    void testMoJaccardIdentical()
    {
        const double j = SeriesDetector::moJaccard(
            QStringLiteral("forced_entry residential night cash"),
            QStringLiteral("forced_entry residential night cash"));
        QVERIFY2(std::abs(j - 1.0) < 1e-9, "Identical MO Jaccard should be 1.0");
    }

    // ── 7. MO Jaccard: disjoint strings → 0.0 ────────────────────────────────
    void testMoJaccardDisjoint()
    {
        const double j = SeriesDetector::moJaccard(
            QStringLiteral("forced_entry residential"),
            QStringLiteral("vehicle daytime weapon"));
        QVERIFY2(std::abs(j) < 1e-9, "Disjoint MO Jaccard should be 0.0");
    }

    // ── 8. MO Jaccard: 50% overlap → 0.33 ─────────────────────────────────────
    void testMoJaccardPartialOverlap()
    {
        // A = {a, b, c}, B = {b, c, d} → intersection=2, union=4 → J=0.5
        const double j = SeriesDetector::moJaccard(
            QStringLiteral("a b c"),
            QStringLiteral("b c d"));
        QVERIFY2(std::abs(j - 0.5) < 0.05,
                 qPrintable(QStringLiteral("Partial overlap Jaccard expected ~0.5, got %1").arg(j)));
    }

    // ── 9. Haversine distance: same point → 0 ────────────────────────────────
    void testHaversineZero()
    {
        const double d = SeriesDetector::haversineKm(51.5, -0.1, 51.5, -0.1);
        QVERIFY2(d < 1e-6, qPrintable(QStringLiteral("Same point haversine should be 0, got %1").arg(d)));
    }

    // ── 10. CrimeEvent overload: 5-event cluster forms series ────────────────
    void testCrimeEventOverload()
    {
        SeriesDetector sd(0.5, 20.0, 3);
        QVector<CrimeEvent> evs;
        const QDate base(2024, 3, 1);
        for (int i = 0; i < 5; ++i)
            evs.append(cev(QStringLiteral("C%1").arg(i),
                           51.5 + i * 0.001, -0.1, base.addDays(i)));

        const auto result = sd.detect(evs);
        QVERIFY2(!result.isEmpty(), "CrimeEvent overload should detect tight cluster as series");
    }
};

QTEST_MAIN(SeriesDetectorDeepTest)
#include "test_series_detector_deep.moc"
