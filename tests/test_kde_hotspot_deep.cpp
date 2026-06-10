// test_kde_hotspot_deep.cpp — Comprehensive KDEHotspot tests
#include <QTest>
#include <QCoreApplication>
#include "models/KDEHotspot.h"
#include <cmath>

class TestKDEHotspotDeep : public QObject
{
    Q_OBJECT

private:
    using Locs = QVector<QPair<double,double>>;

    static Locs makeCluster(double lat, double lon, int n, double spread = 0.002) {
        Locs locs;
        for (int i = 0; i < n; ++i)
            locs.append({lat + (i % 5) * spread, lon + (i / 5) * spread});
        return locs;
    }

private slots:

    // ── Bandwidth ─────────────────────────────────────────────────────────────

    void testSilvermanBandwidthIncreasesWithSpread()
    {
        QVector<double> narrow, wide;
        for (int i = 0; i < 100; ++i) narrow.append(i * 0.001);
        for (int i = 0; i < 100; ++i) wide.append(i * 0.1);
        double bwNarrow = KDEHotspot::silvermanBandwidth(narrow);
        double bwWide   = KDEHotspot::silvermanBandwidth(wide);
        QVERIFY(bwNarrow < bwWide);
    }

    void testSilvermanBandwidthPositive()
    {
        QVector<double> vals;
        for (int i = 0; i < 20; ++i) vals.append(51.0 + i * 0.01);
        double bw = KDEHotspot::silvermanBandwidth(vals);
        QVERIFY(bw > 0.0);
    }

    void testSilvermanBandwidthEmptyInputSafe()
    {
        double bw = KDEHotspot::silvermanBandwidth({});
        QVERIFY(bw >= 0.0);
    }

    void testSilvermanBandwidthSingleValue()
    {
        double bw = KDEHotspot::silvermanBandwidth({51.5});
        QVERIFY(bw >= 0.0);
    }

    void testSilvermanMultiplierScales()
    {
        QVector<double> vals;
        for (int i = 0; i < 20; ++i) vals.append(i * 0.01);
        double bw1 = KDEHotspot::silvermanBandwidth(vals, 1.0);
        double bw2 = KDEHotspot::silvermanBandwidth(vals, 2.0);
        QVERIFY(std::abs(bw2 / bw1 - 2.0) < 0.01);
    }

    // ── KDE Surface ───────────────────────────────────────────────────────────

    void testComputeSurfaceDimensions()
    {
        KDEHotspot kde(20);
        Locs locs = makeCluster(51.5, -0.1, 30);
        auto surf = kde.compute(locs, 51.45, 51.55, -0.15, -0.05);
        QCOMPARE(static_cast<int>(surf.size()), 20);
        for (const auto& row : surf)
            QCOMPARE(static_cast<int>(row.size()), 20);
    }

    void testComputeSurfaceNonNegative()
    {
        KDEHotspot kde(20);
        Locs locs = makeCluster(51.5, -0.1, 30);
        auto surf = kde.compute(locs, 51.45, 51.55, -0.15, -0.05);
        for (const auto& row : surf)
            for (double val : row)
                QVERIFY(val >= 0.0);
    }

    void testComputeSurfacePositiveAtClusterCenter()
    {
        KDEHotspot kde(50);
        Locs locs = makeCluster(51.5, -0.1, 50);
        auto surf = kde.compute(locs, 51.48, 51.52, -0.12, -0.08);
        // Maximum value should be positive
        double maxVal = 0.0;
        for (const auto& row : surf)
            for (double v : row)
                maxVal = std::max(maxVal, v);
        QVERIFY(maxVal > 0.0);
    }

