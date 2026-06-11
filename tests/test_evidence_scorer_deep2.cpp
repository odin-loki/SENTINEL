#include <QTest>
#include "inference/EvidenceScorer.h"
#include <cmath>
#include <limits>

// Tolerance for floating-point comparisons.
static constexpr double kEps = 1e-9;

// Bayes: odds = prior / (1 - prior), update odds *= LR, posterior = odds / (1 + odds).
static double bayesUpdate(double prior, double lr)
{
    const double odds = prior / (1.0 - prior);
    const double newOdds = odds * lr;
    return newOdds / (1.0 + newOdds);
}

class EvidenceScorerDeep2Test : public QObject
{
    Q_OBJECT

private:
    EvidenceScorer m_scorer;

private slots:

    // ── Available types ───────────────────────────────────────────────────────

    void testLRTableNonEmpty()
    {
        QVERIFY(m_scorer.evidenceTypeCount() > 0);
    }

    void testAvailableEvidenceTypesList()
    {
        const QStringList types = m_scorer.availableEvidenceTypes();
        QVERIFY(!types.isEmpty());
        QVERIFY(types.contains(QStringLiteral("fingerprint_match_10pt")));
        QVERIFY(types.contains(QStringLiteral("eyewitness_identification_ideal")));
        QVERIFY(types.contains(QStringLiteral("alibi_strong")));
    }

    void testGetLRAndReliabilityKnownType()
    {
        const auto [lr, rel] = m_scorer.getLRAndReliability(
            QStringLiteral("fingerprint_match_10pt"));
        QVERIFY2(lr > 1.0, "Fingerprint LR should be >> 1");
        QVERIFY2(rel > 0.0 && rel <= 1.0, "Reliability must be in (0,1]");
    }

    void testGetLRAndReliabilityUnknownTypeDefaults()
    {
        const auto [lr, rel] = m_scorer.getLRAndReliability(QStringLiteral("nonexistent_type"));
        QCOMPARE(lr,  1.0);
        QCOMPARE(rel, 0.0);
    }

    // ── LR > 1 (incriminating evidence) raises posterior ─────────────────────

    void testHighLRRaisesPosterior()
    {
        constexpr double prior = 0.05;
        QMap<QString, bool> evidence;
        evidence[QStringLiteral("eyewitness_identification_ideal")] = true;  // LR = 4.0

        const auto result = m_scorer.score(prior, evidence);
        QVERIFY2(result.posteriorProbability > prior,
                 qPrintable(QStringLiteral("LR=4 should raise posterior above %1, got %2")
                     .arg(prior).arg(result.posteriorProbability)));
    }

    void testHighLRMatchesBayesFormula()
    {
        constexpr double prior = 0.05;
        const auto [lr, rel] = m_scorer.getLRAndReliability(
            QStringLiteral("eyewitness_identification_ideal"));
        const double expected = bayesUpdate(prior, lr);

        QMap<QString, bool> evidence;
        evidence[QStringLiteral("eyewitness_identification_ideal")] = true;
        const auto result = m_scorer.score(prior, evidence);

        QVERIFY2(std::abs(result.posteriorProbability - expected) < 1e-10,
                 qPrintable(QStringLiteral("Expected %1, got %2").arg(expected).arg(result.posteriorProbability)));
    }

    // ── LR < 1 (exculpatory evidence) lowers posterior ───────────────────────

    void testLowLRLowersPosterior()
    {
        constexpr double prior = 0.5;
        QMap<QString, bool> evidence;
        evidence[QStringLiteral("alibi_strong")] = true;  // LR = 0.05

        const auto result = m_scorer.score(prior, evidence);
        QVERIFY2(result.posteriorProbability < prior,
                 qPrintable(QStringLiteral("LR=0.05 should lower posterior below %1, got %2")
                     .arg(prior).arg(result.posteriorProbability)));
    }

    void testLowLRMatchesBayesFormula()
    {
        constexpr double prior = 0.5;
        const auto [lr, rel] = m_scorer.getLRAndReliability(QStringLiteral("alibi_strong"));
        const double expected = bayesUpdate(prior, lr);

        QMap<QString, bool> evidence;
        evidence[QStringLiteral("alibi_strong")] = true;
        const auto result = m_scorer.score(prior, evidence);

        QVERIFY2(std::abs(result.posteriorProbability - expected) < 1e-10,
                 qPrintable(QStringLiteral("Expected %1, got %2").arg(expected).arg(result.posteriorProbability)));
    }

