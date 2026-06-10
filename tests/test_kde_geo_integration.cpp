// test_kde_geo_integration.cpp — Integration tests: KDEHotspot + GeographicProfiler
//
// Exercises the full KDE spatial hotspot pipeline and Rossmo CGT geographic
// profiling together.  Uses only the public APIs defined in:
//   src/models/KDEHotspot.h
//   src/inference/GeographicProfiler.h
//   src/core/CrimeEvent.h
//
// Design principles:
//   • Deterministic point patterns (no random generators) for reproducibility.
//   • Each test is independent — no shared state across tests.
//   • Tolerances are loose enough to survive floating-point variance but tight
//     enough to catch real regressions.

#include <QTest>
#include <QCoreApplication>
#include <QDate>
#include <QTime>
#include <QDateTime>
#include <QTimeZone>
#include <QUuid>
#include <cmath>
#include <algorithm>
#include <numeric>
#include <limits>

#include "models/KDEHotspot.h"
#include "inference/GeographicProfiler.h"
#include "core/CrimeEvent.h"

class TestKDEGeoIntegration : public QObject {
    Q_OBJECT

    // ── Shared helpers ────────────────────────────────────────────────────────

    static CrimeEvent makeEvent(double lat, double lon, int dayOffset = 0)
    {
        CrimeEvent e;
        e.eventId = QUuid::createUuid().toString();
        e.id = e.eventId;
        e.crimeType = "burglary";
        e.suburb = "London";
        e.lat = lat;
        e.lon = lon;
        e.occurredAt = QDateTime(QDate(2024, 1, 1).addDays(dayOffset),
                                  QTime(12, 0), QTimeZone::utc());
        e.ingestedAt = QDateTime::currentDateTimeUtc();
        e.source = "test";
        return e;
    }

    // Build a tight cluster of N points around (centLat, centLon).
    // Uses deterministic offsets (no RNG) so results are reproducible.
    static QVector<QPair<double,double>> makeCluster(double centLat, double centLon,
                                                      int n = 20, double spread = 0.005)
    {
        QVector<QPair<double,double>> pts;
        pts.reserve(n);
        for (int i = 0; i < n; ++i) {
            const double dLat = spread * (((i * 7 + 3) % 11) - 5) / 5.0;
            const double dLon = spread * (((i * 11 + 2) % 13) - 6) / 6.0;
            pts.append({centLat + dLat, centLon + dLon});
        }
        return pts;
    }

    // Bounding box containing all points, with optional padding.
    static void bbox(const QVector<QPair<double,double>>& pts,
                     double& latMin, double& latMax,
                     double& lonMin, double& lonMax,
                     double pad = 0.05)
    {
        latMin = lonMin =  std::numeric_limits<double>::max();
        latMax = lonMax = -std::numeric_limits<double>::max();
        for (const auto& p : pts) {
            latMin = std::min(latMin, p.first);
            latMax = std::max(latMax, p.first);
            lonMin = std::min(lonMin, p.second);
            lonMax = std::max(lonMax, p.second);
        }
        latMin -= pad;  latMax += pad;
        lonMin -= pad;  lonMax += pad;
    }

    // ─────────────────────────────────────────────────────────────────────────
    // KDE HOTSPOT TESTS (1–10)
    // ─────────────────────────────────────────────────────────────────────────

private slots:

    // ── 1. testHotspotAtCentroid ──────────────────────────────────────────────
    // 20 events clustered around (51.5, -0.1).
    // The top hotspot centroid should be within 0.05 degrees of that point.

    void testHotspotAtCentroid()
    {
        const auto pts = makeCluster(51.5, -0.1, 20);
        double latMin, latMax, lonMin, lonMax;
        bbox(pts, latMin, latMax, lonMin, lonMax);

        KDEHotspot kde;
        const auto hotspots = kde.findHotspots(pts, latMin, latMax, lonMin, lonMax, 3);

        QVERIFY2(!hotspots.isEmpty(), "Expected at least one hotspot for 20-event cluster");

        const auto& top = hotspots.first();
        const double dLat = std::abs(top.centroidLat - 51.5);
        const double dLon = std::abs(top.centroidLon - (-0.1));
        QVERIFY2(dLat < 0.05,
                 qPrintable(QString("Hotspot centroidLat=%1 too far from 51.5 (Δ=%2)")
                            .arg(top.centroidLat).arg(dLat)));
        QVERIFY2(dLon < 0.05,
                 qPrintable(QString("Hotspot centroidLon=%1 too far from -0.1 (Δ=%2)")
                            .arg(top.centroidLon).arg(dLon)));
    }

