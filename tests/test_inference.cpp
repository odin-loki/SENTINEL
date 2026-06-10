// test_inference.cpp — Comprehensive Qt Test suite for SENTINEL inference engine.
// Covers: GeographicProfiler, MOAnalyser, EvidenceScorer, AnomalyDetector, HintEngine.

#include <QTest>
#include <QCoreApplication>
#include <QSet>
#include <cmath>
#include <algorithm>

#include "inference/GeographicProfiler.h"
#include "inference/MOAnalyser.h"
#include "inference/EvidenceScorer.h"
#include "inference/AnomalyDetector.h"
#include "inference/HintEngine.h"
#include "core/CrimeEvent.h"

// ── helpers ───────────────────────────────────────────────────────────────────

static AnomalyFeatureVector makeFeature(const QString& id,
                                         double lat, double lon,
                                         double tDays, double hourNorm,
                                         int typeCode = 0)
{
    AnomalyFeatureVector fv;
    fv.eventId       = id;
    fv.lat           = lat;
    fv.lon           = lon;
    fv.tDays         = tDays;
    fv.hourNorm      = hourNorm;
    fv.crimeTypeCode = typeCode;
    return fv;
}

static CrimeEvent makeBaseEvent()
{
    CrimeEvent ev;
    ev.eventId    = "EVT001";
    ev.id         = "EVT001";
    ev.crimeType  = "burglary";
    ev.latitude   = 51.5;
    ev.longitude  = -0.1;
    ev.occurredAt = QDateTime::currentDateTimeUtc();
    ev.timestamp  = ev.occurredAt.value();
    return ev;
}

static MOCaseRecord makeMOCase(const QString& id,
                                const QString& mo,
                                bool resolved = false)
{
    MOCaseRecord r;
    r.caseId   = id;
    r.moText   = mo;
    r.resolved = resolved;
    return r;
}

// ─────────────────────────────────────────────────────────────────────────────
// TestGeographicProfiler
// ─────────────────────────────────────────────────────────────────────────────

class TestGeographicProfiler : public QObject
{
    Q_OBJECT
private slots:
    void testProfileNormalization();
    void testProfileGridDimensions();
    void testPeakInsideBoundingBox();
    void testSearchAreas();
    void testSearchAreasPositive();
    void testSingleEventProfile();
    void testRossmoNearZone();
    void testRossmoFarZone();
    void testProfileEmptyEvents();
    void testClusteredEventsPeak();
    void testProbabilitySumAfterSmoothing();
    void testGridCoverage();
};

void TestGeographicProfiler::testProfileNormalization()
{
    GeographicProfiler profiler(1.2, 1.2, 1.0, 30);

    QVector<QPair<double,double>> locs = {
        {51.50, -0.10}, {51.51, -0.09}, {51.49, -0.11}, {51.505, -0.095}
    };
    auto gp = profiler.profile(locs);

    double sum = 0.0;
    for (const auto& row : gp.probabilitySurface)
        for (double v : row)
            sum += v;

    QVERIFY(qAbs(sum - 1.0) < 1e-6);
}

void TestGeographicProfiler::testProfileGridDimensions()
{
    GeographicProfiler profiler(1.2, 1.2, 1.0, 20);

    QVector<QPair<double,double>> locs = {
        {51.5, -0.1}, {51.52, -0.08}, {51.48, -0.12}
    };
    auto gp = profiler.profile(locs);

    QCOMPARE(static_cast<int>(gp.probabilitySurface.size()), 20);
    for (const auto& row : gp.probabilitySurface)
        QCOMPARE(static_cast<int>(row.size()), 20);

    QCOMPARE(static_cast<int>(gp.gridLats.size()), 20);
    QCOMPARE(static_cast<int>(gp.gridLons.size()), 20);
}

void TestGeographicProfiler::testPeakInsideBoundingBox()
{
    GeographicProfiler profiler(1.2, 1.2, 1.0, 30);

    QVector<QPair<double,double>> locs = {
        {51.5, -0.1}, {51.55, -0.05}, {51.45, -0.15}, {51.52, -0.08}
    };
    auto gp = profiler.profile(locs);

    // Peak must lie within the grid extent
    QVERIFY(!gp.gridLats.empty());
    QVERIFY(!gp.gridLons.empty());
    QVERIFY(gp.peakLat >= gp.gridLats.front() - 1e-9);
    QVERIFY(gp.peakLat <= gp.gridLats.back()  + 1e-9);
    QVERIFY(gp.peakLon >= gp.gridLons.front() - 1e-9);
    QVERIFY(gp.peakLon <= gp.gridLons.back()  + 1e-9);

    // Peak must not be wildly outside the original crime bbox (allow 1° margin)
    const double margin = 1.0;
    QVERIFY(gp.peakLat >= 51.45 - margin && gp.peakLat <= 51.55 + margin);
    QVERIFY(gp.peakLon >= -0.15 - margin && gp.peakLon <= -0.05 + margin);
}

