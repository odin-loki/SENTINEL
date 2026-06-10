// test_anomaly_geo.cpp — Advanced tests for AnomalyDetector and GeographicProfiler.
// Covers: temporal/spatial anomaly detection, Rossmo CGT formula, buffer zone behaviour.

#include <QTest>
#include <QCoreApplication>

#include <cmath>
#include <limits>
#include <algorithm>

#include "inference/AnomalyDetector.h"
#include "inference/GeographicProfiler.h"
#include "core/CrimeEvent.h"

static constexpr double TEST_PI = 3.14159265358979323846;

// ── helper ────────────────────────────────────────────────────────────────────

static AnomalyFeatureVector makeFeature(const QString& id,
                                         double lat, double lon,
                                         double tDays,
                                         double hourNorm = 0.5,
                                         int typeCode = 0)
{
    AnomalyFeatureVector fv;
    fv.eventId       = id;
    fv.lat           = lat;
    fv.lon           = lon;
    fv.tDays         = tDays;
    fv.hourNorm      = hourNorm;
    fv.crimeTypeCode = typeCode;
    return fv;
}

// ─────────────────────────────────────────────────────────────────────────────

class TestAnomalyGeo : public QObject
{
    Q_OBJECT
private slots:
    // Part 1 — AnomalyDetector
    void testNoAnomalyInRegularData();
    void testSpatialSpike();
    void testTemporalClustering();
    void testRapidEscalation();
    void testAnomalySignalHasRequiredFields();

    // Part 2 — GeographicProfiler (Rossmo CGT)
    void testRossmoFormulaBasic();
    void testProfileRequiresMinimumEvents();
    void testRossmoBufferZone();
    void testProfileOutput();
    void testMultipleSeriesProfileComparison();
};

// ── Part 1: AnomalyDetector ───────────────────────────────────────────────────

void TestAnomalyGeo::testNoAnomalyInRegularData()
{
    AnomalyDetector det(0.05);

    // 100 events, uniform 1-per-day rate over 100 days, small spatial variation
    QVector<AnomalyFeatureVector> data;
    for (int i = 0; i < 100; ++i) {
        data.append(makeFeature(
            QString("E%1").arg(i),
            51.5 + (i % 10) * 0.01,
            -0.1 + (i / 10) * 0.01,
            static_cast<double>(i),
            0.5
        ));
    }

    det.fit(data);
    auto detections = det.detectAnomalies(data);

    QCOMPARE(detections.size(), data.size());

    // Regular uniform data should produce very few high-severity anomalies
    int highCount = 0;
    for (const auto& s : detections) {
        if (s.combinedScore >= 0.8)
            ++highCount;
    }
    // Allow at most 5 % contamination (5 out of 100)
    QVERIFY(highCount <= 5);
}

void TestAnomalyGeo::testSpatialSpike()
{
    AnomalyDetector det(0.05);

    // Background: 20 events spread across a 0.5° × 0.5° area near London
    QVector<AnomalyFeatureVector> background;
    for (int i = 0; i < 20; ++i) {
        background.append(makeFeature(
            QString("BG%1").arg(i),
            51.0 + (i % 5) * 0.1,    // 51.0 – 51.4 °N
            -0.2 + (i / 5) * 0.1,    // -0.2 – 0.2 °E
            static_cast<double>(i),
            0.5
        ));
    }
    det.fit(background);

    // Add 10 events tightly concentrated far from the background cluster
    QVector<AnomalyFeatureVector> allEvents = background;
    for (int i = 0; i < 10; ++i) {
        allEvents.append(makeFeature(
            QString("SPIKE%1").arg(i),
            53.5 + i * 0.001,    // ~53.5 °N — far from background ~51.2 °N
             1.0 + i * 0.001,    // ~1.0 °E  — far from background ~0.0 °E
            static_cast<double>(i),
            0.5
        ));
    }

    auto detections = det.detectAnomalies(allEvents);

    // Spike events must have a clearly elevated spatial z-score
    // Background centroid ~(51.2, 0.0), σ_lat ≈ 0.16, σ_lon ≈ 0.13
    // Spike at (53.5, 1.0): zSpatial ≈ sqrt(14.4² + 7.7²) ≈ 16.3
    double maxSpikeZ = 0.0;
    for (const auto& s : detections) {
        if (s.eventId.startsWith("SPIKE"))
            maxSpikeZ = std::max(maxSpikeZ, s.zScoreSpatial);
    }
    QVERIFY(maxSpikeZ > 2.0);
}

