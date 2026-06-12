// test_geographic_profiler_deep6.cpp — Deep audit iteration 20: GeographicProfiler
// Verifies: empty/single crime edge cases, surface normalization, grid layout, non-negativity.

#include <QtTest/QtTest>
#include <cmath>
#include <numeric>
#include "inference/GeographicProfiler.h"

namespace {

QVector<QPair<double, double>> tightCluster()
{
    return {
        { 51.500, -0.120 }, { 51.502, -0.118 }, { 51.498, -0.122 },
        { 51.501, -0.119 }, { 51.499, -0.121 }, { 51.5005, -0.1195 }
    };
}

double surfaceSum(const GeographicProfile& result)
{
    double total = 0.0;
    for (const auto& row : result.probabilitySurface)
        for (double v : row)
            total += v;
    return total;
}

} // namespace

class GeographicProfilerDeep6Test : public QObject
{
    Q_OBJECT

private slots:

    void testEmptyCrimesReturnsDefaultProfile()
    {
        GeographicProfiler gp(1.2, 1.2, 0.5, 40);
        const auto result = gp.profile({});

        QCOMPARE(result.method, QStringLiteral("rossmo_cgt"));
        QVERIFY(result.probabilitySurface.empty());
        QVERIFY(result.gridLats.empty());
        QVERIFY(result.gridLons.empty());
        QCOMPARE(result.peakProbability, 0.0);
        QCOMPARE(result.searchArea50pct, 0.0);
        QCOMPARE(result.searchArea80pct, 0.0);
    }

    void testProbabilitySurfaceSumsToOne()
    {
        GeographicProfiler gp(1.2, 1.2, 0.4, 50);
        const auto result = gp.profile(tightCluster());

        const double total = surfaceSum(result);
        QVERIFY2(std::abs(total - 1.0) < 1e-9,
                 qPrintable(QStringLiteral("surface sum %1 should be 1.0")
                     .arg(total, 0, 'g', 12)));
    }

    void testGridDimensionsMatchGridN()
    {
        const int gridN = 37;
        GeographicProfiler gp(1.2, 1.2, 0.3, gridN);
        const auto result = gp.profile(tightCluster());

        QCOMPARE(static_cast<int>(result.gridLats.size()), gridN);
        QCOMPARE(static_cast<int>(result.gridLons.size()), gridN);
        QCOMPARE(static_cast<int>(result.probabilitySurface.size()), gridN);
        for (const auto& row : result.probabilitySurface)
            QCOMPARE(static_cast<int>(row.size()), gridN);
    }

    void testSingleCrimeProducesValidProfile()
    {
        GeographicProfiler gp(1.2, 1.2, 0.5, 30);
        const auto result = gp.profile({ { 51.505, -0.118 } });

        QVERIFY(!result.probabilitySurface.empty());
        QVERIFY(result.peakProbability > 0.0);
        QVERIFY(result.searchArea50pct > 0.0);
        QVERIFY(result.searchArea80pct >= result.searchArea50pct);
        QVERIFY2(std::abs(surfaceSum(result) - 1.0) < 1e-9,
                 "single-crime surface should still normalise to 1");
    }

    void testSurfaceAllNonNegative()
    {
        GeographicProfiler gp(1.2, 1.2, 0.35, 45);
        QVector<QPair<double, double>> crimes = tightCluster();
        crimes.append({ 51.70, 0.02 });
        crimes.append({ 51.30, -0.25 });

        const auto result = gp.profile(crimes);
        for (const auto& row : result.probabilitySurface) {
            for (double v : row)
                QVERIFY2(v >= 0.0,
                         qPrintable(QStringLiteral("negative cell value %1").arg(v)));
        }
    }

    void testPeakNearClusterCentroid()
    {
        const auto crimes = tightCluster();
        double cLat = 0.0, cLon = 0.0;
        for (const auto& c : crimes) {
            cLat += c.first;
            cLon += c.second;
        }
        cLat /= crimes.size();
        cLon /= crimes.size();

        GeographicProfiler gp(1.2, 1.2, 0.3, 55);
        const auto result = gp.profile(crimes);

        QVERIFY2(std::abs(result.peakLat - cLat) < 0.02,
                 qPrintable(QStringLiteral("peak lat %1 vs centroid %2")
                     .arg(result.peakLat).arg(cLat)));
        QVERIFY2(std::abs(result.peakLon - cLon) < 0.02,
                 qPrintable(QStringLiteral("peak lon %1 vs centroid %2")
                     .arg(result.peakLon).arg(cLon)));
    }

    void testTightClusterOn2x2GridDegenerateSurface()
    {
        // Known limitation: 2x2 grid places cells only at bbox corners; a tight
        // cluster can fall entirely in the Rossmo far-field (dist >= 4B) → zero surface.
        GeographicProfiler gp(1.2, 1.2, 0.5, 1);
        const auto result = gp.profile(tightCluster());

        QCOMPARE(static_cast<int>(result.probabilitySurface.size()), 2);
        QCOMPARE(result.peakProbability, 0.0);
        QCOMPARE(surfaceSum(result), 0.0);
    }

    void testLowGridNClampedToTwo()
    {
        // gridN < 2 is clamped to 2 (constructor enforces minimum resolution).
        GeographicProfiler gp(1.2, 1.2, 0.5, 0);
        const auto result = gp.profile(tightCluster());

        QCOMPARE(static_cast<int>(result.gridLats.size()), 2);
        QCOMPARE(static_cast<int>(result.gridLons.size()), 2);
        QCOMPARE(static_cast<int>(result.probabilitySurface.size()), 2);
        QCOMPARE(static_cast<int>(result.probabilitySurface[0].size()), 2);
    }
};

QTEST_GUILESS_MAIN(GeographicProfilerDeep6Test)
#include "test_geographic_profiler_deep6.moc"