void TestGeographicProfiler::testSearchAreas()
{
    GeographicProfiler profiler(1.2, 1.2, 1.0, 40);

    QVector<QPair<double,double>> locs = {
        {51.50, -0.10}, {51.51, -0.09}, {51.49, -0.11},
        {51.50, -0.10}, {51.505, -0.095}
    };
    auto gp = profiler.profile(locs);

    QVERIFY(gp.searchArea50pct < gp.searchArea80pct);
}

void TestGeographicProfiler::testSearchAreasPositive()
{
    GeographicProfiler profiler(1.2, 1.2, 1.0, 30);

    QVector<QPair<double,double>> locs = {
        {51.5, -0.1}, {51.51, -0.09}, {51.49, -0.11}
    };
    auto gp = profiler.profile(locs);

    QVERIFY(gp.searchArea50pct > 0.0);
    QVERIFY(gp.searchArea80pct > 0.0);
}

void TestGeographicProfiler::testSingleEventProfile()
{
    GeographicProfiler profiler(1.2, 1.2, 1.0, 30);

    QVector<QPair<double,double>> locs = {{51.5, -0.1}};
    auto gp = profiler.profile(locs);

    // Peak should be within a generous tolerance of the single event
    QVERIFY(qAbs(gp.peakLat - 51.5) < 1.0);
    QVERIFY(qAbs(gp.peakLon - (-0.1)) < 1.0);
}

void TestGeographicProfiler::testRossmoNearZone()
{
    // The near-zone (dist <= buffer) produces 1/dist^f contributions.
    // The cell nearest the crime location should carry a non-trivial probability.
    GeographicProfiler profiler(1.2, 1.2, 1.0, 50);

    QVector<QPair<double,double>> locs = {{51.5, -0.1}};
    auto gp = profiler.profile(locs);

    // Find the grid cell closest to the crime
    double minDist  = 1e9;
    double nearVal  = 0.0;
    for (int i = 0; i < static_cast<int>(gp.probabilitySurface.size()); ++i) {
        for (int j = 0; j < static_cast<int>(gp.probabilitySurface[i].size()); ++j) {
            double d = std::hypot(gp.gridLats[static_cast<size_t>(i)] - 51.5,
                                  gp.gridLons[static_cast<size_t>(j)] - (-0.1));
            if (d < minDist) { minDist = d; nearVal = gp.probabilitySurface[static_cast<size_t>(i)][static_cast<size_t>(j)]; }
        }
    }

    QVERIFY(nearVal > 0.0);
}

void TestGeographicProfiler::testRossmoFarZone()
{
    // The far-zone formula should yield lower probability than the near-zone.
    // Verify: the cell closest to the crime has >= probability than the grid corner.
    GeographicProfiler profiler(1.2, 1.2, 1.0, 50);

    QVector<QPair<double,double>> locs = {{51.5, -0.1}};
    auto gp = profiler.profile(locs);

    if (gp.probabilitySurface.empty()) return;

    // Cell nearest the crime
    double minDist = 1e9;
    double nearVal = 0.0;
    for (int i = 0; i < static_cast<int>(gp.probabilitySurface.size()); ++i) {
        for (int j = 0; j < static_cast<int>(gp.probabilitySurface[i].size()); ++j) {
            double d = std::hypot(gp.gridLats[static_cast<size_t>(i)] - 51.5,
                                  gp.gridLons[static_cast<size_t>(j)] - (-0.1));
            if (d < minDist) { minDist = d; nearVal = gp.probabilitySurface[static_cast<size_t>(i)][static_cast<size_t>(j)]; }
        }
    }

    // Grid corner (farthest from crime in the grid)
    double cornerVal = gp.probabilitySurface[0][0];

    // Near-zone cell should be at least as probable as the far corner
    QVERIFY(nearVal >= cornerVal - 1e-12);
}

void TestGeographicProfiler::testProfileEmptyEvents()
{
    // Must not crash; empty input → empty surface is acceptable
    GeographicProfiler profiler;
    QVector<QPair<double,double>> locs;
    auto gp = profiler.profile(locs);   // no crash
    // If a surface is returned it must still be valid
    if (!gp.probabilitySurface.empty()) {
        double sum = 0.0;
        for (const auto& row : gp.probabilitySurface)
            for (double v : row) sum += v;
        QVERIFY(sum <= 1.0 + 1e-6);
    }
    QVERIFY(true); // reached here without crash
}

void TestGeographicProfiler::testClusteredEventsPeak()
{
    GeographicProfiler profiler(1.2, 1.2, 1.0, 40);

    // 5 tightly clustered events near (51.5, -0.1) and 1 distant outlier
    QVector<QPair<double,double>> locs = {
        {51.500, -0.100}, {51.501, -0.099}, {51.499, -0.101},
        {51.500, -0.098}, {51.502, -0.102},
        {53.0,    1.0}    // outlier
    };
    auto gp = profiler.profile(locs);

    double distToCluster = std::hypot(gp.peakLat - 51.5, gp.peakLon - (-0.1));
    double distToOutlier = std::hypot(gp.peakLat - 53.0, gp.peakLon -   1.0);

    // Rossmo peak should anchor near the dense cluster
    QVERIFY(distToCluster < distToOutlier);
}