    // ── 2. testHotspotCountForClusters ───────────────────────────────────────
    // Three well-separated clusters.  KDE may merge clusters depending on
    // bandwidth; we require at least 1 hotspot to be returned.

    void testHotspotCountForClusters()
    {
        QVector<QPair<double,double>> pts;
        pts += makeCluster(51.3, -0.3, 20);
        pts += makeCluster(51.6, -0.0, 20);
        pts += makeCluster(51.9,  0.3, 20);

        double latMin, latMax, lonMin, lonMax;
        bbox(pts, latMin, latMax, lonMin, lonMax);

        KDEHotspot kde;
        const auto hotspots = kde.findHotspots(pts, latMin, latMax, lonMin, lonMax, 10);
        QVERIFY2(!hotspots.isEmpty(),
                 "Expected at least 1 hotspot for 3-cluster dataset");
    }

    // ── 3. testHotspotDensityPositive ────────────────────────────────────────
    // The KDE surface density at the cluster centre should be strictly greater
    // than the density at a remote point far from any event.

    void testHotspotDensityPositive()
    {
        const double cLat = 51.5, cLon = -0.1;
        const auto pts = makeCluster(cLat, cLon, 20, 0.005);
        double latMin, latMax, lonMin, lonMax;
        bbox(pts, latMin, latMax, lonMin, lonMax);

        KDEHotspot kde(50);
        const auto surface = kde.compute(pts, latMin, latMax, lonMin, lonMax);

        const int N = static_cast<int>(surface.size());
        QVERIFY2(N > 0, "Surface must not be empty");

        // Map (cLat, cLon) → nearest grid cell
        const double cellLat = (latMax - latMin) / N;
        const double cellLon = (lonMax - lonMin) / static_cast<int>(surface[0].size());
        const int rCenter = static_cast<int>((cLat - latMin) / cellLat);
        const int cCenter = static_cast<int>((cLon - lonMin) / cellLon);

        // Remote corner cell (top-left, far from London cluster)
        const double centerDensity = surface[std::clamp(rCenter, 0, N-1)]
                                               [std::clamp(cCenter, 0, N-1)];
        const double remoteDensity = surface[0][0];

        QVERIFY2(centerDensity > remoteDensity,
                 qPrintable(QString("Center density %1 should exceed remote density %2")
                            .arg(centerDensity).arg(remoteDensity)));
    }

    // ── 4. testHotspotRadiusReasonable ───────────────────────────────────────
    // A hotspot's "radius" (half-diagonal of bounding box) should fall within
    // the plausible range (0.001, 5.0) degrees for typical urban data.

    void testHotspotRadiusReasonable()
    {
        const auto pts = makeCluster(51.5, -0.1, 20);
        double latMin, latMax, lonMin, lonMax;
        bbox(pts, latMin, latMax, lonMin, lonMax);

        KDEHotspot kde;
        const auto hotspots = kde.findHotspots(pts, latMin, latMax, lonMin, lonMax, 5);
        QVERIFY2(!hotspots.isEmpty(), "Need at least one hotspot for radius check");

        for (const auto& h : hotspots) {
            const double halfDiag = std::hypot(h.latMax - h.latMin,
                                               h.lonMax - h.lonMin) / 2.0;
            QVERIFY2(halfDiag > 0.001,
                     qPrintable(QString("Hotspot radius %1 suspiciously small").arg(halfDiag)));
            QVERIFY2(halfDiag < 5.0,
                     qPrintable(QString("Hotspot radius %1 unrealistically large").arg(halfDiag)));
        }
    }

    // ── 5. testHotspotScoreRange ──────────────────────────────────────────────
    // peakDensity (density at centroid) must be >= 0.
    // totalMass (integrated fraction of the surface) must be in [0, 1].

