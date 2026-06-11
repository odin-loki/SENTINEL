// Deep audit iteration 12 — EvidenceScorer (deep4)
// Verifies: LR = P(E|H1)/P(E|H0) per type, posterior = prior*LR/normaliser,
//           bayesFactor aggregation formula.

#include <QTest>
#include "inference/EvidenceScorer.h"
#include <cmath>

static constexpr double kEps = 1e-9;

// posterior = prior * LR / (prior * LR + (1 - prior))
static double posteriorFromLR(double prior, double lr)
{
    const double num = prior * lr;
    return num / (num + (1.0 - prior));
}

// Odds form equivalent: posterior = (priorOdds * LR) / (1 + priorOdds * LR)
static double posteriorFromOdds(double prior, double lr)
{
    const double priorOdds = prior / (1.0 - prior);
    const double postOdds  = priorOdds * lr;
    return postOdds / (1.0 + postOdds);
}

class EvidenceScorerDeep4Test : public QObject
{
    Q_OBJECT

private:
    EvidenceScorer m_scorer;

private slots:

    // ── LR table: present → lr, absent → 1/lr for every registered type ─────

    void testEveryEvidenceTypeLRWhenPresent()
    {
        constexpr double prior = 0.1;
        const QStringList types = m_scorer.availableEvidenceTypes();
        QVERIFY2(!types.isEmpty(), "LR table must not be empty");

        for (const QString& type : types) {
            const auto [tableLR, rel] = m_scorer.getLRAndReliability(type);
            QVERIFY2(tableLR > 0.0,
                     qPrintable(QStringLiteral("LR for '%1' must be positive").arg(type)));

            QMap<QString, bool> present;
            present[type] = true;
            const auto result = m_scorer.score(prior, present);

            QCOMPARE(result.contributions.size(), 1);
            QCOMPARE(result.contributions[0].name, type);
            QVERIFY(result.contributions[0].wasPresent);
            QVERIFY2(std::abs(result.contributions[0].likelihoodRatio - tableLR) < kEps,
                     qPrintable(QStringLiteral("Present LR for '%1': expected %2, got %3")
                         .arg(type).arg(tableLR)
                         .arg(result.contributions[0].likelihoodRatio)));
            QCOMPARE(result.overallLikelihoodRatio, tableLR);
        }
    }

    void testEveryEvidenceTypeLRWhenAbsent()
    {
        constexpr double prior = 0.3;
        const QStringList types = m_scorer.availableEvidenceTypes();

        for (const QString& type : types) {
            const auto [tableLR, rel] = m_scorer.getLRAndReliability(type);
            const double expectedLR = 1.0 / tableLR;

            QMap<QString, bool> absent;
            absent[type] = false;
            const auto result = m_scorer.score(prior, absent);

            QVERIFY2(std::abs(result.contributions[0].likelihoodRatio - expectedLR) < kEps,
                     qPrintable(QStringLiteral("Absent LR for '%1': expected 1/%2=%3, got %4")
                         .arg(type).arg(tableLR).arg(expectedLR)
                         .arg(result.contributions[0].likelihoodRatio)));
            QVERIFY(!result.contributions[0].wasPresent);
        }
    }

    // ── posterior = prior * LR / normaliser ─────────────────────────────────

    void testPosteriorMatchesNormaliserFormulaEveryType()
    {
        constexpr double prior = 0.05;
        const QStringList types = m_scorer.availableEvidenceTypes();

        for (const QString& type : types) {
            const auto [lr, rel] = m_scorer.getLRAndReliability(type);
            const double expected = posteriorFromLR(prior, lr);

            QMap<QString, bool> evidence;
            evidence[type] = true;
            const auto result = m_scorer.score(prior, evidence);

            QVERIFY2(std::abs(result.posteriorProbability - expected) < 1e-10,
                     qPrintable(QStringLiteral("'%1': expected posterior %2, got %3")
                         .arg(type).arg(expected).arg(result.posteriorProbability)));
        }
    }

    void testPosteriorOddsFormMatchesImplementation()
    {
        constexpr double prior = 0.2;
        QMap<QString, bool> evidence;
        evidence[QStringLiteral("cctv_clear_face")]                 = true;  // LR=50
        evidence[QStringLiteral("vehicle_at_scene")]             = true;  // LR=15
        evidence[QStringLiteral("modus_operandi_match_moderate")]  = true;  // LR=3

        const auto result = m_scorer.score(prior, evidence);
        const double expected = posteriorFromOdds(prior, result.overallLikelihoodRatio);

        QVERIFY2(std::abs(result.posteriorProbability - expected) < 1e-12,
                 qPrintable(QStringLiteral("Multi-evidence posterior: expected %1, got %2")
                     .arg(expected).arg(result.posteriorProbability)));
    }

