// test_geographic_profiler_deep5.cpp — Deep audit iteration 17: GeographicProfiler
// Verifies: search-area threshold monotonicity, peak location stability.

#include <QtTest/QtTest>
#include <cmath>
#include <numeric>
#include "inference/GeographicProfiler.h"

namespace {

constexpr double kGeoPi = 3.14159265358979323846;

double searchAreaRecompute(const GeographicProfile& result, double threshold)
{
    std::vector<double> flat;
    for (const auto& row : result.probabilitySurface)
        for (double v : row)
            flat.push_back(v);
    std::sort(flat.begin(), flat.end(), std::greater<double>());

    const double totalMass = std::accumulate(flat.begin(), flat.end(), 0.0);
    if (totalMass <= 0.0) return 0.0;

    const double cellLatKm = std::abs(result.gridLats[1] - result.gridLats[0]) * 111.0;
    const double midLat    = (result.gridLats.front() + result.gridLats.back()) / 2.0;
    const double cellLonKm = std::abs(result.gridLons[1] - result.gridLons[0]) * 111.0 *
                             std::cos(midLat * kGeoPi / 180.0);
    const double cellAreaKm2 = cellLatKm * cellLonKm;

    double cum = 0.0;
    int count = 0;
    for (double v : flat) {
        cum += v;
        ++count;
        if (cum >= threshold * totalMass) break;
    }
    return count * cellAreaKm2;
}

QVector<QPair<double, double>> tightCluster()
{
    return {
        { 51.500, -0.120 }, { 51.502, -0.118 }, { 51.498, -0.122 },
        { 51.501, -0.119 }, { 51.499, -0.121 }, { 51.5005, -0.1195 }
    };
}

} // namespace

class GeographicProfilerDeep5Test : public QObject
{
    Q_OBJECT

private slots:

    void testSearchAreaMonotonic50Vs80MultipleConfigs()
    {
        const QVector<QVector<QPair<double, double>>> configs = {
            tightCluster(),
            { { 51.50, -0.12 }, { 51.51, -0.10 }, { 51.49, -0.13 } },
            { { 51.50, -0.12 }, { 51.55, -0.08 }, { 51.45, -0.15 }, { 51.52, -0.11 } },
        };

        for (const auto& crimes : configs) {
            GeographicProfiler gp(1.2, 1.2, 0.5, 45);
            const auto result = gp.profile(crimes);
            QVERIFY2(result.searchArea50pct <= result.searchArea80pct,
                     qPrintable(QStringLiteral("50%% %1 should <= 80%% %2")
                         .arg(result.searchArea50pct).arg(result.searchArea80pct)));
            QVERIFY(result.searchArea50pct > 0.0);
            QVERIFY(result.searchArea80pct > 0.0);
        }
    }

    void testSearchAreaNonDecreasingWithHigherThreshold()
    {
        GeographicProfiler gp(1.2, 1.2, 0.4, 40);
        const auto result = gp.profile(tightCluster());

        const double area30 = searchAreaRecompute(result, 0.30);
        const double area50 = searchAreaRecompute(result, 0.50);
        const double area70 = searchAreaRecompute(result, 0.70);
        const double area90 = searchAreaRecompute(result, 0.90);

        QVERIFY2(area30 <= area50,
                 qPrintable(QStringLiteral("30%% area %1 should <= 50%% %2")
                     .arg(area30).arg(area50)));
        QVERIFY2(area50 <= area70,
                 qPrintable(QStringLiteral("50%% area %1 should <= 70%% %2")
                     .arg(area50).arg(area70)));
        QVERIFY2(area70 <= area90,
                 qPrintable(QStringLiteral("70%% area %1 should <= 90%% %2")
                     .arg(area70).arg(area90)));
        QVERIFY2(std::abs(result.searchArea50pct - area50) < 1e-6,
                 "profile searchArea50pct should match recomputed 50% area");
        QVERIFY2(std::abs(result.searchArea80pct - searchAreaRecompute(result, 0.80)) < 1e-6,
                 "profile searchArea80pct should match recomputed 80% area");
    }

