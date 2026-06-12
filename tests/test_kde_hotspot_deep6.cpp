// test_kde_hotspot_deep6.cpp — Deep audit iteration 21: KDEHotspot
// Verifies: constructor clamps, kernel origin, empty inputs, suppression, PAI edges, bandwidth.

#include <QtTest/QtTest>
#include <cmath>
#include <numeric>
#include "models/KDEHotspot.h"

class TestKDEHotspotDeep6 : public QObject
{
    Q_OBJECT

    using Locs = QVector<QPair<double, double>>;

    static double surfaceSum(const std::vector<std::vector<double>>& s)
    {
        double total = 0.0;
        for (const auto& row : s)
            for (double v : row) total += v;
        return total;
    }

private slots:

    void testConstructorClampsGridNToMinimumFour()
    {
        KDEHotspot kdeLow(1);
        Locs locs = {{51.5, -0.1}, {51.51, -0.09}};
        const auto surface = kdeLow.compute(locs, 51.48, 51.52, -0.12, -0.08);
        QCOMPARE(static_cast<int>(surface.size()), 4);
        for (const auto& row : surface)
            QCOMPARE(static_cast<int>(row.size()), 4);
    }

    void testBandwidthMultiplierAffectsSurfaceSpread()
    {
        Locs locs = {{51.5, -0.1}, {51.51, -0.09}, {51.49, -0.11}};
        KDEHotspot kdeNarrow(20, 0.2);
        KDEHotspot kdeWide(20, 2.0);

        const auto narrow = kdeNarrow.compute(locs, 51.48, 51.52, -0.12, -0.08);
        const auto wide   = kdeWide.compute(locs, 51.48, 51.52, -0.12, -0.08);

        double narrowMax = 0.0, wideMax = 0.0;
        for (const auto& row : narrow)
            for (double v : row) narrowMax = std::max(narrowMax, v);
        for (const auto& row : wide)
            for (double v : row) wideMax = std::max(wideMax, v);

        QVERIFY2(wideMax < narrowMax,
                 qPrintable(QStringLiteral("wider bandwidth should flatten peak: narrow=%1 wide=%2")
                                .arg(narrowMax).arg(wideMax)));
    }

    void testComputeSingleLocationPeakAtCenter()
    {
        KDEHotspot kde(11);
        const double lat = 51.5;
        const double lon = -0.1;
        const auto surface = kde.compute({{lat, lon}}, 51.48, 51.52, -0.12, -0.08);

        int peakR = 0, peakC = 0;
        double peakVal = 0.0;
        for (int r = 0; r < 11; ++r)
            for (int c = 0; c < 11; ++c)
                if (surface[r][c] > peakVal) {
                    peakVal = surface[r][c];
                    peakR = r;
                    peakC = c;
                }

        const int center = 11 / 2;
        QVERIFY(std::abs(peakR - center) <= 1);
        QVERIFY(std::abs(peakC - center) <= 1);
        QVERIFY(peakVal > 0.0);
    }

    void testComputeEmptyLocationsReturnsZeroSurface()
    {
        KDEHotspot kde(10);
        const auto surface = kde.compute({}, 51.48, 51.52, -0.12, -0.08);
        QCOMPARE(static_cast<int>(surface.size()), 10);
        QCOMPARE(surfaceSum(surface), 0.0);
    }

    void testFindHotspotsEmptyLocationsReturnsEmpty()
    {
        KDEHotspot kde(15);
        const auto regions = kde.findHotspots({}, 51.48, 51.52, -0.12, -0.08, 5, 0.02);
        QVERIFY(regions.isEmpty());
    }

    void testFindHotspotsSuppressionExcludesNearbyPeaks()
    {
        KDEHotspot kde(40);
        Locs locs;
        for (int i = 0; i < 12; ++i)
            locs.append({51.50, -0.10});
        for (int i = 0; i < 6; ++i)
            locs.append({51.58, -0.18});

        const double suppression = 0.06;
        const auto regions = kde.findHotspots(locs, 51.49, 51.60, -0.20, -0.09, 5, suppression);
        QVERIFY(regions.size() >= 2);

        const double dist = std::hypot(regions[0].centroidLat - regions[1].centroidLat,
                                       regions[0].centroidLon - regions[1].centroidLon);
        QVERIFY2(dist >= suppression,
                 qPrintable(QStringLiteral("peak separation=%1 suppression=%2")
                                .arg(dist).arg(suppression)));
    }

    void testPaiAreaFractionEmptyAndZeroSurface()
    {
        KDEHotspot kde(5);
        QCOMPARE(kde.paiAreaFraction({}, 0.5), 1.0);

        const int N = 4;
        std::vector<std::vector<double>> zeros(N, std::vector<double>(N, 0.0));
        QCOMPARE(kde.paiAreaFraction(zeros, 0.5), 1.0);
    }

    void testIdenticalLocationsPeakNearCluster()
    {
        KDEHotspot kde(30);
        const double lat = 51.505;
        const double lon = -0.115;
        Locs locs;
        for (int i = 0; i < 15; ++i)
            locs.append({lat, lon});

        const auto regions = kde.findHotspots(locs, 51.49, 51.52, -0.13, -0.10, 1, 0.03);
        QVERIFY(!regions.isEmpty());
        QVERIFY(std::abs(regions[0].centroidLat - lat) < 0.02);
        QVERIFY(std::abs(regions[0].centroidLon - lon) < 0.02);
        QVERIFY(regions[0].crimeCount >= 15);
    }
};

QTEST_GUILESS_MAIN(TestKDEHotspotDeep6)
#include "test_kde_hotspot_deep6.moc"