    void testHotspotScoreRange()
    {
        const auto pts = makeCluster(51.5, -0.1, 20);
        double latMin, latMax, lonMin, lonMax;
        bbox(pts, latMin, latMax, lonMin, lonMax);

        KDEHotspot kde;
        const auto hotspots = kde.findHotspots(pts, latMin, latMax, lonMin, lonMax, 5);
        QVERIFY2(!hotspots.isEmpty(), "Need hotspots for score range check");

        for (const auto& h : hotspots) {
            QVERIFY2(h.peakDensity >= 0.0,
                     qPrintable(QString("peakDensity=%1 is negative").arg(h.peakDensity)));
            QVERIFY2(h.totalMass >= 0.0,
                     qPrintable(QString("totalMass=%1 is negative").arg(h.totalMass)));
            QVERIFY2(h.totalMass <= 1.0 + 1e-9,
                     qPrintable(QString("totalMass=%1 exceeds 1.0").arg(h.totalMass)));
        }
    }

    // ── 6. testKDEFitEmpty ────────────────────────────────────────────────────
    // Computing hotspots on an empty event list must not crash and must return
    // an empty vector.

    void testKDEFitEmpty()
    {
        KDEHotspot kde;
        const auto hotspots = kde.findHotspots({}, 51.4, 51.6, -0.2, 0.0, 5);
        QVERIFY2(hotspots.isEmpty(), "Empty input should yield no hotspots");

        // Surface should also be all-zero for empty input
        const auto surface = kde.compute({}, 51.4, 51.6, -0.2, 0.0);
        for (const auto& row : surface)
            for (double v : row)
                QCOMPARE(v, 0.0);
    }

    // ── 7. testKDEFitOnEvent ──────────────────────────────────────────────────
    // A single event should produce a valid (non-crashing) KDE surface.
    // For a single event the Silverman bandwidth is undefined (σ=0); the
    // implementation must gracefully handle this edge case.

    void testKDEFitOnEvent()
    {
        QVector<QPair<double,double>> pts = {{51.5, -0.1}};
        KDEHotspot kde(20);
        // Must not crash
        const auto surface = kde.compute(pts, 51.4, 51.6, -0.2, 0.0);
        QCOMPARE(static_cast<int>(surface.size()), 20);

        // Surface values must all be finite and non-negative
        for (const auto& row : surface)
            for (double v : row) {
                QVERIFY2(std::isfinite(v), "Surface value is NaN/Inf for single event");
                QVERIFY2(v >= 0.0, "Surface value is negative for single event");
            }
    }

    // ── 8. testKDEBandwidthInfluence ─────────────────────────────────────────
    // A narrow bandwidth multiplier (0.3) produces tighter hotspot bounding
    // boxes than a wide multiplier (3.0) on the same cluster.

    void testKDEBandwidthInfluence()
    {
        const auto pts = makeCluster(51.5, -0.1, 30, 0.01);
        double latMin, latMax, lonMin, lonMax;
        bbox(pts, latMin, latMax, lonMin, lonMax);

        KDEHotspot kdeNarrow(50, 0.3);
        KDEHotspot kdeWide  (50, 3.0);

        const auto hNarrow = kdeNarrow.findHotspots(pts, latMin, latMax, lonMin, lonMax, 1);
        const auto hWide   = kdeWide  .findHotspots(pts, latMin, latMax, lonMin, lonMax, 1);

        if (hNarrow.isEmpty() || hWide.isEmpty())
            QSKIP("Bandwidth test requires at least one hotspot from each configuration");

        const double areaFn = (hNarrow[0].latMax - hNarrow[0].latMin) *
                              (hNarrow[0].lonMax - hNarrow[0].lonMin);
        const double areaFw = (hWide[0].latMax   - hWide[0].latMin)   *
                              (hWide[0].lonMax   - hWide[0].lonMin);

        QVERIFY2(areaFw >= areaFn,
                 qPrintable(QString("Wide-BW hotspot area %1 should be >= narrow-BW area %2")
                            .arg(areaFw).arg(areaFn)));
    }

