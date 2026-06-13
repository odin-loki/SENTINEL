// test_geographic_profiler_deep7.cpp — Deep audit iteration 22: GeographicProfiler
// Verifies: Rossmo buffer vs normal decay, peak near cluster, empty input,
//           search-area thresholds, far-field contribution, grid dimensions.

#include <QtTest/QtTest>
#include <cmath>
#include "inference/GeographicProfiler.h"

namespace {

QVector<QPair<double, double>> tightCluster()
{
    return {
        { 51.500, -0.120 }, { 51.502, -0.118 }, { 51.498, -0.122 },
        { 51.501, -0.119 }, { 51.499, -0.121 }, { 51.5005, -0.1195 }
    };
}

double rossmoContrib(double gridLat, double gridLon,
                     double crimeLat, double crimeLon,
                     double f, double g, double bufferDeg)
{
    const double dLat = gridLat - crimeLat;
    const double dLon = gridLon - crimeLon;
    const double dist = std::sqrt(dLat * dLat + dLon * dLon);

    if (dist <= bufferDeg) {
        const double denom = std::max(2.0 * bufferDeg - dist, 1e-10);
        return std::pow(bufferDeg, g - f) / std::pow(denom, g);
    }
    if (dist < 4.0 * bufferDeg) {
        return 1.0 / std::pow(dist, f);
    }
    return 0.0;
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

class GeographicProfilerDeep7Test : public QObject
{
    Q_OBJECT

private slots:

    void testRossmoBufferZoneVsNormalDecay()
    {
        const double f = 1.2, g = 1.2, bufferDeg = 0.5 / 111.0;
        const double crimeLat = 51.5, crimeLon = -0.1;

        const double bufferDist = bufferDeg * 0.5;
        const double normalDist = bufferDeg * 2.0;

        const double bufferContrib = rossmoContrib(crimeLat + bufferDist, crimeLon,
                                                   crimeLat, crimeLon, f, g, bufferDeg);
        const double normalContrib = rossmoContrib(crimeLat + normalDist, crimeLon,
                                                   crimeLat, crimeLon, f, g, bufferDeg);

        const double expectedBuffer = std::pow(bufferDeg, g - f)
            / std::pow(std::max(2.0 * bufferDeg - bufferDist, 1e-10), g);
        const double expectedNormal = 1.0 / std::pow(normalDist, f);

        QVERIFY2(std::abs(bufferContrib - expectedBuffer) < 1e-6,
                 qPrintable(QStringLiteral("buffer term expected %1, got %2")
                     .arg(expectedBuffer).arg(bufferContrib)));
        QVERIFY2(std::abs(normalContrib - expectedNormal) < 1e-6,
                 qPrintable(QStringLiteral("normal decay expected %1, got %2")
                     .arg(expectedNormal).arg(normalContrib)));
        QVERIFY2(bufferContrib != normalContrib,
                 "buffer-zone and normal-range terms must use different formulas");
    }

    void testPeakNearCrimeCluster()
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

        QVERIFY(result.peakProbability > 0.0);
        QVERIFY2(std::abs(result.peakLat - cLat) < 0.02,
                 qPrintable(QStringLiteral("peak lat %1 vs centroid %2")
                     .arg(result.peakLat).arg(cLat)));
        QVERIFY2(std::abs(result.peakLon - cLon) < 0.02,
                 qPrintable(QStringLiteral("peak lon %1 vs centroid %2")
                     .arg(result.peakLon).arg(cLon)));
    }

    void testEmptyCrimesReturnsEmptyProfile()
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

    void testSearchArea50LessThan80()
    {
        GeographicProfiler gp(1.2, 1.2, 0.5, 40);
        const auto result = gp.profile(tightCluster());

        QVERIFY2(result.searchArea50pct <= result.searchArea80pct,
                 qPrintable(QStringLiteral("50%% %1 should <= 80%% %2")
                     .arg(result.searchArea50pct).arg(result.searchArea80pct)));
        QVERIFY(result.searchArea50pct > 0.0);
        QVERIFY(result.searchArea80pct > 0.0);
    }

    void testFarCrimesNegligibleContribution()
    {
        const double bufferDeg = 0.5 / 111.0;
        const double farDist = bufferDeg * 5.0;
        const double farContrib = rossmoContrib(51.5 + farDist, -0.1, 51.5, -0.1,
                                               1.2, 1.2, bufferDeg);
        QCOMPARE(farContrib, 0.0);

        GeographicProfiler gp(1.2, 1.2, 0.5, 45);
        auto crimes = tightCluster();
        crimes.append({ 51.5 + farDist, -0.1 });

        const auto withFar = gp.profile(crimes);
        const auto clusterOnly = gp.profile(tightCluster());

        QVERIFY2(std::abs(withFar.peakLat - clusterOnly.peakLat) < 0.03,
                 "distant crime beyond 4B should not shift peak away from cluster");
        QVERIFY2(std::abs(withFar.peakLon - clusterOnly.peakLon) < 0.03,
                 "distant crime beyond 4B should not shift peak away from cluster");
    }

    void testGridDimensionsMatchGridN()
    {
        const int gridN = 42;
        GeographicProfiler gp(1.2, 1.2, 0.3, gridN);
        const auto result = gp.profile(tightCluster());

        QCOMPARE(static_cast<int>(result.gridLats.size()), gridN);
        QCOMPARE(static_cast<int>(result.gridLons.size()), gridN);
        QCOMPARE(static_cast<int>(result.probabilitySurface.size()), gridN);
        for (const auto& row : result.probabilitySurface)
            QCOMPARE(static_cast<int>(row.size()), gridN);
    }

    void testRossmoFarFieldZeroBeyond4B()
    {
        const double bufferDeg = 0.5 / 111.0;
        const double at4B = rossmoContrib(51.5 + bufferDeg * 3.9, -0.1,
                                          51.5, -0.1, 1.2, 1.2, bufferDeg);
        const double beyond4B = rossmoContrib(51.5 + bufferDeg * 4.1, -0.1,
                                              51.5, -0.1, 1.2, 1.2, bufferDeg);
        QVERIFY(at4B > 0.0);
        QCOMPARE(beyond4B, 0.0);
    }

    void testProbabilitySurfaceNormalised()
    {
        GeographicProfiler gp(1.2, 1.2, 0.4, 50);
        const auto result = gp.profile(tightCluster());

        const double total = surfaceSum(result);
        QVERIFY2(std::abs(total - 1.0) < 1e-9,
                 qPrintable(QStringLiteral("surface sum %1 should be 1.0")
                     .arg(total, 0, 'g', 12)));
    }
};

QTEST_GUILESS_MAIN(GeographicProfilerDeep7Test)
#include "test_geographic_profiler_deep7.moc"
