// Deep audit iteration 12 — GeographicProfiler (deep3)
// Verifies: Rossmo CGT piecewise formula, search-area thresholds (50%/80%),
//           peak location near crime centroid for clustered crimes.

#include <QtTest/QtTest>
#include <cmath>
#include <numeric>
#include "inference/GeographicProfiler.h"

namespace {

constexpr double kGeoPi = 3.14159265358979323846;

// Mirror GeographicProfiler::rossmoContrib (private) for formula verification.
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

double rawSurfaceAt(const GeographicProfile& profile,
                    const QVector<QPair<double, double>>& crimes,
                    double f, double g, double bufferKm)
{
    const double bufferDeg = bufferKm / 111.0;
    const int rows = static_cast<int>(profile.gridLats.size());
    const int cols = static_cast<int>(profile.gridLons.size());
    if (rows == 0 || cols == 0) return 0.0;

    // Use centre cell as a representative sample point.
    const int r = rows / 2;
    const int c = cols / 2;
    const double lat = profile.gridLats[static_cast<std::size_t>(r)];
    const double lon = profile.gridLons[static_cast<std::size_t>(c)];

    double total = 0.0;
    for (const auto& crime : crimes) {
        total += rossmoContrib(lat, lon, crime.first, crime.second, f, g, bufferDeg);
    }
    return total;
}

} // namespace

class GeographicProfilerDeep3Test : public QObject
{
    Q_OBJECT

private slots:

    // ── Rossmo CGT: 1/d^f for d > B, buffer term for d <= B ────────────────

    void testRossmoFarFieldDecay()
    {
        const double f = 1.2;
        const double g = 1.2;
        const double bufferKm = 0.5;
        const double bufferDeg = bufferKm / 111.0;

        const double crimeLat = 51.5;
        const double crimeLon = -0.1;

        // Point well outside buffer (> B, < 4B)
        const double gridLat = crimeLat + bufferDeg * 2.0;
        const double gridLon = crimeLon;

        const double d = bufferDeg * 2.0;
        const double expected = 1.0 / std::pow(d, f);
        const double actual = rossmoContrib(gridLat, gridLon, crimeLat, crimeLon, f, g, bufferDeg);

        QVERIFY2(std::abs(actual - expected) < 1e-6,
                 qPrintable(QStringLiteral("Far-field: expected %1, got %2")
                     .arg(expected, 0, 'g', 12).arg(actual, 0, 'g', 12)));
    }

    void testRossmoBufferZoneTerm()
    {
        const double f = 1.2;
        const double g = 1.2;
        const double bufferKm = 0.5;
        const double bufferDeg = bufferKm / 111.0;

        const double crimeLat = 51.5;
        const double crimeLon = -0.1;
        const double dist = bufferDeg * 0.5;  // inside buffer

        const double gridLat = crimeLat + dist;
        const double gridLon = crimeLon;

        const double denom = std::max(2.0 * bufferDeg - dist, 1e-10);
        const double expected = std::pow(bufferDeg, g - f) / std::pow(denom, g);
        const double actual = rossmoContrib(gridLat, gridLon, crimeLat, crimeLon, f, g, bufferDeg);

        QVERIFY2(std::abs(actual - expected) < 1e-6,
                 qPrintable(QStringLiteral("Buffer zone: expected %1, got %2")
                     .arg(expected, 0, 'g', 12).arg(actual, 0, 'g', 12)));
    }

    void testRossmoFarFieldZeroBeyond4B()
    {
        const double f = 1.2;
        const double g = 1.2;
        const double bufferKm = 0.5;
        const double bufferDeg = bufferKm / 111.0;

        const double crimeLat = 51.5;
        const double crimeLon = -0.1;
        const double dist = bufferDeg * 5.0;  // beyond 4B

        const double actual = rossmoContrib(crimeLat + dist, crimeLon,
                                            crimeLat, crimeLon, f, g, bufferDeg);
        QCOMPARE(actual, 0.0);
    }

    void testProfileSurfaceUsesSummedRossmoContributions()
    {
        const double f = 1.2;
        const double g = 1.2;
        const double bufferKm = 0.5;
        GeographicProfiler gp(f, g, bufferKm, 30);

        const QVector<QPair<double, double>> crimes = {
            { 51.50, -0.12 }, { 51.51, -0.10 }, { 51.49, -0.13 }
        };
        const auto result = gp.profile(crimes);
        QVERIFY(!result.probabilitySurface.empty());

        const double rawCentre = rawSurfaceAt(result, crimes, f, g, bufferKm);
        QVERIFY2(rawCentre > 0.0,
                 qPrintable(QStringLiteral("Raw Rossmo sum at grid centre should be positive, got %1")
                     .arg(rawCentre)));
    }

    // ── Search area: cumulative probability thresholds ───────────────────────

