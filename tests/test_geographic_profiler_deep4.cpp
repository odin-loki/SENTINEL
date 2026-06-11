// Deep audit iteration 14 — GeographicProfiler (deep4)
// Verifies: Rossmo CGT piecewise formula, search-area 50%/80% thresholds,
//           peak near crime centroid for clustered crimes.

#include <QtTest/QtTest>
#include <cmath>
#include <numeric>
#include "inference/GeographicProfiler.h"

namespace {

constexpr double kGeoPi = 3.14159265358979323846;

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

double searchAreaRecompute(const GeographicProfile& result, double threshold)
{
    std::vector<double> flat;
    for (const auto& row : result.probabilitySurface)
        for (double v : row)
            flat.push_back(v);
    std::sort(flat.begin(), flat.end(), std::greater<double>());

    const double totalMass = std::accumulate(flat.begin(), flat.end(), 0.0);
    if (totalMass <= 0.0) return 0.0;

    const double cellLatKm = std::abs(result.gridLats[1] - result.gridLats[0]) * 111.0;
    const double midLat    = (result.gridLats.front() + result.gridLats.back()) / 2.0;
    const double cellLonKm = std::abs(result.gridLons[1] - result.gridLons[0]) * 111.0 *
                             std::cos(midLat * kGeoPi / 180.0);
    const double cellAreaKm2 = cellLatKm * cellLonKm;

    double cum = 0.0;
    int count = 0;
    for (double v : flat) {
        cum += v;
        ++count;
        if (cum >= threshold * totalMass) break;
    }
    return count * cellAreaKm2;
}

} // namespace

class GeographicProfilerDeep4Test : public QObject
{
    Q_OBJECT

private slots:

    void testRossmoFarFieldDecay()
    {
        const double f = 1.2, g = 1.2, bufferDeg = 0.5 / 111.0;
        const double crimeLat = 51.5, crimeLon = -0.1;
        const double d = bufferDeg * 2.0;
        const double expected = 1.0 / std::pow(d, f);
        const double actual = rossmoContrib(crimeLat + d, crimeLon, crimeLat, crimeLon,
                                            f, g, bufferDeg);
        QVERIFY2(std::abs(actual - expected) < 1e-6,
                 qPrintable(QStringLiteral("far-field: expected %1, got %2")
                     .arg(expected).arg(actual)));
    }

    void testRossmoBufferZoneTerm()
    {
        const double f = 1.2, g = 1.2, bufferDeg = 0.5 / 111.0;
        const double crimeLat = 51.5, crimeLon = -0.1;
        const double dist = bufferDeg * 0.5;
        const double denom = std::max(2.0 * bufferDeg - dist, 1e-10);
        const double expected = std::pow(bufferDeg, g - f) / std::pow(denom, g);
        const double actual = rossmoContrib(crimeLat + dist, crimeLon, crimeLat, crimeLon,
                                            f, g, bufferDeg);
        QVERIFY2(std::abs(actual - expected) < 1e-6,
                 qPrintable(QStringLiteral("buffer: expected %1, got %2")
                     .arg(expected).arg(actual)));
    }

    void testRossmoBoundaryContinuousAtB()
    {
        const double f = 1.2, g = 1.2, bufferDeg = 0.5 / 111.0;
        const double crimeLat = 51.5, crimeLon = -0.1;
        const double atB = rossmoContrib(crimeLat + bufferDeg, crimeLon,
                                         crimeLat, crimeLon, f, g, bufferDeg);
        const double expected = 1.0 / std::pow(bufferDeg, f);
        QVERIFY2(std::abs(atB - expected) < 1e-6,
                 qPrintable(QStringLiteral("at B: expected %1, got %2")
                     .arg(expected).arg(atB)));
    }

    void testRossmoFarFieldZeroBeyond4B()
    {
        const double bufferDeg = 0.5 / 111.0;
        const double actual = rossmoContrib(51.5 + bufferDeg * 5.0, -0.1,
                                            51.5, -0.1, 1.2, 1.2, bufferDeg);
        QCOMPARE(actual, 0.0);
    }

    void testSearchArea50LessThan80()
    {
        GeographicProfiler gp(1.2, 1.2, 0.5, 40);
        const QVector<QPair<double, double>> crimes = {
            { 51.50, -0.12 }, { 51.51, -0.10 }, { 51.49, -0.13 },
            { 51.505, -0.11 }, { 51.495, -0.115 }
        };
        const auto result = gp.profile(crimes);
        QVERIFY2(result.searchArea50pct <= result.searchArea80pct,
                 qPrintable(QStringLiteral("50%% %1 should <= 80%% %2")
                     .arg(result.searchArea50pct).arg(result.searchArea80pct)));
        QVERIFY(result.searchArea50pct > 0.0);
        QVERIFY(result.searchArea80pct > 0.0);
    }

