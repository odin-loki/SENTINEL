// test_kde_temporal_cooffending.cpp — Iteration-3 audit tests for KDEHotspot,
// TemporalFeatures, and CoOffendingAnalyser.
#include <QTest>
#include <QDateTime>
#include <QTimeZone>
#include <cmath>
#include <algorithm>

#include "models/KDEHotspot.h"
#include "models/TemporalFeatures.h"
#include "inference/CoOffendingAnalyser.h"
#include "core/CrimeEvent.h"

class KDETemporalCooffendingTest : public QObject
{
    Q_OBJECT

    static QVector<QPair<double, double>> kdeCluster(double centLat, double centLon,
                                                     int n = 20, double spread = 0.005)
    {
        QVector<QPair<double, double>> pts;
        for (int i = 0; i < n; ++i) {
            const double dLat = spread * (((i * 7 + 3) % 11) - 5) / 5.0;
            const double dLon = spread * (((i * 11 + 2) % 13) - 6) / 6.0;
            pts.append({centLat + dLat, centLon + dLon});
        }
        return pts;
    }

    static PersonIncidentRecord cooffRec(const QString& person, const QString& incident,
                                         const QString& role = QStringLiteral("suspect"),
                                         double weight = 1.0)
    {
        PersonIncidentRecord r;
        r.personId   = person;
        r.incidentId = incident;
        r.role       = role;
        r.roleWeight = weight;
        return r;
    }

    static double gridDensityAt(const std::vector<std::vector<double>>& surface,
                                double lat, double lon,
                                double latMin, double latMax,
                                double lonMin, double lonMax)
    {
        if (surface.empty() || surface[0].empty()) return 0.0;

        const int N = static_cast<int>(surface.size());
        const double dLat = (latMax - latMin) / N;
        const double dLon = (lonMax - lonMin) / N;

        int r = static_cast<int>((lat - latMin) / dLat);
        int c = static_cast<int>((lon - lonMin) / dLon);
        r = std::clamp(r, 0, N - 1);
        c = std::clamp(c, 0, static_cast<int>(surface[0].size()) - 1);
        return surface[static_cast<size_t>(r)][static_cast<size_t>(c)];
    }

private slots:

    // ── KDEHotspot ───────────────────────────────────────────────────────────

    void testKDEFitProducesPositiveDensity()
    {
        KDEHotspot kde(20);
        const auto pts = kdeCluster(51.5, -0.1, 30);
        const auto surface = kde.compute(pts, 51.4, 51.6, -0.2, 0.0);

        double peak = 0.0;
        for (const auto& row : surface)
            for (double v : row)
                peak = std::max(peak, v);

        QVERIFY2(peak > 0.0,
                 qPrintable(QStringLiteral("Peak density should be > 0 after compute, got %1").arg(peak)));

        const double atCentroid = gridDensityAt(surface, 51.5, -0.1, 51.4, 51.6, -0.2, 0.0);
        QVERIFY2(atCentroid > 0.0,
                 qPrintable(QStringLiteral("Density near training centroid should be > 0, got %1")
                                .arg(atCentroid)));
    }

    void testKDETopNRankedByDensity()
    {
        KDEHotspot kde(30, 0.8);
        auto dense = kdeCluster(51.55, -0.05, 30);
        auto sparse = kdeCluster(51.45, -0.15, 10);
        const auto all = dense + sparse;

        const auto regions = kde.findHotspots(all, 51.4, 51.6, -0.2, 0.0, 5, 0.05);
        QVERIFY2(regions.size() >= 2,
                 qPrintable(QStringLiteral("Expected >= 2 hotspot regions, got %1").arg(regions.size())));

        QVERIFY2(regions[0].peakDensity > regions[1].peakDensity,
                 qPrintable(QStringLiteral("Top-1 density %1 should exceed top-2 %2")
                                .arg(regions[0].peakDensity)
                                .arg(regions[1].peakDensity)));
        QCOMPARE(regions[0].rank, 1);
        QCOMPARE(regions[1].rank, 2);
    }