void TestGeographicProfiler::testProbabilitySumAfterSmoothing()
{
    // profile() applies Gaussian smoothing then re-normalises; sum must still be 1.
    GeographicProfiler profiler(1.2, 1.2, 1.0, 25);

    QVector<QPair<double,double>> locs = {
        {51.50, -0.10}, {51.52, -0.08}, {51.48, -0.12},
        {51.51, -0.11}, {51.49, -0.09}
    };
    auto gp = profiler.profile(locs);

    double sum = 0.0;
    for (const auto& row : gp.probabilitySurface)
        for (double v : row) sum += v;

    QVERIFY(qAbs(sum - 1.0) < 1e-6);
}

void TestGeographicProfiler::testGridCoverage()
{
    GeographicProfiler profiler(1.2, 1.2, 1.0, 30);

    QVector<QPair<double,double>> locs = {
        {51.5, -0.1}, {51.6, -0.05}, {51.4, -0.15}
    };
    const double minLat = 51.4, maxLat = 51.6;
    const double minLon = -0.15, maxLon = -0.05;

    auto gp = profiler.profile(locs);

    QVERIFY(!gp.gridLats.empty());
    QVERIFY(!gp.gridLons.empty());

    // Grid front (minimum) must be ≤ bbox min (grid extends at least to the crimes)
    QVERIFY(gp.gridLats.front() <= minLat + 1e-6);
    QVERIFY(gp.gridLats.back()  >= maxLat - 1e-6);
    QVERIFY(gp.gridLons.front() <= minLon + 1e-6);
    QVERIFY(gp.gridLons.back()  >= maxLon - 1e-6);
}

// ─────────────────────────────────────────────────────────────────────────────
// TestMOAnalyser
// ─────────────────────────────────────────────────────────────────────────────

class TestMOAnalyser : public QObject
{
    Q_OBJECT
private slots:
    void testCosineSimilarityIdentical();
    void testCosineSimilarityOrthogonal();
    void testCosineSimilarityEmpty();
    void testCosineSimilarityPartial();
    void testFitAndFindSimilar();
    void testFindSimilarTopK();
    void testFindSimilarMinSimilarity();
    void testFindSimilarEmpty();
    void testFindSimilarOrdering();
    void testFitEmptyCases();
};

void TestMOAnalyser::testCosineSimilarityIdentical()
{
    // Querying with the exact same text as a fitted case → similarity close to 1.
    MOAnalyser ana;
    const QString mo = "forced entry residential theft knife night solo victim elderly";
    ana.fit({ makeMOCase("C1", mo) });

    auto results = ana.findSimilar(mo, 5, 0.0);

    QVERIFY(!results.isEmpty());
    QVERIFY(results[0].similarityScore > 0.9);
}

void TestMOAnalyser::testCosineSimilarityOrthogonal()
{
    // Two cases with completely disjoint vocabulary: cross-similarity must be ≈ 0.
    MOAnalyser ana;
    ana.fit({
        makeMOCase("C1", "alpha beta gamma"),
        makeMOCase("C2", "delta epsilon zeta")
    });

    auto results = ana.findSimilar("delta epsilon zeta", 5, 0.0);

    for (const auto& m : results) {
        if (m.caseId == "C1") {
            QVERIFY(m.similarityScore < 0.05);
        }
        if (m.caseId == "C2") {
            QVERIFY(m.similarityScore > 0.5);
        }
    }
}

void TestMOAnalyser::testCosineSimilarityEmpty()
{
    // Empty query must not crash; results may be empty or have zero similarity.
    MOAnalyser ana;
    ana.fit({ makeMOCase("C1", "burglary residential forced entry night") });

    auto results = ana.findSimilar("", 5, 0.0);   // no crash
    for (const auto& m : results)
        QVERIFY(m.similarityScore >= 0.0);
}

void TestMOAnalyser::testCosineSimilarityPartial()
{
    // Partial vocabulary overlap → similarity strictly in (0, 1).
    MOAnalyser ana;
    ana.fit({
        makeMOCase("C1", "alpha beta gamma delta"),
        makeMOCase("C2", "beta gamma epsilon zeta")
    });

    auto results = ana.findSimilar("alpha beta gamma delta", 5, 0.0);

    for (const auto& m : results) {
        if (m.caseId == "C2") {
            QVERIFY(m.similarityScore > 0.0);
            QVERIFY(m.similarityScore < 1.0);
        }
    }
}

void TestMOAnalyser::testFitAndFindSimilar()
{
    MOAnalyser ana;
    ana.fit({
        makeMOCase("C1", "residential burglary forced entry back window night"),
        makeMOCase("C2", "vehicle theft carjacking armed daytime city"),
        makeMOCase("C3", "assault robbery street victim pedestrian")
    });

    auto results = ana.findSimilar(
        "residential burglary forced entry back window night", 5, 0.0);

    QVERIFY(!results.isEmpty());
    QVERIFY(results[0].similarityScore > 0.5);
}