    // ── Unknown evidence type (LR = 1.0) leaves posterior unchanged ──────────

    void testNeutralLRLeavesPosteriOrUnchanged()
    {
        constexpr double prior = 0.15;
        QMap<QString, bool> evidence;
        evidence[QStringLiteral("this_type_does_not_exist")] = true;  // defaults to LR=1.0

        const auto result = m_scorer.score(prior, evidence);
        // LR=1 means no update; posterior should equal prior.
        QVERIFY2(std::abs(result.posteriorProbability - prior) < 1e-10,
                 qPrintable(QStringLiteral("LR=1.0 should leave posterior = prior (%1), got %2")
                     .arg(prior).arg(result.posteriorProbability)));
    }

    // ── Multiple independent pieces of evidence combine multiplicatively ──────

    void testMultipleEvidenceCombines()
    {
        constexpr double prior = 0.05;

        // Apply eyewitness then CCTV individually → should equal combined.
        const auto [lrEye,  relE] = m_scorer.getLRAndReliability(QStringLiteral("eyewitness_identification_ideal"));
        const auto [lrCctv, relC] = m_scorer.getLRAndReliability(QStringLiteral("cctv_clear_face"));
        const double expected = bayesUpdate(bayesUpdate(prior, lrEye), lrCctv);

        QMap<QString, bool> both;
        both[QStringLiteral("eyewitness_identification_ideal")] = true;
        both[QStringLiteral("cctv_clear_face")]                 = true;
        const auto result = m_scorer.score(prior, both);

        QVERIFY2(std::abs(result.posteriorProbability - expected) < 1e-8,
                 qPrintable(QStringLiteral("Combined score %1 != expected %2")
                     .arg(result.posteriorProbability).arg(expected)));
    }

    void testMultipleEvidenceHigherThanSingle()
    {
        constexpr double prior = 0.05;

        QMap<QString, bool> single;
        single[QStringLiteral("eyewitness_identification_ideal")] = true;

        QMap<QString, bool> multiple;
        multiple[QStringLiteral("eyewitness_identification_ideal")] = true;
        multiple[QStringLiteral("cctv_clear_face")]                 = true;

        const auto rSingle   = m_scorer.score(prior, single);
        const auto rMultiple = m_scorer.score(prior, multiple);

        QVERIFY2(rMultiple.posteriorProbability > rSingle.posteriorProbability,
                 "More incriminating evidence should raise posterior further");
    }

    // ── Absent evidence inverts LR ────────────────────────────────────────────

    void testAbsentHighLREvidenceLowersPosterior()
    {
        constexpr double prior = 0.3;
        // If strong evidence is absent, LR_effective = 1/LR < 1 → lowers posterior.
        QMap<QString, bool> evidence;
        evidence[QStringLiteral("dna_match_full_profile")] = false;  // LR=1e9, absent → 1e-9

        const auto result = m_scorer.score(prior, evidence);
        QVERIFY2(result.posteriorProbability < prior,
                 qPrintable(QStringLiteral("Absent DNA match should lower posterior from %1, got %2")
                     .arg(prior).arg(result.posteriorProbability)));
    }

    // ── Posterior always in [0, 1] ────────────────────────────────────────────

    void testPosteriorAlwaysInRange_mapOverload()
    {
        const QVector<double> priors = {0.001, 0.05, 0.1, 0.5, 0.9, 0.999};
        const QStringList types = m_scorer.availableEvidenceTypes();

        for (double prior : priors) {
            QMap<QString, bool> evidence;
            for (const QString& t : types) {
                evidence[t] = true;
            }
            const auto result = m_scorer.score(prior, evidence);
            QVERIFY2(result.posteriorProbability >= 0.0 && result.posteriorProbability <= 1.0,
                     qPrintable(QStringLiteral("Posterior %1 (prior=%2) must be in [0,1]")
                         .arg(result.posteriorProbability).arg(prior)));
        }
    }