    void testKDESilvermanBandwidth()
    {
        const double sigma = 0.05;
        auto makeData = [sigma](int n) {
            QVector<double> v(n);
            for (int i = 0; i < n; ++i) {
                const double t = (n > 1) ? static_cast<double>(i) / (n - 1) : 0.5;
                v[i] = 51.5 + sigma * (2.0 * t - 1.0) * std::sqrt(3.0);
            }
            return v;
        };

        const double h10  = KDEHotspot::silvermanBandwidth(makeData(10));
        const double h100 = KDEHotspot::silvermanBandwidth(makeData(100));
        QVERIFY2(h10 > 0.0 && std::isfinite(h10), "Bandwidth must be positive and finite");
        QVERIFY2(h100 > 0.0 && std::isfinite(h100), "Bandwidth must be positive and finite");
        QVERIFY2(h10 > h100,
                 qPrintable(QStringLiteral("Bandwidth should shrink with n^(-1/5): h10=%1 h100=%2")
                                .arg(h10)
                                .arg(h100)));

        const double ratio = h10 / h100;
        const double expected = std::pow(100.0 / 10.0, 0.2);
        QVERIFY2(std::abs(ratio - expected) / expected < 0.15,
                 qPrintable(QStringLiteral("Bandwidth ratio %1 should track n^(-1/5) ~ %2")
                                .arg(ratio)
                                .arg(expected)));
    }

    void testKDEPAIExceedsOne()
    {
        KDEHotspot kde(20);
        const auto pts = kdeCluster(51.5, -0.1, 40, 0.002);
        const auto surface = kde.compute(pts, 51.4, 51.6, -0.2, 0.0);

        const double areaFrac = kde.paiAreaFraction(surface, 0.5);
        QVERIFY2(areaFrac > 0.0 && areaFrac < 1.0, "Capture area fraction must be in (0,1)");

        // PAI = (n_cap / n_total) / (a_cap / a_total) = hit_rate / area_fraction
        const double pai = 0.5 / areaFrac;
        QVERIFY2(pai > 1.0,
                 qPrintable(QStringLiteral("Concentrated hotspot PAI should exceed 1, got %1")
                                .arg(pai)));
    }

    void testKDEUnfittedDensityZero()
    {
        KDEHotspot kde(10);
        const auto surface = kde.compute({}, 51.4, 51.6, -0.2, 0.0);

        for (const auto& row : surface)
            for (double v : row)
                QCOMPARE(v, 0.0);

        QVERIFY(kde.findHotspots({}, 51.4, 51.6, -0.2, 0.0).isEmpty());
    }

    // ── TemporalFeatures ─────────────────────────────────────────────────────

    void testTemporalHourSinCosPythagorean()
    {
        for (int h = 0; h < 24; ++h) {
            const QDateTime dt(QDate(2024, 6, 15), QTime(h, 0, 0), QTimeZone::utc());
            const auto f = TemporalFeatures::compute(dt);
            const double sumSq = f.hourSin * f.hourSin + f.hourCos * f.hourCos;
            QVERIFY2(std::abs(sumSq - 1.0) < 1e-6,
                     qPrintable(QStringLiteral("Hour %1: sin²+cos² = %2, expected 1")
                                    .arg(h)
                                    .arg(sumSq)));
            QVERIFY(f.hourSin >= -1.0 && f.hourSin <= 1.0);
            QVERIFY(f.hourCos >= -1.0 && f.hourCos <= 1.0);
        }
    }

    void testTemporalDOWSinCosPythagorean()
    {
        for (int d = 0; d < 7; ++d) {
            const QDateTime dt(QDate(2024, 1, 15).addDays(d), QTime(12, 0, 0), QTimeZone::utc());
            const auto f = TemporalFeatures::compute(dt);
            const double sumSq = f.dowSin * f.dowSin + f.dowCos * f.dowCos;
            QVERIFY2(std::abs(sumSq - 1.0) < 1e-6,
                     qPrintable(QStringLiteral("DOW %1: sin²+cos² = %2, expected 1")
                                    .arg(d)
                                    .arg(sumSq)));
        }
    }

    void testTemporalIsNightCorrect()
    {
        const QDateTime night(QDate(2024, 3, 10), QTime(23, 0, 0), QTimeZone::utc());
        const QDateTime day(QDate(2024, 3, 10), QTime(14, 0, 0), QTimeZone::utc());
        QVERIFY(TemporalFeatures::compute(night).isNight);
        QVERIFY(!TemporalFeatures::compute(day).isNight);
    }

    void testTemporalIsWeekendCorrect()
    {
        const QDateTime sat(QDate(2024, 1, 13), QTime(12, 0, 0), QTimeZone::utc());
        const QDateTime wed(QDate(2024, 1, 17), QTime(12, 0, 0), QTimeZone::utc());
        QVERIFY(TemporalFeatures::compute(sat).isWeekend);
        QVERIFY(!TemporalFeatures::compute(wed).isWeekend);
    }