void TestMOAnalyser::testFindSimilarTopK()
{
    MOAnalyser ana;
    ana.fit({
        makeMOCase("C1", "robbery street pedestrian knife night"),
        makeMOCase("C2", "theft vehicle window smash city"),
        makeMOCase("C3", "assault residential forced entry"),
        makeMOCase("C4", "fraud deception elderly victim phone"),
        makeMOCase("C5", "arson fire commercial premises night")
    });

    auto results = ana.findSimilar("robbery assault theft", 2, 0.0);

    QVERIFY(results.size() <= 2);
}

void TestMOAnalyser::testFindSimilarMinSimilarity()
{
    MOAnalyser ana;
    ana.fit({
        makeMOCase("C1", "alpha beta gamma"),
        makeMOCase("C2", "delta epsilon zeta"),
        makeMOCase("C3", "theta iota kappa")
    });

    // Query shares no vocabulary → all similarities are 0 → filtered by 0.99 threshold
    auto results = ana.findSimilar("completely unique unrelated words xyz qrst", 5, 0.99);

    QVERIFY(results.isEmpty());
}

void TestMOAnalyser::testFindSimilarEmpty()
{
    MOAnalyser ana;
    ana.fit({ makeMOCase("C1", "burglary residential night forced entry") });

    auto results = ana.findSimilar("", 5, 0.0);   // must not crash

    for (const auto& m : results)
        QVERIFY(m.similarityScore >= 0.0);
}

void TestMOAnalyser::testFindSimilarOrdering()
{
    MOAnalyser ana;
    ana.fit({
        makeMOCase("C1", "burglary residential forced entry night window"),
        makeMOCase("C2", "burglary commercial door kick daytime"),
        makeMOCase("C3", "assault street knife pedestrian victim")
    });

    auto results = ana.findSimilar("burglary residential forced entry", 5, 0.0);

    // Must be sorted descending by similarity
    for (int i = 1; i < results.size(); ++i)
        QVERIFY(results[i-1].similarityScore >= results[i].similarityScore - 1e-9);
}

void TestMOAnalyser::testFitEmptyCases()
{
    MOAnalyser ana;
    ana.fit({});                                   // no crash

    auto results = ana.findSimilar("test query burglary", 5, 0.0);  // no crash
    QVERIFY(results.size() >= 0);
}

// ─────────────────────────────────────────────────────────────────────────────
// TestEvidenceScorer
// ─────────────────────────────────────────────────────────────────────────────

class TestEvidenceScorer : public QObject
{
    Q_OBJECT
private slots:
    void testAvailableEvidenceTypes();
    void testGetLRKnown();
    void testGetLRUnknown();
    void testBayesianUpdateDNA();
    void testBayesianUpdateAlibi();
    void testLROneNoChange();
    void testMultipleEvidenceChain();
    void testAbsentEvidenceReduces();
    void testPosteriorProbabilityBounded();
    void testPriorOddsPreserved();
    void testNotes();
    void testEmptyEvidence();
    void testSequentialUpdateOrder();
    void testEyewitnessIdealLR();
    void testAlibiExculpatory();
};

void TestEvidenceScorer::testAvailableEvidenceTypes()
{
    EvidenceScorer scorer;
    QStringList types = scorer.availableEvidenceTypes();

    QVERIFY(types.size() >= 10);
    QVERIFY(types.contains("dna_match_full_profile"));
    QVERIFY(types.contains("eyewitness_identification_ideal"));
    QVERIFY(types.contains("fingerprint_match_10pt"));
    QVERIFY(types.contains("cctv_clear_face"));
    QVERIFY(types.contains("phone_records_at_scene"));
    QVERIFY(types.contains("alibi_strong"));
    QVERIFY(types.contains("modus_operandi_match_high"));
    QVERIFY(types.contains("geographic_profile_in_peak_zone"));
    QVERIFY(types.contains("prior_conviction_same_type"));
    QVERIFY(types.contains("vehicle_at_scene"));
}

void TestEvidenceScorer::testGetLRKnown()
{
    EvidenceScorer scorer;
    auto [lr, rel] = scorer.getLRAndReliability("fingerprint_match_10pt");

    QVERIFY(qAbs(lr  - 1e6 ) < 1.0);   // LR = 1 000 000
    QVERIFY(qAbs(rel - 0.99) < 1e-9);
}

void TestEvidenceScorer::testGetLRUnknown()
{
    EvidenceScorer scorer;
    auto [lr, rel] = scorer.getLRAndReliability("nonexistent_evidence_type");

    QVERIFY(qAbs(lr  - 1.0) < 1e-9);
    QVERIFY(qAbs(rel - 0.0) < 1e-9);
}

void TestEvidenceScorer::testBayesianUpdateDNA()
{
    EvidenceScorer scorer;
    // prior = 0.05; DNA LR = 1e9 → posterior ≈ 1.0
    QVector<EvidenceItem> ev = {{ "dna_match_full_profile", true }};
    auto results = scorer.score(ev, 0.05);

    QVERIFY(!results.isEmpty());
    QVERIFY(results.last().posteriorProbability > 0.99);
}

