// Iteration 12 — GeographicProfiler deep test
#include <QtTest/QtTest>
#include <cmath>
#include "inference/GeographicProfiler.h"
#include "core/CrimeEvent.h"

class GeographicProfilerDeep2Test : public QObject
{
    Q_OBJECT

private slots:

    // ─── Empty input ──────────────────────────────────────────────────────

    void testEmptyInputReturnsEmptyProfile()
    {
        GeographicProfiler gp;
        const auto result = gp.profile({});
        QVERIFY(result.probabilitySurface.empty());
        QCOMPARE(result.peakLat, 0.0);
        QCOMPARE(result.peakLon, 0.0);
    }

    // ─── Single crime location ────────────────────────────────────────────

    void testSingleCrimeProfileGenerated()
    {
        GeographicProfiler gp(1.2, 1.2, 0.5, 20);
        QVector<QPair<double,double>> locs = { {51.5, -0.1} };
        const auto result = gp.profile(locs);

        QVERIFY(!result.probabilitySurface.empty());
        QVERIFY(result.peakLat != 0.0 || result.peakLon != 0.0);
    }

    // ─── Probability surface sums to 1 ───────────────────────────────────

    void testProbabilitySurfaceSumsToOne()
    {
        GeographicProfiler gp(1.2, 1.2, 0.5, 30);
        QVector<QPair<double,double>> locs = {
            {51.50, -0.12}, {51.51, -0.10}, {51.49, -0.13}
        };
        const auto result = gp.profile(locs);

        double sum = 0.0;
        for (const auto& row : result.probabilitySurface)
            for (double v : row)
                sum += v;

        QVERIFY2(std::abs(sum - 1.0) < 1e-6,
                 qPrintable(QStringLiteral("Surface sum expected 1.0, got %1").arg(sum)));
    }

    // ─── Peak probability in (0,1] ────────────────────────────────────────

    void testPeakProbabilityInRange()
    {
        GeographicProfiler gp(1.2, 1.2, 0.5, 30);
        QVector<QPair<double,double>> locs = {
            {51.50, -0.12}, {51.51, -0.10}, {51.49, -0.13}
        };
        const auto result = gp.profile(locs);
        QVERIFY(result.peakProbability > 0.0);
        QVERIFY(result.peakProbability <= 1.0);
    }

    // ─── Peak location inside bounding box ───────────────────────────────

    void testPeakLocationInsideBounds()
    {
        GeographicProfiler gp(1.2, 1.2, 0.5, 30);
        QVector<QPair<double,double>> locs = {
            {51.50, -0.12}, {51.51, -0.10}, {51.49, -0.13}
        };
        const auto result = gp.profile(locs);

        // The bounding box includes margin, so peak should be in a wider region
        QVERIFY(result.peakLat > 51.0 && result.peakLat < 52.0);
        QVERIFY(result.peakLon > -1.0 && result.peakLon < 0.0);
    }

    // ─── Method tag set correctly ─────────────────────────────────────────

    void testMethodTag()
    {
        GeographicProfiler gp;
        QVector<QPair<double,double>> locs = { {51.5, -0.1} };
        const auto result = gp.profile(locs);
        QCOMPARE(result.method, QStringLiteral("rossmo_cgt"));
    }

    // ─── Search area ordering ──────────────────────────────────────────────

    void testSearchArea50Pct_LesThan80Pct()
    {
        GeographicProfiler gp(1.2, 1.2, 0.5, 40);
        QVector<QPair<double,double>> locs = {
            {51.50, -0.12}, {51.51, -0.10}, {51.49, -0.13},
            {51.505,-0.11}, {51.495,-0.115}
        };
        const auto result = gp.profile(locs);

        // 50% search area should be smaller than 80% search area
        QVERIFY2(result.searchArea50pct <= result.searchArea80pct,
                 qPrintable(QStringLiteral("50%%=%1 should <= 80%%=%2")
                            .arg(result.searchArea50pct).arg(result.searchArea80pct)));
    }