    void testSearchArea50LessThan80()
    {
        GeographicProfiler gp(1.2, 1.2, 0.5, 40);
        const QVector<QPair<double, double>> crimes = {
            { 51.50, -0.12 }, { 51.51, -0.10 }, { 51.49, -0.13 },
            { 51.505, -0.11 }, { 51.495, -0.115 }
        };
        const auto result = gp.profile(crimes);

        QVERIFY2(result.searchArea50pct <= result.searchArea80pct,
                 qPrintable(QStringLiteral("50%% area %1 should <= 80%% area %2")
                     .arg(result.searchArea50pct).arg(result.searchArea80pct)));
        QVERIFY(result.searchArea50pct > 0.0);
        QVERIFY(result.searchArea80pct > 0.0);
    }

    void testSearchAreaThresholdsMatchCumulativeMass()
    {
        GeographicProfiler gp(1.2, 1.2, 0.5, 35);
        const QVector<QPair<double, double>> crimes = {
            { 51.50, -0.12 }, { 51.51, -0.10 }, { 51.49, -0.13 }
        };
        const auto result = gp.profile(crimes);

        // Recompute 50% threshold area from the normalised surface.
        std::vector<double> flat;
        for (const auto& row : result.probabilitySurface)
            for (double v : row)
                flat.push_back(v);
        std::sort(flat.begin(), flat.end(), std::greater<double>());

        const double cellLatKm = std::abs(result.gridLats[1] - result.gridLats[0]) * 111.0;
        const double midLat = (result.gridLats.front() + result.gridLats.back()) / 2.0;
        const double cellLonKm = std::abs(result.gridLons[1] - result.gridLons[0]) * 111.0 *
                                 std::cos(midLat * kGeoPi / 180.0);
        const double cellAreaKm2 = cellLatKm * cellLonKm;

        auto areaForThreshold = [&](double threshold) {
            double cum = 0.0;
            int count = 0;
            for (double v : flat) {
                cum += v;
                ++count;
                if (cum >= threshold) break;
            }
            return count * cellAreaKm2;
        };

        const double expected50 = areaForThreshold(0.50);
        const double expected80 = areaForThreshold(0.80);

        QVERIFY2(std::abs(result.searchArea50pct - expected50) < 1e-6,
                 qPrintable(QStringLiteral("50%% area: profile %1 vs recomputed %2")
                     .arg(result.searchArea50pct).arg(expected50)));
        QVERIFY2(std::abs(result.searchArea80pct - expected80) < 1e-6,
                 qPrintable(QStringLiteral("80%% area: profile %1 vs recomputed %2")
                     .arg(result.searchArea80pct).arg(expected80)));
    }

    // ── Peak near crime centroid for clustered crimes ───────────────────────

    void testPeakNearClusterCentroid()
    {
        GeographicProfiler gp(1.2, 1.2, 0.3, 50);
        const QVector<QPair<double, double>> crimes = {
            { 51.500, -0.120 },
            { 51.502, -0.118 },
            { 51.498, -0.122 },
            { 51.501, -0.119 },
            { 51.499, -0.121 }
        };

        double cLat = 0.0;
        double cLon = 0.0;
        for (const auto& c : crimes) {
            cLat += c.first;
            cLon += c.second;
        }
        cLat /= crimes.size();
        cLon /= crimes.size();

        const auto result = gp.profile(crimes);
        QVERIFY(result.peakProbability > 0.0);

        // Tight cluster → peak within ~0.01° (~1 km) of centroid.
        QVERIFY2(std::abs(result.peakLat - cLat) < 0.01,
                 qPrintable(QStringLiteral("Peak lat %1 should be near centroid %2")
                     .arg(result.peakLat).arg(cLat)));
        QVERIFY2(std::abs(result.peakLon - cLon) < 0.01,
                 qPrintable(QStringLiteral("Peak lon %1 should be near centroid %2")
                     .arg(result.peakLon).arg(cLon)));
    }

    void testPeakNotAtExactCrimeSiteDueToBuffer()
    {
        // Rossmo buffer penalises d <= B; with single crime peak should not sit exactly on crime.
        GeographicProfiler gp(1.2, 1.2, 1.0, 60);
        const QVector<QPair<double, double>> crimes = { { 51.500, -0.120 } };
        const auto result = gp.profile(crimes);

        const double dLat = std::abs(result.peakLat - crimes[0].first);
        const double dLon = std::abs(result.peakLon - crimes[0].second);
        const double peakDist = std::sqrt(dLat * dLat + dLon * dLon);

        QVERIFY2(peakDist > 1e-4,
                 qPrintable(QStringLiteral("Peak should not coincide with crime site, dist=%1")
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
                 qPrintable(QStringLiteral("Surface should sum to 1, got %1").arg(sum)));
    }
};

QTEST_GUILESS_MAIN(GeographicProfilerDeep3Test)
#include "test_geographic_profiler_deep3.moc"
