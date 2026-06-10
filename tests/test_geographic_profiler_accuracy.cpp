// test_geographic_profiler_accuracy.cpp
// Validates that the Rossmo CGT geographic profiler produces sensible
// output: peak in correct area, search-area ratios, edge cases.
#include <QTest>

#include "inference/GeographicProfiler.h"
#include "core/CrimeEvent.h"

class GeographicProfilerAccuracyTest : public QObject
{
    Q_OBJECT

private slots:

    // ── 1. Peak probability must be within bounding box of crime locations ────
    void testPeakWithinBoundingBox()
    {
        // 6 crimes clustered around (51.50, -0.10)
        QVector<QPair<double,double>> locs = {
            {51.50, -0.10}, {51.51, -0.09}, {51.49, -0.11},
            {51.50, -0.11}, {51.51, -0.10}, {51.49, -0.09}
        };

        GeographicProfiler gp;
        const auto result = gp.profile(locs);

        QVERIFY2(result.peakProbability > 0.0, "Peak probability must be positive");

        // Peak should be within the crime cluster region (+/- 1 degree tolerance)
        QVERIFY2(result.peakLat >= 51.3 && result.peakLat <= 51.7,
                 qPrintable(QStringLiteral("Peak lat %1 outside expected range").arg(result.peakLat)));
        QVERIFY2(result.peakLon >= -0.3 && result.peakLon <= 0.1,
                 qPrintable(QStringLiteral("Peak lon %1 outside expected range").arg(result.peakLon)));
    }

    // ── 2. 50% search area < 80% search area ─────────────────────────────────
    void testSearchAreaMonotonicity()
    {
        QVector<QPair<double,double>> locs = {
            {51.50, -0.10}, {51.51, -0.09}, {51.49, -0.11},
            {51.50, -0.11}, {51.51, -0.10}
        };

        GeographicProfiler gp;
        const auto result = gp.profile(locs);

        QVERIFY2(result.searchArea50pct >= 0.0, "50% search area must be non-negative");
        QVERIFY2(result.searchArea80pct >= 0.0, "80% search area must be non-negative");
        QVERIFY2(result.searchArea50pct <= result.searchArea80pct,
                 qPrintable(QStringLiteral("50%% area (%1 km²) must be <= 80%% area (%2 km²)")
                    .arg(result.searchArea50pct).arg(result.searchArea80pct)));
    }

    // ── 3. Single crime location → valid (non-crashing) result ───────────────
    void testSingleLocation()
    {
        QVector<QPair<double,double>> locs = { {51.50, -0.10} };
        GeographicProfiler gp;
        const auto result = gp.profile(locs);
        // Should not crash; result might have low probability
        QVERIFY2(result.peakProbability >= 0.0, "Single-location peak probability must be >= 0");
    }

    // ── 4. Empty locations → returns default (zero probability) ──────────────
    void testEmptyLocations()
    {
        QVector<QPair<double,double>> locs;
        GeographicProfiler gp;
        const auto result = gp.profile(locs);
        QVERIFY2(result.peakProbability <= 0.0,
                 "Empty locations must give zero peak probability");
    }

    // ── 5. Method label is "rossmo_cgt" ─────────────────────────────────────
    void testMethodLabel()
    {
        QVector<QPair<double,double>> locs = {
            {51.50, -0.10}, {51.51, -0.09}, {51.52, -0.08}
        };
        GeographicProfiler gp;
        const auto result = gp.profile(locs);
        QCOMPARE(result.method, QStringLiteral("rossmo_cgt"));
    }

    // ── 6. Two distinct clusters → peak near the denser one ──────────────────
    void testDenseClusterDominates()
    {
        // 5 crimes at cluster A (51.50, -0.10), 2 at cluster B (53.00, 0.50)
        QVector<QPair<double,double>> locs = {
            {51.50, -0.10}, {51.505, -0.095}, {51.495, -0.105},
            {51.502, -0.098}, {51.498, -0.102},
            {53.00,  0.50},  {53.01,  0.51}
        };

        GeographicProfiler gp;
        const auto result = gp.profile(locs);

        // Peak should be nearer to cluster A than cluster B
        const double dA = std::hypot(result.peakLat - 51.50, result.peakLon - (-0.10));
        const double dB = std::hypot(result.peakLat - 53.00, result.peakLon - 0.50);

        QVERIFY2(dA < dB,
                 qPrintable(QStringLiteral("Peak (%1,%2) should be closer to dense cluster A "
                    "(dist=%3) than sparse cluster B (dist=%4)")
                    .arg(result.peakLat).arg(result.peakLon).arg(dA).arg(dB)));
    }

    // ── 7. Probability surface sums to 1.0 (normalised) ──────────────────────
    void testProbabilitySurfaceNormalised()
    {
        QVector<QPair<double,double>> locs = {
            {51.50, -0.10}, {51.51, -0.09}, {51.49, -0.11},
            {51.52, -0.08}, {51.48, -0.12}
        };

        GeographicProfiler gp;
        const auto result = gp.profile(locs);

        double total = 0.0;
        for (const auto& row : result.probabilitySurface)
            for (double v : row) total += v;

        QVERIFY2(std::abs(total - 1.0) < 0.01,
                 qPrintable(QStringLiteral("Probability surface sum should be ~1.0, got %1").arg(total)));
    }

    // ── 8. Grid coordinates have correct count ────────────────────────────────
    void testGridDimensions()
    {
        QVector<QPair<double,double>> locs = {
            {51.50, -0.10}, {51.51, -0.09}, {51.49, -0.11},
            {51.52, -0.08}
        };

        constexpr int GRID_N = 30;
        GeographicProfiler gp(1.2, 1.2, 0.5, GRID_N);
        const auto result = gp.profile(locs);

        QCOMPARE(static_cast<int>(result.gridLats.size()), GRID_N);
        QCOMPARE(static_cast<int>(result.gridLons.size()), GRID_N);
        QCOMPARE(static_cast<int>(result.probabilitySurface.size()), GRID_N);
        if (!result.probabilitySurface.empty())
            QCOMPARE(static_cast<int>(result.probabilitySurface[0].size()), GRID_N);
    }
};

QTEST_MAIN(GeographicProfilerAccuracyTest)
#include "test_geographic_profiler_accuracy.moc"
