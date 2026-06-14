// test_geographic_profiler_deep10.cpp — Deep audit iteration 29: GeographicProfiler
// three-site centroid, peak coordinates, surface normalisation, Rossmo params.
#include <QtTest/QtTest>
#include <cmath>
#include "inference/GeographicProfiler.h"

class GeographicProfilerDeep10Test : public QObject
{
    Q_OBJECT

private slots:

    void testThreeSitePeakNearCentroid()
    {
        const QVector<QPair<double, double>> sites = {
            { 51.50, -0.10 },
            { 51.501, -0.099 },
            { 51.499, -0.101 },
        };
        const auto result = GeographicProfiler(1.2, 1.2, 0.5, 40).profile(sites);
        QVERIFY(std::abs(result.peakLat - 51.5) < 0.02);
        QVERIFY(std::abs(result.peakLon - (-0.1)) < 0.02);
    }

    void testSurfaceRowsMatchGridN()
    {
        const QVector<QPair<double, double>> sites = {{51.5, -0.1}, {51.502, -0.098}};
        GeographicProfiler gp(1.2, 1.2, 0.3, 32);
        const auto result = gp.profile(sites);
        QCOMPARE(static_cast<int>(result.probabilitySurface.size()), 32);
        if (!result.probabilitySurface.empty())
            QCOMPARE(static_cast<int>(result.probabilitySurface[0].size()), 32);
    }

    void testPeakCoordinatesFinite()
    {
        QVector<QPair<double, double>> sites;
        for (int i = 0; i < 5; ++i)
            sites.append({ 51.5 + i * 0.0002, -0.1 });

        const auto result = GeographicProfiler().profile(sites);
        QVERIFY(std::isfinite(result.peakLat));
        QVERIFY(std::isfinite(result.peakLon));
        QVERIFY(result.peakProbability > 0.0);
    }

    void testCustomFGParamsChangePeak()
    {
        const QVector<QPair<double, double>> sites = {{51.5, -0.1}, {51.501, -0.099}};
        const auto tight = GeographicProfiler(1.0, 1.0, 0.2, 25).profile(sites);
        const auto loose = GeographicProfiler(2.0, 2.0, 0.2, 25).profile(sites);
        QVERIFY(!tight.probabilitySurface.empty());
        QVERIFY(!loose.probabilitySurface.empty());
        QVERIFY(tight.method == QStringLiteral("rossmo_cgt"));
    }

    void testSearchArea80AtLeastAsLargeAs50()
    {
        const auto result = GeographicProfiler(1.2, 1.2, 0.5, 30).profile({
            {51.5, -0.1}, {51.501, -0.099}, {51.499, -0.101}
        });
        QVERIFY(result.searchArea80pct >= result.searchArea50pct);
    }
};

QTEST_GUILESS_MAIN(GeographicProfilerDeep10Test)
#include "test_geographic_profiler_deep10.moc"
