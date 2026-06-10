// test_kde_hotspot_advanced.cpp
// Advanced tests for KDEHotspot: surface normalization, bandwidth effects,
// findHotspots topK, PAI area fraction, Silverman bandwidth, and edge cases.
#include <QTest>
#include "models/KDEHotspot.h"
#include <cmath>
#include <algorithm>
#include <numeric>

class KDEHotspotAdvancedTest : public QObject
{
    Q_OBJECT

private:
    using Pt = QPair<double, double>;

    static QVector<Pt> londonCluster(int n = 20)
    {
        QVector<Pt> pts;
        for (int i = 0; i < n; ++i)
            pts.append({ 51.5 + (i % 5) * 0.002, -0.1 + (i % 5) * 0.002 });
        return pts;
    }

    static const double LAT_MIN;
    static const double LAT_MAX;
    static const double LON_MIN;
    static const double LON_MAX;

private slots:

    // 1. compute: returns correct grid dimensions
    void testComputeGridDimensions()
    {
        KDEHotspot kde(20);
        const auto surface = kde.compute(londonCluster(), LAT_MIN, LAT_MAX, LON_MIN, LON_MAX);
        QVERIFY2(static_cast<int>(surface.size()) == 20,
                 qPrintable(QStringLiteral("Surface rows %1 expected 20").arg(surface.size())));
        for (const auto& row : surface)
            QVERIFY2(static_cast<int>(row.size()) == 20,
                     qPrintable(QStringLiteral("Surface cols %1 expected 20").arg(row.size())));
    }

    // 2. compute: all values non-negative
    void testComputeNonNegative()
    {
        KDEHotspot kde(20);
        const auto surface = kde.compute(londonCluster(), LAT_MIN, LAT_MAX, LON_MIN, LON_MAX);
        for (const auto& row : surface)
            for (double v : row)
                QVERIFY2(v >= 0.0,
                         qPrintable(QStringLiteral("Surface value %1 must be >= 0").arg(v)));
    }

    // 3. compute: sum > 0
    void testComputeSumPositive()
    {
        KDEHotspot kde(20);
        const auto surface = kde.compute(londonCluster(), LAT_MIN, LAT_MAX, LON_MIN, LON_MAX);
        double total = 0.0;
        for (const auto& row : surface) for (double v : row) total += v;
        QVERIFY2(total > 0.0,
                 qPrintable(QStringLiteral("Surface total %1 must be > 0").arg(total)));
    }

    // 4. Larger bandwidth -> smoother (lower max value)
    void testLargerBandwidthSmoother()
    {
        KDEHotspot kdeSharp(20, 0.1);
        KDEHotspot kdeSmooth(20, 5.0);
        const auto sharp  = kdeSharp.compute(londonCluster(), LAT_MIN, LAT_MAX, LON_MIN, LON_MAX);
        const auto smooth = kdeSmooth.compute(londonCluster(), LAT_MIN, LAT_MAX, LON_MIN, LON_MAX);

        double maxSharp = 0.0, maxSmooth = 0.0;
        for (const auto& row : sharp)  for (double v : row) maxSharp  = std::max(maxSharp, v);
        for (const auto& row : smooth) for (double v : row) maxSmooth = std::max(maxSmooth, v);

        QVERIFY2(maxSharp >= maxSmooth,
                 qPrintable(QStringLiteral("Sharp max %1 should >= smooth max %2")
                    .arg(maxSharp).arg(maxSmooth)));
    }

    // 5. findHotspots: returns non-empty for cluster data
    void testFindHotspotsNonEmpty()
    {
        KDEHotspot kde(30);
        const auto hotspots = kde.findHotspots(londonCluster(), LAT_MIN, LAT_MAX, LON_MIN, LON_MAX, 3);
        QVERIFY2(!hotspots.isEmpty(), "findHotspots should return non-empty results");
    }

    // 6. findHotspots: topK respected
    void testFindHotspotsTopK()
    {
        KDEHotspot kde(30);
        const auto hotspots = kde.findHotspots(londonCluster(), LAT_MIN, LAT_MAX, LON_MIN, LON_MAX, 2);
        QVERIFY2(hotspots.size() <= 2,
                 qPrintable(QStringLiteral("findHotspots(topK=2) returned %1").arg(hotspots.size())));
    }

    // 7. findHotspots: rank field correct (first = rank 1)
    void testFindHotspotsRankOrdering()
    {
        KDEHotspot kde(30);
        const auto hotspots = kde.findHotspots(londonCluster(), LAT_MIN, LAT_MAX, LON_MIN, LON_MAX, 3);
        QVERIFY(!hotspots.isEmpty());
        QVERIFY2(hotspots.first().rank == 1, "First hotspot should have rank 1");
        for (int i = 1; i < hotspots.size(); ++i)
            QVERIFY2(hotspots[i].rank >= hotspots[i-1].rank,
                     "Hotspots should be ordered by rank ascending");
    }

    // 8. paiAreaFraction: returns value in (0, 1]
    void testPaiAreaFractionRange()
    {
        KDEHotspot kde(20);
        const auto surface = kde.compute(londonCluster(), LAT_MIN, LAT_MAX, LON_MIN, LON_MAX);
        const double area = kde.paiAreaFraction(surface, 0.5);
        QVERIFY2(area > 0.0 && area <= 1.0,
                 qPrintable(QStringLiteral("PAI area fraction %1 must be in (0,1]").arg(area)));
    }

    // 9. silvermanBandwidth: positive for non-empty data
    void testSilvermanBandwidthPositive()
    {
        QVector<double> vals;
        for (int i = 0; i < 20; ++i) vals.append(51.5 + i * 0.001);
        const double bw = KDEHotspot::silvermanBandwidth(vals);
        QVERIFY2(bw > 0.0,
                 qPrintable(QStringLiteral("Silverman bandwidth %1 must be > 0").arg(bw)));
    }

    // 10. Empty locations: no crash, surface all zeros or empty
    void testEmptyLocationsNoCrash()
    {
        KDEHotspot kde(10);
        const auto surface = kde.compute({}, LAT_MIN, LAT_MAX, LON_MIN, LON_MAX);
        double total = 0.0;
        for (const auto& row : surface) for (double v : row) total += v;
        QVERIFY2(total == 0.0 || surface.empty(), "Empty input should produce all-zero surface");
    }
};

const double KDEHotspotAdvancedTest::LAT_MIN = 51.48;
const double KDEHotspotAdvancedTest::LAT_MAX = 51.52;
const double KDEHotspotAdvancedTest::LON_MIN = -0.15;
const double KDEHotspotAdvancedTest::LON_MAX = -0.05;

QTEST_MAIN(KDEHotspotAdvancedTest)
#include "test_kde_hotspot_advanced.moc"
