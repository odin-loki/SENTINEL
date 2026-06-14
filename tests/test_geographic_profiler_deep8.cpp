// test_geographic_profiler_deep8.cpp — Deep audit iteration 25: GeographicProfiler
// multi-site anchors, search area 50/80%, peak probability, empty input.
#include <QtTest/QtTest>
#include <cmath>
#include "inference/GeographicProfiler.h"

class GeographicProfilerDeep8Test : public QObject
{
    Q_OBJECT

private slots:

    void testMultiSitePeakNearCentroid()
    {
        QVector<QPair<double, double>> sites;
        sites.append({ 51.50, -0.12 });
        sites.append({ 51.501, -0.119 });
        sites.append({ 51.499, -0.121 });

        GeographicProfiler gp(1.2, 1.2);
        const auto result = gp.profile(sites);
        QVERIFY(result.peakProbability > 0.0);
        QVERIFY(std::abs(result.peakLat - 51.50) < 0.01);
    }

    void testSearchArea50LessThan80()
    {
        QVector<QPair<double, double>> sites;
        for (int i = 0; i < 8; ++i)
            sites.append({ 51.5 + i * 0.0005, -0.1 });

        GeographicProfiler gp;
        const auto result = gp.profile(sites);
        QVERIFY2(result.searchArea50pct <= result.searchArea80pct,
                 qPrintable(QStringLiteral("50%%=%1 80%%=%2")
                                .arg(result.searchArea50pct).arg(result.searchArea80pct)));
        QVERIFY(result.searchArea50pct > 0.0);
    }

    void testEmptySitesReturnsEmptyProfile()
    {
        GeographicProfiler gp;
        const auto result = gp.profile({});
        QCOMPARE(result.probabilitySurface.size(), size_t(0));
        QCOMPARE(result.peakProbability, 0.0);
    }

    void testSurfaceNormalised()
    {
        QVector<QPair<double, double>> sites = {{51.5, -0.1}, {51.501, -0.099}};
        GeographicProfiler gp;
        const auto result = gp.profile(sites);
        double sum = 0.0;
        for (const auto& row : result.probabilitySurface)
            for (double v : row) sum += v;
        QVERIFY2(std::abs(sum - 1.0) < 0.05,
                 qPrintable(QStringLiteral("sum=%1").arg(sum)));
    }

    void testGridDimensionsPositive()
    {
        GeographicProfiler gp;
        const auto result = gp.profile({{51.5, -0.1}, {51.51, -0.09}});
        QVERIFY(!result.gridLats.empty());
        QVERIFY(!result.gridLons.empty());
        QCOMPARE(result.probabilitySurface.size(), size_t(result.gridLats.size()));
    }

    void testFarSiteDoesNotShiftPeakAwayFromCluster()
    {
        QVector<QPair<double, double>> cluster;
        for (int i = 0; i < 6; ++i)
            cluster.append({ 51.5 + i * 1e-5, -0.1 });

        QVector<QPair<double, double>> spread = cluster;
        spread.append({ 52.0, -0.5 });

        GeographicProfiler gp(1.2, 1.2, 0.5, 45);
        const auto clusterOnly = gp.profile(cluster);
        const auto withFar = gp.profile(spread);
        QVERIFY2(std::abs(withFar.peakLat - clusterOnly.peakLat) < 0.03,
                 "distant outlier should not shift peak latitude");
        QVERIFY2(std::abs(withFar.peakLon - clusterOnly.peakLon) < 0.03,
                 "distant outlier should not shift peak longitude");
    }

    void testMethodFieldSet()
    {
        GeographicProfiler gp;
        const auto result = gp.profile({{51.5, -0.1}});
        QVERIFY(!result.method.isEmpty());
    }
};

QTEST_GUILESS_MAIN(GeographicProfilerDeep8Test)
#include "test_geographic_profiler_deep8.moc"