void TestAnomalyGeo::testTemporalClustering()
{
    AnomalyDetector det(0.05);

    // Background: 1 event per day, days 1–50
    QVector<AnomalyFeatureVector> background;
    for (int i = 0; i < 50; ++i) {
        background.append(makeFeature(
            QString("BG%1").arg(i),
            51.5 + i * 0.001,
            -0.1 + i * 0.001,
            static_cast<double>(i + 1),   // tDays 1..50
            0.5
        ));
    }
    det.fit(background);

    // Add 15 events all on day 100 — far outside the training range
    QVector<AnomalyFeatureVector> allEvents = background;
    for (int i = 0; i < 15; ++i) {
        allEvents.append(makeFeature(
            QString("CLUSTER%1").arg(i),
            51.5 + i * 0.001,
            -0.1 + i * 0.001,
            100.0,    // day 100
            0.5
        ));
    }

    auto detections = det.detectAnomalies(allEvents);

    // Background mean ≈ 25.5, σ ≈ 14.6 → z for day 100 ≈ 5.1
    double minClusterZ = std::numeric_limits<double>::max();
    double maxBgZ      = 0.0;
    for (const auto& s : detections) {
        if (s.eventId.startsWith("CLUSTER"))
            minClusterZ = std::min(minClusterZ, s.zScoreTemporal);
        else
            maxBgZ = std::max(maxBgZ, s.zScoreTemporal);
    }

    QVERIFY(minClusterZ > 2.0);
    QVERIFY(minClusterZ > maxBgZ);
}

void TestAnomalyGeo::testRapidEscalation()
{
    AnomalyDetector det(0.05);

    // Daily counts: 1, 1, 1, 1, 5, 10, 20 — plus spatial drift to ensure detectability
    const int counts[] = {1, 1, 1, 1, 5, 10, 20};
    QVector<AnomalyFeatureVector> data;
    for (int day = 1; day <= 7; ++day) {
        for (int k = 0; k < counts[day - 1]; ++k) {
            data.append(makeFeature(
                QString("D%1E%2").arg(day).arg(k),
                51.5 + (day - 1) * 0.3,   // lat increases day-by-day
                -0.1 + k * 0.001,
                static_cast<double>(day),
                0.5
            ));
        }
    }

    det.fit(data);
    auto detections = det.detectAnomalies(data);

    QCOMPARE(detections.size(), data.size());

    // The sharp spatial escalation must produce at least one elevated signal
    bool anyElevated = false;
    for (const auto& s : detections) {
        if (s.isAnomaly || s.combinedScore > 0.5)
            anyElevated = true;
    }
    QVERIFY(anyElevated);
}

void TestAnomalyGeo::testAnomalySignalHasRequiredFields()
{
    AnomalyDetector det(0.05);

    QVector<AnomalyFeatureVector> data;
    for (int i = 0; i < 15; ++i) {
        data.append(makeFeature(
            QString("E%1").arg(i),
            51.5 + i * 0.01,
            -0.1 + i * 0.01,
            static_cast<double>(i * 3),
            static_cast<double>(i % 24) / 24.0
        ));
    }

    det.fit(data);
    auto detected = det.detectAnomalies(data);

    QVERIFY(!detected.isEmpty());

    for (const auto& sig : detected) {
        // eventId must be set
        QVERIFY(!sig.eventId.isEmpty());

        // All numeric fields must be finite
        QVERIFY(std::isfinite(sig.isolationScore));
        QVERIFY(std::isfinite(sig.lofScore));
        QVERIFY(std::isfinite(sig.zScoreTemporal));
        QVERIFY(std::isfinite(sig.zScoreSpatial));
        QVERIFY(std::isfinite(sig.combinedScore));

        // Scores must be non-negative
        QVERIFY(sig.isolationScore  >= 0.0);
        QVERIFY(sig.lofScore        >= 0.0);
        QVERIFY(sig.zScoreTemporal  >= 0.0);
        QVERIFY(sig.zScoreSpatial   >= 0.0);
        QVERIFY(sig.combinedScore   >= 0.0);

        // combinedScore is a weighted sum of [0,1] components, so bounded above
        QVERIFY(sig.combinedScore <= 2.0);
    }
}

// ── Part 2: GeographicProfiler ────────────────────────────────────────────────

void TestAnomalyGeo::testRossmoFormulaBasic()
{
    // Use a small buffer (1 km ≈ 0.009°) relative to crime ring radius (0.1°).
    // All crimes are in the FAR zone → Rossmo weights decrease with distance
    // from grid point → peak lands near the ring centroid (the "home base").
    GeographicProfiler profiler(1.2, 1.2, 1.0, 80);  // 1 km buffer

    const double homeLat = 51.5;
    const double homeLon = -0.1;
    const double r = 0.1;  // circle radius in degrees
    const int    N = 12;

    QVector<QPair<double,double>> locs;
    for (int i = 0; i < N; ++i) {
        double angle = 2.0 * TEST_PI * i / N;
        locs.append({homeLat + r * std::cos(angle),
                     homeLon + r * std::sin(angle)});
    }

    auto gp = profiler.profile(locs);

    QVERIFY(!gp.probabilitySurface.empty());
    QVERIFY(gp.peakProbability > 0.0);

    // For a symmetric crime ring, the Rossmo peak should be inside the ring,
    // within 0.2° of the centroid (generous tolerance for discrete grid).
    double dist = std::hypot(gp.peakLat - homeLat, gp.peakLon - homeLon);
    qDebug() << "Peak at (" << gp.peakLat << "," << gp.peakLon
             << ") distance to center:" << dist;
    QVERIFY2(dist <= 0.2,
             "Rossmo peak should be within 0.2° of the crime ring centroid");
}