void TestEvidenceScorer::testBayesianUpdateAlibi()
{
    EvidenceScorer scorer;
    // prior = 0.5; alibi_strong LR = 0.05 → posterior ≈ 0.0476
    QVector<EvidenceItem> ev = {{ "alibi_strong", true }};
    auto results = scorer.score(ev, 0.5);

    QVERIFY(!results.isEmpty());
    QVERIFY(results.last().posteriorProbability < 0.1);
}

void TestEvidenceScorer::testLROneNoChange()
{
    EvidenceScorer scorer;
    // Unknown type → LR = 1 → posterior unchanged from prior
    QVector<EvidenceItem> ev = {{ "nonexistent_type_xyz", true }};
    auto results = scorer.score(ev, 0.3);

    QVERIFY(!results.isEmpty());
    QVERIFY(qAbs(results.last().posteriorProbability - 0.3) < 0.01);
}

void TestEvidenceScorer::testMultipleEvidenceChain()
{
    EvidenceScorer scorer;
    // eyewitness (LR=4) + CCTV (LR=50) + phone (LR=200), prior=0.1
    // → combined posterior > 0.95
    QVector<EvidenceItem> ev = {
        { "eyewitness_identification_ideal", true },
        { "cctv_clear_face",                 true },
        { "phone_records_at_scene",           true }
    };
    auto results = scorer.score(ev, 0.1);

    QVERIFY(!results.isEmpty());
    QVERIFY(results.last().posteriorProbability > 0.95);
}

void TestEvidenceScorer::testAbsentEvidenceReduces()
{
    EvidenceScorer scorer;
    // fingerprint ABSENT: LR used as 1/LR → posteriorProb << prior of 0.5
    QVector<EvidenceItem> ev = {{ "fingerprint_match_10pt", false }};
    auto results = scorer.score(ev, 0.5);

    QVERIFY(!results.isEmpty());
    QVERIFY(results.last().posteriorProbability < 0.01);
}

void TestEvidenceScorer::testPosteriorProbabilityBounded()
{
    EvidenceScorer scorer;
    QVector<EvidenceItem> ev = {
        { "dna_match_full_profile",         true  },
        { "alibi_strong",                   true  },
        { "eyewitness_identification_poor", false },
        { "modus_operandi_match_high",      true  }
    };
    auto results = scorer.score(ev, 0.2);

    for (const auto& r : results) {
        QVERIFY(r.posteriorProbability >= 0.0);
        QVERIFY(r.posteriorProbability <= 1.0);
    }
}

void TestEvidenceScorer::testPriorOddsPreserved()
{
    EvidenceScorer scorer;
    // LR = 1 (unknown type) → posterior == prior
    QVector<EvidenceItem> ev = {{ "unknown_type_xyz_123", true }};
    const double prior = 0.25;
    auto results = scorer.score(ev, prior);

    QVERIFY(!results.isEmpty());
    QVERIFY(qAbs(results.last().posteriorProbability - prior) < 0.01);
}

void TestEvidenceScorer::testNotes()
{
    EvidenceScorer scorer;
    QVector<EvidenceItem> ev = {
        { "cctv_clear_face", true  },   // present
        { "cctv_clear_face", false }    // absent
    };
    auto results = scorer.score(ev, 0.1);

    QVERIFY(results.size() == 2);

    // Present evidence: notes should be non-empty
    QVERIFY(!results[0].notes.isEmpty());

    // Absent evidence: notes should reference "Absent"
    QVERIFY(!results[1].notes.isEmpty());
    QVERIFY(results[1].notes.startsWith("Absent:") ||
            results[1].notes.contains("Absent", Qt::CaseInsensitive));
}

void TestEvidenceScorer::testEmptyEvidence()
{
    EvidenceScorer scorer;
    auto results = scorer.score({}, 0.3);

    QVERIFY(results.isEmpty());
}

void TestEvidenceScorer::testSequentialUpdateOrder()
{
    // Bayesian multiplication is commutative: order of evidence should not
    // change the final posterior probability.
    EvidenceScorer scorer;
    const double prior = 0.1;

    QVector<EvidenceItem> order1 = {
        { "dna_match_full_profile", true },
        { "alibi_strong",           true }
    };
    QVector<EvidenceItem> order2 = {
        { "alibi_strong",           true },
        { "dna_match_full_profile", true }
    };

    auto r1 = scorer.score(order1, prior);
    auto r2 = scorer.score(order2, prior);

    QVERIFY(!r1.isEmpty() && !r2.isEmpty());

    const double p1 = r1.last().posteriorProbability;
    const double p2 = r2.last().posteriorProbability;
    QVERIFY(qAbs(p1 - p2) < 1e-6);
}

void TestEvidenceScorer::testEyewitnessIdealLR()
{
    EvidenceScorer scorer;
    auto [lr, rel] = scorer.getLRAndReliability("eyewitness_identification_ideal");

    QVERIFY(qAbs(lr  - 4.0 ) < 1e-9);
    QVERIFY(qAbs(rel - 0.70) < 1e-9);
}