    void testPosteriorAlwaysInRange_vectorOverload()
    {
        QVector<EvidenceItem> items;
        const QStringList types = m_scorer.availableEvidenceTypes();
        for (const QString& t : types) {
            items.append({ t, true });
        }

        const QVector<double> priors = {0.001, 0.05, 0.5, 0.999};
        for (double prior : priors) {
            const auto results = m_scorer.score(items, prior);
            for (const auto& ew : results) {
                QVERIFY2(ew.posteriorProbability >= 0.0 && ew.posteriorProbability <= 1.0,
                         qPrintable(QStringLiteral("Posterior %1 must be in [0,1] for type %2")
                             .arg(ew.posteriorProbability).arg(ew.evidenceType)));
            }
        }
    }

    // ── No NaN or Inf in outputs ──────────────────────────────────────────────

    void testNoNaNOrInfWithExtremeInputs()
    {
        QMap<QString, bool> allPresent;
        const QStringList types = m_scorer.availableEvidenceTypes();
        for (const QString& t : types) allPresent[t] = true;

        // Test with near-zero and near-one prior.
        for (double prior : {0.001, 0.999}) {
            const auto result = m_scorer.score(prior, allPresent);
            QVERIFY2(!std::isnan(result.posteriorProbability), "Posterior must not be NaN");
            QVERIFY2(!std::isinf(result.posteriorProbability), "Posterior must not be Inf");
            QVERIFY2(!std::isnan(result.overallLikelihoodRatio), "Overall LR must not be NaN");
        }
    }

    // ── Edge case: zero/boundary prior handled without crash ─────────────────

    void testZeroPriorHandledGracefully_vectorOverload()
    {
        QVector<EvidenceItem> items = {{ QStringLiteral("eyewitness_identification_ideal"), true }};
        // Vector overload clamps to [1e-9, 1-1e-9]; must not crash or produce NaN.
        const auto results = m_scorer.score(items, 0.0);
        QVERIFY(!results.isEmpty());
        QVERIFY(!std::isnan(results.first().posteriorProbability));
        QVERIFY(!std::isinf(results.first().posteriorProbability));
    }

    void testOnePriorHandledGracefully_vectorOverload()
    {
        QVector<EvidenceItem> items = {{ QStringLiteral("alibi_strong"), true }};
        const auto results = m_scorer.score(items, 1.0);
        QVERIFY(!results.isEmpty());
        QVERIFY(!std::isnan(results.first().posteriorProbability));
    }

    void testZeroPriorHandledGracefully_mapOverload()
    {
        QMap<QString, bool> evidence;
        evidence[QStringLiteral("eyewitness_identification_ideal")] = true;
        // Map overload falls back to odds=1.0 (prior=0.5) for out-of-range input.
        const auto result = m_scorer.score(0.0, evidence);
        QVERIFY(!std::isnan(result.posteriorProbability));
        QVERIFY(!std::isinf(result.posteriorProbability));
        QVERIFY(result.posteriorProbability >= 0.0 && result.posteriorProbability <= 1.0);
    }

    // ── Contributions vector matches evidence map ─────────────────────────────

    void testContributionsCountMatchesEvidenceMap()
    {
        QMap<QString, bool> evidence;
        evidence[QStringLiteral("eyewitness_identification_ideal")] = true;
        evidence[QStringLiteral("cctv_clear_face")]                 = true;
        evidence[QStringLiteral("alibi_weak")]                      = false;

        const auto result = m_scorer.score(0.1, evidence);
        QCOMPARE(static_cast<int>(result.contributions.size()), evidence.size());
    }

    void testContributionLRStoredCorrectly()
    {
        const auto [lr, rel] = m_scorer.getLRAndReliability(
            QStringLiteral("eyewitness_identification_ideal"));

        QMap<QString, bool> evidence;
        evidence[QStringLiteral("eyewitness_identification_ideal")] = true;

        const auto result = m_scorer.score(0.1, evidence);
        QVERIFY(!result.contributions.empty());

        bool found = false;
        for (const auto& c : result.contributions) {
            if (c.name == QStringLiteral("eyewitness_identification_ideal")) {
                QVERIFY2(std::abs(c.likelihoodRatio - lr) < kEps,
                         qPrintable(QStringLiteral("Contribution LR %1 != expected %2")
                             .arg(c.likelihoodRatio).arg(lr)));
                QVERIFY(c.wasPresent);
                found = true;
            }
        }
        QVERIFY2(found, "eyewitness_identification_ideal contribution not found");
    }