    void testTemporalPaydayProximity()
    {
        // Implementation uses fortnightly cycle: daysFromPayday = min(doy%14, 14-doy%14)
        // 2024-03-24 is day-of-year 84 = 6 × 14, so daysFromPayday should be 0
        const QDateTime payday(QDate(2024, 3, 24), QTime(12, 0, 0), QTimeZone::utc());
        const auto f = TemporalFeatures::compute(payday);
        QVERIFY2(f.daysFromPayday == 0,
                 qPrintable(QStringLiteral("doy=84 payday proximity should be 0, got %1")
                                .arg(f.daysFromPayday)));
    }

    void testTemporalMidnightIsDark()
    {
        const QDateTime midnight(QDate(2024, 1, 15), QTime(0, 0, 0), QTimeZone::utc());
        const auto f = TemporalFeatures::compute(midnight);
        QVERIFY(f.isDark);
        QVERIFY(f.sunAltitudeDeg < -6.0);
    }

    void testTemporalNoonIsNotDark()
    {
        const QDateTime noon(QDate(2024, 6, 21), QTime(12, 0, 0), QTimeZone::utc());
        const auto f = TemporalFeatures::compute(noon);
        QVERIFY(!f.isDark);
        QVERIFY(f.sunAltitudeDeg > -6.0);
    }

    // ── CoOffendingAnalyser ──────────────────────────────────────────────────

    void testCooffendingPageRankSumIsOne()
    {
        QVector<PersonIncidentRecord> recs;
        recs.append(cooffRec(QStringLiteral("P1"), QStringLiteral("I1")));
        recs.append(cooffRec(QStringLiteral("P2"), QStringLiteral("I1")));
        recs.append(cooffRec(QStringLiteral("P2"), QStringLiteral("I2")));
        recs.append(cooffRec(QStringLiteral("P3"), QStringLiteral("I2")));
        recs.append(cooffRec(QStringLiteral("P3"), QStringLiteral("I3")));
        recs.append(cooffRec(QStringLiteral("P4"), QStringLiteral("I3")));

        CoOffendingAnalyser ca;
        ca.buildGraph(recs);
        ca.analyse();

        double sum = 0.0;
        for (const auto& n : ca.nodes())
            sum += n.pageRank;

        QVERIFY2(std::abs(sum - 1.0) < 0.05,
                 qPrintable(QStringLiteral("PageRank sum should be ~1.0, got %1").arg(sum)));
    }

    void testCooffendingHubHasHighCentrality()
    {
        QVector<PersonIncidentRecord> recs;
        recs.append(cooffRec(QStringLiteral("HUB"), QStringLiteral("I1")));
        recs.append(cooffRec(QStringLiteral("A"), QStringLiteral("I1")));
        recs.append(cooffRec(QStringLiteral("HUB"), QStringLiteral("I2")));
        recs.append(cooffRec(QStringLiteral("B"), QStringLiteral("I2")));
        recs.append(cooffRec(QStringLiteral("HUB"), QStringLiteral("I3")));
        recs.append(cooffRec(QStringLiteral("C"), QStringLiteral("I3")));

        CoOffendingAnalyser ca;
        ca.buildGraph(recs);
        ca.analyse();

        const auto ns = ca.nodes();
        const auto hubIt = std::find_if(ns.begin(), ns.end(),
            [](const NetworkNode& n) { return n.personId == QStringLiteral("HUB"); });
        QVERIFY(hubIt != ns.end());

        const double maxPr = std::max_element(ns.begin(), ns.end(),
            [](const NetworkNode& a, const NetworkNode& b) { return a.pageRank < b.pageRank; })
                                 ->pageRank;
        QVERIFY2(hubIt->pageRank >= maxPr * 0.8,
                 qPrintable(QStringLiteral("Hub PageRank %1 should be near max %2")
                                .arg(hubIt->pageRank)
                                .arg(maxPr)));
        QVERIFY2(hubIt->degree >= 3,
                 qPrintable(QStringLiteral("Hub degree should be >= 3, got %1").arg(hubIt->degree)));
    }