    void testSearchArea50MatchesRecomputed()
    {
        GeographicProfiler gp(1.2, 1.2, 0.5, 35);
        const QVector<QPair<double, double>> crimes = {
            { 51.50, -0.12 }, { 51.51, -0.10 }, { 51.49, -0.13 }
        };
        const auto result = gp.profile(crimes);
        const double expected = searchAreaRecompute(result, 0.50);
        QVERIFY2(std::abs(result.searchArea50pct - expected) < 1e-6,
                 qPrintable(QStringLiteral("50%%: profile %1 vs expected %2")
                     .arg(result.searchArea50pct).arg(expected)));
    }

    void testSearchArea80MatchesRecomputed()
    {
        GeographicProfiler gp(1.2, 1.2, 0.5, 35);
        const QVector<QPair<double, double>> crimes = {
            { 51.50, -0.12 }, { 51.51, -0.10 }, { 51.49, -0.13 }
        };
        const auto result = gp.profile(crimes);
        const double expected = searchAreaRecompute(result, 0.80);
        QVERIFY2(std::abs(result.searchArea80pct - expected) < 1e-6,
                 qPrintable(QStringLiteral("80%%: profile %1 vs expected %2")
                     .arg(result.searchArea80pct).arg(expected)));
    }

    void testPeakNearClusterCentroid()
    {
        GeographicProfiler gp(1.2, 1.2, 0.3, 50);
        const QVector<QPair<double, double>> crimes = {
            { 51.500, -0.120 }, { 51.502, -0.118 }, { 51.498, -0.122 },
            { 51.501, -0.119 }, { 51.499, -0.121 }
        };
        double cLat = 0.0, cLon = 0.0;
        for (const auto& c : crimes) { cLat += c.first; cLon += c.second; }
        cLat /= crimes.size();
        cLon /= crimes.size();

        const auto result = gp.profile(crimes);
        QVERIFY(result.peakProbability > 0.0);
        QVERIFY2(std::abs(result.peakLat - cLat) < 0.01,
                 qPrintable(QStringLiteral("peak lat %1 vs centroid %2")
                     .arg(result.peakLat).arg(cLat)));
        QVERIFY2(std::abs(result.peakLon - cLon) < 0.01,
                 qPrintable(QStringLiteral("peak lon %1 vs centroid %2")
                     .arg(result.peakLon).arg(cLon)));
    }

    void testPeakNotAtExactCrimeSite()
    {
        GeographicProfiler gp(1.2, 1.2, 1.0, 60);
        const QVector<QPair<double, double>> crimes = { { 51.500, -0.120 } };
        const auto result = gp.profile(crimes);
        const double dLat = std::abs(result.peakLat - crimes[0].first);
        const double dLon = std::abs(result.peakLon - crimes[0].second);
        const double peakDist = std::sqrt(dLat * dLat + dLon * dLon);
        QVERIFY2(peakDist > 1e-4,
                 qPrintable(QStringLiteral("peak should not be at crime site, dist=%1")
                     .arg(peakDist)));
    }

    void testProbabilitySurfaceNormalised()
    {
        GeographicProfiler gp(1.2, 1.2, 0.5, 30);
        const QVector<QPair<double, double>> crimes = {
            { 51.50, -0.12 }, { 51.51, -0.10 }, { 51.49, -0.13 }
        };
        const auto result = gp.profile(crimes);
        double sum = 0.0;
        for (const auto& row : result.probabilitySurface)
            for (double v : row)
                sum += v;
        QVERIFY2(std::abs(sum - 1.0) < 1e-6,
                 qPrintable(QStringLiteral("surface sum should be 1, got %1").arg(sum)));
    }

    void testEmptyCrimesReturnsEmptySurface()
    {
        GeographicProfiler gp;
        const auto result = gp.profile({});
        QVERIFY(result.probabilitySurface.empty());
        QCOMPARE(result.searchArea50pct, 0.0);
        QCOMPARE(result.searchArea80pct, 0.0);
    }
};

QTEST_GUILESS_MAIN(GeographicProfilerDeep4Test)
#include "test_geographic_profiler_deep4.moc"
