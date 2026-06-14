// test_evidence_scorer_deep11.cpp — Deep audit iteration 29: EvidenceScorer
// bayesFactor, QVector API, posterior bounds, absent evidence baseline.
#include <QTest>
#include <cmath>
#include "inference/EvidenceScorer.h"

class TestEvidenceScorerDeep11 : public QObject
{
    Q_OBJECT

private slots:

    void testBayesFactorUnityForSharedEvidence()
    {
        EvidenceScorer scorer;
        QMap<QString, bool> ev;
        ev[QStringLiteral("dna_match_full_profile")] = true;

        const double bf = scorer.bayesFactor(0.01, 0.99, ev);
        QVERIFY(std::abs(bf - 1.0) < 1e-6);
    }

    void testVectorScoreApiReturnsWeights()
    {
        EvidenceScorer scorer;
        QVector<EvidenceItem> items;
        EvidenceItem dna;
        dna.type    = QStringLiteral("dna_match_full_profile");
        dna.present = true;
        items.append(dna);

        const auto weights = scorer.score(items, 0.05);
        QVERIFY(!weights.isEmpty());
        QVERIFY(weights.first().posteriorProbability > 0.05);
    }

    void testPosteriorStaysInUnitInterval()
    {
        EvidenceScorer scorer;
        QMap<QString, bool> ev;
        ev[QStringLiteral("fingerprint_match_10pt")] = true;
        ev[QStringLiteral("alibi_strong")]           = true;

        const auto result = scorer.score(0.3, ev);
        QVERIFY(result.posteriorProbability >= 0.0);
        QVERIFY(result.posteriorProbability <= 1.0);
    }

    void testEmptyEvidenceMapNearPrior()
    {
        EvidenceScorer scorer;
        const double prior = 0.12;
        const auto result = scorer.score(prior, {});
        QVERIFY(std::abs(result.posteriorProbability - prior) < 0.05);
    }

    void testExculpatoryEvidenceLowersPosterior()
    {
        EvidenceScorer scorer;
        QMap<QString, bool> inc;
        inc[QStringLiteral("alibi_strong")] = false;

        QMap<QString, bool> exc;
        exc[QStringLiteral("alibi_strong")] = true;

        const auto neutral = scorer.score(0.2, inc);
        const auto guilty  = scorer.score(0.2, exc);
        QVERIFY(neutral.posteriorProbability > guilty.posteriorProbability);
    }
};

QTEST_GUILESS_MAIN(TestEvidenceScorerDeep11)
#include "test_evidence_scorer_deep11.moc"
