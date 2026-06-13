// test_evidence_scorer_deep8.cpp — Deep audit iteration 22: EvidenceScorer
// Verifies: prior edge cases, absent neutral LR, DNA saturation safety, bayesFactor
// LR ratio, unknown key handling, posterior bounds.

#include <QTest>
#include "inference/EvidenceScorer.h"
#include <cmath>

static constexpr double kEps = 1e-9;

class EvidenceScorerDeep8Test : public QObject
{
    Q_OBJECT

private:
    EvidenceScorer m_scorer;

private slots:
    void testPriorZeroClampedToMinimum();
    void testPriorOneClampedToMaximum();
    void testAbsentUnknownEvidenceLRIsOne();
    void testStrongDnaPosteriorDoesNotOverflow();
    void testBayesFactorUsesLRRatioNotPosteriorOdds();
    void testUnknownEvidenceKeyIgnoredInVectorApi();
    void testPosteriorAlwaysInUnitInterval();
    void testAbsentIncriminatingEvidenceUsesInverseLR();
};

// ─── Tests ─────────────────────────────────────────────────────────────────

void EvidenceScorerDeep8Test::testPriorZeroClampedToMinimum()
{
    QMap<QString, bool> evidence;
    evidence[QStringLiteral("vehicle_at_scene")] = true;

    const auto result = m_scorer.score(0.0, evidence);
    QVERIFY2(result.posteriorProbability > 0.0 && result.posteriorProbability < 1.0,
             qPrintable(QStringLiteral("prior 0 clamped posterior=%1 must stay in (0,1)")
                            .arg(result.posteriorProbability)));
    QVERIFY2(!std::isnan(result.posteriorProbability),
             "prior 0 must not produce NaN posterior");
}

void EvidenceScorerDeep8Test::testPriorOneClampedToMaximum()
{
    QMap<QString, bool> evidence;
    evidence[QStringLiteral("alibi_strong")] = true;

    const auto result = m_scorer.score(1.0, evidence);
    QVERIFY2(result.posteriorProbability > 0.0 && result.posteriorProbability < 1.0,
             qPrintable(QStringLiteral("prior 1 clamped posterior=%1 must stay in (0,1)")
                            .arg(result.posteriorProbability)));
    QVERIFY2(!std::isnan(result.posteriorProbability),
             "prior 1 must not produce NaN posterior");
}

void EvidenceScorerDeep8Test::testAbsentUnknownEvidenceLRIsOne()
{
    constexpr double prior = 0.25;
    QMap<QString, bool> evidence;
    evidence[QStringLiteral("unknown_key_not_in_table")] = false;

    const auto result = m_scorer.score(prior, evidence);
    QCOMPARE(result.overallLikelihoodRatio, 1.0);
    QVERIFY2(std::abs(result.posteriorProbability - prior) < kEps,
             "absent unknown evidence should leave posterior unchanged");

    const QVector<EvidenceItem> items = {
        { QStringLiteral("unknown_key_not_in_table"), false }
    };
    const auto vecResults = m_scorer.score(items, prior);
    QCOMPARE(vecResults.size(), 1);
    QCOMPARE(vecResults[0].likelihoodRatio, 1.0);
    QVERIFY2(std::abs(vecResults[0].posteriorProbability - prior) < kEps,
             "vector API absent unknown evidence should leave posterior unchanged");
}

void EvidenceScorerDeep8Test::testStrongDnaPosteriorDoesNotOverflow()
{
    QMap<QString, bool> evidence;
    evidence[QStringLiteral("dna_match_full_profile")] = true;

    const QVector<double> priors = { 1e-6, 0.001, 0.01, 0.1, 0.5 };
    for (double prior : priors) {
        const auto result = m_scorer.score(prior, evidence);
        QVERIFY2(!std::isnan(result.posteriorProbability),
                 "DNA posterior must not be NaN");
        QVERIFY2(!std::isinf(result.posteriorProbability),
                 "DNA posterior must not be infinite");
        QVERIFY2(result.posteriorProbability >= 0.0 && result.posteriorProbability <= 1.0,
                 qPrintable(QStringLiteral("DNA posterior %1 out of [0,1] for prior=%2")
                                .arg(result.posteriorProbability).arg(prior)));
        QVERIFY2(result.posteriorProbability >= 0.999,
                 qPrintable(QStringLiteral("strong DNA posterior %1 should be >= 0.999")
                                .arg(result.posteriorProbability)));
    }
}

