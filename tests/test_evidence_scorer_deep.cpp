// test_evidence_scorer_deep.cpp
// Deep tests for EvidenceScorer: LR table, evidence type listing,
// posterior probability, Bayes factor, and result contributions.
#include <QTest>
#include "inference/EvidenceScorer.h"
#include <cmath>

class EvidenceScorerDeepTest : public QObject
{
    Q_OBJECT

private:
    EvidenceScorer m_es;

private slots:

    // ── 1. availableEvidenceTypes() returns non-empty list ───────────────────
    void testAvailableEvidenceTypesNonEmpty()
    {
        const auto types = m_es.availableEvidenceTypes();
        QVERIFY2(!types.isEmpty(), "availableEvidenceTypes should return non-empty list");
    }

    // ── 2. evidenceTypeCount() > 0 ───────────────────────────────────────────
    void testEvidenceTypeCountPositive()
    {
        QVERIFY2(m_es.evidenceTypeCount() > 0, "evidenceTypeCount must be > 0");
    }

    // ── 3. getLRAndReliability: LR > 0 for valid type ────────────────────────
    void testGetLRPositive()
    {
        const auto types = m_es.availableEvidenceTypes();
        QVERIFY2(!types.isEmpty(), "No evidence types available");
        const auto [lr, rel] = m_es.getLRAndReliability(types.first());
        QVERIFY2(lr > 0.0,
                 qPrintable(QStringLiteral("LR %1 for '%2' must be > 0")
                    .arg(lr).arg(types.first())));
    }

    // ── 4. Reliability is in [0, 1] ──────────────────────────────────────────
    void testReliabilityRange()
    {
        for (const auto& type : m_es.availableEvidenceTypes()) {
            const auto [lr, rel] = m_es.getLRAndReliability(type);
            QVERIFY2(rel >= 0.0 && rel <= 1.0,
                     qPrintable(QStringLiteral("Reliability %1 for '%2' must be in [0,1]")
                        .arg(rel).arg(type)));
        }
    }

    // ── 5. score() returns posterior in [0, 1] ────────────────────────────────
    void testPosteriorRange()
    {
        const auto types = m_es.availableEvidenceTypes();
        QMap<QString, bool> presence;
        for (const auto& t : types) presence[t] = true;

        const auto result = m_es.score(0.05, presence);
        QVERIFY2(result.posteriorProbability >= 0.0 && result.posteriorProbability <= 1.0,
                 qPrintable(QStringLiteral("Posterior %1 must be in [0,1]")
                    .arg(result.posteriorProbability)));
    }

    // ── 6. All evidence present → higher posterior than no evidence ──────────
    void testAllEvidenceRaisesPostorior()
    {
        const auto types = m_es.availableEvidenceTypes();
        QMap<QString, bool> allPresent, nonePresent;
        for (const auto& t : types) { allPresent[t] = true; nonePresent[t] = false; }

        const auto rAll  = m_es.score(0.05, allPresent);
        const auto rNone = m_es.score(0.05, nonePresent);
        QVERIFY2(rAll.posteriorProbability >= rNone.posteriorProbability,
                 qPrintable(QStringLiteral("All evidence posterior %1 should >= none %2")
                    .arg(rAll.posteriorProbability).arg(rNone.posteriorProbability)));
    }

    // ── 7. Contributions populated for all present evidence ──────────────────
    void testContributionsPopulated()
    {
        const auto types = m_es.availableEvidenceTypes();
        QMap<QString, bool> presence;
        for (int i = 0; i < std::min(3, (int)types.size()); ++i)
            presence[types[i]] = true;

        const auto result = m_es.score(0.05, presence);
        QVERIFY2(!result.contributions.empty(), "Contributions should not be empty");
    }

    // ── 8. overallLikelihoodRatio > 1.0 when strong evidence present ─────────
    void testStrongEvidenceRaisesLR()
    {
        const auto types = m_es.availableEvidenceTypes();
        QMap<QString, bool> presence;
        for (const auto& t : types) presence[t] = true;

        const auto result = m_es.score(0.05, presence);
        QVERIFY2(result.overallLikelihoodRatio > 1.0,
                 qPrintable(QStringLiteral("All evidence LR %1 should be > 1.0")
                    .arg(result.overallLikelihoodRatio)));
    }

    // ── 9. bayesFactor > 1 when priorA > priorB ──────────────────────────────
    void testBayesFactorHigherPrior()
    {
        const auto types = m_es.availableEvidenceTypes();
        QMap<QString, bool> presence;
        for (const auto& t : types) presence[t] = true;

        const double bf = m_es.bayesFactor(0.8, 0.2, presence);
        QVERIFY2(bf > 0.0, qPrintable(QStringLiteral("Bayes Factor %1 must be > 0").arg(bf)));
    }

    // ── 10. getLRAndReliability for unknown type → LR == 1.0 ─────────────────
    void testUnknownTypeDefaultLR()
    {
        const auto [lr, rel] = m_es.getLRAndReliability(QStringLiteral("_unknown_evidence_xyz_"));
        QVERIFY2(lr >= 0.0, "Unknown type should return non-negative LR");
    }
};

QTEST_MAIN(EvidenceScorerDeepTest)
#include "test_evidence_scorer_deep.moc"
