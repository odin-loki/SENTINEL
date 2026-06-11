// test_kde_hotspot_deep3.cpp — iteration-13 deep audit: Silverman, co-located peak, suppression, PAI
#include <QTest>
#include <cmath>
#include <numeric>
#include "models/KDEHotspot.h"

class TestKDEHotspotDeep3 : public QObject
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

    static int peakCellRow(const std::vector<std::vector<double>>& s)
    {
        int bestR = 0;
        int bestC = 0;
        double best = -1.0;
        for (int r = 0; r < static_cast<int>(s.size()); ++r) {
            for (int c = 0; c < static_cast<int>(s[r].size()); ++c) {
                if (s[r][c] > best) {
                    best = s[r][c];
                    bestR = r;
                    bestC = c;
                }
            }
        }
        Q_UNUSED(bestC);
        return bestR;
    }

private slots:

    void testSilvermanBandwidthFormula()
    {
        QVector<double> vals;
        for (int i = 0; i < 10; ++i)
            vals.append(static_cast<double>(i));

        const double n = 10.0;
        const double mean = 4.5;
        double var = 0.0;
        for (double v : vals)
            var += (v - mean) * (v - mean);
        var /= (n - 1.0);
        const double expected = 1.06 * std::sqrt(var) * std::pow(n, -0.2);

        const double h = KDEHotspot::silvermanBandwidth(vals);
        QVERIFY(std::abs(h - expected) < 1e-9);
    }

    void testSilvermanIdenticalPointsFloor()
    {
        QVector<double> vals(8, 51.5);
        const double h = KDEHotspot::silvermanBandwidth(vals);
        QVERIFY(h >= 1e-6);
    }

    void testColocatedEventsProducePeak()
    {
        KDEHotspot kde(30);
        Locs locs;
        const double lat = 51.5;
        const double lon = -0.1;
        for (int i = 0; i < 12; ++i)
            locs.append({lat, lon});

        const auto surface = kde.compute(locs, lat - 0.02, lat + 0.02, lon - 0.02, lon + 0.02);
        const int N = static_cast<int>(surface.size());
        const double dLat = 0.04 / N;
        const int peakR = peakCellRow(surface);
        const double peakLat = (lat - 0.02) + (peakR + 0.5) * dLat;

        QVERIFY2(std::abs(peakLat - lat) < dLat,
                 qPrintable(QStringLiteral("peakLat=%1 expected near %2")
                                .arg(peakLat).arg(lat)));
        QVERIFY(surface[peakR][N / 2] > 0.0);
    }

    void testComputeSurfaceNormalised()
    {
        KDEHotspot kde(15);
        Locs locs = {{51.5, -0.1}, {51.51, -0.09}, {51.49, -0.11}};
        const auto surface = kde.compute(locs, 51.47, 51.53, -0.12, -0.08);
        QVERIFY(std::abs(surfaceSum(surface) - 1.0) < 1e-9);
    }

    void testFindHotspotsGreedySuppression()
    {
        KDEHotspot kde(40);
        Locs locs;
        // Two well-separated clusters
        for (int i = 0; i < 8; ++i)
            locs.append({51.50 + i * 0.0001, -0.10});
        for (int i = 0; i < 8; ++i)
            locs.append({51.60 + i * 0.0001, -0.20});

        const auto regions = kde.findHotspots(locs, 51.49, 51.62, -0.21, -0.09, 2, 0.05);
        QVERIFY(regions.size() >= 2);

        const double dist = std::hypot(regions[0].centroidLat - regions[1].centroidLat,
                                       regions[0].centroidLon - regions[1].centroidLon);
        QVERIFY2(dist >= 0.05,
                 qPrintable(QStringLiteral("suppressed peaks too close: dist=%1").arg(dist)));
    }

    void testFindHotspotsRespectsTopK()
    {
        KDEHotspot kde(25);
        Locs locs;
        for (int i = 0; i < 15; ++i)
            locs.append({51.5 + i * 0.002, -0.1});

        const auto regions = kde.findHotspots(locs, 51.49, 51.54, -0.11, -0.09, 3, 0.008);
        QVERIFY(regions.size() <= 3);
    }

    void testPaiAreaFractionConcentrated()
    {
        KDEHotspot kde(10);
        const int N = 10;
        std::vector<std::vector<double>> surface(N, std::vector<double>(N, 0.1 / (N * N - 1)));
        surface[0][0] = 0.9;

        const double frac = kde.paiAreaFraction(surface, 0.5);
        QVERIFY2(frac <= 0.1,
                 qPrintable(QStringLiteral("PAI frac=%1 expected small").arg(frac)));
        QVERIFY(frac > 0.0);
    }

    void testPaiAreaFractionFullCoverage()
    {
        KDEHotspot kde(8);
        const int N = 8;
        std::vector<std::vector<double>> surface(N, std::vector<double>(N, 1.0 / (N * N)));
        QCOMPARE(kde.paiAreaFraction(surface, 1.0), 1.0);
    }

    void testPaiAreaFractionEmptySurface()
    {
        KDEHotspot kde;
        std::vector<std::vector<double>> empty;
        QCOMPARE(kde.paiAreaFraction(empty, 0.5), 1.0);
    }

    void testComputeEmptyLocations()
    {
        KDEHotspot kde(10);
        const auto surface = kde.compute({}, 51.0, 52.0, -1.0, 0.0);
        QCOMPARE(surfaceSum(surface), 0.0);
    }

    void testFindHotspotsEmptyLocations()
    {
        KDEHotspot kde(10);
        const auto regions = kde.findHotspots({}, 51.0, 52.0, -1.0, 0.0);
        QVERIFY(regions.isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestKDEHotspotDeep3)
#include "test_kde_hotspot_deep3.moc"
