// Deep audit iteration 12 — EvidenceScorer
// Covers: score() in [0,1], empty list → prior, single strong item,
// multiple weak items, neutral/incriminating/exculpatory LR, bayesFactor.

#include <QTest>
#include "inference/EvidenceScorer.h"
#include <cmath>

// Bayes update helper: prior → posterior given LR.
static double bayesUpdate(double prior, double lr)
{
    const double odds    = prior / (1.0 - prior);
    const double newOdds = odds * lr;
    return newOdds / (1.0 + newOdds);
}

class EvidenceScorerDeep3Test : public QObject
{
    Q_OBJECT

private:
    EvidenceScorer m_scorer;

private slots:

    // ── scoreEvidence() [= score(prior, map)] returns value in [0,1] ──────────

    void testScoreAlwaysInRange()
    {
        const QStringList types = m_scorer.availableEvidenceTypes();
        QMap<QString, bool> allPresent;
        for (const QString& t : types) allPresent[t] = true;

        const QVector<double> priors = {0.001, 0.05, 0.1, 0.3, 0.5, 0.8, 0.999};
        for (double prior : priors) {
            const auto result = m_scorer.score(prior, allPresent);
            QVERIFY2(result.posteriorProbability >= 0.0 && result.posteriorProbability <= 1.0,
                     qPrintable(QString("posterior %1 (prior=%2) must be in [0,1]")
                         .arg(result.posteriorProbability).arg(prior)));
        }
    }

    void testScoreInRangeVectorOverload()
    {
        const QStringList types = m_scorer.availableEvidenceTypes();
        QVector<EvidenceItem> items;
        for (const QString& t : types) items.append({t, true});

        for (double prior : {0.01, 0.1, 0.5, 0.99}) {
            const auto results = m_scorer.score(items, prior);
            for (const auto& ew : results) {
                QVERIFY2(ew.posteriorProbability >= 0.0 && ew.posteriorProbability <= 1.0,
                         qPrintable(QString("posterior %1 out of [0,1] for type %2")
                             .arg(ew.posteriorProbability).arg(ew.evidenceType)));
            }
        }
    }

    // ── Empty evidence list → score = prior ──────────────────────────────────

    void testEmptyEvidenceMapReturnsPrior()
    {
        for (double prior : {0.01, 0.05, 0.2, 0.5, 0.9}) {
            const auto result = m_scorer.score(prior, QMap<QString, bool>{});
            QVERIFY2(std::abs(result.posteriorProbability - prior) < 1e-12,
                     qPrintable(QString("empty evidence: posterior %1 != prior %2")
                         .arg(result.posteriorProbability).arg(prior)));
            QVERIFY(result.contributions.empty());
        }
    }

    void testEmptyEvidenceVectorReturnsEmptyList()
    {
        const auto results = m_scorer.score(QVector<EvidenceItem>{}, 0.1);
        QVERIFY(results.isEmpty());
    }

    // ── Single strong evidence item → score > 0.7 ────────────────────────────

    void testSingleStrongEvidenceHighScore()
    {
        // DNA full profile: LR = 1e9. Even with low prior (0.1), posterior → ~1.0.
        QMap<QString, bool> evidence;
        evidence[QStringLiteral("dna_match_full_profile")] = true;
        const auto result = m_scorer.score(0.1, evidence);
        QVERIFY2(result.posteriorProbability > 0.7,
                 qPrintable(QString("DNA full profile should produce score >0.7, got %1")
                     .arg(result.posteriorProbability)));
    }

    void testSingleStrongFingerprintHighScore()
    {
        // Fingerprint 10-pt: LR = 1e6. Prior = 0.01 → still very high posterior.
        QMap<QString, bool> evidence;
        evidence[QStringLiteral("fingerprint_match_10pt")] = true;
        const auto result = m_scorer.score(0.01, evidence);
        QVERIFY2(result.posteriorProbability > 0.7,
                 qPrintable(QString("Fingerprint 10pt should produce score >0.7, got %1")
                     .arg(result.posteriorProbability)));
    }

    // ── Multiple weak items → moderate combined score ─────────────────────────

