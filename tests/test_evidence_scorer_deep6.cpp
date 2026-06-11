// test_evidence_scorer_deep6.cpp — Deep audit iteration 17: EvidenceScorer
// Verifies: vector EvidenceItem API, strong DNA LR stability, multi-evidence parity.

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

class EvidenceScorerDeep6Test : public QObject
{
    Q_OBJECT

private:
    EvidenceScorer m_scorer;

private slots:

    void testVectorApiMatchesMapApiSingleEvidence()
    {
        constexpr double prior = 0.08;
        const QString type = QStringLiteral("cctv_clear_face");

        QMap<QString, bool> mapEvidence;
        mapEvidence[type] = true;
        const auto mapResult = m_scorer.score(prior, mapEvidence);

        const QVector<EvidenceItem> items = { { type, true } };
        const auto vecResults = m_scorer.score(items, prior);

        QCOMPARE(vecResults.size(), 1);
        QCOMPARE(vecResults[0].evidenceType, type);
        QVERIFY2(std::abs(vecResults[0].likelihoodRatio - mapResult.overallLikelihoodRatio) < kEps,
                 "vector LR should match map overall LR");
        QVERIFY2(std::abs(vecResults[0].posteriorProbability - mapResult.posteriorProbability) < 1e-12,
                 "vector posterior should match map posterior");
    }

    void testVectorApiMatchesMapApiMultiEvidence()
    {
        constexpr double prior = 0.05;
        QMap<QString, bool> mapEvidence;
        mapEvidence[QStringLiteral("vehicle_at_scene")]            = true;
        mapEvidence[QStringLiteral("modus_operandi_match_moderate")] = true;
        mapEvidence[QStringLiteral("alibi_strong")]               = false;

        const auto mapResult = m_scorer.score(prior, mapEvidence);

        const QVector<EvidenceItem> items = {
            { QStringLiteral("vehicle_at_scene"), true },
            { QStringLiteral("modus_operandi_match_moderate"), true },
            { QStringLiteral("alibi_strong"), false },
        };
        const auto vecResults = m_scorer.score(items, prior);
        QCOMPARE(vecResults.size(), 3);

        double runningProduct = 1.0;
        for (const auto& ew : vecResults)
            runningProduct *= ew.likelihoodRatio;

        QVERIFY2(std::abs(runningProduct - mapResult.overallLikelihoodRatio) < 1e-10,
                 qPrintable(QStringLiteral("vector product LR %1 vs map %2")
                     .arg(runningProduct).arg(mapResult.overallLikelihoodRatio)));
        QVERIFY2(std::abs(vecResults.back().posteriorProbability - mapResult.posteriorProbability) < 1e-12,
                 "final vector posterior should match map posterior");
    }

    void testVectorIncrementalPosteriorChain()
    {
        constexpr double prior = 0.1;
        const QVector<EvidenceItem> items = {
            { QStringLiteral("blood_type_match"), true },
            { QStringLiteral("informant_tip_reliable"), true },
            { QStringLiteral("alibi_weak"), false },
        };

        const auto results = m_scorer.score(items, prior);
        QCOMPARE(results.size(), 3);

        double runningPrior = prior;
        for (const auto& ew : results) {
            const double expected = posteriorFromLR(runningPrior, ew.likelihoodRatio);
            QVERIFY2(std::abs(ew.posteriorProbability - expected) < 1e-10,
                     qPrintable(QStringLiteral("step '%1': expected %2, got %3")
                         .arg(ew.evidenceType).arg(expected).arg(ew.posteriorProbability)));
            runningPrior = ew.posteriorProbability;
        }
    }

    void testStrongDnaPosteriorNearOneNoNaN()
    {
        QMap<QString, bool> evidence;
        evidence[QStringLiteral("dna_match_full_profile")] = true;

        const QVector<double> priors = { 1e-6, 0.001, 0.01, 0.1, 0.5 };
        for (double prior : priors) {
            const auto mapResult = m_scorer.score(prior, evidence);
            QVERIFY2(!std::isnan(mapResult.posteriorProbability),
                     "DNA posterior must not be NaN");
            QVERIFY2(!std::isinf(mapResult.posteriorProbability),
                     "DNA posterior must not be infinite");
            QVERIFY2(mapResult.posteriorProbability > 0.999,
                     qPrintable(QStringLiteral("DNA posterior %1 (prior=%2) should be > 0.999")
                         .arg(mapResult.posteriorProbability).arg(prior)));

            const QVector<EvidenceItem> items = {
                { QStringLiteral("dna_match_full_profile"), true }
            };
            const auto vecResults = m_scorer.score(items, prior);
            QCOMPARE(vecResults.size(), 1);
            QVERIFY2(std::abs(vecResults[0].posteriorProbability - mapResult.posteriorProbability) < 1e-12,
                     "vector DNA posterior should match map API");
        }
    }

