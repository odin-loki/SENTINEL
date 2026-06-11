// test_kde_hotspot_deep4.cpp — iteration-15 deep audit: co-located events, bandwidth clamp, empty grid
#include <QTest>
#include <cmath>
#include <numeric>
#include "models/KDEHotspot.h"

class TestKDEHotspotDeep4 : public QObject
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

    static QPair<int, int> peakCell(const std::vector<std::vector<double>>& s)
    {
        int bestR = 0, bestC = 0;
        double best = -1.0;
        for (int r = 0; r < static_cast<int>(s.size()); ++r) {
            for (int c = 0; c < static_cast<int>(s[r].size()); ++c) {
                if (s[r][c] > best) {
                    best  = s[r][c];
                    bestR = r;
                    bestC = c;
                }
            }
        }
        return {bestR, bestC};
    }

private slots:

    void testEmptyLocationsZeroSurface()
    {
        KDEHotspot kde(12);
        const auto surface = kde.compute({}, 51.0, 52.0, -1.0, 0.0);
        QCOMPARE(static_cast<int>(surface.size()), 12);
        QCOMPARE(surfaceSum(surface), 0.0);
    }

    void testEmptyGridAllCellsZero()
    {
        KDEHotspot kde(16);
        const auto surface = kde.compute({}, 51.48, 51.52, -0.12, -0.08);
        for (const auto& row : surface) {
            for (double v : row)
                QCOMPARE(v, 0.0);
        }
    }

    void testMinimumGridNClampedToFour()
    {
        KDEHotspot kde(2);
        Locs locs = {{51.5, -0.1}};
        const auto surface = kde.compute(locs, 51.49, 51.51, -0.11, -0.09);
        QCOMPARE(static_cast<int>(surface.size()), 4);
        QCOMPARE(static_cast<int>(surface[0].size()), 4);
    }

    void testColocatedEventsConcentratePeakMass()
    {
        KDEHotspot kde(25);
        const double lat = 51.5;
        const double lon = -0.1;
        Locs colocated;
        for (int i = 0; i < 15; ++i)
            colocated.append({lat, lon});

        Locs scattered;
        for (int i = 0; i < 15; ++i)
            scattered.append({51.48 + i * 0.004, -0.12 + i * 0.003});

        const auto sColoc = kde.compute(colocated, lat - 0.03, lat + 0.03, lon - 0.03, lon + 0.03);
        const auto sScat  = kde.compute(scattered, 51.47, 51.54, -0.13, -0.07);

        const auto peakColoc = peakCell(sColoc);
        const auto peakScat  = peakCell(sScat);
        QVERIFY2(sColoc[peakColoc.first][peakColoc.second]
                     > sScat[peakScat.first][peakScat.second],
                 "co-located events should produce a sharper peak than scattered events");
    }

    void testColocatedPeakNearEventCoordinate()
    {
        KDEHotspot kde(20);
        const double lat = 51.507;
        const double lon = -0.127;
        Locs locs;
        for (int i = 0; i < 10; ++i)
            locs.append({lat, lon});

        const double latMin = lat - 0.01;
        const double latMax = lat + 0.01;
        const double lonMin = lon - 0.01;
        const double lonMax = lon + 0.01;
        const auto surface  = kde.compute(locs, latMin, latMax, lonMin, lonMax);

        const int N    = static_cast<int>(surface.size());
        const double dLat = (latMax - latMin) / N;
        const auto peak   = peakCell(surface);
        const double peakLat = latMin + (peak.first + 0.5) * dLat;

        QVERIFY2(std::abs(peakLat - lat) < dLat,
                 qPrintable(QStringLiteral("peakLat=%1 expected within one cell of %2")
                                .arg(peakLat).arg(lat)));
    }

    void testBandwidthClampIdenticalPointsStillPeak()
    {
        // Identical coordinates → Silverman σ=0 → h=1e-6; clamp must widen to half a cell.
        KDEHotspot kde(10);
        Locs locs(8, {51.5, -0.1});
        const auto surface = kde.compute(locs, 51.49, 51.51, -0.11, -0.09);

        const auto peak = peakCell(surface);
        QVERIFY2(surface[peak.first][peak.second] > 0.0,
                 "bandwidth clamp must allow non-zero peak for identical events");
        QVERIFY(std::abs(surfaceSum(surface) - 1.0) < 1e-9);
    }

    void testBandwidthClampSinglePointUsesGridCell()
    {
        KDEHotspot kde(8);
        const double lat = 51.5;
        const double lon = -0.1;
        const auto surface = kde.compute({{lat, lon}}, lat - 0.02, lat + 0.02,
                                         lon - 0.02, lon + 0.02);

        const auto peak = peakCell(surface);
        QVERIFY(surface[peak.first][peak.second] > 0.0);
        QVERIFY2(surfaceSum(surface) > 0.0, "single co-located point must yield non-empty surface");
    }

    void testFindHotspotsEmptyLocations()
    {
        KDEHotspot kde(10);
        const auto regions = kde.findHotspots({}, 51.0, 52.0, -1.0, 0.0);
        QVERIFY(regions.isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestKDEHotspotDeep4)
#include "test_kde_hotspot_deep4.moc"