    void testVectorOverloadPosteriorIncrementalUpdate()
    {
        constexpr double prior = 0.1;
        const QVector<EvidenceItem> items = {
            { QStringLiteral("dna_match_partial"), true },
            { QStringLiteral("alibi_weak"), false },
        };

        const auto results = m_scorer.score(items, prior);
        QCOMPARE(results.size(), 2);

        double runningPrior = prior;
        for (const auto& ew : results) {
            const double expected = posteriorFromLR(runningPrior, ew.likelihoodRatio);
            QVERIFY2(std::abs(ew.posteriorProbability - expected) < 1e-10,
                     qPrintable(QStringLiteral("Step '%1': expected %2, got %3")
                         .arg(ew.evidenceType).arg(expected).arg(ew.posteriorProbability)));
            runningPrior = ew.posteriorProbability;
        }
    }

    // ── bayesFactor aggregation ─────────────────────────────────────────────

    void testBayesFactorEqualsOverallLRRatio()
    {
        // BF = overallLR_A / overallLR_B (prior-independent, numerically stable).
        QMap<QString, bool> evidence;
        evidence[QStringLiteral("fingerprint_match_8pt")] = true;
        evidence[QStringLiteral("cctv_partial")]        = true;

        const double priorA = 0.3;
        const double priorB = 0.05;
        const double bf     = m_scorer.bayesFactor(priorA, priorB, evidence);

        const auto resA = m_scorer.score(priorA, evidence);
        const auto resB = m_scorer.score(priorB, evidence);
        const double expectedBF = resA.overallLikelihoodRatio /
                                  std::max(resB.overallLikelihoodRatio, 1e-12);

        QVERIFY2(std::abs(bf - expectedBF) < 1e-9,
                 qPrintable(QStringLiteral("BF=%1, expected %2").arg(bf).arg(expectedBF)));
    }

    void testBayesFactorOneWhenEqualPriors()
    {
        QMap<QString, bool> evidence;
        evidence[QStringLiteral("cctv_clear_face")] = true;

        const double bf = m_scorer.bayesFactor(0.3, 0.3, evidence);
        QVERIFY2(std::abs(bf - 1.0) < 1e-9,
                 qPrintable(QStringLiteral("Equal priors BF should be 1, got %1").arg(bf)));
    }

    void testBayesFactorStableWithStrongEvidence()
    {
        // Strong DNA (LR=1e9) previously broke posterior-odds BF calculation.
        QMap<QString, bool> evidence;
        evidence[QStringLiteral("dna_match_full_profile")] = true;

        const double bf = m_scorer.bayesFactor(0.01, 0.5, evidence);
        QVERIFY2(std::abs(bf - 1.0) < 1e-9,
                 qPrintable(QStringLiteral("Same evidence BF should be 1, got %1").arg(bf, 0, 'g', 17)));
    }

    void testBayesFactorOneWhenNoEvidence()
    {
        const double bf = m_scorer.bayesFactor(0.2, 0.8, QMap<QString, bool>{});
        QVERIFY2(std::abs(bf - 1.0) < 1e-9,
                 qPrintable(QStringLiteral("Empty evidence BF should be 1, got %1").arg(bf)));
    }

    void testBayesFactorMatchesOverallLRRatio()
    {
        QMap<QString, bool> evidence;
        evidence[QStringLiteral("shoe_impression_match")] = true;

        const auto scored = m_scorer.score(0.1, evidence);
        const double overallLR = scored.overallLikelihoodRatio;

        const double bf = m_scorer.bayesFactor(0.2, 0.4, evidence);

        // lrA = lrB = overallLR → BF = 1; verify overallLR is as expected.
        const auto [tableLR, rel] = m_scorer.getLRAndReliability(
            QStringLiteral("shoe_impression_match"));
        QCOMPARE(overallLR, tableLR);
        QVERIFY2(std::abs(bf - 1.0) < 1e-9,
                 qPrintable(QStringLiteral("BF with single evidence should be 1, got %1").arg(bf)));
    }

    // ── Combined LR product and exculpatory inversion ─────────────────────────

    void testCombinedLRIsProduct()
    {
        QMap<QString, bool> evidence;
        evidence[QStringLiteral("blood_type_match")]      = true;   // 3.0
        evidence[QStringLiteral("informant_tip_reliable")] = true;  // 5.0
        evidence[QStringLiteral("alibi_strong")]          = false;  // 1/0.05 = 20

        const auto result = m_scorer.score(0.1, evidence);
        const double expectedLR = 3.0 * 5.0 * 20.0;

        QVERIFY2(std::abs(result.overallLikelihoodRatio - expectedLR) < 1e-8,
                 qPrintable(QStringLiteral("Product LR: expected %1, got %2")
                     .arg(expectedLR).arg(result.overallLikelihoodRatio)));

        const double expectedPost = posteriorFromLR(0.1, expectedLR);
        QVERIFY2(std::abs(result.posteriorProbability - expectedPost) < 1e-10,
                 qPrintable(QStringLiteral("Combined posterior: expected %1, got %2")
                     .arg(expectedPost).arg(result.posteriorProbability)));
    }
};

QTEST_GUILESS_MAIN(EvidenceScorerDeep4Test)
#include "test_evidence_scorer_deep4.moc"
