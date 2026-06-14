// test_evidence_scorer_deep9.cpp — Deep audit iteration 25: EvidenceScorer
// Bayes factor, prior edges, LR table, posterior bounds.
#include <QTest>
#include <cmath>
#include "inference/EvidenceScorer.h"

class TestEvidenceScorerDeep9 : public QObject
{
    Q_OBJECT

private slots:

    void testPosteriorInUnitInterval()
    {
        EvidenceScorer scorer;
        QMap<QString, bool> ev;
        ev[QStringLiteral("dna_match_full_profile")] = true;
        const auto result = scorer.score(0.05, ev);
        QVERIFY2(result.posteriorProbability >= 0.0
                     && result.posteriorProbability <= 1.0,
                 qPrintable(QStringLiteral("post=%1").arg(result.posteriorProbability)));
    }

    void testPriorZeroHandled()
    {
        EvidenceScorer scorer;
        const auto result = scorer.score(0.0, {});
        QVERIFY2(result.posteriorProbability >= 0.0,
                 qPrintable(QStringLiteral("post=%1").arg(result.posteriorProbability)));
    }

    void testPriorOneHandled()
    {
        EvidenceScorer scorer;
        const auto result = scorer.score(1.0, {});
        QVERIFY2(result.posteriorProbability <= 1.0,
                 qPrintable(QStringLiteral("post=%1").arg(result.posteriorProbability)));
    }

    void testBayesFactorGreaterThanOneWithSupportingEvidence()
    {
        EvidenceScorer scorer;
        QMap<QString, bool> ev;
        ev[QStringLiteral("dna_match_full_profile")] = true;
        const double bf = scorer.bayesFactor(0.5, 0.5, ev);
        QVERIFY2(bf >= 1.0, qPrintable(QStringLiteral("bf=%1").arg(bf)));
    }

    void testAbsentEvidenceLRBelowOne()
    {
        EvidenceScorer scorer;
        QMap<QString, bool> ev;
        ev[QStringLiteral("dna_match_full_profile")] = false;
        const auto result = scorer.score(0.5, ev);
        QVERIFY2(result.overallLikelihoodRatio <= 1.0,
                 qPrintable(QStringLiteral("lr=%1").arg(result.overallLikelihoodRatio)));
    }

    void testAvailableEvidenceTypesNonEmpty()
    {
        EvidenceScorer scorer;
        QVERIFY(scorer.availableEvidenceTypes().size() >= 5);
        QVERIFY(scorer.evidenceTypeCount() >= 5);
    }

    void testContributionsMatchPresence()
    {
        EvidenceScorer scorer;
        QMap<QString, bool> ev;
        ev[QStringLiteral("fingerprint_match_10pt")] = true;
        const auto result = scorer.score(0.1, ev);
        QVERIFY(!result.contributions.empty());
        bool found = false;
        for (const auto& c : result.contributions) {
            if (c.wasPresent) found = true;
        }
        QVERIFY(found);
    }
};

QTEST_GUILESS_MAIN(TestEvidenceScorerDeep9)
#include "test_evidence_scorer_deep9.moc"