void TestEvidenceScorer::testAlibiExculpatory()
{
    EvidenceScorer scorer;
    // prior=0.5, alibi_strong LR=0.05
    // priorOdds = 1.0 → runningOdds = 0.05 → posteriorProb = 0.05/1.05 ≈ 0.04762
    QVector<EvidenceItem> ev = {{ "alibi_strong", true }};
    auto results = scorer.score(ev, 0.5);

    QVERIFY(!results.isEmpty());

    const double expected = 0.05 / (1.0 + 0.05);   // ≈ 0.047619
    QVERIFY(qAbs(results.last().posteriorProbability - expected) < 0.01);
}

// ─────────────────────────────────────────────────────────────────────────────
// TestAnomalyDetector
// ─────────────────────────────────────────────────────────────────────────────

class TestAnomalyDetector : public QObject
{
    Q_OBJECT
private slots:
    void testComputeZScoreNormal();
    void testComputeZScoreOffset();
    void testComputeZScoreZeroStd();
    void testDetectEmpty();
    void testDetectNonNegativeComposit();
    void testNormalEventsNotAnomaly();
    void testOutlierDetected();
    void testFitThenDetect();
};

void TestAnomalyDetector::testComputeZScoreNormal()
{
    // All events at the same location and time → mean equals every value → zScores = 0.
    AnomalyDetector det(0.05);

    QVector<AnomalyFeatureVector> data;
    for (int i = 0; i < 10; ++i)
        data.append(makeFeature(QString("E%1").arg(i), 51.5, -0.1, 100.0, 0.5));

    det.fit(data);
    auto anomalyResults = det.detectAnomalies(data);

    QVERIFY(!anomalyResults.isEmpty());
    for (const auto& s : anomalyResults) {
        QVERIFY(qAbs(s.zScoreTemporal) < 1e-6);
        QVERIFY(qAbs(s.zScoreSpatial)  < 1e-6);
    }
}

void TestAnomalyDetector::testComputeZScoreOffset()
{
    // An event far from the fitted temporal mean must produce a larger |zScoreTemporal|
    // than any of the normal events.
    AnomalyDetector det(0.05);

    QVector<AnomalyFeatureVector> data;
    for (int i = 0; i < 9; ++i)
        data.append(makeFeature(QString("N%1").arg(i), 51.5, -0.1, 100.0, 0.5));
    data.append(makeFeature("OUT", 51.5, -0.1, 10000.0, 0.5));   // extreme outlier

    det.fit(data);
    auto anomalyResults = det.detectAnomalies(data);

    double normalMaxZ   = 0.0;
    double outlierAbsZ  = 0.0;
    for (const auto& s : anomalyResults) {
        if (s.eventId == "OUT")
            outlierAbsZ = std::abs(s.zScoreTemporal);
        else
            normalMaxZ = std::max(normalMaxZ, std::abs(s.zScoreTemporal));
    }
    QVERIFY(outlierAbsZ >= normalMaxZ - 1e-6);
}

void TestAnomalyDetector::testComputeZScoreZeroStd()
{
    // All events identical → std = 0 → implementation must not produce NaN/Inf.
    AnomalyDetector det(0.05);

    QVector<AnomalyFeatureVector> data;
    for (int i = 0; i < 5; ++i)
        data.append(makeFeature(QString("E%1").arg(i), 51.5, -0.1, 100.0, 0.5));

    det.fit(data);
    auto anomalyResults = det.detectAnomalies(data);

    for (const auto& s : anomalyResults) {
        QVERIFY(std::isfinite(s.zScoreTemporal));
        QVERIFY(std::isfinite(s.zScoreSpatial));
        QVERIFY(std::isfinite(s.combinedScore));
    }
}

void TestAnomalyDetector::testDetectEmpty()
{
    AnomalyDetector det(0.05);

    QVector<AnomalyFeatureVector> data;
    for (int i = 0; i < 5; ++i)
        data.append(makeFeature(QString("E%1").arg(i), 51.5, -0.1, double(i), 0.5));

    det.fit(data);

    auto anomalyResults = det.detectAnomalies({});   // no crash
    QVERIFY(anomalyResults.isEmpty());
}

void TestAnomalyDetector::testDetectNonNegativeComposit()
{
    AnomalyDetector det(0.05);

    QVector<AnomalyFeatureVector> data;
    for (int i = 0; i < 10; ++i)
        data.append(makeFeature(QString("E%1").arg(i),
                                51.5 + i * 0.001, -0.1 + i * 0.001,
                                double(i) * 10.0, 0.5));

    det.fit(data);
    auto anomalyResults = det.detectAnomalies(data);

    for (const auto& s : anomalyResults)
        QVERIFY(s.combinedScore >= 0.0);
}