    void testPeakStableAcrossRepeatedRuns()
    {
        GeographicProfiler gp(1.2, 1.2, 0.3, 50);
        const auto crimes = tightCluster();

        const auto first  = gp.profile(crimes);
        const auto second = gp.profile(crimes);

        QCOMPARE(first.peakLat, second.peakLat);
        QCOMPARE(first.peakLon, second.peakLon);
        QCOMPARE(first.peakProbability, second.peakProbability);
    }

    void testPeakStableWhenAddingPeripheralCrime()
    {
        GeographicProfiler gp(1.2, 1.2, 0.35, 55);
        const auto core = tightCluster();

        const auto base = gp.profile(core);
        auto extended   = core;
        extended.append({ 51.80, 0.05 });  // distant peripheral crime
        const auto withExtra = gp.profile(extended);

        QVERIFY2(std::abs(withExtra.peakLat - base.peakLat) < 0.015,
                 qPrintable(QStringLiteral("peak lat drift %1 -> %2 should stay within 0.015°")
                     .arg(base.peakLat).arg(withExtra.peakLat)));
        QVERIFY2(std::abs(withExtra.peakLon - base.peakLon) < 0.015,
                 qPrintable(QStringLiteral("peak lon drift %1 -> %2 should stay within 0.015°")
                     .arg(base.peakLon).arg(withExtra.peakLon)));
    }

    void testPeakStableAcrossGridResolutions()
    {
        const auto crimes = tightCluster();
        double cLat = 0.0, cLon = 0.0;
        for (const auto& c : crimes) { cLat += c.first; cLon += c.second; }
        cLat /= crimes.size();
        cLon /= crimes.size();

        GeographicProfiler gpFine(1.2, 1.2, 0.3, 70);
        GeographicProfiler gpCoarse(1.2, 1.2, 0.3, 35);

        const auto fine   = gpFine.profile(crimes);
        const auto coarse = gpCoarse.profile(crimes);

        QVERIFY2(std::abs(fine.peakLat - cLat) < 0.02,
                 qPrintable(QStringLiteral("fine peak lat %1 vs centroid %2")
                     .arg(fine.peakLat).arg(cLat)));
        QVERIFY2(std::abs(coarse.peakLat - cLat) < 0.02,
                 qPrintable(QStringLiteral("coarse peak lat %1 vs centroid %2")
                     .arg(coarse.peakLat).arg(cLat)));
        QVERIFY2(std::abs(fine.peakLat - coarse.peakLat) < 0.025,
                 qPrintable(QStringLiteral("fine/coarse peak lat %1 vs %2")
                     .arg(fine.peakLat).arg(coarse.peakLat)));
        QVERIFY2(std::abs(fine.peakLon - coarse.peakLon) < 0.025,
                 qPrintable(QStringLiteral("fine/coarse peak lon %1 vs %2")
                     .arg(fine.peakLon).arg(coarse.peakLon)));
    }

    void testSearchArea80GrowsWhenCrimesDisperse()
    {
        GeographicProfiler gp(1.2, 1.2, 0.5, 45);
        const auto compact = tightCluster();

        QVector<QPair<double, double>> dispersed = compact;
        dispersed.append({ 51.65, -0.05 });
        dispersed.append({ 51.35, -0.18 });

        const auto compactResult   = gp.profile(compact);
        const auto dispersedResult = gp.profile(dispersed);

        QVERIFY2(dispersedResult.searchArea80pct >= compactResult.searchArea80pct,
                 qPrintable(QStringLiteral("dispersed 80%% area %1 should be >= compact %2")
                     .arg(dispersedResult.searchArea80pct).arg(compactResult.searchArea80pct)));
    }

    void testPeakProbabilityHighestOnSurface()
    {
        GeographicProfiler gp(1.2, 1.2, 0.4, 40);
        const auto result = gp.profile(tightCluster());

        double maxCell = 0.0;
        for (const auto& row : result.probabilitySurface)
            for (double v : row)
                maxCell = std::max(maxCell, v);

        QCOMPARE(result.peakProbability, maxCell);
        QVERIFY(result.peakProbability > 0.0);
    }
};

QTEST_GUILESS_MAIN(GeographicProfilerDeep5Test)
#include "test_geographic_profiler_deep5.moc"
