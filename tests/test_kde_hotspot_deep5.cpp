// test_kde_hotspot_deep5.cpp — iteration-18 deep audit: ranks, topK clamp, PAI edge, crime counts
#include <QTest>
#include <cmath>
#include <numeric>
#include "models/KDEHotspot.h"

class TestKDEHotspotDeep5 : public QObject
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

    void testFindHotspotsRanksAreSequential()
    {
        KDEHotspot kde(30);
        Locs locs;
        for (int i = 0; i < 6; ++i)
            locs.append({51.50 + i * 0.003, -0.10});
        for (int i = 0; i < 6; ++i)
            locs.append({51.58 + i * 0.0001, -0.18});

        const auto regions = kde.findHotspots(locs, 51.49, 51.60, -0.20, -0.09, 3, 0.04);
        QVERIFY(regions.size() >= 2);
        for (int i = 0; i < regions.size(); ++i)
            QCOMPARE(regions[i].rank, i + 1);
    }

    void testTopKClampedToLocationCount()
    {
        KDEHotspot kde(20);
        Locs locs = {{51.5, -0.1}, {51.51, -0.09}, {51.49, -0.11}};

        const auto regions = kde.findHotspots(locs, 51.48, 51.52, -0.12, -0.08, 50, 0.005);
        QVERIFY(regions.size() <= 3);
        QVERIFY(!regions.isEmpty());
    }

    void testSilvermanSingleElementReturnsDefault()
    {
        const double h = KDEHotspot::silvermanBandwidth({51.5});
        QCOMPARE(h, 0.01);
    }

    void testSilvermanTwoElementsUsesFormula()
    {
        const QVector<double> vals = {51.0, 52.0};
        const double n = 2.0;
        const double mean = 51.5;
        const double var = ((51.0 - mean) * (51.0 - mean) + (52.0 - mean) * (52.0 - mean)) / (n - 1.0);
        const double expected = std::max(1.06 * std::sqrt(var) * std::pow(n, -0.2), 1e-6);
        const double h = KDEHotspot::silvermanBandwidth(vals);
        QVERIFY(std::abs(h - expected) < 1e-9);
    }

    void testFindHotspotsPeakDensityDecreasesWithRank()
    {
        KDEHotspot kde(35);
        Locs locs;
        for (int i = 0; i < 10; ++i)
            locs.append({51.50, -0.10});
        for (int i = 0; i < 5; ++i)
            locs.append({51.58, -0.18});

        const auto regions = kde.findHotspots(locs, 51.49, 51.60, -0.20, -0.09, 2, 0.05);
        if (regions.size() >= 2) {
            QVERIFY2(regions[0].peakDensity >= regions[1].peakDensity,
                     qPrintable(QStringLiteral("rank-1 peak=%1 should >= rank-2 peak=%2")
                                    .arg(regions[0].peakDensity)
                                    .arg(regions[1].peakDensity)));
        }
    }

    void testPaiAreaFractionUniformSurface()
    {
        KDEHotspot kde(6);
        const int N = 6;
        std::vector<std::vector<double>> surface(N, std::vector<double>(N, 1.0 / (N * N)));

        const double frac50 = kde.paiAreaFraction(surface, 0.5);
        const double frac100 = kde.paiAreaFraction(surface, 1.0);
        QVERIFY(frac50 > 0.0);
        QVERIFY(frac50 <= 1.0);
        QCOMPARE(frac100, 1.0);
    }

    void testHotspotCrimeCountWithinBoundingBox()
    {
        KDEHotspot kde(25);
        const double clusterLat = 51.50;
        const double clusterLon = -0.10;
        Locs locs;
        for (int i = 0; i < 8; ++i)
            locs.append({clusterLat + i * 0.00005, clusterLon});

        const auto regions = kde.findHotspots(locs, 51.49, 51.52, -0.12, -0.08, 1, 0.02);
        QVERIFY(!regions.isEmpty());
        QVERIFY2(regions[0].crimeCount >= 1,
                 qPrintable(QStringLiteral("crimeCount=%1 expected >= 1")
                                .arg(regions[0].crimeCount)));
        QVERIFY(regions[0].totalMass > 0.0);
    }

    void testComputeDegenerateZeroSpanBounds()
    {
        // latMin == latMax: dLat=0; bandwidth clamp must still yield normalised surface.
        KDEHotspot kde(8);
        const double lat = 51.5;
        const double lon = -0.1;
        Locs locs = {{lat, lon}, {lat, lon + 0.001}};

        const auto surface = kde.compute(locs, lat, lat, lon - 0.01, lon + 0.01);
        QVERIFY2(surfaceSum(surface) > 0.0,
                 "zero lat span should still produce non-empty normalised surface");
        QVERIFY(std::abs(surfaceSum(surface) - 1.0) < 1e-9);
    }
};

QTEST_GUILESS_MAIN(TestKDEHotspotDeep5)
#include "test_kde_hotspot_deep5.moc"