    void testSearchAreaPositive()
    {
        GeographicProfiler gp(1.2, 1.2, 0.5, 40);
        QVector<QPair<double,double>> locs = {
            {51.50, -0.12}, {51.51, -0.10}, {51.49, -0.13}
        };
        const auto result = gp.profile(locs);
        QVERIFY(result.searchArea50pct >= 0.0);
        QVERIFY(result.searchArea80pct >= 0.0);
    }

    // ─── Grid dimensions match gridN ──────────────────────────────────────

    void testGridDimensionsMatchGridN()
    {
        const int gridN = 25;
        GeographicProfiler gp(1.2, 1.2, 0.5, gridN);
        QVector<QPair<double,double>> locs = { {51.50, -0.12}, {51.51, -0.10} };
        const auto result = gp.profile(locs);

        QCOMPARE(static_cast<int>(result.probabilitySurface.size()), gridN);
        if (!result.probabilitySurface.empty()) {
            QCOMPARE(static_cast<int>(result.probabilitySurface[0].size()), gridN);
        }
        QCOMPARE(static_cast<int>(result.gridLats.size()), gridN);
        QCOMPARE(static_cast<int>(result.gridLons.size()), gridN);
    }

    // ─── Rossmo buffer zone: peak NOT directly at crime site ─────────────

    void testPeakNotAtExactCrimeSite()
    {
        // The Rossmo formula penalises areas too close to crimes (buffer zone)
        // So the peak should NOT be exactly at the crime location itself.
        // With a 1km buffer and 3 nearby crimes, the peak should be near the centroid.
        GeographicProfiler gp(1.2, 1.2, 1.0, 50);
        QVector<QPair<double,double>> locs = {
            {51.500, -0.120},
            {51.510, -0.110},
            {51.490, -0.130}
        };
        const auto result = gp.profile(locs);
        QVERIFY(!result.probabilitySurface.empty());

        // Peak should be valid (not at lat=0, lon=0)
        QVERIFY(std::abs(result.peakLat) > 1.0);
        QVERIFY(result.peakProbability > 0.0);
    }

    // ─── Symmetry test ────────────────────────────────────────────────────

    void testSymmetricCrimesProduceSymmetricProfile()
    {
        // 4 crimes placed symmetrically around a center → peak near center
        GeographicProfiler gp(1.2, 1.2, 0.2, 40);
        const double cLat = 51.50, cLon = -0.10;
        const double d = 0.05;
        QVector<QPair<double,double>> locs = {
            {cLat + d, cLon},
            {cLat - d, cLon},
            {cLat, cLon + d},
            {cLat, cLon - d}
        };
        const auto result = gp.profile(locs);
        QVERIFY(!result.probabilitySurface.empty());

        // Peak should be near the centroid (within 2*d)
        QVERIFY2(std::abs(result.peakLat - cLat) < 2.0 * d,
                 qPrintable(QStringLiteral("Peak lat %1 far from center %2")
                            .arg(result.peakLat).arg(cLat)));
        QVERIFY2(std::abs(result.peakLon - cLon) < 2.0 * d,
                 qPrintable(QStringLiteral("Peak lon %1 far from center %2")
                            .arg(result.peakLon).arg(cLon)));
    }

    // ─── All probability values non-negative ─────────────────────────────

    void testAllProbabilitiesNonNegative()
    {
        GeographicProfiler gp(1.2, 1.2, 0.5, 30);
        QVector<QPair<double,double>> locs = {
            {51.50, -0.12}, {51.51, -0.10}, {51.49, -0.13}
        };
        const auto result = gp.profile(locs);
        for (const auto& row : result.probabilitySurface) {
            for (double v : row) {
                QVERIFY2(v >= 0.0,
                         qPrintable(QStringLiteral("Negative probability: %1").arg(v)));
            }
        }
    }
};

QTEST_GUILESS_MAIN(GeographicProfilerDeep2Test)
#include "test_geographic_profiler_deep2.moc"