void TestAnomalyDetector::testNormalEventsNotAnomaly()
{
    AnomalyDetector det(0.05);

    // 20 near-identical events → very small spread → most should NOT be anomalies
    QVector<AnomalyFeatureVector> data;
    for (int i = 0; i < 20; ++i)
        data.append(makeFeature(QString("N%1").arg(i),
                                51.5 + i * 0.0001,
                               -0.1 + i * 0.0001,
                                100.0 + i * 0.1,
                                0.5));

    det.fit(data);
    auto anomalyResults = det.detectAnomalies(data);

    int anomalyCount = 0;
    for (const auto& s : anomalyResults)
        if (s.isAnomaly) ++anomalyCount;

    // Allow contamination-fraction (5 %) anomalies → at most 4 out of 20
    QVERIFY(anomalyCount <= 4);
}

void TestAnomalyDetector::testOutlierDetected()
{
    AnomalyDetector det(0.05);

    // Fit on 19 normal events
    QVector<AnomalyFeatureVector> normals;
    for (int i = 0; i < 19; ++i)
        normals.append(makeFeature(QString("N%1").arg(i),
                                   51.5, -0.1, 100.0 + i * 0.5, 0.5));
    det.fit(normals);

    // Detect on normals + 1 extreme temporal outlier
    AnomalyFeatureVector outlier = makeFeature("OUTLIER", 51.5, -0.1, 99999.0, 0.5);
    QVector<AnomalyFeatureVector> all = normals;
    all.append(outlier);

    auto anomalyResults = det.detectAnomalies(all);

    double outlierScore    = 0.0;
    double normalMaxScore  = 0.0;
    for (const auto& s : anomalyResults) {
        if (s.eventId == "OUTLIER")
            outlierScore = s.combinedScore;
        else
            normalMaxScore = std::max(normalMaxScore, s.combinedScore);
    }

    QVERIFY(outlierScore >= normalMaxScore);
}

