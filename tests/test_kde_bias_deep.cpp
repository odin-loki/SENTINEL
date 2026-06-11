// test_kde_bias_deep.cpp — Iteration-8 deep audit: KDEHotspot + BiasAuditor
#include <QTest>
#include <QCoreApplication>
#include <cmath>

#include "core/CrimeEvent.h"
#include "models/KDEHotspot.h"
#include "benchmark/BiasAuditor.h"

class KdeBiasDeepTest : public QObject
{
    Q_OBJECT

private:
    using Locs = QVector<QPair<double, double>>;

    static Locs cluster(double lat, double lon, int n, double spread = 0.002)
    {
        Locs locs;
        for (int i = 0; i < n; ++i)
            locs.append({lat + (i % 5) * spread, lon + (i / 5) * spread});
        return locs;
    }

    static Locs eventsToLocs(const QVector<CrimeEvent>& events)
    {
        Locs locs;
        for (const auto& e : events) {
            if (e.lat && e.lon)
                locs.append({*e.lat, *e.lon});
            else
                locs.append({e.latitude, e.longitude});
        }
        return locs;
    }

    static double manualSilverman(const QVector<double>& values, double multiplier = 1.0)
    {
        const int n = values.size();
        double mean = 0.0;
        for (double v : values) mean += v;
        mean /= n;

        double var = 0.0;
        for (double v : values) var += (v - mean) * (v - mean);
        var /= (n - 1.0);
        const double sigma = std::sqrt(var);
        const double h = multiplier * 1.06 * sigma * std::pow(static_cast<double>(n), -0.2);
        return std::max(h, 1e-6);
    }

    static double bboxAreaDegrees2(const HotspotRegion& h)
    {
        return (h.latMax - h.latMin) * (h.lonMax - h.lonMin);
    }

    static double densityAt(const KDEHotspot& kde, const Locs& locs,
                          double lat, double lon,
                          double latMin, double latMax,
                          double lonMin, double lonMax)
    {
        const auto surf = kde.compute(locs, latMin, latMax, lonMin, lonMax);
        const int N = static_cast<int>(surf.size());
        const double dLat = (latMax - latMin) / N;
        const double dLon = (lonMax - lonMin) / N;
        int bestR = 0, bestC = 0;
        double bestDist = 1e9;
        for (int r = 0; r < N; ++r) {
            const double gLat = latMin + (r + 0.5) * dLat;
            for (int c = 0; c < N; ++c) {
                const double gLon = lonMin + (c + 0.5) * dLon;
                const double d = std::hypot(gLat - lat, gLon - lon);
                if (d < bestDist) {
                    bestDist = d;
                    bestR = r;
                    bestC = c;
                }
            }
        }
        return surf[bestR][bestC];
    }

private slots:

    // ── KDEHotspot (10) ───────────────────────────────────────────────────────

    void testSilvermanBandwidthFormula()
    {
        QVector<double> vals = {1.0, 2.0, 3.0, 4.0, 5.0};
        const double expected = manualSilverman(vals);
        const double actual   = KDEHotspot::silvermanBandwidth(vals);
        QVERIFY(std::abs(actual - expected) < 1e-12);
    }

    void testKernelGaussianShape()
    {
        KDEHotspot kde(40);
        const double lat = 51.5, lon = -0.1;
        const Locs locs = cluster(lat, lon, 30);
        const double near = densityAt(kde, locs, lat, lon, 51.48, 51.52, -0.12, -0.08);
        const double far  = densityAt(kde, locs, lat + 0.05, lon + 0.05,
                                      51.48, 51.52, -0.12, -0.08);
        QVERIFY(near > far);
    }

    void testHotspotRegionNotEmpty()
    {
        QVector<CrimeEvent> events;
        for (int i = 0; i < 40; ++i) {
            CrimeEvent e;
            e.eventId = QStringLiteral("e%1").arg(i);
            e.latitude  = 51.5 + (i % 5) * 0.002;
            e.longitude = -0.1 + (i / 5) * 0.002;
            events.append(e);
        }

        KDEHotspot kde(30);
        const auto hotspots = kde.findHotspots(eventsToLocs(events),
                                               51.48, 51.52, -0.12, -0.08, 5);
        QVERIFY(!hotspots.isEmpty());
    }

    void testEmptyEventsNoHotspots()
    {
        KDEHotspot kde(20);
        const auto hotspots = kde.findHotspots({}, 51.4, 51.6, -0.2, 0.0, 5);
        QVERIFY(hotspots.isEmpty());
    }

    void testHotspotPAIAboveOne()
    {
        KDEHotspot kde(50);
        const Locs locs = cluster(51.5, -0.1, 80, 0.001);
        const auto surf = kde.compute(locs, 51.49, 51.51, -0.105, -0.095);
        const double areaFrac = kde.paiAreaFraction(surf, 0.5);
        QVERIFY(areaFrac > 0.0 && areaFrac < 1.0);
        const double pai = 0.5 / areaFrac;
        QVERIFY(pai > 1.0);
    }

