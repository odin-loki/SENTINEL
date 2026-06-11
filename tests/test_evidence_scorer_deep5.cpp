// Deep audit iteration 14 — EvidenceScorer (deep5)
// Verifies: Bayesian LR updates, posterior in [0,1], bayesFactor with equal priors = 1.

#include <QTest>
#include "inference/EvidenceScorer.h"
#include <cmath>

static constexpr double kEps = 1e-9;

static double posteriorFromLR(double prior, double lr)
{
    const double num = prior * lr;
    return num / (num + (1.0 - prior));
}

static double posteriorFromOdds(double prior, double lr)
{
    const double priorOdds = prior / (1.0 - prior);
    const double postOdds  = priorOdds * lr;
    return postOdds / (1.0 + postOdds);
}

class EvidenceScorerDeep5Test : public QObject
{
    Q_OBJECT

private:
    EvidenceScorer m_scorer;

private slots:

    void testPosteriorAlwaysInRange()
    {
        const QStringList types = m_scorer.availableEvidenceTypes();
        QMap<QString, bool> allPresent;
        for (const QString& t : types) allPresent[t] = true;

        const QVector<double> priors = {0.001, 0.05, 0.1, 0.3, 0.5, 0.8, 0.999};
        for (double prior : priors) {
            const auto result = m_scorer.score(prior, allPresent);
            QVERIFY2(result.posteriorProbability >= 0.0 &&
                     result.posteriorProbability <= 1.0,
                     qPrintable(QStringLiteral("posterior %1 (prior=%2) not in [0,1]")
                         .arg(result.posteriorProbability).arg(prior)));
        }
    }

    void testPosteriorInRangeWithExtremeLR()
    {
        QMap<QString, bool> evidence;
        evidence[QStringLiteral("dna_match_full_profile")] = true;
        const auto result = m_scorer.score(0.001, evidence);
        QVERIFY2(result.posteriorProbability >= 0.0 &&
                 result.posteriorProbability <= 1.0,
                 qPrintable(QStringLiteral("extreme LR posterior %1 not in [0,1]")
                     .arg(result.posteriorProbability)));
        QVERIFY2(result.posteriorProbability > 0.99,
                 "DNA full profile should yield very high posterior");
    }

    void testOutOfRangePriorClamped()
    {
        QMap<QString, bool> evidence;
        evidence[QStringLiteral("cctv_clear_face")] = true;
        const auto result = m_scorer.score(1.5, evidence);
        QVERIFY2(result.posteriorProbability >= 0.0 &&
                 result.posteriorProbability <= 1.0,
                 "out-of-range prior must still yield valid posterior");
        QVERIFY2(!std::isnan(result.posteriorProbability),
                 "posterior must not be NaN");
    }

    void testPresentLRMatchesTable()
    {
        constexpr double prior = 0.1;
        const QStringList types = m_scorer.availableEvidenceTypes();
        for (const QString& type : types) {
            const auto [tableLR, rel] = m_scorer.getLRAndReliability(type);
            QMap<QString, bool> present;
            present[type] = true;
            const auto result = m_scorer.score(prior, present);
            QVERIFY2(std::abs(result.contributions[0].likelihoodRatio - tableLR) < kEps,
                     qPrintable(QStringLiteral("present LR for '%1'").arg(type)));
        }
    }

    void testAbsentLRIsInverse()
    {
        constexpr double prior = 0.3;
        const auto [tableLR, rel] = m_scorer.getLRAndReliability(
            QStringLiteral("alibi_strong"));
        QMap<QString, bool> absent;
        absent[QStringLiteral("alibi_strong")] = false;
        const auto result = m_scorer.score(prior, absent);
        const double expectedLR = 1.0 / tableLR;
        QVERIFY2(std::abs(result.contributions[0].likelihoodRatio - expectedLR) < kEps,
                 qPrintable(QStringLiteral("absent LR expected %1, got %2")
                     .arg(expectedLR).arg(result.contributions[0].likelihoodRatio)));
    }

    void testPosteriorMatchesBayesFormula()
    {
        constexpr double prior = 0.05;
        QMap<QString, bool> evidence;
        evidence[QStringLiteral("cctv_clear_face")]                 = true;
        evidence[QStringLiteral("vehicle_at_scene")]             = true;
        evidence[QStringLiteral("modus_operandi_match_moderate")]  = true;
        const auto result = m_scorer.score(prior, evidence);
        const double expected = posteriorFromOdds(prior, result.overallLikelihoodRatio);
        QVERIFY2(std::abs(result.posteriorProbability - expected) < 1e-12,
                 qPrintable(QStringLiteral("posterior %1 vs expected %2")
                     .arg(result.posteriorProbability).arg(expected)));
    }

    void testBayesFactorOneWhenEqualPriors()
    {
        QMap<QString, bool> evidence;
        evidence[QStringLiteral("cctv_clear_face")] = true;
        const double bf = m_scorer.bayesFactor(0.3, 0.3, evidence);
        QVERIFY2(std::abs(bf - 1.0) < 1e-9,
                 qPrintable(QStringLiteral("equal priors BF should be 1, got %1").arg(bf)));
    }

    void testBayesFactorOneWithStrongEvidence()
    {
        QMap<QString, bool> evidence;
        evidence[QStringLiteral("dna_match_full_profile")] = true;
        const double bf = m_scorer.bayesFactor(0.01, 0.5, evidence);
        QVERIFY2(std::abs(bf - 1.0) < 1e-9,
                 qPrintable(QStringLiteral("same evidence BF should be 1, got %1")
                     .arg(bf, 0, 'g', 17)));
    }

    void testBayesFactorOneWhenNoEvidence()
    {
        const double bf = m_scorer.bayesFactor(0.2, 0.8, QMap<QString, bool>{});
        QVERIFY2(std::abs(bf - 1.0) < 1e-9,
                 qPrintable(QStringLiteral("empty evidence BF should be 1, got %1").arg(bf)));
    }

    void testCombinedLRIsProduct()
    {
        QMap<QString, bool> evidence;
        evidence[QStringLiteral("blood_type_match")]      = true;
        evidence[QStringLiteral("informant_tip_reliable")] = true;
        evidence[QStringLiteral("alibi_strong")]          = false;
        const auto result = m_scorer.score(0.1, evidence);
        const double expectedLR = 3.0 * 5.0 * 20.0;
        QVERIFY2(std::abs(result.overallLikelihoodRatio - expectedLR) < 1e-8,
                 qPrintable(QStringLiteral("product LR expected %1, got %2")
                     .arg(expectedLR).arg(result.overallLikelihoodRatio)));
        const double expectedPost = posteriorFromLR(0.1, expectedLR);
        QVERIFY2(std::abs(result.posteriorProbability - expectedPost) < 1e-10,
                 qPrintable(QStringLiteral("combined posterior expected %1, got %2")
                     .arg(expectedPost).arg(result.posteriorProbability)));
    }
};

QTEST_GUILESS_MAIN(EvidenceScorerDeep5Test)
#include "test_evidence_scorer_deep5.moc"
