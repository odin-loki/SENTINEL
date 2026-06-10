// test_kde_hotspot_accuracy.cpp
// Validates KDEHotspot compute(), findHotspots(), silvermanBandwidth(), and paiAreaFraction().
#include <QTest>
#include "models/KDEHotspot.h"
#include <cmath>
#include <numeric>

class KDEHotspotAccuracyTest : public QObject
{
    Q_OBJECT

private:
    static QVector<QPair<double,double>> clusterAt(double lat, double lon, int n,
                                                    double spread = 0.002)
    {
        QVector<QPair<double,double>> pts;
        pts.reserve(n);
        for (int i = 0; i < n; ++i) {
            const double dlat = spread * (static_cast<double>(i % 5 - 2) / 2.0);
            const double dlon = spread * (static_cast<double>(i / 5 - 2) / 2.0);
            pts.append({ lat + dlat, lon + dlon });
        }
        return pts;
    }

private slots:

    // ── 1. compute() returns grid of correct dimensions ──────────────────────
    void testGridDimensions()
    {
        KDEHotspot kde(40);
        const auto grid = kde.compute(clusterAt(51.5, -0.1, 20), 51.4, 51.6, -0.2, 0.0);
        QCOMPARE((int)grid.size(), 40);
        for (const auto& row : grid)
            QCOMPARE((int)row.size(), 40);
    }

    // ── 2. Density surface sums to approximately 1 (normalised) ──────────────
    void testSurfaceNormalised()
    {
        KDEHotspot kde(50);
        const auto grid = kde.compute(clusterAt(51.5, -0.1, 30), 51.4, 51.6, -0.2, 0.0);

        double total = 0.0;
        for (const auto& row : grid)
            for (double v : row)
                total += v;

        // Surface is normalised → total should be close to 1.0
        QVERIFY2(std::abs(total - 1.0) < 0.05,
                 qPrintable(QStringLiteral("Surface sum %1 should be ~1.0").arg(total)));
    }

    // ── 3. Peak density is within the input cluster bounding box ─────────────
    void testPeakInsideCluster()
    {
        KDEHotspot kde(60);
        const double cLat = 51.5, cLon = -0.1;
        const auto grid = kde.compute(clusterAt(cLat, cLon, 50), 51.4, 51.6, -0.2, 0.0);

        // Find peak cell
        int peakR = 0, peakC = 0;
        double maxVal = 0.0;
        for (int r = 0; r < (int)grid.size(); ++r) {
            for (int c = 0; c < (int)grid[r].size(); ++c) {
                if (grid[r][c] > maxVal) { maxVal = grid[r][c]; peakR = r; peakC = c; }
            }
        }

        // Peak row/col should be in the central half of the grid
        const int N = (int)grid.size();
        QVERIFY2(peakR > N / 4 && peakR < 3 * N / 4,
                 qPrintable(QStringLiteral("Peak row %1 should be central").arg(peakR)));
        QVERIFY2(peakC > N / 4 && peakC < 3 * N / 4,
                 qPrintable(QStringLiteral("Peak col %1 should be central").arg(peakC)));
    }

    // ── 4. findHotspots() returns correct count ───────────────────────────────
    void testFindHotspotsCount()
    {
        KDEHotspot kde(60);
        auto pts = clusterAt(51.5, -0.1, 30);
        pts += clusterAt(51.7, 0.1,  30);

        const auto hs = kde.findHotspots(pts, 51.4, 51.8, -0.2, 0.2, 2, 0.05);
        QVERIFY2(hs.size() >= 1 && hs.size() <= 2,
                 qPrintable(QStringLiteral("Expected 1-2 hotspots, got %1").arg(hs.size())));
    }

    // ── 5. Hotspot ranks are sequential from 1 ───────────────────────────────
    void testHotspotRanksSequential()
    {
        KDEHotspot kde(50);
        auto pts = clusterAt(51.5, -0.1, 25);

        const auto hs = kde.findHotspots(pts, 51.4, 51.6, -0.2, 0.0, 3, 0.05);
        for (int i = 0; i < hs.size(); ++i) {
            QCOMPARE(hs[i].rank, i + 1);
        }
    }

    // ── 6. Hotspot peak density > 0 ──────────────────────────────────────────
    void testHotspotPeakDensityPositive()
    {
        KDEHotspot kde(50);
        const auto hs = kde.findHotspots(
            clusterAt(51.5, -0.1, 20), 51.4, 51.6, -0.2, 0.0, 1, 0.05);
        QVERIFY(!hs.isEmpty());
        QVERIFY2(hs.first().peakDensity > 0.0, "Peak density must be positive");
    }

    // ── 7. Silverman bandwidth: same range, more data → smaller bandwidth ────
    void testSilvermanBandwidthScaling()
    {
        // Both datasets sample the same range [0, 1] → same σ
        // More samples → n^(-1/5) smaller → smaller bandwidth
        QVector<double> small(20), large(200);
        for (int i = 0; i < 20;  ++i) small[i] = static_cast<double>(i) / 19.0;
        for (int i = 0; i < 200; ++i) large[i] = static_cast<double>(i) / 199.0;

        const double bwSmall = KDEHotspot::silvermanBandwidth(small);
        const double bwLarge = KDEHotspot::silvermanBandwidth(large);

        // n^(-1/5): 200 points → narrower bandwidth than 20 points with same σ
        QVERIFY2(bwLarge < bwSmall,
                 qPrintable(QStringLiteral(
                    "Larger n should give smaller bandwidth: small=%1, large=%2")
                    .arg(bwSmall).arg(bwLarge)));
    }

    // ── 8. paiAreaFraction: top 10% area contains > 10% mass for clustered data
    void testPAIAreaFractionClustered()
    {
        KDEHotspot kde(50);
        const auto grid = kde.compute(clusterAt(51.5, -0.1, 50), 51.4, 51.6, -0.2, 0.0);
        const double area = kde.paiAreaFraction(grid, 0.5);

        // Clustered data: 50% of mass in < 50% of area
        QVERIFY2(area < 0.5,
                 qPrintable(QStringLiteral(
                    "Clustered data PAI area fraction %1 should be < 0.5").arg(area)));
    }

    // ── 9. Empty location list → surface is zero everywhere ──────────────────
    void testEmptyLocationsZeroSurface()
    {
        KDEHotspot kde(30);
        const auto grid = kde.compute({}, 51.4, 51.6, -0.2, 0.0);
        double total = 0.0;
        for (const auto& row : grid) for (double v : row) total += v;
        QVERIFY2(total == 0.0 || std::abs(total) < 1e-12,
                 "Empty locations should produce zero surface");
    }

    // ── 10. Two tight clusters: hottest one has more crimes ──────────────────
    void testHottestHotspotMoreCrimes()
    {
        KDEHotspot kde(80);
        // 50 crimes at cluster A, 10 at cluster B
        auto pts = clusterAt(51.5, -0.1, 50, 0.001);
        pts += clusterAt(51.7,  0.1, 10, 0.001);

        const auto hs = kde.findHotspots(pts, 51.4, 51.8, -0.2, 0.2, 2, 0.1);
        if (hs.size() >= 2) {
            // Rank 1 hotspot should have more crimes than rank 2
            QVERIFY2(hs[0].crimeCount >= hs[1].crimeCount,
                     qPrintable(QStringLiteral(
                        "Hotspot 1 crimes (%1) should >= hotspot 2 (%2)")
                        .arg(hs[0].crimeCount).arg(hs[1].crimeCount)));
        }
    }
};

QTEST_MAIN(KDEHotspotAccuracyTest)
#include "test_kde_hotspot_accuracy.moc"
