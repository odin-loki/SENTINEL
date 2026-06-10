// test_kde_hotspot.cpp — KDEHotspot unit tests
#include <QTest>
#include <QCoreApplication>
#include "models/KDEHotspot.h"

class TestKDEHotspot : public QObject {
    Q_OBJECT

    // Generate a cluster of points around (centLat, centLon)
    static QVector<QPair<double,double>> cluster(double centLat, double centLon,
                                                   int n = 20, double spread = 0.005)
    {
        QVector<QPair<double,double>> pts;
        for (int i = 0; i < n; ++i) {
            // Deterministic offsets in a small cloud
            const double dLat = spread * (((i * 7 + 3) % 11) - 5) / 5.0;
            const double dLon = spread * (((i * 11 + 2) % 13) - 6) / 6.0;
            pts.append({centLat + dLat, centLon + dLon});
        }
        return pts;
    }

private slots:

    void testEmptyInput() {
        KDEHotspot kde;
        const auto surface = kde.compute({}, 51.4, 51.6, -0.2, 0.0);
        QCOMPARE(static_cast<int>(surface.size()), 50);
        for (const auto& row : surface)
            for (double v : row) QCOMPARE(v, 0.0);
    }

    void testSurfaceDimensions() {
        KDEHotspot kde(20);
        const auto pts = cluster(51.5, -0.1);
        const auto surface = kde.compute(pts, 51.4, 51.6, -0.2, 0.0);
        QCOMPARE(static_cast<int>(surface.size()), 20);
        for (const auto& row : surface) QCOMPARE(static_cast<int>(row.size()), 20);
    }

    void testSurfaceNormalisedToOne() {
        KDEHotspot kde(10);
        const auto pts = cluster(51.5, -0.1);
        const auto surface = kde.compute(pts, 51.4, 51.6, -0.2, 0.0);
        double total = 0.0;
        for (const auto& row : surface) for (double v : row) total += v;
        QVERIFY(std::abs(total - 1.0) < 1e-6);
    }

    void testSurfaceAllNonNegative() {
        KDEHotspot kde(10);
        const auto pts = cluster(51.5, -0.1);
        const auto surface = kde.compute(pts, 51.4, 51.6, -0.2, 0.0);
        for (const auto& row : surface)
            for (double v : row) QVERIFY(v >= 0.0);
    }

    void testPeakNearClusterCentroid() {
        KDEHotspot kde(20);
        // Cluster at (51.5, -0.1)
        const auto pts = cluster(51.5, -0.1);
        const auto surface = kde.compute(pts, 51.4, 51.6, -0.2, 0.0);

        // Find the peak cell
        double maxVal = 0.0;
        int peakR = 0, peakC = 0;
        for (int r = 0; r < 20; ++r)
            for (int c = 0; c < 20; ++c)
                if (surface[r][c] > maxVal) { maxVal = surface[r][c]; peakR=r; peakC=c; }

        // Peak should be within the middle 50% of the grid (cluster is centered)
        QVERIFY(peakR >= 3 && peakR <= 17);
        QVERIFY(peakC >= 3 && peakC <= 17);
    }

    void testFindHotspotsEmpty() {
        KDEHotspot kde;
        QVERIFY(kde.findHotspots({}, 51.4, 51.6, -0.2, 0.0).isEmpty());
    }

    void testFindHotspotsOneCluster() {
        KDEHotspot kde(30);
        const auto pts = cluster(51.5, -0.1, 30);
        const auto regions = kde.findHotspots(pts, 51.4, 51.6, -0.2, 0.0, 3);
        QVERIFY(!regions.isEmpty());
        // The top region should be near the cluster centroid
        const auto& top = regions.first();
        QVERIFY(std::abs(top.centroidLat - 51.5) < 0.15);
        QVERIFY(std::abs(top.centroidLon - (-0.1)) < 0.15);
    }

    void testFindHotspotsTwoClusters() {
        KDEHotspot kde(40, 0.8);
        // Two well-separated clusters
        auto pts1 = cluster(51.45, -0.15, 20);
        auto pts2 = cluster(51.55, -0.05, 20);
        auto all = pts1 + pts2;
        const auto regions = kde.findHotspots(all, 51.4, 51.6, -0.2, 0.0, 5, 0.05);
        QVERIFY(regions.size() >= 2);  // should find both clusters
    }

    void testHotspotRankOrdering() {
        KDEHotspot kde(20);
        const auto pts = cluster(51.5, -0.1, 50);
        const auto regions = kde.findHotspots(pts, 51.4, 51.6, -0.2, 0.0, 3);
        for (int i = 0; i < regions.size(); ++i) {
            QCOMPARE(regions[i].rank, i + 1);
        }
    }

