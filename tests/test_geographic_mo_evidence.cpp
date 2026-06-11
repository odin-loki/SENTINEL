// test_geographic_mo_evidence.cpp
// Iteration-3 audit tests for GeographicProfiler, MOAnalyser, and EvidenceScorer.
// Mathematical verification: Rossmo CGT normalization/search areas, TF-IDF cosine
// similarity, and Bayesian LR / posterior updating.

#include <QTest>
#include <QTimeZone>
#include <cmath>
#include <algorithm>
#include <numeric>

#include "inference/GeographicProfiler.h"
#include "inference/MOAnalyser.h"
#include "inference/EvidenceScorer.h"
#include "core/CrimeEvent.h"

namespace {

using Pt = QPair<double, double>;

static MOCaseRecord makeCase(const QString& id, const QString& mo,
                              bool resolved = false)
{
    MOCaseRecord r;
    r.caseId   = id;
    r.moText   = mo;
    r.resolved = resolved;
    return r;
}

static double surfaceSum(const GeographicProfile& gp)
{
    double total = 0.0;
    for (const auto& row : gp.probabilitySurface)
        for (double v : row)
            total += v;
    return total;
}

static double distDeg(double lat1, double lon1, double lat2, double lon2)
{
    const double dLat = lat1 - lat2;
    const double dLon = lon1 - lon2;
    return std::sqrt(dLat * dLat + dLon * dLon);
}

// Reference UTC timestamp for audit traceability (matches project test convention).
static const QDateTime kAuditRefTime =
    QDateTime(QDate(2024, 6, 15), QTime(12, 0, 0), QTimeZone::utc());

} // namespace

class GeoMOEvidenceTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        QVERIFY(kAuditRefTime.isValid());
        QVERIFY(kAuditRefTime.timeZone() == QTimeZone::utc());
    }

    // ── GeographicProfiler ───────────────────────────────────────────────────

    void testRossmoMinimumTwoEvents()
    {
        GeographicProfiler gp(1.2, 1.2, 0.5, 40);
        QVector<Pt> crimes;
        crimes.append({ 51.5074, -0.1278 });
        crimes.append({ 51.5174, -0.1178 });

        const auto result = gp.profile(crimes);

        QVERIFY2(!result.probabilitySurface.empty(),
                 "Two-event profile must produce a non-empty surface");
        QVERIFY2(result.peakProbability > 0.0,
                 "Two-event profile must have positive peak probability");
        QVERIFY2(result.searchArea50pct >= 0.0,
                 "searchArea50pct must be non-negative");
        QVERIFY2(result.searchArea80pct >= 0.0,
                 "searchArea80pct must be non-negative");
        QCOMPARE(result.method, QStringLiteral("rossmo_cgt"));
    }

    void testRossmoSearchArea50LessThan80()
    {
        GeographicProfiler gp;
        QVector<Pt> crimes;
        for (int i = 0; i < 6; ++i)
            crimes.append({ 51.50 + i * 0.008, -0.12 + i * 0.006 });

        const auto result = gp.profile(crimes);
        QVERIFY2(result.searchArea50pct <= result.searchArea80pct,
                 qPrintable(QStringLiteral("50%% area (%1 km²) must be <= 80%% area (%2 km²)")
                                .arg(result.searchArea50pct)
                                .arg(result.searchArea80pct)));
    }

    void testRossmoGridNormalized()
    {
        GeographicProfiler gp;
        QVector<Pt> crimes;
        for (int i = 0; i < 5; ++i)
            crimes.append({ 51.5074 + i * 0.01, -0.1278 + i * 0.01 });

        const auto result = gp.profile(crimes);
        const double total = surfaceSum(result);
        QVERIFY2(std::abs(total - 1.0) < 1e-6,
                 qPrintable(QStringLiteral("Normalised surface sum %1, expected ~1.0")
                                .arg(total)));
    }

    void testRossmoMarauderPeak()
    {
        const double cLat = 51.50;
        const double cLon = -0.10;
        const double r = 0.005;

        QVector<Pt> crimes;
        for (int i = 0; i < 8; ++i) {
            const double angle = 2.0 * M_PI * i / 8.0;
            crimes.append({ cLat + r * std::cos(angle), cLon + r * std::sin(angle) });
        }

        GeographicProfiler gp(1.2, 1.2, 0.2, 80);
        const auto result = gp.profile(crimes);

        const double peakDist = distDeg(result.peakLat, result.peakLon, cLat, cLon);
        QVERIFY2(peakDist < 0.08,
                 qPrintable(QStringLiteral("Peak (%1,%2) should be near cluster centre; dist=%3 deg")
                                .arg(result.peakLat)
                                .arg(result.peakLon)
                                .arg(peakDist)));
    }

    void testRossmoMeanIsWeighted()
    {
        // Dense cluster (6 events) vs sparse outlier (1 event) — peak should lean toward dense side.
        const double denseLat = 51.50;
        const double denseLon = -0.12;
        const double sparseLat = 51.58;
        const double sparseLon = -0.05;

        QVector<Pt> crimes;
        for (int i = 0; i < 6; ++i)
            crimes.append({ denseLat + i * 0.001, denseLon + i * 0.001 });
        crimes.append({ sparseLat, sparseLon });

        GeographicProfiler gp(1.2, 1.2, 0.3, 80);
        const auto result = gp.profile(crimes);

        const double distDense = distDeg(result.peakLat, result.peakLon, denseLat, denseLon);
        const double distSparse = distDeg(result.peakLat, result.peakLon, sparseLat, sparseLon);
        QVERIFY2(distDense < distSparse,
                 qPrintable(QStringLiteral("Peak should be nearer dense cluster "
                                            "(distDense=%1, distSparse=%2)")
                                .arg(distDense)
                                .arg(distSparse)));
    }

    void testRossmoPeakProbPositive()
    {
        GeographicProfiler gp;
        const auto result = gp.profile({ { 51.5074, -0.1278 },
                                         { 51.5174, -0.1178 },
                                         { 51.5274, -0.1078 } });
        QVERIFY2(result.peakProbability > 0.0,
                 qPrintable(QStringLiteral("peakProbability must be > 0, got %1")
                                .arg(result.peakProbability)));

        double maxCell = 0.0;
        for (const auto& row : result.probabilitySurface)
            for (double v : row)
                maxCell = std::max(maxCell, v);
        QVERIFY2(std::abs(result.peakProbability - maxCell) < 1e-12,
                 "peakProbability must equal maximum grid cell");
    }

    // ── MOAnalyser ─────────────────────────────────────────────────────────

    void testMOAnalyserCosineSimilarityRange()
    {
        QVector<MOCaseRecord> cases;
        cases << makeCase(QStringLiteral("c1"),
                          QStringLiteral("burglary residential night window forced entry"))
              << makeCase(QStringLiteral("c2"),
                          QStringLiteral("robbery street knife night cash"))
              << makeCase(QStringLiteral("c3"),
                          QStringLiteral("arson commercial accelerant night"));

        MOAnalyser analyser;
        analyser.fit(cases);

        const auto matches = analyser.findSimilar(
            QStringLiteral("burglary residential night"), 10, 0.0);
        for (const auto& m : matches) {
            QVERIFY2(m.similarityScore >= 0.0 && m.similarityScore <= 1.0,
                     qPrintable(QStringLiteral("Similarity %1 must be in [0,1]")
                                    .arg(m.similarityScore)));
        }
    }

    void testMOAnalyserSameTextPerfectSim()
    {
        const QString mo =
            QStringLiteral("forced entry night crowbar residential jewels gloves solo masked");

        QVector<MOCaseRecord> cases;
        cases << makeCase(QStringLiteral("exact"), mo)
              << makeCase(QStringLiteral("other"),
                          QStringLiteral("vehicle theft carpark daytime electronics"));

        MOAnalyser analyser;
        analyser.fit(cases);

        const auto matches = analyser.findSimilar(mo, 5, 0.0);
        QVERIFY(!matches.isEmpty());
        QCOMPARE(matches.first().caseId, QStringLiteral("exact"));
        QVERIFY2(matches.first().similarityScore > 0.99,
                 qPrintable(QStringLiteral("Identical MO should yield ~1.0, got %1")
                                .arg(matches.first().similarityScore)));
    }

    void testMOAnalyserDifferentTextZeroSim()
    {
        QVector<MOCaseRecord> cases;
        cases << makeCase(QStringLiteral("c1"),
                          QStringLiteral("alpha beta gamma delta epsilon"))
              << makeCase(QStringLiteral("c2"),
                          QStringLiteral("zeta eta theta iota kappa"));

        MOAnalyser analyser;
        analyser.fit(cases);

        const auto matches = analyser.findSimilar(
            QStringLiteral("xyzzy plugh qwerty nonsense"), 5, 0.0);
        QVERIFY2(matches.isEmpty() ||
                     std::all_of(matches.cbegin(), matches.cend(),
                                 [](const MOMatch& m) {
                                     return m.similarityScore < 0.01;
                                 }),
                 "Disjoint vocabulary should yield empty or ~zero similarity");
    }

    void testMOAnalyserResolvedBoost()
    {
        const QString caseMo =
            QStringLiteral("burglary residential night window forced entry crowbar jewels");
        const QString queryMo =
            QStringLiteral("burglary residential night window forced entry crowbar");

        QVector<MOCaseRecord> cases;
        cases << makeCase(QStringLiteral("resolved"), caseMo, true)
              << makeCase(QStringLiteral("unresolved"), caseMo, false);

        MOAnalyser analyser;
        analyser.fit(cases);

        const auto matches = analyser.findSimilar(queryMo, 10, 0.0);
        QCOMPARE(matches.size(), 2);

        double resolvedScore = 0.0;
        double unresolvedScore = 0.0;
        for (const auto& m : matches) {
            if (m.caseId == QStringLiteral("resolved"))
                resolvedScore = m.similarityScore;
            if (m.caseId == QStringLiteral("unresolved"))
                unresolvedScore = m.similarityScore;
        }

        QVERIFY2(unresolvedScore > 0.0 && unresolvedScore < 1.0,
                 "Query should partially match corpus MO (raw sim in (0,1))");
        QVERIFY2(resolvedScore > unresolvedScore,
                 qPrintable(QStringLiteral("Resolved boost: resolved=%1 unresolved=%2")
                                .arg(resolvedScore)
                                .arg(unresolvedScore)));
        QVERIFY2(resolvedScore <= 1.0, "Boosted score must be capped at 1.0");
    }

    void testMOAnalyserTopKLimit()
    {
        QVector<MOCaseRecord> cases;
        for (int i = 0; i < 10; ++i) {
            cases << makeCase(QStringLiteral("c%1").arg(i),
                              QStringLiteral("robbery street night variant token_%1").arg(i));
        }

        MOAnalyser analyser;
        analyser.fit(cases);

        const int k = 3;
        const auto matches = analyser.findSimilar(
            QStringLiteral("robbery street night"), k, 0.0);
        QVERIFY2(matches.size() <= k,
                 qPrintable(QStringLiteral("findSimilar must return at most %1, got %2")
                                .arg(k)
                                .arg(matches.size())));
    }

    void testMOAnalyserEmptyCaseBase()
    {
        MOAnalyser analyser;
        QVERIFY(!analyser.isFitted());

        const auto matches = analyser.findSimilar(
            QStringLiteral("any query text"), 10, 0.0);
        QVERIFY2(matches.isEmpty(),
                 "findSimilar with zero fitted cases must return empty vector");
    }

    // ── EvidenceScorer ───────────────────────────────────────────────────────

    void testEvidenceScorerLRPositive()
    {
        EvidenceScorer scorer;
        const auto types = scorer.availableEvidenceTypes();
        QVERIFY2(types.size() >= 5, "Expected at least 5 evidence types");

        for (const QString& type : types) {
            const auto [lr, rel] = scorer.getLRAndReliability(type);
            Q_UNUSED(rel)
            QVERIFY2(lr > 0.0,
                     qPrintable(QStringLiteral("LR for '%1' must be positive, got %2")
                                    .arg(type)
                                    .arg(lr)));
        }
    }

    void testEvidenceScorerBayesFactor()
    {
        EvidenceScorer scorer;
        QMap<QString, bool> ev;
        ev[QStringLiteral("modus_operandi_match_high")] = true;

        const double prior = 0.10;
        const auto res = scorer.score(prior, ev);

        const double priorOdds = prior / (1.0 - prior);
        const double postOdds =
            res.posteriorProbability / (1.0 - res.posteriorProbability);
        const double oddsMultiplier = postOdds / priorOdds;

        // Bayes update: posterior odds = prior odds × overall LR
        QVERIFY2(std::abs(oddsMultiplier - res.overallLikelihoodRatio) < 1e-9,
                 qPrintable(QStringLiteral("Odds multiplier %1 should equal overall LR %2")
                                .arg(oddsMultiplier)
                                .arg(res.overallLikelihoodRatio)));

        const double expectedLR = 8.0; // modus_operandi_match_high table entry
        QVERIFY2(std::abs(res.overallLikelihoodRatio - expectedLR) < 1e-6,
                 qPrintable(QStringLiteral("Overall LR should be %1, got %2")
                                .arg(expectedLR)
                                .arg(res.overallLikelihoodRatio)));
    }

    void testEvidenceScorerPosteriorProbInRange()
    {
        EvidenceScorer scorer;
        QMap<QString, bool> ev;
        ev[QStringLiteral("fingerprint_match_10pt")] = true;
        ev[QStringLiteral("alibi_strong")] = true;

        const auto res = scorer.score(0.25, ev);
        QVERIFY2(res.posteriorProbability >= 0.0 && res.posteriorProbability <= 1.0,
                 qPrintable(QStringLiteral("Posterior %1 must be in [0,1]")
                                .arg(res.posteriorProbability)));
    }

    void testEvidenceScorerAvailableTypes()
    {
        EvidenceScorer scorer;
        const auto types = scorer.availableEvidenceTypes();
        QVERIFY2(types.size() >= 5,
                 qPrintable(QStringLiteral("Expected >= 5 evidence types, got %1")
                                .arg(types.size())));
        QVERIFY2(scorer.evidenceTypeCount() == types.size(),
                 "evidenceTypeCount must match availableEvidenceTypes size");
    }

    void testEvidenceScorerHighLRIncreasesProb()
    {
        EvidenceScorer scorer;
        const double prior = 0.05;

        QMap<QString, bool> strong;
        strong[QStringLiteral("dna_match_full_profile")] = true;

        const auto res = scorer.score(prior, strong);
        QVERIFY2(res.posteriorProbability > prior,
                 qPrintable(QStringLiteral("High LR evidence: posterior %1 should exceed prior %2")
                                .arg(res.posteriorProbability)
                                .arg(prior)));
        QVERIFY2(res.posteriorProbability > 0.99,
                 "DNA full profile should push posterior near certainty");
    }

    void testEvidenceScorerLowLRDecreasesProb()
    {
        EvidenceScorer scorer;
        const double prior = 0.80;

        QMap<QString, bool> exculpatory;
        exculpatory[QStringLiteral("alibi_strong")] = true;

        const auto res = scorer.score(prior, exculpatory);
        QVERIFY2(res.posteriorProbability < prior,
                 qPrintable(QStringLiteral("LR<1 evidence: posterior %1 should be below prior %2")
                                .arg(res.posteriorProbability)
                                .arg(prior)));
        QVERIFY2(res.overallLikelihoodRatio < 1.0,
                 "alibi_strong LR (0.05) should yield overall LR < 1");
    }
};

QTEST_MAIN(GeoMOEvidenceTest)
#include "test_geographic_mo_evidence.moc"