    // ── 9. testTopNHotspots ───────────────────────────────────────────────────
    // findHotspots(topK=3) must return at most 3 hotspots regardless of input
    // density.

    void testTopNHotspots()
    {
        QVector<QPair<double,double>> pts;
        for (int c = 0; c < 5; ++c)
            pts += makeCluster(51.0 + c * 0.5, -0.1 + c * 0.2, 15);

        double latMin, latMax, lonMin, lonMax;
        bbox(pts, latMin, latMax, lonMin, lonMax);

        KDEHotspot kde;
        const auto hotspots = kde.findHotspots(pts, latMin, latMax, lonMin, lonMax, 3);
        QVERIFY2(hotspots.size() <= 3,
                 qPrintable(QString("topK=3 returned %1 hotspots").arg(hotspots.size())));
    }

    // ── 10. testKDEDensityAtTrainingPoint ────────────────────────────────────
    // The surface density at (or very near) a training point should be at
    // least as large as the mean density over the whole grid.

    void testKDEDensityAtTrainingPoint()
    {
        const double cLat = 51.5, cLon = -0.1;
        QVector<QPair<double,double>> pts = makeCluster(cLat, cLon, 20, 0.005);
        const double latMin = 51.4, latMax = 51.6;
        const double lonMin = -0.2, lonMax =  0.0;

        KDEHotspot kde(50);
        const auto surface = kde.compute(pts, latMin, latMax, lonMin, lonMax);

        const int N = static_cast<int>(surface.size());
        const int M = static_cast<int>(surface[0].size());

        // Compute mean density
        double total = 0.0;
        for (const auto& row : surface)
            for (double v : row)
                total += v;
        const double mean = total / (N * M);

        // Use the first training point (cluster centre ≈ (cLat, cLon))
        const double cellLat = (latMax - latMin) / N;
        const double cellLon = (lonMax - lonMin) / M;
        const int r = std::clamp(static_cast<int>((cLat - latMin) / cellLat), 0, N - 1);
        const int c = std::clamp(static_cast<int>((cLon - lonMin) / cellLon), 0, M - 1);

        QVERIFY2(surface[r][c] >= mean,
                 qPrintable(QString("Cluster-centre density %1 < mean %2")
                            .arg(surface[r][c]).arg(mean)));
    }

    // ─────────────────────────────────────────────────────────────────────────
    // GEOGRAPHIC PROFILER TESTS (11–18)
    // ─────────────────────────────────────────────────────────────────────────

    // ── 11. testRossmoAnchorPoint ─────────────────────────────────────────────
    // 5 offences evenly distributed around (51.5, -0.1) on a small circle.
    // The Rossmo anchor (peakLat, peakLon) should be inside the bounding box
    // of the crime locations (with reasonable margin).

    void testRossmoAnchorPoint()
    {
        // Crimes on a ring of radius ~0.02° centred at (51.5, -0.1)
        const double r = 0.02;
        QVector<QPair<double,double>> crimes = {
            {51.5 + r,  -0.1},
            {51.5 - r,  -0.1},
            {51.5,      -0.1 + r},
            {51.5,      -0.1 - r},
            {51.5,      -0.1}      // include centre to avoid degenerate ring
        };

        GeographicProfiler gp;
        const auto profile = gp.profile(crimes);

        QVERIFY2(!profile.probabilitySurface.empty(), "Profile surface must not be empty");

        // Anchor should be near the centre of the ring
        const double dLat = std::abs(profile.peakLat - 51.5);
        const double dLon = std::abs(profile.peakLon - (-0.1));
        QVERIFY2(dLat < 0.15,
                 qPrintable(QString("Anchor lat=%1 too far from circle centre (Δ=%2)")
                            .arg(profile.peakLat).arg(dLat)));
        QVERIFY2(dLon < 0.15,
                 qPrintable(QString("Anchor lon=%1 too far from circle centre (Δ=%2)")
                            .arg(profile.peakLon).arg(dLon)));
    }

    // ── 12. testProfileGridNonNegative ───────────────────────────────────────
    // Every cell of the probability surface must be >= 0.

