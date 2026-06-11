#include <QTest>
#include "models/KDEHotspot.h"
#include <cmath>
#include <numeric>

class TestKDEHotspotDeep2 : public QObject
{
    Q_OBJECT

private:
    using Locs = QVector<QPair<double,double>>;

    static double surfaceSum(const std::vector<std::vector<double>>& s)
    {
        double total = 0.0;
        for (const auto& row : s) for (double v : row) total += v;
        return total;
    }

private slots:

    void testSilvermanBandwidthIdenticalPoints()
    {
        // 5 identical values → sigma=0 → bandwidth clamps to 1e-6
        QVector<double> vals(5, 51.5);
        const double h = KDEHotspot::silvermanBandwidth(vals);
        QVERIFY2(h >= 1e-6,
                 qPrintable(QStringLiteral("Bandwidth for identical points should be >= 1e-6, got %1").arg(h)));
        QVERIFY2(h <= 1e-4,
                 qPrintable(QStringLiteral("Bandwidth for identical points should be near 1e-6, got %1").arg(h)));
    }

    void testSilvermanBandwidthSpreadData()
    {
        QVector<double> vals;
        for (int i = 0; i < 20; ++i) vals.append(51.0 + i * 0.05);
        const double h = KDEHotspot::silvermanBandwidth(vals);
        QVERIFY2(h > 1e-6,
                 qPrintable(QStringLiteral("Bandwidth for spread data should be >1e-6, got %1").arg(h)));
        QVERIFY(h > 0.0);
    }

    void testSilvermanBandwidthFormula()
    {
        // 10 evenly-spaced points: verify h ~ 1.06 * sigma * n^(-0.2)
        QVector<double> vals;
        for (int i = 0; i < 10; ++i) vals.append(static_cast<double>(i));
        const double n = 10.0;
        // mean=4.5, var = sum((i-4.5)^2) / 9
        // sum = 2*(0.25+2.25+6.25+12.25+20.25) = 2*41.25 = 82.5
        // var = 82.5/9 = 9.1666..., sigma = 3.0277...
        const double mean = 4.5;
        double var = 0.0;
        for (double v : vals) var += (v - mean) * (v - mean);
        var /= (n - 1.0);
        const double sigma = std::sqrt(var);
        const double expected = 1.06 * sigma * std::pow(n, -0.2);

        const double h = KDEHotspot::silvermanBandwidth(vals);
        QVERIFY2(qAbs(h - expected) < 1e-9,
                 qPrintable(QStringLiteral("Expected h=%1, got %2").arg(expected).arg(h)));
    }

    void testComputeSurfaceSumsToOne()
    {
        KDEHotspot kde(20);
        Locs locs;
        locs.append({51.5, -0.1});
        locs.append({51.51, -0.09});
        locs.append({51.52, -0.11});

        const auto surface = kde.compute(locs, 51.48, 51.54, -0.13, -0.07);
        const double total = surfaceSum(surface);
        QVERIFY2(qAbs(total - 1.0) < 1e-9,
                 qPrintable(QStringLiteral("Normalised surface should sum to 1.0, got %1").arg(total)));
    }

    void testComputeSurfaceAllNonNegative()
    {
        KDEHotspot kde(10);
        Locs locs = {{51.5, -0.1}, {51.51, -0.09}};
        const auto surface = kde.compute(locs, 51.48, 51.54, -0.13, -0.07);
        for (const auto& row : surface)
            for (double v : row)
                QVERIFY(v >= 0.0);
    }

    void testFindHotspotsAtMostTopK()
    {
        KDEHotspot kde(30);
        Locs locs;
        for (int i = 0; i < 20; ++i)
            locs.append({51.5 + i * 0.001, -0.1 + i * 0.001});

        const auto regions = kde.findHotspots(locs, 51.49, 51.52, -0.11, -0.08, 3, 0.01);
        QVERIFY2(regions.size() <= 3,
                 qPrintable(QStringLiteral("findHotspots(topK=3) should return <=3, got %1")
                            .arg(regions.size())));
    }

    void testFindHotspotsSingleEvent()
    {
        KDEHotspot kde(20);
        Locs locs = {{51.5, -0.1}};
        const auto regions = kde.findHotspots(locs, 51.48, 51.52, -0.12, -0.08, 5, 0.01);
        QVERIFY2(regions.size() == 1,
                 qPrintable(QStringLiteral("Single event should produce 1 hotspot, got %1")
                            .arg(regions.size())));
        QVERIFY(regions[0].rank == 1);
        QVERIFY(regions[0].peakDensity > 0.0);
    }

    void testFindHotspotsRankedCorrectly()
    {
        KDEHotspot kde(40);
        Locs locs;
        // Dense cluster at (51.5, -0.1) with 10 points
        for (int i = 0; i < 10; ++i) locs.append({51.5 + i * 0.0001, -0.1});
        // Sparse cluster at (51.6, -0.2) with 2 points
        locs.append({51.6, -0.2});
        locs.append({51.601, -0.2});

        const auto regions = kde.findHotspots(locs, 51.49, 51.62, -0.21, -0.09, 2, 0.05);
        QVERIFY(regions.size() >= 1);
        QCOMPARE(regions[0].rank, 1);
        if (regions.size() >= 2) QCOMPARE(regions[1].rank, 2);
    }

    void testPaiAreaFractionUniformSurface()
    {
        KDEHotspot kde(10);
        // Uniform: all cells have equal density
        const int N = 10;
        std::vector<std::vector<double>> surface(N, std::vector<double>(N, 1.0 / (N * N)));
        const double frac = kde.paiAreaFraction(surface, 1.0);
        QVERIFY2(qAbs(frac - 1.0) < 1e-9,
                 qPrintable(QStringLiteral("Uniform surface, pFrac=1.0 should return 1.0, got %1").arg(frac)));
    }

    void testPaiAreaFractionConcentrated()
    {
        KDEHotspot kde(10);
        // One cell has 90% of density, rest share 10%
        const int N = 10;
        const double total_cells = N * N;
        std::vector<std::vector<double>> surface(N, std::vector<double>(N, 0.1 / (total_cells - 1)));
        surface[0][0] = 0.9;

        // Top cell (10% of total cells when N=10 → 1 cell out of 100 = 1%)
        // The top 1% area should capture >50% of density
        const double frac = kde.paiAreaFraction(surface, 0.5);
        // Should only need a tiny fraction to capture 50%
        QVERIFY2(frac <= 0.1,
                 qPrintable(QStringLiteral("Top 10%% area should capture >50%% density, frac=%1").arg(frac)));
    }

    void testPaiAreaFractionEmptySurface()
    {
        KDEHotspot kde;
        std::vector<std::vector<double>> empty;
        const double frac = kde.paiAreaFraction(empty, 0.5);
        QCOMPARE(frac, 1.0);
    }

    void testComputeEmptyLocations()
    {
        KDEHotspot kde(10);
        const auto surface = kde.compute({}, 51.0, 52.0, -1.0, 1.0);
        const double total = surfaceSum(surface);
        QCOMPARE(total, 0.0);
    }
};

QTEST_GUILESS_MAIN(TestKDEHotspotDeep2)
#include "test_kde_hotspot_deep2.moc"
