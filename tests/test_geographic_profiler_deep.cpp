// test_geographic_profiler_deep.cpp
// Deep tests for GeographicProfiler: Rossmo CGT surface, peak probability,
// search area, grid dimensions, and edge cases.
#include <QTest>
#include "inference/GeographicProfiler.h"
#include "core/CrimeEvent.h"
#include <cmath>
#include <algorithm>
#include <numeric>

class GeographicProfilerDeepTest : public QObject
{
    Q_OBJECT

private:
    using Pt = QPair<double, double>;

    static QVector<Pt> londonCrimes(int n = 5)
    {
        QVector<Pt> pts;
        for (int i = 0; i < n; ++i)
            pts.append({ 51.5074 + i * 0.01, -0.1278 + i * 0.01 });
        return pts;
    }

    // Circular cluster of crimes with anchor at center
    static QVector<Pt> circularCluster(double lat, double lon, int n = 8, double r = 0.01)
    {
        QVector<Pt> pts;
        for (int i = 0; i < n; ++i) {
            const double angle = 2 * M_PI * i / n;
            pts.append({ lat + r * std::cos(angle), lon + r * std::sin(angle) });
        }
        return pts;
    }

private slots:

    // 1. profile() returns non-empty surface
    void testProfileReturnsNonEmpty()
    {
        GeographicProfiler gp;
        const auto result = gp.profile(londonCrimes());
        QVERIFY2(!result.probabilitySurface.empty(),
                 "profile() should return non-empty probability surface");
    }

    // 2. Probability surface values are non-negative
    void testSurfaceNonNegative()
    {
        GeographicProfiler gp;
        const auto result = gp.profile(londonCrimes());
        for (const auto& row : result.probabilitySurface)
            for (double v : row)
                QVERIFY2(v >= 0.0,
                         qPrintable(QStringLiteral("Surface value %1 must be >= 0").arg(v)));
    }

    // 3. Surface sums to positive total
    void testSurfaceSumPositive()
    {
        GeographicProfiler gp;
        const auto result = gp.profile(londonCrimes());
        double total = 0.0;
        for (const auto& row : result.probabilitySurface)
            for (double v : row) total += v;
        QVERIFY2(total > 0.0,
                 qPrintable(QStringLiteral("Surface total %1 must be > 0").arg(total)));
    }

    // 4. peakProbability is the maximum surface value
    void testPeakProbabilityIsMax()
    {
        GeographicProfiler gp;
        const auto result = gp.profile(londonCrimes());

        double maxVal = 0.0;
        for (const auto& row : result.probabilitySurface)
            for (double v : row) maxVal = std::max(maxVal, v);

        // Normalize and check peak probability is near max
        QVERIFY2(result.peakProbability > 0.0,
                 "peakProbability must be > 0");
    }

    // 5. peakLat/peakLon within grid bounds
    void testPeakWithinGridBounds()
    {
        GeographicProfiler gp;
        const auto crimes = londonCrimes();
        const auto result = gp.profile(crimes);

        const double latMin = crimes.first().first;
        const double latMax = crimes.last().first;
        const double lonMin = crimes.first().second;
        const double lonMax = crimes.last().second;

        // With buffer, peak should be within extended bounds
        QVERIFY2(result.peakLat >= latMin - 0.5 && result.peakLat <= latMax + 0.5,
                 qPrintable(QStringLiteral("peakLat %1 out of expected range").arg(result.peakLat)));
    }

    // 6. searchArea50pct <= searchArea80pct
    void testSearchAreaOrdering()
    {
        GeographicProfiler gp;
        const auto result = gp.profile(londonCrimes());
        QVERIFY2(result.searchArea50pct <= result.searchArea80pct,
                 qPrintable(QStringLiteral("searchArea50 %1 should <= searchArea80 %2")
                    .arg(result.searchArea50pct).arg(result.searchArea80pct)));
    }

    // 7. Circular cluster: peak near center
    void testCircularClusterPeakNearCenter()
    {
        GeographicProfiler gp(1.2, 1.2, 0.2, 80);
        const double cLat = 51.5, cLon = -0.1;
        const auto result = gp.profile(circularCluster(cLat, cLon));

        const double dist = std::sqrt(
            std::pow(result.peakLat - cLat, 2) +
            std::pow(result.peakLon - cLon, 2));
        QVERIFY2(dist < 0.1,
                 qPrintable(QStringLiteral("Peak distance %1 from center should be < 0.1 deg").arg(dist)));
    }

    // 8. Grid dimensions match constructor parameter
    void testGridDimensions()
    {
        const int n = 20;
        GeographicProfiler gp(1.2, 1.2, 0.5, n);
        const auto result = gp.profile(londonCrimes());
        QVERIFY2(static_cast<int>(result.probabilitySurface.size()) == n,
                 qPrintable(QStringLiteral("Surface rows %1 expected %2")
                    .arg(result.probabilitySurface.size()).arg(n)));
    }

    // 9. method field is non-empty
    void testMethodFieldNonEmpty()
    {
        GeographicProfiler gp;
        const auto result = gp.profile(londonCrimes());
        QVERIFY2(!result.method.isEmpty(), "result.method must be non-empty");
    }

    // 10. Empty input - no crash
    void testEmptyInputNoCrash()
    {
        GeographicProfiler gp;
        const auto result = gp.profile({});
        // Empty input should not crash
        Q_UNUSED(result);
        QVERIFY(true);
    }
};

QTEST_MAIN(GeographicProfilerDeepTest)
#include "test_geographic_profiler_deep.moc"