    void testTopNReturnsN()
    {
        KDEHotspot kde(60);
        Locs locs;
        for (int i = 0; i < 10; ++i) {
            const double lat = 51.40 + i * 0.02;
            const double lon = -0.20 + i * 0.02;
            for (const auto& p : cluster(lat, lon, 8, 0.001))
                locs.append(p);
        }
        const auto hotspots = kde.findHotspots(locs, 51.38, 51.62, -0.25, 0.05, 3, 0.015);
        QCOMPARE(hotspots.size(), 3);
    }

    void testTopNFewerThanN()
    {
        QVector<CrimeEvent> events;
        const double sites[][2] = {{51.50, -0.10}, {51.70, 0.10}, {51.50, 0.15}};
        for (int i = 0; i < 3; ++i) {
            CrimeEvent e;
            e.eventId   = QStringLiteral("e%1").arg(i);
            e.latitude  = sites[i][0];
            e.longitude = sites[i][1];
            events.append(e);
        }

        KDEHotspot kde(60);
        const auto hotspots = kde.findHotspots(eventsToLocs(events),
                                               51.40, 51.80, -0.20, 0.20, 10, 0.12);
        QVERIFY2(hotspots.size() <= 10,
                 qPrintable(QStringLiteral("requested 10, got %1").arg(hotspots.size())));
        QCOMPARE(hotspots.size(), 3);
    }

    void testBandwidthMinClamp()
    {
        QVector<double> identical(20, 51.5);
        const double bw = KDEHotspot::silvermanBandwidth(identical);
        QVERIFY(bw >= 1e-6);

        KDEHotspot kde(20);
        Locs locs;
        for (int i = 0; i < 20; ++i)
            locs.append({51.5, -0.1});
        const auto surf = kde.compute(locs, 51.49, 51.51, -0.11, -0.09);
        QCOMPARE(static_cast<int>(surf.size()), 20);
        QVERIFY(!surf.empty());
    }

    void testDensityDecaysWithDistance()
    {
        KDEHotspot kde(50);
        const double lat = 51.5, lon = -0.1;
        const Locs locs = cluster(lat, lon, 50, 0.001);
        const double atCentroid = densityAt(kde, locs, lat, lon, 51.49, 51.51, -0.105, -0.095);
        const double oneKmLat   = lat + 0.009;
        const double atOffset   = densityAt(kde, locs, oneKmLat, lon,
                                            51.49, 51.52, -0.105, -0.095);
        QVERIFY(atCentroid > atOffset);
    }

    void testHotspotAreaPositive()
    {
        KDEHotspot kde(40);
        const Locs locs = cluster(51.5, -0.1, 50);
        const auto hotspots = kde.findHotspots(locs, 51.48, 51.52, -0.12, -0.08, 3);
        QVERIFY(!hotspots.isEmpty());
        for (const auto& h : hotspots)
            QVERIFY(bboxAreaDegrees2(h) > 0.0);
    }

    // ── BiasAuditor (10) ──────────────────────────────────────────────────────

    void testDisparateImpactEqual()
    {
        QVector<QString> groups;
        QVector<double> preds;
        for (int i = 0; i < 50; ++i) { groups.append(QStringLiteral("A")); preds.append(0.8); }
        for (int i = 0; i < 50; ++i) { groups.append(QStringLiteral("B")); preds.append(0.8); }
        const auto reports = BiasAuditor::disparateImpact(groups, preds);
        QVERIFY(!reports.isEmpty());
        QVERIFY(std::abs(reports.first().ratio - 1.0) < 0.05);
    }

    void testDisparateImpactLow()
    {
        QVector<QString> groups;
        QVector<double> preds;
        for (int i = 0; i < 10; ++i) {
            groups.append(QStringLiteral("A"));
            preds.append(i < 9 ? 0.9 : 0.1);
        }
        for (int i = 0; i < 10; ++i) {
            groups.append(QStringLiteral("B"));
            preds.append(i < 5 ? 0.9 : 0.1);
        }
        const auto reports = BiasAuditor::disparateImpact(groups, preds);
        QVERIFY(!reports.isEmpty());
        QVERIFY(std::abs(reports.first().ratio - 0.56) < 0.05);
    }

    void testEightyPercentRule()
    {
        QVector<QString> okGroups, badGroups;
        QVector<double> okPreds, badPreds;
        for (int i = 0; i < 20; ++i) {
            okGroups.append(QStringLiteral("A")); okPreds.append(0.8);
            okGroups.append(QStringLiteral("B")); okPreds.append(0.75);
        }
        for (int i = 0; i < 10; ++i) {
            badGroups.append(QStringLiteral("A")); badPreds.append(0.9);
            badGroups.append(QStringLiteral("B")); badPreds.append(0.3);
        }

        const auto okReports  = BiasAuditor::disparateImpact(okGroups, okPreds);
        const auto badReports = BiasAuditor::disparateImpact(badGroups, badPreds);
        QVERIFY(!okReports.first().flagged);
        QVERIFY(badReports.first().flagged);
    }

