#include <QTest>
#include "inference/GeographicProfiler.h"
#include <cmath>

class TestGeographicProfilerDeep2 : public QObject
{
    Q_OBJECT

    static QVector<QPair<double,double>> sites(std::initializer_list<std::pair<double,double>> pts)
    {
        QVector<QPair<double,double>> v;
        for (auto& p : pts)
            v << qMakePair(p.first, p.second);
        return v;
    }

    static double dist2D(double lat1, double lon1, double lat2, double lon2)
    {
        double dl = lat1 - lat2, dn = lon1 - lon2;
        return std::sqrt(dl*dl + dn*dn);
    }

private slots:

    // Empty input: profile is returned with empty probability surface
    void testEmptyInput()
    {
        GeographicProfiler gp;
        auto prof = gp.profile({});
        QVERIFY(prof.probabilitySurface.empty());
    }

    // Single crime site: the peak must NOT be at the crime scene.
    // Rossmo's buffer zone term suppresses probability near the crime site;
    // the maximum occurs at approximately one buffer radius away.
    void testSingleCrimeSiteBufferZoneEffect()
    {
        // bufferKm=0.5 → bufferDeg = 0.5/111 ≈ 0.004505°
        GeographicProfiler gp(1.2, 1.2, 0.5, 40);
        const double crimeLat = 51.5, crimeLon = -0.1;
        auto prof = gp.profile(sites({{crimeLat, crimeLon}}));

        QVERIFY(!prof.probabilitySurface.empty());
        QCOMPARE(prof.method, QStringLiteral("rossmo_cgt"));

        double distPeakToCrime = dist2D(prof.peakLat, prof.peakLon, crimeLat, crimeLon);
        const double bufferDeg = 0.5 / 111.0;

        // Peak must be meaningfully away from the crime site (buffer zone effect)
        QVERIFY2(distPeakToCrime > 0.3 * bufferDeg,
                 qPrintable(QString("Peak distance to crime %1° should be > 0.3×bufferDeg %2°")
                            .arg(distPeakToCrime).arg(0.3 * bufferDeg)));

        // Peak should not be at the crime scene itself
        QVERIFY2(distPeakToCrime > 1e-6,
                 "Peak must not coincide with the single crime site (buffer zone)");
    }

    // Probability surface must integrate (sum) to 1.0 after normalisation
    void testSingleCrimeNormalization()
    {
        GeographicProfiler gp(1.2, 1.2, 0.5, 20);
        auto prof = gp.profile(sites({{51.5, -0.1}}));

        double totalProb = 0.0;
        for (const auto& row : prof.probabilitySurface)
            for (double v : row)
                totalProb += v;

        QVERIFY2(std::abs(totalProb - 1.0) < 1e-9,
                 qPrintable(QString("Surface sum %1 must equal 1.0").arg(totalProb)));
    }

    // All surface values must be finite and non-negative (no NaN/Inf at any
    // grid cell, including those placed at exactly buffer distance from a crime)
    void testAllSurfaceValuesFiniteNonNegative()
    {
        GeographicProfiler gp(1.2, 1.2, 0.5, 20);
        auto prof = gp.profile(sites({{51.5, -0.1}}));

        for (std::size_t r = 0; r < prof.probabilitySurface.size(); ++r) {
            for (std::size_t c = 0; c < prof.probabilitySurface[r].size(); ++c) {
                double v = prof.probabilitySurface[r][c];
                QVERIFY2(std::isfinite(v),
                         qPrintable(QString("Cell [%1][%2] = %3 is not finite")
                                    .arg(r).arg(c).arg(v)));
                QVERIFY2(v >= 0.0,
                         qPrintable(QString("Cell [%1][%2] = %3 is negative")
                                    .arg(r).arg(c).arg(v)));
            }
        }
    }

    // Multiple crime scenes: profile normalises correctly and peak has positive probability
    void testMultipleCrimeSitesNormalized()
    {
        GeographicProfiler gp(1.2, 1.2, 0.5, 20);
        auto prof = gp.profile(sites({{51.50, -0.10},
                                      {51.51, -0.10},
                                      {51.50, -0.09}}));

        double totalProb = 0.0;
        for (const auto& row : prof.probabilitySurface)
            for (double v : row)
                totalProb += v;

        QVERIFY2(std::abs(totalProb - 1.0) < 1e-9,
                 qPrintable(QString("Multi-crime surface sum %1 must equal 1.0").arg(totalProb)));
        QVERIFY2(prof.peakProbability > 0.0, "Peak probability must be positive");
        QVERIFY2(prof.searchArea50pct >= 0.0, "50% search area must be non-negative");
        QVERIFY2(prof.searchArea80pct >= prof.searchArea50pct,
                 "80% search area must be >= 50% search area");
    }

    // Collinear crime scenes: the profile is non-zero and normalised;
    // the peak is somewhere in the grid (not NaN)
    void testCollinearCrimeSites()
    {
        GeographicProfiler gp(1.2, 1.2, 0.5, 20);
        // Three crimes along the same latitude
        auto prof = gp.profile(sites({{51.50, -0.12},
                                      {51.50, -0.10},
                                      {51.50, -0.08}}));

        QVERIFY(!prof.probabilitySurface.empty());
        QVERIFY2(std::isfinite(prof.peakLat) && std::isfinite(prof.peakLon),
                 "Peak coordinates must be finite for collinear crimes");

        double totalProb = 0.0;
        for (const auto& row : prof.probabilitySurface)
            for (double v : row)
                totalProb += v;
        QVERIFY2(std::abs(totalProb - 1.0) < 1e-9, "Collinear surface must normalise to 1.0");
    }

    // The profile returned for a single crime is NOT symmetric (peak offset from site)
    // and the peak probability is plausible (not 0 or 1 degenerate)
    void testPeakProbabilityInRange()
    {
        GeographicProfiler gp(1.2, 1.2, 0.5, 20);
        auto prof = gp.profile(sites({{51.5, -0.1}}));

        // With a 20×20 grid, peak probability should be << 1 and > 1/(20*20)
        const double minExpectedPeak = 1.0 / (20.0 * 20.0);
        QVERIFY2(prof.peakProbability > minExpectedPeak,
                 qPrintable(QString("Peak probability %1 must be above uniform floor %2")
                            .arg(prof.peakProbability).arg(minExpectedPeak)));
        QVERIFY2(prof.peakProbability <= 1.0, "Peak probability must be <= 1");
    }

    // gridLats and gridLons must be sorted ascending and have correct size
    void testGridAxesSortedAndSized()
    {
        GeographicProfiler gp(1.2, 1.2, 0.5, 15);
        auto prof = gp.profile(sites({{51.5, -0.1}}));

        QCOMPARE(static_cast<int>(prof.gridLats.size()), 15);
        QCOMPARE(static_cast<int>(prof.gridLons.size()), 15);

        for (std::size_t i = 1; i < prof.gridLats.size(); ++i)
            QVERIFY2(prof.gridLats[i] > prof.gridLats[i-1], "gridLats must be ascending");
        for (std::size_t i = 1; i < prof.gridLons.size(); ++i)
            QVERIFY2(prof.gridLons[i] > prof.gridLons[i-1], "gridLons must be ascending");
    }
};

QTEST_GUILESS_MAIN(TestGeographicProfilerDeep2)
#include "test_geographic_profiler_deep2.moc"