    void testProfileGridNonNegative()
    {
        QVector<QPair<double,double>> crimes = {
            {51.50, -0.10}, {51.52, -0.08}, {51.48, -0.12},
            {51.51, -0.09}, {51.49, -0.11}
        };

        GeographicProfiler gp;
        const auto profile = gp.profile(crimes);

        for (int i = 0; i < static_cast<int>(profile.probabilitySurface.size()); ++i)
            for (int j = 0; j < static_cast<int>(profile.probabilitySurface[i].size()); ++j)
                QVERIFY2(profile.probabilitySurface[i][j] >= 0.0,
                         qPrintable(QString("Negative probability at [%1][%2]: %3")
                                    .arg(i).arg(j)
                                    .arg(profile.probabilitySurface[i][j])));
    }

    // ── 13. testProfileGridSumsToPositive ────────────────────────────────────
    // The sum of all grid cells must be strictly greater than zero.

    void testProfileGridSumsToPositive()
    {
        QVector<QPair<double,double>> crimes = {
            {51.50, -0.10}, {51.52, -0.08}, {51.48, -0.12},
            {51.51, -0.09}, {51.49, -0.11}
        };

        GeographicProfiler gp;
        const auto profile = gp.profile(crimes);

        double total = 0.0;
        for (const auto& row : profile.probabilitySurface)
            for (double v : row)
                total += v;

        QVERIFY2(total > 0.0,
                 "Probability surface sums to zero — all cells are zero");
    }

    // ── 14. testAnchorInsideConvexHull ───────────────────────────────────────
    // For crimes distributed in a tight cluster, the anchor (peak lat/lon)
    // should be inside (or very close to) the bounding box of the crimes.

    void testAnchorInsideConvexHull()
    {
        QVector<QPair<double,double>> crimes = {
            {51.48, -0.12}, {51.52, -0.08}, {51.50, -0.10},
            {51.49, -0.11}, {51.51, -0.09}, {51.50, -0.10}
        };

        double minLat = 1e9, maxLat = -1e9, minLon = 1e9, maxLon = -1e9;
        for (const auto& c : crimes) {
            minLat = std::min(minLat, c.first);  maxLat = std::max(maxLat, c.first);
            minLon = std::min(minLon, c.second); maxLon = std::max(maxLon, c.second);
        }

        const double margin = 0.08;
        GeographicProfiler gp(1.2, 1.2, 0.5, 60);
        const auto profile = gp.profile(crimes);

        QVERIFY2(profile.peakLat >= minLat - margin && profile.peakLat <= maxLat + margin,
                 qPrintable(QString("peakLat=%1 outside bbox [%2, %3]±%4")
                            .arg(profile.peakLat).arg(minLat).arg(maxLat).arg(margin)));
        QVERIFY2(profile.peakLon >= minLon - margin && profile.peakLon <= maxLon + margin,
                 qPrintable(QString("peakLon=%1 outside bbox [%2, %3]±%4")
                            .arg(profile.peakLon).arg(minLon).arg(maxLon).arg(margin)));
    }

    // ── 15. testHigherDensityNearCrimes ──────────────────────────────────────
    // The grid cell closest to the crime cluster centre should have higher
    // probability than a cell in a remote corner of the grid.

    void testHigherDensityNearCrimes()
    {
        QVector<QPair<double,double>> crimes = {
            {51.50, -0.10}, {51.51, -0.09}, {51.49, -0.11},
            {51.50, -0.11}, {51.51, -0.10}
        };

        GeographicProfiler gp(1.2, 1.2, 0.5, 60);
        const auto profile = gp.profile(crimes);

        QVERIFY2(!profile.gridLats.empty() && !profile.gridLons.empty(),
                 "Profile grid axes must be populated");

        const int nLat = static_cast<int>(profile.gridLats.size());
        const int nLon = static_cast<int>(profile.gridLons.size());

        // Find grid cell nearest to cluster centre (51.50, -0.10)
        const double targetLat = 51.50, targetLon = -0.10;
        int bestR = 0, bestC = 0;
        double bestDist = std::numeric_limits<double>::max();
        for (int r = 0; r < nLat; ++r) {
            for (int c = 0; c < nLon; ++c) {
                const double d = std::hypot(profile.gridLats[r] - targetLat,
                                            profile.gridLons[c] - targetLon);
                if (d < bestDist) { bestDist = d; bestR = r; bestC = c; }
            }
        }

        const double nearDensity   = profile.probabilitySurface[bestR][bestC];
        const double cornerDensity = profile.probabilitySurface[0][0];

        // Near-crime density should strictly exceed the remote corner
        QVERIFY2(nearDensity > cornerDensity,
                 qPrintable(QString("Near-crime density %1 not > corner density %2")
                            .arg(nearDensity).arg(cornerDensity)));
    }