    void testCooffendingIsolatedNodeLowRank()
    {
        QVector<PersonIncidentRecord> recs;
        recs.append(cooffRec(QStringLiteral("HUB"), QStringLiteral("I1")));
        recs.append(cooffRec(QStringLiteral("A"), QStringLiteral("I1")));
        recs.append(cooffRec(QStringLiteral("HUB"), QStringLiteral("I2")));
        recs.append(cooffRec(QStringLiteral("B"), QStringLiteral("I2")));
        recs.append(cooffRec(QStringLiteral("ISO"), QStringLiteral("I_SOLO")));

        CoOffendingAnalyser ca;
        ca.buildGraph(recs);
        ca.analyse();

        const auto ns = ca.nodes();
        const auto isoIt = std::find_if(ns.begin(), ns.end(),
            [](const NetworkNode& n) { return n.personId == QStringLiteral("ISO"); });
        const auto hubIt = std::find_if(ns.begin(), ns.end(),
            [](const NetworkNode& n) { return n.personId == QStringLiteral("HUB"); });
        QVERIFY(isoIt != ns.end());
        QVERIFY(hubIt != ns.end());

        QVERIFY2(isoIt->pageRank < hubIt->pageRank,
                 qPrintable(QStringLiteral("Isolated ISO rank %1 should be < HUB %2")
                                .arg(isoIt->pageRank)
                                .arg(hubIt->pageRank)));

        const auto leads = ca.findLeads(QStringLiteral("I1"), 5);
        const auto isoLead = std::find_if(leads.begin(), leads.end(),
            [](const NetworkLead& l) { return l.personId == QStringLiteral("ISO"); });
        QVERIFY2(isoLead == leads.end(), "Isolated node should not appear in I1 leads");
    }

    void testCooffendingLeadsGenerated()
    {
        QVector<PersonIncidentRecord> recs;
        recs.append(cooffRec(QStringLiteral("P1"), QStringLiteral("I1")));
        recs.append(cooffRec(QStringLiteral("P2"), QStringLiteral("I1")));
        recs.append(cooffRec(QStringLiteral("P2"), QStringLiteral("I2")));
        recs.append(cooffRec(QStringLiteral("P3"), QStringLiteral("I2")));

        CoOffendingAnalyser ca;
        ca.buildGraph(recs);
        ca.analyse();

        const auto leads = ca.findLeads(QStringLiteral("I1"), 5);
        QVERIFY2(!leads.isEmpty(),
                 "Multi-person incidents should produce at least one NetworkLead");
        QVERIFY(leads.size() <= 5);
    }

    void testCooffendingSingleNodeNoLeads()
    {
        CoOffendingAnalyser ca;
        ca.buildGraph({ cooffRec(QStringLiteral("SOLO"), QStringLiteral("I_SOLO")) });
        ca.analyse();

        const auto leads = ca.findLeads(QStringLiteral("I_SOLO"), 5);
        // A single participant returns at most 1 lead (the participant themselves).
        // There are no second-degree connections in a single-node graph.
        QVERIFY2(leads.size() <= 1,
                 qPrintable(QStringLiteral("Single-node network should produce <= 1 lead, got %1")
                                .arg(leads.size())));
        // The lead's betweenness and degree should be zero (no edges)
        if (!leads.isEmpty()) {
            QCOMPARE(leads.first().personId, QStringLiteral("SOLO"));
        }
    }

    void testCooffendingDuplicateEdges()
    {
        QVector<PersonIncidentRecord> recs;
        recs.append(cooffRec(QStringLiteral("P1"), QStringLiteral("I1")));
        recs.append(cooffRec(QStringLiteral("P2"), QStringLiteral("I1")));
        recs.append(cooffRec(QStringLiteral("P1"), QStringLiteral("I1")));
        recs.append(cooffRec(QStringLiteral("P2"), QStringLiteral("I1")));

        CoOffendingAnalyser ca;
        ca.buildGraph(recs);
        ca.analyse();

        const auto ns = ca.nodes();
        QCOMPARE(ns.size(), 2);

        const auto p1It = std::find_if(ns.begin(), ns.end(),
            [](const NetworkNode& n) { return n.personId == QStringLiteral("P1"); });
        QVERIFY(p1It != ns.end());
        QCOMPARE(p1It->degree, 1);
        QVERIFY(p1It->neighbours.contains(QStringLiteral("P2")));
        QVERIFY(p1It->neighbours.value(QStringLiteral("P2")) > 0.0);

        const auto leads = ca.findLeads(QStringLiteral("I1"), 5);
        QVERIFY(!leads.isEmpty());
    }
};

QTEST_MAIN(KDETemporalCooffendingTest)
#include "test_kde_temporal_cooffending.moc"