    void testComputeSurfacePeakNearCluster()
    {
        KDEHotspot kde(50);
        // Cluster at (51.5, -0.1)
        Locs locs = makeCluster(51.5, -0.1, 50);
        auto surf = kde.compute(locs, 51.48, 51.52, -0.12, -0.08);

        int N = 50;
        double latMin = 51.48, latMax = 51.52;
        double lonMin = -0.12, lonMax = -0.08;

        // Find the peak cell
        int peakRow = -1, peakCol = -1;
        double maxVal = 0.0;
        for (int r = 0; r < N; ++r)
            for (int c = 0; c < N; ++c)
                if (surf[r][c] > maxVal) {
                    maxVal = surf[r][c];
                    peakRow = r; peakCol = c;
                }

        // Peak should be near center of grid (where cluster is)
        double peakLat = latMin + peakRow * (latMax - latMin) / (N - 1);
        double peakLon = lonMin + peakCol * (lonMax - lonMin) / (N - 1);
        QVERIFY(std::abs(peakLat - 51.5) < 0.02);
        QVERIFY(std::abs(peakLon - (-0.1)) < 0.02);
    }

    void testComputeEmptyLocationsNocrash()
    {
        KDEHotspot kde(20);
        auto surf = kde.compute({}, 51.48, 51.52, -0.12, -0.08);
        QCOMPARE(static_cast<int>(surf.size()), 20);
    }

    // ── Hotspot detection ─────────────────────────────────────────────────────

    void testFindHotspotsCountLimitedByTopK()
    {
        KDEHotspot kde(30);
        // Single tight cluster
        Locs locs = makeCluster(51.5, -0.1, 50);
        auto hotspots = kde.findHotspots(locs, 51.4, 51.6, -0.2, 0.0, 3);
        QVERIFY(hotspots.size() <= 3);
    }

    void testFindHotspotsNonEmpty()
    {
        KDEHotspot kde(30);
        Locs locs = makeCluster(51.5, -0.1, 50);
        auto hotspots = kde.findHotspots(locs, 51.4, 51.6, -0.2, 0.0, 5);
        QVERIFY(!hotspots.isEmpty());
    }

    void testFindHotspotsRankedByDensity()
    {
        KDEHotspot kde(30);
        // Two clusters: one with 50 events, one with 10 events
        Locs locs = makeCluster(51.5, -0.1, 50);  // Primary cluster
        for (const auto& p : makeCluster(51.51, -0.09, 10))
            locs.append(p);

        auto hotspots = kde.findHotspots(locs, 51.4, 51.6, -0.2, 0.0, 5);
        QVERIFY(!hotspots.isEmpty());
        // First hotspot should have highest density
        for (int i = 1; i < hotspots.size(); ++i) {
            QVERIFY(hotspots[0].peakDensity >= hotspots[i].peakDensity);
        }
    }

    void testFindHotspotsHaveValidCoordinates()
    {
        KDEHotspot kde(30);
        Locs locs = makeCluster(51.5, -0.1, 50);
        auto hotspots = kde.findHotspots(locs, 51.4, 51.6, -0.2, 0.0, 5);
        for (const auto& h : hotspots) {
            QVERIFY(h.centroidLat >= 51.4 && h.centroidLat <= 51.6);
            QVERIFY(h.centroidLon >= -0.2 && h.centroidLon <= 0.0);
        }
    }

    void testFindHotspotsEmptyLocationsNocrash()
    {
        KDEHotspot kde(30);
        auto hotspots = kde.findHotspots({}, 51.4, 51.6, -0.2, 0.0, 5);
        QVERIFY(hotspots.isEmpty());
    }

    void testFindHotspotsRankAssigned()
    {
        KDEHotspot kde(30);
        Locs locs = makeCluster(51.5, -0.1, 50);
        auto hotspots = kde.findHotspots(locs, 51.4, 51.6, -0.2, 0.0, 3);
        for (int i = 0; i < hotspots.size(); ++i)
            QCOMPARE(hotspots[i].rank, i + 1);
    }

