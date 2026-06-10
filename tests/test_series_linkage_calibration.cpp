// tests/test_series_linkage_calibration.cpp
// Calibration tests for SeriesDetector: link probability, near-repeat params,
// MO Jaccard similarity, haversine distance, and series detection.

#include <QTest>
#include <QCoreApplication>
#include <QVector>
#include <cmath>

#include "models/SeriesDetector.h"
#include "core/CrimeEvent.h"

class TestSeriesLinkageCalibration : public QObject
{
    Q_OBJECT

private slots:
    void testLinkProbabilityRange();
    void testLinkProbabilityHighForNearRepeat();
    void testLinkProbabilityLowForUnrelated();
    void testNearRepeatParamsForBurglary();
    void testNearRepeatParamsForTheft();
    void testMOJaccardSymmetric();
    void testMOJaccardIdentical();
    void testMOJaccardDisjoint();
    void testHaversineKnownDistance();
    void testDetectSeries();
};

// ── Helpers ──────────────────────────────────────────────────────────────────

static SeriesEvent makeSeriesEvent(const QString& id,
                                    double lat, double lon,
                                    double tDays,
                                    const QString& crimeType = "burglary",
                                    const QString& mo = "")
{
    SeriesEvent ev;
    ev.eventId    = id;
    ev.lat        = lat;
    ev.lon        = lon;
    ev.tDays      = tDays;
    ev.crimeType  = crimeType;
    ev.moText     = mo;
    return ev;
}

static CrimeSeries makeSeries(const QString& id,
                               double centLat, double centLon,
                               double firstDays, double lastDays,
                               int memberCount = 3,
                               const QString& crimeType = "burglary")
{
    CrimeSeries s;
    s.seriesId         = id;
    s.centroidLat      = centLat;
    s.centroidLon      = centLon;
    s.firstDays        = firstDays;
    s.lastDays         = lastDays;
    s.dominantCrimeType = crimeType;
    for (int i = 0; i < memberCount; ++i)
        s.members.append(makeSeriesEvent(QString("m%1").arg(i), centLat, centLon,
                                         firstDays + i, crimeType));
    return s;
}

// ── 1. linkProbability() always returns probability in [0,1] ─────────────────
void TestSeriesLinkageCalibration::testLinkProbabilityRange()
{
    SeriesDetector det;

    // Sample combinations of distance and time that span a wide range
    const double lats[]  = {51.5, 51.501, 51.6,  52.0};
    const double lons[]  = {-0.1, -0.1,   -0.11, -0.2};
    const double tDays[] = {100,   101,    110,   200};

    CrimeSeries series = makeSeries("S1", 51.5, -0.1, 90, 100);

    for (int i = 0; i < 4; ++i) {
        SeriesEvent ev = makeSeriesEvent("ev", lats[i], lons[i], tDays[i]);
        SeriesMatch m  = det.linkProbability(ev, series, 0.5);
        QVERIFY2(m.linkProbability >= 0.0 && m.linkProbability <= 1.0,
                 qPrintable(QString("linkProbability=%1").arg(m.linkProbability)));
    }
}

// ── 2. Same location, same MO, immediate time → high probability (>= 0.25) ───
// Formula: raw = 0.05 * multiplier * composite / 0.5
// Burglary multiplier=4.5 → max raw = 0.05*4.5*1.0/0.5 = 0.45
// Threshold 0.25 is safely above the 0.05 base rate and meaningful.
void TestSeriesLinkageCalibration::testLinkProbabilityHighForNearRepeat()
{
    SeriesDetector det;

    // New event exactly at the centroid, same time as the last series member
    CrimeSeries series = makeSeries("S1", 51.5, -0.1, 90, 92); // members at t=90,91,92
    SeriesEvent ev = makeSeriesEvent("ev", 51.5, -0.1, 92, "burglary", "forced entry window");
    for (auto& mem : series.members)
        mem.moText = "forced entry window";

    SeriesMatch m = det.linkProbability(ev, series, 1.0); // identical MO, dist=0, dt=0
    QVERIFY2(m.linkProbability >= 0.25,
             qPrintable(QString("Expected high prob (>=0.25), got %1").arg(m.linkProbability)));
}

// ── 3. Far apart, different MO, long time → low probability (< 0.3) ──────────
void TestSeriesLinkageCalibration::testLinkProbabilityLowForUnrelated()
{
    SeriesDetector det;

    // New event 20 km away and 180 days later, completely different MO
    CrimeSeries series = makeSeries("S1", 51.5, -0.1, 0, 5);
    SeriesEvent ev     = makeSeriesEvent("ev", 51.68, -0.1, 185, "assault", "knife weapon");

    SeriesMatch m = det.linkProbability(ev, series, 0.0); // zero MO similarity
    QVERIFY2(m.linkProbability < 0.3,
             qPrintable(QString("Expected low prob, got %1").arg(m.linkProbability)));
}

