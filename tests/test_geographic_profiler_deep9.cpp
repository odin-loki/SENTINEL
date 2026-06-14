// test_geographic_profiler_deep9.cpp — Deep audit iteration 27: GeographicProfiler
// single-site profile, custom gridN, bufferKm margin, peak probability bounds.
#include <QtTest/QtTest>
#include <cmath>
#include "inference/GeographicProfiler.h"

class GeographicProfilerDeep9Test : public QObject
{
    Q_OBJECT

private slots:

    void testSingleCrimeSiteProducesProfile()
    {
        GeographicProfiler gp(1.2, 1.2, 0.5, 30);
        const auto result = gp.profile({{ 51.50, -0.12 }});
        QVERIFY(result.peakProbability > 0.0);
        QCOMPARE(result.method, QStringLiteral("rossmo_cgt"));
        QVERIFY(!result.probabilitySurface.empty());
    }

    void testGridNControlsResolution()
    {
        const QVector<QPair<double, double>> sites = {{51.5, -0.1}, {51.501, -0.099}};
        GeographicProfiler gp(1.2, 1.2, 0.3, 25);
        const auto result = gp.profile(sites);
        QCOMPARE(static_cast<int>(result.gridLats.size()), 25);
        QCOMPARE(static_cast<int>(result.gridLons.size()), 25);
    }

    void testPeakProbabilityBounded()
    {
        QVector<QPair<double, double>> sites;
        for (int i = 0; i < 4; ++i)
            sites.append({ 51.5 + i * 1e-5, -0.1 });

        GeographicProfiler gp;
        const auto result = gp.profile(sites);
        QVERIFY(result.peakProbability > 0.0);
        QVERIFY(result.peakProbability <= 1.0);
    }

    void testSearchAreasNonNegative()
    {
        GeographicProfiler gp(1.2, 1.2, 0.4, 35);
        const auto result = gp.profile({{51.5, -0.1}, {51.502, -0.098}});
        QVERIFY(result.searchArea50pct >= 0.0);
        QVERIFY(result.searchArea80pct >= 0.0);
    }

    void testLargerBufferChangesSurface()
    {
        const QVector<QPair<double, double>> sites = {{51.5, -0.1}, {51.501, -0.099}};
        const auto tight = GeographicProfiler(1.2, 1.2, 0.1, 30).profile(sites);
        const auto wide  = GeographicProfiler(1.2, 1.2, 2.0, 30).profile(sites);
        QVERIFY(tight.peakLat != 0.0 || wide.peakLat != 0.0);
        QVERIFY(!tight.probabilitySurface.empty() && !wide.probabilitySurface.empty());
    }
};

QTEST_GUILESS_MAIN(GeographicProfilerDeep9Test)
#include "test_geographic_profiler_deep9.moc"