    void testFindHotspotsBoundingBoxValid()
    {
        KDEHotspot kde(30);
        Locs locs = makeCluster(51.5, -0.1, 50);
        auto hotspots = kde.findHotspots(locs, 51.4, 51.6, -0.2, 0.0, 5);
        for (const auto& h : hotspots) {
            QVERIFY(h.latMin <= h.centroidLat);
            QVERIFY(h.latMax >= h.centroidLat);
            QVERIFY(h.lonMin <= h.centroidLon);
            QVERIFY(h.lonMax >= h.centroidLon);
        }
    }

    void testFindHotspotsNonOverlapping()
    {
        KDEHotspot kde(50);
        // Two well-separated clusters
        Locs locs = makeCluster(51.5, -0.1, 40);
        for (const auto& p : makeCluster(51.7, 0.1, 40))
            locs.append(p);

        double suppressionRadius = 0.1;
        auto hotspots = kde.findHotspots(locs, 51.4, 51.8, -0.2, 0.2, 2, suppressionRadius);

        if (hotspots.size() == 2) {
            // The two centroids should be further apart than suppression radius
            double dLat = hotspots[0].centroidLat - hotspots[1].centroidLat;
            double dLon = hotspots[0].centroidLon - hotspots[1].centroidLon;
            double dist = std::sqrt(dLat*dLat + dLon*dLon);
            QVERIFY(dist >= suppressionRadius * 0.5);  // Some tolerance
        }
        QVERIFY(hotspots.size() >= 1);
    }

    // ── PAI area fraction ─────────────────────────────────────────────────────

    void testPAIAreaFractionRange()
    {
        KDEHotspot kde(30);
        Locs locs = makeCluster(51.5, -0.1, 30);
        auto surf = kde.compute(locs, 51.4, 51.6, -0.2, 0.0);
        double area = kde.paiAreaFraction(surf, 0.5);
        QVERIFY(area >= 0.0 && area <= 1.0);
    }

    void testPAIAreaFractionDecreasesWithLowerThreshold()
    {
        KDEHotspot kde(30);
        Locs locs = makeCluster(51.5, -0.1, 30);
        auto surf = kde.compute(locs, 51.4, 51.6, -0.2, 0.0);
        double area50 = kde.paiAreaFraction(surf, 0.5);
        double area80 = kde.paiAreaFraction(surf, 0.8);
        // To capture 80% of density requires more area than for 50%
        QVERIFY(area50 <= area80);
    }

    void testPAIAreaFractionForUniformSurface()
    {
        KDEHotspot kde(20);
        // Uniform surface: each cell has same value
        std::vector<std::vector<double>> surf(20, std::vector<double>(20, 1.0));
        double area = kde.paiAreaFraction(surf, 0.5);
        // For uniform surface, 50% of density = 50% of area
        QVERIFY(std::abs(area - 0.5) < 0.1);
    }

    // ── Bandwidth multiplier effect ───────────────────────────────────────────

    void testLargerBandwidthSmootherSurface()
    {
        Locs locs = makeCluster(51.5, -0.1, 50);

        KDEHotspot kdeSharp(30, 0.5);
        KDEHotspot kdeSmooth(30, 3.0);

        auto surfSharp  = kdeSharp.compute(locs,  51.4, 51.6, -0.2, 0.0);
        auto surfSmooth = kdeSmooth.compute(locs, 51.4, 51.6, -0.2, 0.0);

        // Smooth surface should have lower peak density (mass spread out more)
        double maxSharp = 0.0, maxSmooth = 0.0;
        for (int r = 0; r < 30; ++r) {
            for (int c = 0; c < 30; ++c) {
                maxSharp  = std::max(maxSharp,  surfSharp[r][c]);
                maxSmooth = std::max(maxSmooth, surfSmooth[r][c]);
            }
        }
        QVERIFY(maxSharp >= maxSmooth);
    }
};

QTEST_MAIN(TestKDEHotspotDeep)
#include "test_kde_hotspot_deep.moc"
