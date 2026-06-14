// test_kde_hotspot_deep7.cpp — Deep audit iteration 24: KDEHotspot
// Silverman bandwidth, surface normalisation, findHotspots, PAI, empty input.
#include <QtTest/QtTest>
#include <cmath>
#include "models/KDEHotspot.h"

class TestKDEHotspotDeep7 : public QObject
{
    Q_OBJECT

    using Locs = QVector<QPair<double, double>>;

    static QVector<double> latValues(const Locs& locs)
    {
        QVector<double> v;
        for (const auto& p : locs) v.append(p.first);
        return v;
    }

private slots:

    void testSilvermanBandwidthPositive()
    {
        const double bw = KDEHotspot::silvermanBandwidth(latValues({{51.5, -0.1}, {51.51, -0.09}}));
        QVERIFY2(bw > 0.0, qPrintable(QStringLiteral("bw=%1").arg(bw)));
    }

    void testSurfaceNormalisationNonNegative()
    {
        KDEHotspot kde(15);
        Locs locs = {{51.5, -0.1}, {51.51, -0.09}};
        const auto surface = kde.compute(locs, 51.48, 51.52, -0.12, -0.08);
        for (const auto& row : surface)
            for (double v : row)
                QVERIFY2(v >= 0.0, qPrintable(QStringLiteral("negative cell=%1").arg(v)));
    }

    void testFindHotspotsReturnsRankedRegions()
    {
        KDEHotspot kde(20);
        Locs locs;
        for (int i = 0; i < 8; ++i)
            locs.append({ 51.5 + i * 1e-5, -0.1 + i * 1e-5 });

        const auto hotspots = kde.findHotspots(locs, 51.48, 51.52, -0.12, -0.08, 3);
        QVERIFY(!hotspots.isEmpty());
        QVERIFY(hotspots.size() <= 3);
        if (hotspots.size() >= 2)
            QVERIFY(hotspots[0].peakDensity >= hotspots[1].peakDensity);
    }

    void testPAIAreaFractionInRange()
    {
        KDEHotspot kde(12);
        Locs locs = {{51.5, -0.1}, {51.51, -0.09}, {51.49, -0.11}};
        const auto surface = kde.compute(locs, 51.48, 51.52, -0.12, -0.08);
        const double pai = kde.paiAreaFraction(surface, 0.5);
        QVERIFY2(pai > 0.0 && pai <= 1.0,
                 qPrintable(QStringLiteral("pai=%1").arg(pai)));
    }

    void testEmptyInputReturnsZeroSurface()
    {
        KDEHotspot kde(10);
        const auto surface = kde.compute(Locs{}, 51.48, 51.52, -0.12, -0.08);
        double sum = 0.0;
        for (const auto& row : surface)
            for (double v : row) sum += v;
        QCOMPARE(sum, 0.0);
    }

    void testGridBoundsClamp()
    {
        KDEHotspot kde(8);
        Locs locs = {{51.5, -0.1}};
        const auto surface = kde.compute(locs, 51.49, 51.51, -0.11, -0.09);
        QCOMPARE(static_cast<int>(surface.size()), 8);
        QCOMPARE(static_cast<int>(surface[0].size()), 8);
    }

    void testHotspotCrimeCountPositive()
    {
        KDEHotspot kde(16);
        Locs locs = {{51.5, -0.1}, {51.5001, -0.1001}, {51.4999, -0.0999}};
        const auto hotspots = kde.findHotspots(locs, 51.48, 51.52, -0.12, -0.08, 1);
        QVERIFY(!hotspots.isEmpty());
        QVERIFY(hotspots.first().crimeCount >= 1);
    }
};

QTEST_GUILESS_MAIN(TestKDEHotspotDeep7)
#include "test_kde_hotspot_deep7.moc"