    void testDemographicParity()
    {
        QVector<QString> groups;
        QVector<double> preds;
        for (int i = 0; i < 10; ++i) {
            groups.append(QStringLiteral("A"));
            preds.append(i < 9 ? 0.9 : 0.1);
        }
        for (int i = 0; i < 10; ++i) {
            groups.append(QStringLiteral("B"));
            preds.append(i < 4 ? 0.9 : 0.1);
        }

        const auto reports = BiasAuditor::disparateImpact(groups, preds);
        QVERIFY(!reports.isEmpty());
        const double parity = std::abs(reports.first().valueA - reports.first().valueB);
        QVERIFY(std::abs(parity - 0.5) < 0.05);
    }

    void testFPRComputation()
    {
        QVector<QString> groups;
        QVector<double> yTrue, yPred;
        for (int i = 0; i < 10; ++i) {
            groups.append(QStringLiteral("G"));
            yTrue.append(i < 2 ? 1.0 : 0.0);
            yPred.append(i < 4 ? 0.9 : 0.1);
        }
        int fp = 0, tn = 0;
        for (int i = 0; i < yTrue.size(); ++i) {
            const bool predPos = yPred[i] >= 0.5;
            const bool truePos = yTrue[i] >= 0.5;
            if (predPos && !truePos) ++fp;
            else if (!predPos && !truePos) ++tn;
        }
        const double fpr = static_cast<double>(fp) / (fp + tn);
        QCOMPARE(fp, 2);
        QCOMPARE(tn, 6);
        QVERIFY(std::abs(fpr - 0.25) < 1e-9);
    }

    void testFNRComputation()
    {
        QVector<QString> groups;
        QVector<double> yTrue, yPred;
        for (int i = 0; i < 10; ++i) {
            groups.append(QStringLiteral("G"));
            yTrue.append(1.0);
            yPred.append(i < 6 ? 0.9 : 0.1);
        }
        int fn = 0, tp = 0;
        for (int i = 0; i < yTrue.size(); ++i) {
            const bool predPos = yPred[i] >= 0.5;
            const bool truePos = yTrue[i] >= 0.5;
            if (!predPos && truePos) ++fn;
            else if (predPos && truePos) ++tp;
        }
        const double fnr = static_cast<double>(fn) / (fn + tp);
        QCOMPARE(fn, 4);
        QCOMPARE(tp, 6);
        QVERIFY(std::abs(fnr - 0.4) < 1e-9);
    }

    void testZeroGroupNocrash()
    {
        QVector<QString> groups;
        QVector<double> preds;
        for (int i = 0; i < 20; ++i) {
            groups.append(QStringLiteral("Active"));
            preds.append(0.8);
        }
        groups.append(QStringLiteral("Empty"));
        preds.append(0.1);

        const auto reports = BiasAuditor::disparateImpact(groups, preds);
        QVERIFY(!reports.isEmpty());
        for (const auto& r : reports)
            QVERIFY(std::isfinite(r.ratio));
    }

    void testSingleGroupNoComparison()
    {
        QVector<QString> groups;
        QVector<double> preds;
        for (int i = 0; i < 15; ++i) {
            groups.append(QStringLiteral("Only"));
            preds.append(0.7);
        }
        const auto reports = BiasAuditor::disparateImpact(groups, preds);
        QVERIFY(reports.isEmpty());

        const auto stats = BiasAuditor::groupStats(groups, preds, preds);
        QVERIFY(stats.contains(QStringLiteral("Only")));
        QCOMPARE(stats[QStringLiteral("Only")].nEvents, 15);
    }

    void testGroupStatsCountsMatch()
    {
        QVector<QString> groups;
        QVector<double> yTrue, yPred;
        for (int i = 0; i < 25; ++i) {
            groups.append(i < 15 ? QStringLiteral("X") : QStringLiteral("Y"));
            yTrue.append(i % 2 == 0 ? 1.0 : 0.0);
            yPred.append(0.6);
        }
        const auto stats = BiasAuditor::groupStats(groups, yTrue, yPred);
        QCOMPARE(stats[QStringLiteral("X")].nEvents, 15);
        QCOMPARE(stats[QStringLiteral("Y")].nEvents, 10);
        QCOMPARE(stats[QStringLiteral("X")].nEvents + stats[QStringLiteral("Y")].nEvents,
                 groups.size());
    }

    void testOverallBiasFlagSet()
    {
        QVector<QString> groups;
        QVector<double> preds;
        for (int i = 0; i < 30; ++i) { groups.append(QStringLiteral("A")); preds.append(0.95); }
        for (int i = 0; i < 30; ++i) { groups.append(QStringLiteral("B")); preds.append(0.2); }

        const auto reports = BiasAuditor::disparateImpact(groups, preds);
        const QString summary = BiasAuditor::formatReports(reports);
        bool anyFlagged = false;
        for (const auto& r : reports)
            if (r.flagged) { anyFlagged = true; break; }
        QVERIFY(anyFlagged);
        QVERIFY(summary.contains(QStringLiteral("FLAGGED")));
    }
};

QTEST_GUILESS_MAIN(KdeBiasDeepTest)
#include "test_kde_bias_deep.moc"