    void testMultipleWeakItemsModerateScore()
    {
        // no_alibi (LR=1.2) + modus_operandi_match_moderate (LR=3.0) + eyewitness_poor (LR=1.5)
        // Starting from prior=0.05, these raise the score but not to near-certainty.
        QMap<QString, bool> evidence;
        evidence[QStringLiteral("no_alibi")]                        = true; // LR=1.2
        evidence[QStringLiteral("modus_operandi_match_moderate")]   = true; // LR=3.0
        evidence[QStringLiteral("eyewitness_identification_poor")]  = true; // LR=1.5
        const auto result = m_scorer.score(0.05, evidence);
        QVERIFY2(result.posteriorProbability > 0.05,
                 "Three weak items should raise posterior above prior");
        QVERIFY2(result.posteriorProbability < 0.9,
                 "Three weak items from low prior should not saturate near 1.0");
    }

    // ── LR = 1 (unknown type) → posterior = prior ────────────────────────────

    void testNeutralLRLeavesPosteriOrUnchanged()
    {
        constexpr double prior = 0.15;
        QMap<QString, bool> evidence;
        evidence[QStringLiteral("completely_unknown_type_zyx99")] = true; // defaults to LR=1.0
        const auto result = m_scorer.score(prior, evidence);
        QVERIFY2(std::abs(result.posteriorProbability - prior) < 1e-10,
                 qPrintable(QString("LR=1.0 should not change prior %1, got %2")
                     .arg(prior).arg(result.posteriorProbability)));
    }

    // ── LR > 1 incriminating evidence increases score ─────────────────────────

    void testIncriminatingLRIncreasesScore()
    {
        constexpr double prior = 0.1;
        QMap<QString, bool> evidence;
        evidence[QStringLiteral("eyewitness_identification_ideal")] = true; // LR=4.0
        const auto result = m_scorer.score(prior, evidence);
        QVERIFY2(result.posteriorProbability > prior,
                 qPrintable(QString("LR=4 should raise posterior above %1, got %2")
                     .arg(prior).arg(result.posteriorProbability)));
    }

    void testIncriminatingLRMatchesBayesFormula()
    {
        constexpr double prior = 0.05;
        const auto [lr, rel] = m_scorer.getLRAndReliability(
            QStringLiteral("eyewitness_identification_ideal"));
        const double expected = bayesUpdate(prior, lr);

        QMap<QString, bool> evidence;
        evidence[QStringLiteral("eyewitness_identification_ideal")] = true;
        const auto result = m_scorer.score(prior, evidence);
        QVERIFY2(std::abs(result.posteriorProbability - expected) < 1e-10,
                 qPrintable(QString("Expected %1, got %2").arg(expected)
                     .arg(result.posteriorProbability)));
    }

    void testAllLRValuesArePositive()
    {
        // LR must be > 0 for a valid likelihood ratio.
        const QStringList types = m_scorer.availableEvidenceTypes();
        for (const QString& t : types) {
            const auto [lr, rel] = m_scorer.getLRAndReliability(t);
            QVERIFY2(lr > 0.0,
                     qPrintable(QString("LR for '%1' must be positive, got %2").arg(t).arg(lr)));
        }
    }

    // ── LR < 1 exculpatory evidence decreases score ───────────────────────────

    void testExculpatoryLRDecreasesScore()
    {
        constexpr double prior = 0.5;
        QMap<QString, bool> evidence;
        evidence[QStringLiteral("alibi_strong")] = true; // LR=0.05
        const auto result = m_scorer.score(prior, evidence);
        QVERIFY2(result.posteriorProbability < prior,
                 qPrintable(QString("alibi_strong (LR=0.05) should lower posterior below %1, got %2")
                     .arg(prior).arg(result.posteriorProbability)));
    }

    void testExculpatoryLRMatchesBayesFormula()
    {
        constexpr double prior = 0.4;
        const auto [lr, rel] = m_scorer.getLRAndReliability(QStringLiteral("alibi_strong"));
        const double expected = bayesUpdate(prior, lr);

        QMap<QString, bool> evidence;
        evidence[QStringLiteral("alibi_strong")] = true;
        const auto result = m_scorer.score(prior, evidence);
        QVERIFY2(std::abs(result.posteriorProbability - expected) < 1e-10,
                 qPrintable(QString("alibi_strong: expected %1, got %2")
                     .arg(expected).arg(result.posteriorProbability)));
    }