    void testHotspotPeakDensityDescending() {
        KDEHotspot kde(20);
        auto pts1 = cluster(51.45, -0.15, 10);
        auto pts2 = cluster(51.55, -0.05, 30);   // denser cluster
        auto all = pts1 + pts2;
        const auto regions = kde.findHotspots(all, 51.4, 51.6, -0.2, 0.0, 5, 0.05);
        if (regions.size() >= 2) {
            QVERIFY(regions[0].peakDensity >= regions[1].peakDensity);
        }
    }

    void testTopKRespected() {
        KDEHotspot kde(20);
        const auto pts = cluster(51.5, -0.1, 50);
        const int K = 2;
        const auto regions = kde.findHotspots(pts, 51.4, 51.6, -0.2, 0.0, K);
        QVERIFY(regions.size() <= K);
    }

    void testPaiAreaFractionRange() {
        KDEHotspot kde(10);
        const auto pts = cluster(51.5, -0.1, 20);
        const auto surface = kde.compute(pts, 51.4, 51.6, -0.2, 0.0);
        const double frac = kde.paiAreaFraction(surface, 0.5);
        QVERIFY(frac > 0.0 && frac <= 1.0);
    }

    void testPaiAreaFractionConcentratedCluster() {
        // Concentrated cluster → small fraction of area contains most density
        KDEHotspot kde(20);
        const auto pts = cluster(51.5, -0.1, 30, 0.002);   // very tight
        const auto surface = kde.compute(pts, 51.4, 51.6, -0.2, 0.0);
        const double frac = kde.paiAreaFraction(surface, 0.8);
        // At least 80% mass should be in ≤ 50% of cells
        QVERIFY(frac <= 0.5);
    }

    void testSilvermanBandwidthSingleValue() {
        const double h = KDEHotspot::silvermanBandwidth({51.5}, 1.0);
        QVERIFY(h > 0.0 && std::isfinite(h));
    }

    void testSilvermanBandwidthScales() {
        // Same σ but different n: n^(-1/5) decreases as n grows → larger n = smaller h
        const double sigma = 0.05;
        auto makeData = [sigma](int n) {
            QVector<double> v(n);
            // Values centred at 51.5 with given std dev
            for (int i = 0; i < n; ++i) {
                const double t = (n > 1) ? static_cast<double>(i) / (n - 1) : 0.5;
                v[i] = 51.5 + sigma * (2.0 * t - 1.0) * std::sqrt(3.0);   // uniform approx
            }
            return v;
        };
        const double hSmall = KDEHotspot::silvermanBandwidth(makeData(10));
        const double hLarge = KDEHotspot::silvermanBandwidth(makeData(1000));
        // With same σ, more samples → smaller bandwidth: h_small/h_large = (1000/10)^(1/5) = ~4
        QVERIFY(hSmall > hLarge);
    }

    void testBandwidthMultiplierEffect() {
        const auto pts = cluster(51.5, -0.1, 20);
        KDEHotspot kde1(10, 0.5);
        KDEHotspot kde2(10, 2.0);
        const auto s1 = kde1.compute(pts, 51.4, 51.6, -0.2, 0.0);
        const auto s2 = kde2.compute(pts, 51.4, 51.6, -0.2, 0.0);
        // Wider bandwidth → more diffuse surface → lower peak
        double peak1 = 0.0, peak2 = 0.0;
        for (const auto& row : s1) for (double v : row) peak1 = std::max(peak1, v);
        for (const auto& row : s2) for (double v : row) peak2 = std::max(peak2, v);
        QVERIFY(peak1 >= peak2);  // tighter bw → higher peak
    }

    void testHotspotCrimeCountNonNegative() {
        KDEHotspot kde(20);
        const auto pts = cluster(51.5, -0.1, 25);
        for (const auto& r : kde.findHotspots(pts, 51.4, 51.6, -0.2, 0.0, 3))
            QVERIFY(r.crimeCount >= 0);
    }

    void testTotalMassNonNegative() {
        KDEHotspot kde(20);
        const auto pts = cluster(51.5, -0.1, 25);
        for (const auto& r : kde.findHotspots(pts, 51.4, 51.6, -0.2, 0.0, 3))
            QVERIFY(r.totalMass >= 0.0);
    }
};

// ─── main ─────────────────────────────────────────────────────────────────────

static int runTest(QObject* obj, const char* logFile) {
    QStringList args = { "test", "-o", QString("%1,txt").arg(logFile) };
    return QTest::qExec(obj, args);
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    int r = 0;
    TestKDEHotspot t1; r |= runTest(&t1, "kde_hotspot.txt");
    return r;
}

#include "test_kde_hotspot.moc"