// ── 4. Burglary near-repeat params should match calibration table ─────────────
void TestSeriesLinkageCalibration::testNearRepeatParamsForBurglary()
{
    auto p = SeriesDetector::nearRepeatFor("burglary");
    // From SeriesDetector.cpp: { 200.0, 14.0, 4.5 }
    QCOMPARE(p.distM,      200.0);
    QCOMPARE(p.days,        14.0);
    QCOMPARE(p.multiplier,   4.5);
}

// ── 5. Theft defaults differ from burglary (theft not in table → defaults) ────
void TestSeriesLinkageCalibration::testNearRepeatParamsForTheft()
{
    auto burgP = SeriesDetector::nearRepeatFor("burglary");
    auto theftP = SeriesDetector::nearRepeatFor("theft"); // falls back to defaults

    // Theft should have different parameters than burglary
    QVERIFY2(burgP.distM != theftP.distM ||
             burgP.days  != theftP.days  ||
             burgP.multiplier != theftP.multiplier,
             "Theft and burglary should not share identical near-repeat params");

    // Default params are valid
    QVERIFY(theftP.distM > 0.0);
    QVERIFY(theftP.days  > 0.0);
    QVERIFY(theftP.multiplier > 0.0);
}

// ── 6. moJaccard(a,b) == moJaccard(b,a) ──────────────────────────────────────
void TestSeriesLinkageCalibration::testMOJaccardSymmetric()
{
    const QString a = "forced entry rear window smashed glass";
    const QString b = "smashed glass side door forced";

    double ab = SeriesDetector::moJaccard(a, b);
    double ba = SeriesDetector::moJaccard(b, a);

    QVERIFY2(std::abs(ab - ba) < 1e-9,
             qPrintable(QString("J(a,b)=%1 J(b,a)=%2").arg(ab).arg(ba)));
}

// ── 7. moJaccard(a,a) == 1.0 ─────────────────────────────────────────────────
void TestSeriesLinkageCalibration::testMOJaccardIdentical()
{
    const QString s = "burglary forced entry rear window";
    double j = SeriesDetector::moJaccard(s, s);
    QVERIFY2(std::abs(j - 1.0) < 1e-9,
             qPrintable(QString("J(a,a)=%1").arg(j)));
}

// ── 8. moJaccard on completely disjoint sets == 0.0 ──────────────────────────
void TestSeriesLinkageCalibration::testMOJaccardDisjoint()
{
    double j = SeriesDetector::moJaccard("cat dog", "fish bird");
    QVERIFY2(std::abs(j - 0.0) < 1e-9,
             qPrintable(QString("J(disjoint)=%1").arg(j)));
}

// ── 9. Haversine: London → Manchester ≈ 262 km ───────────────────────────────
void TestSeriesLinkageCalibration::testHaversineKnownDistance()
{
    // London: 51.5074° N, 0.1278° W
    // Manchester: 53.4808° N, 2.2426° W
    double d = SeriesDetector::haversineKm(51.5074, -0.1278, 53.4808, -2.2426);
    // Accept ±5 km tolerance
    QVERIFY2(std::abs(d - 262.0) < 5.0,
             qPrintable(QString("London-Manchester=%1 km (expected ≈262)").arg(d)));
}

// ── 10. 20 tightly-clustered events should form at least 1 series ────────────
void TestSeriesLinkageCalibration::testDetectSeries()
{
    // eps = 0.3 km, epsDays = 14, minSamples = 3
    SeriesDetector det(0.3, 14.0, 3);

    QVector<SeriesEvent> events;
    // 20 events within 0.1 km radius and 10 days — well within DBSCAN eps
    for (int i = 0; i < 20; ++i) {
        double latOffset = (i % 5)  * 0.0003;   // ~33 m steps
        double lonOffset = (i / 5)  * 0.0003;
        double t         = static_cast<double>(i) * 0.5; // 0..9.5 days
        events.append(makeSeriesEvent(QString("e%1").arg(i),
                                       51.5 + latOffset,
                                       -0.1 + lonOffset,
                                       t,
                                       "burglary",
                                       "forced entry"));
    }

    QVector<CrimeSeries> series = det.detectSeries(events);
    QVERIFY2(!series.isEmpty(),
             "Expected at least 1 series from 20 clustered events");

    // Total members across all series should equal 20
    int totalMembers = 0;
    for (const auto& s : series)
        totalMembers += s.members.size();
    QVERIFY2(totalMembers == 20,
             qPrintable(QString("Total members=%1, expected 20").arg(totalMembers)));
}

// ─────────────────────────────────────────────────────────────────────────────

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    TestSeriesLinkageCalibration t;
    return QTest::qExec(&t, argc, argv);
}
#include "test_series_linkage_calibration.moc"