void EvidenceScorerDeep8Test::testBayesFactorUsesLRRatioNotPosteriorOdds()
{
    QMap<QString, bool> evidence;
    evidence[QStringLiteral("dna_match_full_profile")] = true;

    const double bf = m_scorer.bayesFactor(1e-6, 0.9, evidence);
    QVERIFY2(std::abs(bf - 1.0) < 1e-9,
             qPrintable(QStringLiteral("BF should be 1 when evidence LR is prior-independent, got %1")
                            .arg(bf, 0, 'g', 17)));

    // Posterior-odds ratio would differ under saturation; LR ratio stays 1.
    const auto lowPriorResult  = m_scorer.score(1e-6, evidence);
    const auto highPriorResult = m_scorer.score(0.9, evidence);
    QCOMPARE(lowPriorResult.overallLikelihoodRatio, highPriorResult.overallLikelihoodRatio);

    const double postOddsRatio =
        (lowPriorResult.posteriorProbability / (1.0 - lowPriorResult.posteriorProbability)) /
        (highPriorResult.posteriorProbability / (1.0 - highPriorResult.posteriorProbability));
    QVERIFY2(std::abs(postOddsRatio - 1.0) > 1e-6,
             "posterior-odds ratio should differ under strong DNA; BF must not use it");
}

void EvidenceScorerDeep8Test::testUnknownEvidenceKeyIgnoredInVectorApi()
{
    constexpr double prior = 0.12;
    const QVector<EvidenceItem> items = {
        { QStringLiteral("made_up_evidence_type"), true },
        { QStringLiteral("blood_type_match"), true },
    };

    const auto results = m_scorer.score(items, prior);
    QCOMPARE(results.size(), 2);
    QCOMPARE(results[0].likelihoodRatio, 1.0);
    QVERIFY(results[0].notes.contains(QStringLiteral("Unknown evidence type")));

    const auto [bloodLR, rel] = m_scorer.getLRAndReliability(
        QStringLiteral("blood_type_match"));
    QCOMPARE(results[1].likelihoodRatio, bloodLR);
    QVERIFY2(results[1].posteriorProbability > prior,
             "known incriminating evidence should raise posterior");
}

void EvidenceScorerDeep8Test::testPosteriorAlwaysInUnitInterval()
{
    const QStringList types = m_scorer.availableEvidenceTypes();
    QVERIFY(types.size() >= 5);

    const QVector<double> priors = { 1e-6, 0.05, 0.25, 0.75, 0.999 };
    for (double prior : priors) {
        for (int i = 0; i < 5; ++i) {
            QMap<QString, bool> present;
            present[types[i]] = true;

            QMap<QString, bool> absent;
            absent[types[i]] = false;

            const auto presentResult = m_scorer.score(prior, present);
            const auto absentResult  = m_scorer.score(prior, absent);

            QVERIFY2(presentResult.posteriorProbability >= 0.0
                         && presentResult.posteriorProbability <= 1.0,
                     "present evidence posterior out of [0,1]");
            QVERIFY2(absentResult.posteriorProbability >= 0.0
                         && absentResult.posteriorProbability <= 1.0,
                     "absent evidence posterior out of [0,1]");
        }
    }
}

void EvidenceScorerDeep8Test::testAbsentIncriminatingEvidenceUsesInverseLR()
{
    constexpr double prior = 0.2;
    const QString type = QStringLiteral("fingerprint_match_8pt");
    const auto [tableLR, rel] = m_scorer.getLRAndReliability(type);

    QMap<QString, bool> absent;
    absent[type] = false;
    const auto result = m_scorer.score(prior, absent);

    const double expectedLR = 1.0 / tableLR;
    QVERIFY2(std::abs(result.overallLikelihoodRatio - expectedLR) < 1e-12,
             qPrintable(QStringLiteral("absent incriminating LR expected %1, got %2")
                            .arg(expectedLR).arg(result.overallLikelihoodRatio)));
    QVERIFY2(result.posteriorProbability < prior,
             "absent incriminating evidence should lower posterior");
}

QTEST_GUILESS_MAIN(EvidenceScorerDeep8Test)
#include "test_evidence_scorer_deep8.moc"