void TestAnomalyGeo::testProfileRequiresMinimumEvents()
{
    GeographicProfiler profiler;

    // Empty input — must not crash; empty or trivially-valid profile is acceptable
    {
        auto gp = profiler.profile({});
        if (!gp.probabilitySurface.empty()) {
            double s = 0.0;
            for (const auto& row : gp.probabilitySurface)
                for (double v : row) s += v;
            QVERIFY(s >= 0.0 && s <= 1.0 + 1e-6);
        }
        QVERIFY(true);
    }

    // Single event — must not crash; if surface returned it must be valid
    {
        auto gp = profiler.profile({{51.5, -0.1}});
        if (!gp.probabilitySurface.empty()) {
            double s = 0.0;
            for (const auto& row : gp.probabilitySurface)
                for (double v : row) s += v;
            QVERIFY(s >= 0.0 && s <= 1.0 + 1e-6);
        }
        QVERIFY(true);
    }
}

void TestAnomalyGeo::testRossmoBufferZone()
{
    // 10 crimes clustered north of a "suspected home base" at ~51.0 °N.
    // All crimes are >= 0.5° away from the home base.
    // After Rossmo scoring the peak should stay anchored near the crime cluster,
    // NOT drift more than 0.5° away from the cluster midpoint.
    GeographicProfiler profiler(1.2, 1.2, 5.0, 60);  // 5 km buffer ≈ 0.045°

    QVector<QPair<double,double>> locs;
    for (int i = 0; i < 10; ++i) {
        locs.append({
            51.5 + i * 0.005,   // 51.500 – 51.545 °N
            -0.1 + i * 0.003    // small E spread
        });
    }

    auto gp = profiler.profile(locs);

    // Grid must be populated
    QVERIFY(!gp.gridLats.empty());
    QVERIFY(!gp.gridLons.empty());

    // Peak must lie within the grid
    QVERIFY(gp.peakLat >= gp.gridLats.front() - 1e-9);
    QVERIFY(gp.peakLat <= gp.gridLats.back()  + 1e-9);

    // Crime-cluster midpoint
    const double crimeMidLat = 51.5 + 4.5 * 0.005;   // ≈ 51.5225
    QVERIFY(std::abs(gp.peakLat - crimeMidLat) <= 0.5);
}

void TestAnomalyGeo::testProfileOutput()
{
    GeographicProfiler profiler(1.2, 1.2, 1.0, 40);

    QVector<QPair<double,double>> locs;
    for (int i = 0; i < 10; ++i)
        locs.append({51.5 + i * 0.01, -0.1 + i * 0.01});

    auto gp = profiler.profile(locs);

    // Probability surface: non-empty, correct grid dimensions
    QVERIFY(!gp.probabilitySurface.empty());
    QCOMPARE(static_cast<int>(gp.probabilitySurface.size()), 40);
    for (const auto& row : gp.probabilitySurface)
        QCOMPARE(static_cast<int>(row.size()), 40);

    // Grid axes populated
    QCOMPARE(static_cast<int>(gp.gridLats.size()), 40);
    QCOMPARE(static_cast<int>(gp.gridLons.size()), 40);

    // Anchor point (home-base estimate) must be finite and inside the grid
    QVERIFY(std::isfinite(gp.peakLat));
    QVERIFY(std::isfinite(gp.peakLon));
    QVERIFY(gp.peakProbability > 0.0);
    QVERIFY(gp.peakLat >= gp.gridLats.front() - 1e-9);
    QVERIFY(gp.peakLat <= gp.gridLats.back()  + 1e-9);
    QVERIFY(gp.peakLon >= gp.gridLons.front() - 1e-9);
    QVERIFY(gp.peakLon <= gp.gridLons.back()  + 1e-9);

    // Method tag must be set
    QVERIFY(!gp.method.isEmpty());
}

void TestAnomalyGeo::testMultipleSeriesProfileComparison()
{
    GeographicProfiler profiler(1.2, 1.2, 3.0, 40);  // 3 km buffer ≈ 0.027°

    // Series 1 — London area (~51.5 °N, -0.1 °E)
    QVector<QPair<double,double>> series1 = {
        {51.50, -0.10}, {51.51, -0.09}, {51.49, -0.11},
        {51.50, -0.08}, {51.52, -0.12}
    };

    // Series 2 — Manchester area (~53.5 °N, -2.2 °E)
    QVector<QPair<double,double>> series2 = {
        {53.48, -2.20}, {53.50, -2.18}, {53.49, -2.22},
        {53.51, -2.19}, {53.47, -2.21}
    };

    auto profile1 = profiler.profile(series1);
    auto profile2 = profiler.profile(series2);

    QVERIFY(!profile1.probabilitySurface.empty());
    QVERIFY(!profile2.probabilitySurface.empty());

    // Anchor points must be in clearly different locations (>1° latitude apart)
    double latDiff = std::abs(profile1.peakLat - profile2.peakLat);
    QVERIFY(latDiff > 1.0);
}

// ─────────────────────────────────────────────────────────────────────────────

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    TestAnomalyGeo tests;
    return QTest::qExec(&tests, argc, argv);
}
#include "test_anomaly_geo.moc"

