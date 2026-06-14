// test_evidence_scorer_deep10.cpp — Deep audit iteration 27: EvidenceScorer
// contributions list, available types, LR lookup, multi-evidence fusion.
#include <QTest>
#include <cmath>
#include "inference/EvidenceScorer.h"

class TestEvidenceScorerDeep10 : public QObject
{
    Q_OBJECT

private slots:

    void testContributionsPopulated()
    {
        EvidenceScorer scorer;
        QMap<QString, bool> ev;
        ev[QStringLiteral("dna_match_full_profile")] = true;
        ev[QStringLiteral("fingerprint_match_10pt")]  = true;

        const auto result = scorer.score(0.1, ev);
        QVERIFY(!result.contributions.empty());
        QVERIFY(result.overallLikelihoodRatio > 1.0);
    }

    void testAvailableEvidenceTypesNonEmpty()
    {
        EvidenceScorer scorer;
        const auto types = scorer.availableEvidenceTypes();
        QVERIFY(!types.isEmpty());
        QVERIFY(types.contains(QStringLiteral("dna_match_full_profile")));
    }

    void testEvidenceTypeCountMatchesTable()
    {
        EvidenceScorer scorer;
        QCOMPARE(scorer.evidenceTypeCount(),
                 scorer.availableEvidenceTypes().size());
    }

    void testGetLRAndReliabilityValid()
    {
        EvidenceScorer scorer;
        const auto lr = scorer.getLRAndReliability(
            QStringLiteral("dna_match_full_profile"));
        QVERIFY(lr.first > 1.0);
        QVERIFY(lr.second > 0.0 && lr.second <= 1.0);
    }

    void testContradictoryEvidencePullsPosteriorTowardPrior()
    {
        EvidenceScorer scorer;
        QMap<QString, bool> supporting;
        supporting[QStringLiteral("dna_match_full_profile")] = true;

        QMap<QString, bool> contradicting;
        contradicting[QStringLiteral("dna_match_full_profile")] = false;
        contradicting[QStringLiteral("alibi_strong")]           = true;

        const auto hi = scorer.score(0.05, supporting);
        const auto lo = scorer.score(0.05, contradicting);
        QVERIFY(hi.posteriorProbability > lo.posteriorProbability);
    }
};

QTEST_GUILESS_MAIN(TestEvidenceScorerDeep10)
#include "test_evidence_scorer_deep10.moc"
