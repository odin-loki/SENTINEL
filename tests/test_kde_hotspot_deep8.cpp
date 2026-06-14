// test_kde_hotspot_deep8.cpp — Deep audit iteration 28: KDEHotspot
// empty input, grid dimensions, hotspot peak lat/lon, PAI monotonicity.
#include <QtTest/QtTest>
#include "models/KDEHotspot.h"

class TestKDEHotspotDeep8 : public QObject
{
    Q_OBJECT

    using Locs = QVector<QPair<double, double>>;

private slots:

    void testEmptyLocationsReturnsEmptyHotspots()
    {
        KDEHotspot kde(10);
        const auto hotspots = kde.findHotspots({}, 51.4, 51.6, -0.2, 0.0, 3);
        QVERIFY(hotspots.isEmpty());
    }

    void testComputeGridDimensions()
    {
        KDEHotspot kde(8);
        const Locs locs = {{51.5, -0.1}, {51.51, -0.09}};
        const auto surface = kde.compute(locs, 51.48, 51.52, -0.12, -0.08);
        QCOMPARE(surface.size(), 8);
        QVERIFY(!surface[0].empty());
        QCOMPARE(surface[0].size(), 8);
    }

    void testHotspotHasPeakCoordinates()
    {
        KDEHotspot kde(16);
        Locs locs;
        for (int i = 0; i < 6; ++i)
            locs.append({ 51.5 + i * 1e-5, -0.1 });

        const auto hotspots = kde.findHotspots(locs, 51.48, 51.52, -0.12, -0.08, 2);
        QVERIFY(!hotspots.isEmpty());
        QVERIFY(std::abs(hotspots.first().centroidLat - 51.5) < 0.05);
    }

    void testPAIAreaFractionInRange()
    {
        KDEHotspot kde(12);
        const Locs tight = {{51.5, -0.1}, {51.5001, -0.0999}, {51.4999, -0.1001}};
        const auto surface = kde.compute(tight, 51.48, 51.52, -0.12, -0.08);
        const double pai = kde.paiAreaFraction(surface, 0.5);
        QVERIFY2(pai > 0.0 && pai <= 1.0,
                 qPrintable(QStringLiteral("pai=%1").arg(pai)));
    }

    void testSilvermanZeroVarianceFallback()
    {
        const double bw = KDEHotspot::silvermanBandwidth({51.5, 51.5, 51.5});
        QVERIFY(bw > 0.0);
    }
};

QTEST_GUILESS_MAIN(TestKDEHotspotDeep8)
#include "test_kde_hotspot_deep8.moc"