void TestAnomalyDetector::testFitThenDetect()
{
    AnomalyDetector det(0.05);

    QVector<AnomalyFeatureVector> normals;
    for (int i = 0; i < 10; ++i)
        normals.append(makeFeature(QString("N%1").arg(i),
                                   51.5 + i * 0.001, -0.1,
                                   double(i) * 10.0, 0.5));
    det.fit(normals);

    // Detect on normals + a spatial/temporal outlier
    AnomalyFeatureVector outlier = makeFeature("O1", 55.0, 5.0, 50000.0, 0.9, 99);
    QVector<AnomalyFeatureVector> all = normals;
    all.append(outlier);

    auto anomalyResults = det.detectAnomalies(all);

    QCOMPARE(anomalyResults.size(), all.size());
    for (const auto& s : anomalyResults) {
        QVERIFY(!s.eventId.isEmpty());
        QVERIFY(std::isfinite(s.combinedScore));
        QVERIFY(s.combinedScore >= 0.0);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// TestHintEngine
// ─────────────────────────────────────────────────────────────────────────────

class TestHintEngine : public QObject
{
    Q_OBJECT
private slots:
    void testEmptyInput();
    void testLeadsHaveValidConfidence();
    void testLeadsHaveValidPriority();
    void testLeadsHaveNonEmptySummary();
    void testAnomalyLeads();
    void testSeriesLeads();
    void testLeadsSortedByPriority();
    void testMultipleLeadTypes();
};

static SeriesMatch makeSeriesMatch(const QString& id, int members, double prob)
{
    SeriesMatch sm;
    sm.seriesId        = id;
    sm.memberCount     = members;
    sm.linkProbability = prob;
    sm.compositeScore  = prob * 0.9;
    sm.method          = "spatial_temporal";
    return sm;
}

static AnomalySignal makeAnomalySignal(const QString& eventId, bool isAnomaly)
{
    AnomalySignal sig;
    sig.eventId       = eventId;
    sig.combinedScore = isAnomaly ? 0.95 : 0.1;
    sig.isAnomaly     = isAnomaly;
    sig.signalReasons = isAnomaly
                        ? std::vector<QString>{"temporal_outlier", "spatial_outlier"}
                        : std::vector<QString>{};
    return sig;
}

void TestHintEngine::testEmptyInput()
{
    HintEngine engine;
    HintEngineInput input;
    input.event = makeBaseEvent();

    auto leads = engine.generate(input);   // no crash

    // Empty or minimal leads; all must be well-formed if present
    for (const auto& lead : leads) {
        QVERIFY(lead.confidence >= 0.0 && lead.confidence <= 1.0);
        QVERIFY(lead.rank >= 0);
    }
}

void TestHintEngine::testLeadsHaveValidConfidence()
{
    HintEngine engine;
    HintEngineInput input;
    input.event = makeBaseEvent();
    input.seriesMatches.append(makeSeriesMatch("S1", 3, 0.80));
    input.moMatches.append([]{
        MOMatch m;
        m.caseId          = "C1";
        m.similarityScore = 0.75;
        m.resolved        = true;
        return m;
    }());

    auto leads = engine.generate(input);

    for (const auto& lead : leads) {
        QVERIFY(lead.confidence >= 0.0);
        QVERIFY(lead.confidence <= 1.0);
    }
}

void TestHintEngine::testLeadsHaveValidPriority()
{
    HintEngine engine;
    HintEngineInput input;
    input.event = makeBaseEvent();
    input.seriesMatches.append(makeSeriesMatch("S1", 5, 0.92));

    auto leads = engine.generate(input);

    for (const auto& lead : leads)
        QVERIFY(lead.rank >= 1);
}

void TestHintEngine::testLeadsHaveNonEmptySummary()
{
    HintEngine engine;
    HintEngineInput input;
    input.event = makeBaseEvent();
    input.seriesMatches.append(makeSeriesMatch("S1", 4, 0.85));

    auto leads = engine.generate(input);

    for (const auto& lead : leads)
        QVERIFY(!lead.headline.isEmpty() || !lead.detail.isEmpty());
}

void TestHintEngine::testAnomalyLeads()
{
    HintEngine engine;
    HintEngineInput input;
    input.event        = makeBaseEvent();
    input.anomalySignal = makeAnomalySignal("EVT001", true);

    auto leads = engine.generate(input);

    QVERIFY(!leads.isEmpty());

    bool found = false;
    for (const auto& lead : leads) {
        if (lead.category.contains("anomal", Qt::CaseInsensitive) ||
            lead.headline.contains("anomal", Qt::CaseInsensitive) ||
            lead.detail.contains("anomal", Qt::CaseInsensitive))
        {
            found = true;
            break;
        }
    }
    QVERIFY(found);
}

void TestHintEngine::testSeriesLeads()
{
    HintEngine engine;
    HintEngineInput input;
    input.event = makeBaseEvent();
    input.seriesMatches.append(makeSeriesMatch("SER001", 5, 0.92));

    auto leads = engine.generate(input);

    QVERIFY(!leads.isEmpty());

    bool found = false;
    for (const auto& lead : leads) {
        if (lead.category.contains("series", Qt::CaseInsensitive) ||
            lead.headline.contains("series", Qt::CaseInsensitive) ||
            lead.headline.contains("SER001", Qt::CaseSensitive)   ||
            lead.detail.contains("series",  Qt::CaseInsensitive)  ||
            lead.detail.contains("SER001",  Qt::CaseSensitive))
        {
            found = true;
            break;
        }
    }
    QVERIFY(found);
}

void TestHintEngine::testLeadsSortedByPriority()
{
    HintEngine engine;
    HintEngineInput input;
    input.event = makeBaseEvent();

    for (int i = 0; i < 3; ++i)
        input.seriesMatches.append(makeSeriesMatch(
            QString("S%1").arg(i), 2 + i, 0.5 + i * 0.15));

    {
        MOMatch m;
        m.caseId          = "C1";
        m.similarityScore = 0.8;
        m.resolved        = true;
        input.moMatches.append(m);
    }

    auto leads = engine.generate(input);

    if (leads.size() < 2) return;   // not enough leads to check ordering

    // Accept either rank-ascending or confidence-descending ordering
    bool rankAscending  = true;
    bool confidenceDesc = true;
    for (int i = 1; i < leads.size(); ++i) {
        if (leads[i].rank       < leads[i-1].rank)       rankAscending  = false;
        if (leads[i].confidence > leads[i-1].confidence) confidenceDesc = false;
    }
    QVERIFY(rankAscending || confidenceDesc);
}

void TestHintEngine::testMultipleLeadTypes()
{
    HintEngine engine;
    HintEngineInput input;
    input.event = makeBaseEvent();
    input.seriesMatches.append(makeSeriesMatch("S1", 4, 0.90));
    input.anomalySignal = makeAnomalySignal("EVT001", true);

    {
        MOMatch m;
        m.caseId          = "C1";
        m.similarityScore = 0.75;
        m.resolved        = false;
        input.moMatches.append(m);
    }

    auto leads = engine.generate(input);

    // Multiple inference inputs → expect at least 2 leads
    QVERIFY(leads.size() >= 2);

    // Verify all returned leads are well-formed
    for (const auto& lead : leads) {
        QVERIFY(lead.confidence >= 0.0 && lead.confidence <= 1.0);
        QVERIFY(lead.rank >= 0);
        QVERIFY(!lead.headline.isEmpty() || !lead.detail.isEmpty());
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// main
// ─────────────────────────────────────────────────────────────────────────────

static int runTest(QObject* obj, const char* logFile)
{
    QStringList args = { "test", "-o", QString("%1,txt").arg(logFile) };
    return QTest::qExec(obj, args);
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    int r = 0;
    TestGeographicProfiler t1; r |= runTest(&t1, "inf_geo.txt");
    TestMOAnalyser         t2; r |= runTest(&t2, "inf_mo.txt");
    TestEvidenceScorer     t3; r |= runTest(&t3, "inf_ev.txt");
    TestAnomalyDetector    t4; r |= runTest(&t4, "inf_anom.txt");
    TestHintEngine         t5; r |= runTest(&t5, "inf_hint.txt");
    return r;
}
#include "test_inference.moc"