    void testStrongDnaOverallLRStable()
    {
        QMap<QString, bool> evidence;
        evidence[QStringLiteral("dna_match_full_profile")] = true;

        const auto [tableLR, rel] = m_scorer.getLRAndReliability(
            QStringLiteral("dna_match_full_profile"));
        QCOMPARE(tableLR, 1e9);

        const auto lowPrior  = m_scorer.score(0.001, evidence);
        const auto highPrior = m_scorer.score(0.9, evidence);

        QCOMPARE(lowPrior.overallLikelihoodRatio, tableLR);
        QCOMPARE(highPrior.overallLikelihoodRatio, tableLR);

        const double bf = m_scorer.bayesFactor(0.001, 0.9, evidence);
        QVERIFY2(std::abs(bf - 1.0) < 1e-9,
                 qPrintable(QStringLiteral("DNA BF should be 1 (prior-independent), got %1")
                     .arg(bf, 0, 'g', 17)));
    }

    void testMultiEvidenceVectorProductLR()
    {
        constexpr double prior = 0.12;
        const QVector<EvidenceItem> items = {
            { QStringLiteral("fingerprint_match_8pt"), true },
            { QStringLiteral("shoe_impression_match"), true },
            { QStringLiteral("no_alibi"), true },
        };

        const auto results = m_scorer.score(items, prior);
        QCOMPARE(results.size(), 3);

        const auto [lrFp, relFp] = m_scorer.getLRAndReliability(
            QStringLiteral("fingerprint_match_8pt"));
        const auto [lrShoe, relShoe] = m_scorer.getLRAndReliability(
            QStringLiteral("shoe_impression_match"));
        const auto [lrNoAlibi, relNoAlibi] = m_scorer.getLRAndReliability(
            QStringLiteral("no_alibi"));

        const double expectedLR = lrFp * lrShoe * lrNoAlibi;
        const double actualLR   = results.back().posteriorOdds / (prior / (1.0 - prior));
        QVERIFY2(std::abs(actualLR - expectedLR) / expectedLR < 1e-12,
                 qPrintable(QStringLiteral("multi-evidence LR expected %1, got %2")
                     .arg(expectedLR).arg(actualLR)));

        const double expectedPost = posteriorFromOdds(prior, expectedLR);
        QVERIFY2(std::abs(results.back().posteriorProbability - expectedPost) < 1e-10,
                 qPrintable(QStringLiteral("multi-evidence posterior expected %1, got %2")
                     .arg(expectedPost).arg(results.back().posteriorProbability)));
    }

    void testVectorEmptyReturnsEmpty()
    {
        const auto results = m_scorer.score(QVector<EvidenceItem>{}, 0.2);
        QVERIFY(results.isEmpty());
    }

    void testMapAndVectorMixedExculpatoryMultiEvidence()
    {
        constexpr double prior = 0.15;
        QMap<QString, bool> mapEvidence;
        mapEvidence[QStringLiteral("dna_match_partial")] = true;
        mapEvidence[QStringLiteral("alibi_strong")]      = false;
        mapEvidence[QStringLiteral("cctv_partial")]      = true;

        const auto mapResult = m_scorer.score(prior, mapEvidence);

        const QVector<EvidenceItem> items = {
            { QStringLiteral("dna_match_partial"), true },
            { QStringLiteral("alibi_strong"), false },
            { QStringLiteral("cctv_partial"), true },
        };
        const auto vecResults = m_scorer.score(items, prior);

        const double expectedLR = 1e4 * (1.0 / 0.05) * 5.0;
        QVERIFY2(std::abs(mapResult.overallLikelihoodRatio - expectedLR) < 1e-6,
                 qPrintable(QStringLiteral("map combined LR expected %1, got %2")
                     .arg(expectedLR).arg(mapResult.overallLikelihoodRatio)));
        QVERIFY2(std::abs(vecResults.back().posteriorProbability - mapResult.posteriorProbability) < 1e-12,
                 "mixed exculpatory multi-evidence posteriors must agree");
    }
};

QTEST_GUILESS_MAIN(EvidenceScorerDeep6Test)
#include "test_evidence_scorer_deep6.moc"