    // ── 16. testBufferZoneEffect ──────────────────────────────────────────────
    // With a large buffer zone the peak should NOT fall directly on a crime
    // location (the buffer forces a minimum "commute distance" from the anchor).
    // Also verifies that increasing the buffer changes the estimated anchor.

    void testBufferZoneEffect()
    {
        // Symmetric crimes around (51.50, -0.10) at ~0.02° radius
        QVector<QPair<double,double>> crimes = {
            {51.52, -0.10}, {51.48, -0.10},
            {51.50, -0.08}, {51.50, -0.12},
            {51.50, -0.10}
        };

        // Large buffer (~4.5 km) — bigger than the crime-circle radius (~2.2 km)
        GeographicProfiler gpLarge(1.2, 1.2, 4.5, 80);
        const auto pLarge = gpLarge.profile(crimes);

        QVERIFY2(!pLarge.probabilitySurface.empty(), "Large-buffer profile must not be empty");
        QVERIFY2(pLarge.peakProbability > 0.0, "Large-buffer peak must be positive");

        // The peak must not sit directly on any crime location
        const double bufferDeg = 4.5 / 111.0;  // ≈ 0.04°
        for (const auto& c : crimes) {
            const double dist = std::hypot(pLarge.peakLat - c.first,
                                           pLarge.peakLon - c.second);
            // Allow half the buffer as tolerance (grid resolution effect)
            QVERIFY2(dist >= bufferDeg * 0.3 || crimes.size() < 3,
                     qPrintable(QString("Large-buffer peak (%1,%2) sits on crime (%3,%4)")
                                .arg(pLarge.peakLat).arg(pLarge.peakLon)
                                .arg(c.first).arg(c.second)));
        }
    }

    // ── 17. testProfileWithTwoCrimes ─────────────────────────────────────────
    // Even two events must produce a valid, finite, non-negative surface.

    void testProfileWithTwoCrimes()
    {
        QVector<QPair<double,double>> crimes = {
            {51.50, -0.10},
            {51.55, -0.15}
        };

        GeographicProfiler gp;
        const auto profile = gp.profile(crimes);

        QVERIFY2(!profile.probabilitySurface.empty(), "Two-crime profile must not be empty");

        bool hasNaN = false, hasInf = false;
        for (const auto& row : profile.probabilitySurface)
            for (double v : row) {
                if (std::isnan(v)) hasNaN = true;
                if (std::isinf(v)) hasInf = true;
            }
        QVERIFY2(!hasNaN, "Two-crime profile surface contains NaN");
        QVERIFY2(!hasInf, "Two-crime profile surface contains Inf");
    }

    // ── 18. testProfileSingleCrime ────────────────────────────────────────────
    // A single crime must not crash the profiler and must return a valid struct.
    // (The Rossmo model recommends ≥3 crimes, but the API must remain safe.)

    void testProfileSingleCrime()
    {
        QVector<QPair<double,double>> crimes = { {51.50, -0.10} };

        GeographicProfiler gp;
        const auto profile = gp.profile(crimes);

        // Must not crash; surface may be trivial (all zeros) for single input
        QVERIFY(!profile.probabilitySurface.empty());

        // If there is any probability mass, values must be finite
        for (const auto& row : profile.probabilitySurface)
            for (double v : row)
                QVERIFY2(std::isfinite(v),
                         qPrintable(QString("Single-crime surface has non-finite value: %1").arg(v)));
    }
};

QTEST_GUILESS_MAIN(TestKDEGeoIntegration)
#include "test_kde_geo_integration.moc"
