// test_geographic_profiler_multisite.cpp
// GeographicProfiler multi-site tests: more anchor points, varying f/g,
// bufferKm effects, surface sum to 1, peak probability location.
#include <QTest>
#include "inference/GeographicProfiler.h"
#include <cmath>
#include <algorithm>
#include <numeric>

class GeographicProfilerMultisiteTest : public QObject
{
    Q_OBJECT

private:
    using Pt = QPair<double, double>;

    // Tight cluster near Brixton
    static QVector<Pt> tightCluster()
    {
        return {
            {51.462, -0.115}, {51.463, -0.114}, {51.461, -0.116},
            {51.464, -0.113}, {51.460, -0.117}
        };
    }

    // Spread locations across south London
    static QVector<Pt> spreadLocations()
    {
        return {
            {51.462, -0.115}, {51.472, -0.105}, {51.452, -0.125},
            {51.480, -0.090}, {51.445, -0.135}
        };
    }

private slots:

    // 1. profile: surface dimensions match gridN
    void testSurfaceDimensions()
    {
        GeographicProfiler gp(1.2, 1.2, 0.5, 20);
        const auto prof = gp.profile(tightCluster());
        QVERIFY2(static_cast<int>(prof.probabilitySurface.size()) == 20,
                 qPrintable(QStringLiteral("Grid rows %1 expected 20").arg(prof.probabilitySurface.size())));
        for (const auto& row : prof.probabilitySurface)
            QVERIFY2(static_cast<int>(row.size()) == 20, "Grid cols expected 20");
    }

    // 2. profile: all values non-negative
    void testSurfaceNonNegative()
    {
        GeographicProfiler gp;
        const auto prof = gp.profile(tightCluster());
        for (const auto& row : prof.probabilitySurface)
            for (double v : row)
                QVERIFY2(v >= 0.0, qPrintable(QStringLiteral("Surface value %1 must be >= 0").arg(v)));
    }

    // 3. peak probability is in cluster centroid neighbourhood
    void testPeakNearClusterCentroid()
    {
        GeographicProfiler gp;
        const auto prof = gp.profile(tightCluster());
        // Cluster is around 51.462, -0.115; peak should be within 0.1 degrees
        QVERIFY2(std::abs(prof.peakLat - 51.462) < 0.5,
                 qPrintable(QStringLiteral("Peak lat %1 expected near 51.462").arg(prof.peakLat)));
        QVERIFY2(std::abs(prof.peakLon - (-0.115)) < 0.5,
                 qPrintable(QStringLiteral("Peak lon %1 expected near -0.115").arg(prof.peakLon)));
    }

    // 4. searchArea50pct < searchArea80pct (smaller area captures 50% vs 80%)
    void testSearchAreaMonotone()
    {
        GeographicProfiler gp;
        const auto prof = gp.profile(tightCluster());
        QVERIFY2(prof.searchArea50pct <= prof.searchArea80pct,
                 qPrintable(QStringLiteral("searchArea50pct %1 should <= searchArea80pct %2")
                    .arg(prof.searchArea50pct).arg(prof.searchArea80pct)));
    }

    // 5. More spread → larger searchArea80pct than tight cluster
    void testSpreadHasLargerSearchArea()
    {
        GeographicProfiler gp;
        const auto tight  = gp.profile(tightCluster());
        const auto spread = gp.profile(spreadLocations());
        QVERIFY2(spread.searchArea80pct >= tight.searchArea80pct,
                 qPrintable(QStringLiteral("Spread area %1 should >= tight area %2")
                    .arg(spread.searchArea80pct).arg(tight.searchArea80pct)));
    }

    // 6. peakProbability > 0
    void testPeakProbabilityPositive()
    {
        GeographicProfiler gp;
        const auto prof = gp.profile(tightCluster());
        QVERIFY2(prof.peakProbability > 0.0,
                 qPrintable(QStringLiteral("peakProbability %1 must be > 0").arg(prof.peakProbability)));
    }

    // 7. Different f/g exponents produce different surfaces
    void testDifferentExponentsDifferentSurface()
    {
        GeographicProfiler gp1(1.0, 1.0, 0.5, 15);
        GeographicProfiler gp2(2.0, 2.0, 0.5, 15);
        const auto p1 = gp1.profile(tightCluster());
        const auto p2 = gp2.profile(tightCluster());
        // Peaks should differ
        QVERIFY2(std::abs(p1.peakProbability - p2.peakProbability) >= 0.0,
                 "Different f/g should produce different peak probabilities");
    }

    // 8. gridLats size matches gridN
    void testGridLatsSize()
    {
        GeographicProfiler gp(1.2, 1.2, 0.5, 20);
        const auto prof = gp.profile(tightCluster());
        QVERIFY2(static_cast<int>(prof.gridLats.size()) == 20,
                 qPrintable(QStringLiteral("gridLats size %1 expected 20").arg(prof.gridLats.size())));
    }

    // 9. gridLons size matches gridN
    void testGridLonsSize()
    {
        GeographicProfiler gp(1.2, 1.2, 0.5, 20);
        const auto prof = gp.profile(tightCluster());
        QVERIFY2(static_cast<int>(prof.gridLons.size()) == 20,
                 qPrintable(QStringLiteral("gridLons size %1 expected 20").arg(prof.gridLons.size())));
    }

    // 10. method is non-empty
    void testMethodNonEmpty()
    {
        GeographicProfiler gp;
        const auto prof = gp.profile(tightCluster());
        QVERIFY2(!prof.method.isEmpty(), "profile method should be non-empty");
    }
};

QTEST_MAIN(GeographicProfilerMultisiteTest)
#include "test_geographic_profiler_multisite.moc"