    // ── priorOdds and posterior odds relationship ─────────────────────────────

    void testPriorOddsPosteriOrOddsRelationship()
    {
        // posterior_odds = prior_odds * overallLR
        constexpr double prior = 0.1;
        QMap<QString, bool> evidence;
        evidence[QStringLiteral("eyewitness_identification_ideal")] = true;
        evidence[QStringLiteral("vehicle_at_scene")]                = true;

        const auto result = m_scorer.score(prior, evidence);
        const double priorOdds  = prior / (1.0 - prior);
        const double postOdds   = result.posteriorProbability /
                                  (1.0 - result.posteriorProbability);
        const double impliedLR  = postOdds / priorOdds;
        QVERIFY2(std::abs(impliedLR - result.overallLikelihoodRatio) < 1e-8,
                 qPrintable(QString("posterior_odds/prior_odds=%1 != overallLR=%2")
                     .arg(impliedLR).arg(result.overallLikelihoodRatio)));
    }

    // ── bayesFactor ───────────────────────────────────────────────────────────

    void testBayesFactorEqualPriorsIsOne()
    {
        QMap<QString, bool> evidence;
        evidence[QStringLiteral("cctv_clear_face")] = true;
        const double bf = m_scorer.bayesFactor(0.2, 0.2, evidence);
        QVERIFY2(std::abs(bf - 1.0) < 1e-9,
                 qPrintable(QString("Equal priors: BF=%1 should be 1.0").arg(bf)));
    }

    void testBayesFactorIsNonNegative()
    {
        QMap<QString, bool> evidence;
        evidence[QStringLiteral("modus_operandi_match_high")] = true;
        const double bf = m_scorer.bayesFactor(0.4, 0.1, evidence);
        QVERIFY2(!std::isnan(bf) && !std::isinf(bf) && bf >= 0.0,
                 qPrintable(QString("BF=%1 must be finite and non-negative").arg(bf)));
    }

    void testBayesFactorZeroPriorSafe()
    {
        // Zero priors should return 1.0 gracefully (not crash or NaN).
        QMap<QString, bool> evidence;
        evidence[QStringLiteral("eyewitness_identification_ideal")] = true;
        const double bf = m_scorer.bayesFactor(0.0, 0.1, evidence);
        QVERIFY2(!std::isnan(bf) && !std::isinf(bf),
                 "BF with zero prior must not produce NaN or Inf");
    }

    // ── Overall LR is product of individual LRs ───────────────────────────────

    void testOverallLRIsProductOfContributions()
    {
        QMap<QString, bool> evidence;
        evidence[QStringLiteral("eyewitness_identification_ideal")] = true;
        evidence[QStringLiteral("cctv_clear_face")]                 = true;

        const auto result = m_scorer.score(0.1, evidence);
        double product = 1.0;
        for (const auto& c : result.contributions) product *= c.likelihoodRatio;
        QVERIFY2(std::abs(product - result.overallLikelihoodRatio) < 1e-8,
                 qPrintable(QString("product %1 != overallLR %2").arg(product)
                     .arg(result.overallLikelihoodRatio)));
    }

    // ── No NaN or Inf with extreme priors ────────────────────────────────────

    void testNoNaNOrInfWithExtremePriors()
    {
        QMap<QString, bool> evidence;
        const QStringList types = m_scorer.availableEvidenceTypes();
        for (const QString& t : types) evidence[t] = true;

        for (double prior : {0.001, 0.999}) {
            const auto result = m_scorer.score(prior, evidence);
            QVERIFY2(!std::isnan(result.posteriorProbability), "posterior must not be NaN");
            QVERIFY2(!std::isinf(result.posteriorProbability), "posterior must not be Inf");
            QVERIFY2(!std::isnan(result.overallLikelihoodRatio), "overallLR must not be NaN");
        }
    }
};

QTEST_GUILESS_MAIN(EvidenceScorerDeep3Test)
#include "test_evidence_scorer_deep3.moc"