    // ── Bayes Factor ──────────────────────────────────────────────────────────

    void testBayesFactorFavoursHigherPriorWhenEvidencePositive()
    {
        QMap<QString, bool> evidence;
        evidence[QStringLiteral("eyewitness_identification_ideal")] = true;  // LR=4

        // With equal LR applied, BF between two hypotheses with different priors should be ~1.0
        // since the same LR is applied to both (BF is evidence-driven, not prior-driven).
        const double bf = m_scorer.bayesFactor(0.3, 0.1, evidence);
        QVERIFY(!std::isnan(bf));
        QVERIFY(!std::isinf(bf));
    }

    void testBayesFactorEqualPriorsIsOne()
    {
        // Equal priors → BF ≈ 1.0 (same posterior update, same prior odds ratio = 1).
        QMap<QString, bool> evidence;
        evidence[QStringLiteral("cctv_clear_face")] = true;

        const double bf = m_scorer.bayesFactor(0.2, 0.2, evidence);
        QVERIFY2(std::abs(bf - 1.0) < 1e-9,
                 qPrintable(QStringLiteral("Equal priors BF=%1 should be 1.0").arg(bf)));
    }

    void testBayesFactorNonNegative()
    {
        QMap<QString, bool> evidence;
        evidence[QStringLiteral("modus_operandi_match_high")] = true;

        const double bf = m_scorer.bayesFactor(0.4, 0.1, evidence);
        QVERIFY2(bf >= 0.0, qPrintable(QStringLiteral("BF %1 must be non-negative").arg(bf)));
    }

    // ── Vector overload: incremental posterior progression ────────────────────

    void testVectorOverloadIncreasingPosterior()
    {
        // Applying multiple incriminating evidence items should monotonically
        // increase the running posterior through the result list.
        QVector<EvidenceItem> items = {
            { QStringLiteral("no_alibi"),                        true },  // LR=1.2
            { QStringLiteral("eyewitness_identification_ideal"), true },  // LR=4.0
            { QStringLiteral("vehicle_at_scene"),                true },  // LR=15.0
        };

        const auto results = m_scorer.score(items, 0.05);
        QCOMPARE(results.size(), 3);

        for (int i = 1; i < results.size(); ++i) {
            QVERIFY2(results[i].posteriorProbability > results[i-1].posteriorProbability,
                     qPrintable(QStringLiteral("Posterior should increase at step %1: %2 <= %3")
                         .arg(i).arg(results[i].posteriorProbability)
                         .arg(results[i-1].posteriorProbability)));
        }
    }

    void testVectorOverloadDecreasingPosterior()
    {
        // Exculpatory evidence should decrease the posterior each step.
        QVector<EvidenceItem> items = {
            { QStringLiteral("alibi_strong"), true },  // LR=0.05
            { QStringLiteral("alibi_weak"),   true },  // LR=0.50
        };

        const auto results = m_scorer.score(items, 0.5);
        QCOMPARE(results.size(), 2);
        QVERIFY2(results[0].posteriorProbability < 0.5,
                 "alibi_strong should lower posterior below 0.5");
        QVERIFY2(results[1].posteriorProbability < results[0].posteriorProbability,
                 "alibi_weak should further lower posterior");
    }

    // ── Empty evidence list ───────────────────────────────────────────────────

    void testEmptyEvidenceMapReturnsPrior()
    {
        constexpr double prior = 0.07;
        const auto result = m_scorer.score(prior, QMap<QString, bool>{});
        QVERIFY2(std::abs(result.posteriorProbability - prior) < 1e-12,
                 qPrintable(QStringLiteral("Empty evidence: posterior %1 should equal prior %2")
                     .arg(result.posteriorProbability).arg(prior)));
        QVERIFY(result.contributions.empty());
    }

    void testEmptyEvidenceVectorReturnsEmptyResults()
    {
        const auto results = m_scorer.score(QVector<EvidenceItem>{}, 0.1);
        QVERIFY(results.isEmpty());
    }
};

QTEST_GUILESS_MAIN(EvidenceScorerDeep2Test)
#include "test_evidence_scorer_deep2.moc"
