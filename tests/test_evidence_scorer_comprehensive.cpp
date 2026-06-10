// ─────────────────────────────────────────────────────────────────────────────
// test_evidence_scorer_comprehensive.cpp
//
// Comprehensive tests for EvidenceScorer covering:
//   - Prior preservation with no evidence
//   - Bayesian update direction (positive / negative evidence)
//   - Posterior clamping to [0, 1]
//   - Evidence-type catalogue integrity
//   - Both score() overloads (list API and map API)
//   - Contribution accounting and overall-LR product
//   - Bayesian update formula correctness
//   - Weak-evidence accumulation and single-strong-evidence dominance
// ─────────────────────────────────────────────────────────────────────────────
#include <QTest>
#include <QCoreApplication>
#include <QMap>
#include <QStringList>
#include <cmath>
#include <numeric>

#include "inference/EvidenceScorer.h"

class TestEvidenceScorerComprehensive : public QObject {
    Q_OBJECT
private slots:
    void testPriorProbabilityPreservedWithNoEvidence();
    void testProbabilityIncreasesWithPositiveEvidence();
    void testProbabilityDecreasesWithNegativeEvidence();
    void testPosteriorCappedAt1();
    void testPosteriorAbove0();
    void testAvailableEvidenceTypesNonEmpty();
    void testLRAndReliabilityNonNegative();
    void testMapAPIWithAllPresent();
    void testMapAPIWithNonePresent();
    void testContributions();
    void testOverallLRProduct();
    void testBayesianUpdateFormula();
    void testMultipleWeakEvidenceCombines();
    void testStrongSingleEvidence();
};

// ─────────────────────────────────────────────────────────────────────────────
// 1. No evidence → posterior equals prior exactly (map overload with empty map)
// ─────────────────────────────────────────────────────────────────────────────
void TestEvidenceScorerComprehensive::testPriorProbabilityPreservedWithNoEvidence()
{
    EvidenceScorer scorer;
    const double prior = 0.17;

    auto result = scorer.score(prior, QMap<QString, bool>{});

    QVERIFY2(std::abs(result.posteriorProbability - prior) < 1e-9,
             qPrintable(QString("posterior=%1  expected prior=%2")
                        .arg(result.posteriorProbability).arg(prior)));
    QVERIFY2(std::abs(result.overallLikelihoodRatio - 1.0) < 1e-9,
             "overallLikelihoodRatio should be 1.0 with no evidence");
}

// ─────────────────────────────────────────────────────────────────────────────
// 2. Positive evidence (LR > 1, present) raises the posterior above the prior
// ─────────────────────────────────────────────────────────────────────────────
void TestEvidenceScorerComprehensive::testProbabilityIncreasesWithPositiveEvidence()
{
    EvidenceScorer scorer;
    const double prior = 0.10;

    // fingerprint_match_10pt has a very high LR (> 1)
    QVector<EvidenceItem> ev = {{ "fingerprint_match_10pt", true }};
    auto results = scorer.score(ev, prior);

    QVERIFY(!results.isEmpty());
    const double posterior = results.last().posteriorProbability;
    QVERIFY2(posterior > prior,
             qPrintable(QString("posterior=%1 should be > prior=%2").arg(posterior).arg(prior)));
}

// ─────────────────────────────────────────────────────────────────────────────
// 3. Absent positive evidence (LR effectively < 1 when absent) decreases
//    the posterior below the prior
// ─────────────────────────────────────────────────────────────────────────────
void TestEvidenceScorerComprehensive::testProbabilityDecreasesWithNegativeEvidence()
{
    EvidenceScorer scorer;
    const double prior = 0.5;

    // cctv_clear_face absent: if CCTV were expected and not found, it exculpates
    QVector<EvidenceItem> ev = {{ "cctv_clear_face", false }};
    auto results = scorer.score(ev, prior);

    QVERIFY(!results.isEmpty());
    const double posterior = results.last().posteriorProbability;
    QVERIFY2(posterior < prior,
             qPrintable(QString("posterior=%1 should be < prior=%2").arg(posterior).arg(prior)));
}

// ─────────────────────────────────────────────────────────────────────────────
// 4. Posterior never exceeds 1.0, even with extremely strong positive evidence
// ─────────────────────────────────────────────────────────────────────────────
void TestEvidenceScorerComprehensive::testPosteriorCappedAt1()
{
    EvidenceScorer scorer;

    // Stack the highest-LR evidence many times
    QVector<EvidenceItem> ev;
    for (int i = 0; i < 10; ++i)
        ev << EvidenceItem{ "fingerprint_match_10pt", true };

    auto results = scorer.score(ev, 0.5);
    QVERIFY(!results.isEmpty());

    for (const auto& r : results) {
        QVERIFY2(r.posteriorProbability <= 1.0,
                 qPrintable(QString("posteriorProbability=%1 exceeds 1.0")
                            .arg(r.posteriorProbability)));
    }

    // Also test the map overload
    EvidenceScorer scorer2;
    QStringList types = scorer2.availableEvidenceTypes();
    QMap<QString, bool> allPresent;
    for (const auto& t : types)
        allPresent[t] = true;

    auto result = scorer2.score(0.99, allPresent);
    QVERIFY2(result.posteriorProbability <= 1.0,
             qPrintable(QString("Map-overload posterior=%1 exceeds 1.0")
                        .arg(result.posteriorProbability)));
}

// ─────────────────────────────────────────────────────────────────────────────
// 5. Posterior is always strictly greater than 0
// ─────────────────────────────────────────────────────────────────────────────
void TestEvidenceScorerComprehensive::testPosteriorAbove0()
{
    EvidenceScorer scorer;

    // Stack exculpatory evidence heavily
    QVector<EvidenceItem> ev;
    for (int i = 0; i < 10; ++i)
        ev << EvidenceItem{ "alibi_strong", true };

    auto results = scorer.score(ev, 0.9);
    QVERIFY(!results.isEmpty());

    for (const auto& r : results) {
        QVERIFY2(r.posteriorProbability > 0.0,
                 qPrintable(QString("posteriorProbability=%1 is not > 0")
                            .arg(r.posteriorProbability)));
    }

    // Also map overload
    QStringList types = scorer.availableEvidenceTypes();
    QMap<QString, bool> allAbsent;
    for (const auto& t : types)
        allAbsent[t] = false;

    auto result = scorer.score(0.01, allAbsent);
    QVERIFY2(result.posteriorProbability > 0.0,
             qPrintable(QString("Map-overload posterior=%1 is not > 0")
                        .arg(result.posteriorProbability)));
}

// ─────────────────────────────────────────────────────────────────────────────
// 6. availableEvidenceTypes() returns at least 5 distinct, non-empty strings
// ─────────────────────────────────────────────────────────────────────────────
void TestEvidenceScorerComprehensive::testAvailableEvidenceTypesNonEmpty()
{
    EvidenceScorer scorer;
    QStringList types = scorer.availableEvidenceTypes();

    QVERIFY2(types.size() >= 5,
             qPrintable(QString("Only %1 evidence type(s) available; expected >= 5")
                        .arg(types.size())));

    for (const auto& t : types) {
        QVERIFY2(!t.isEmpty(), "Evidence type string must not be empty");
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// 7. getLRAndReliability() returns LR > 0 and reliability in (0, 1] for every
//    type listed by availableEvidenceTypes()
// ─────────────────────────────────────────────────────────────────────────────
void TestEvidenceScorerComprehensive::testLRAndReliabilityNonNegative()
{
    EvidenceScorer scorer;
    const QStringList types = scorer.availableEvidenceTypes();

    QVERIFY(!types.isEmpty());
    for (const auto& t : types) {
        const auto [lr, rel] = scorer.getLRAndReliability(t);
        QVERIFY2(lr > 0.0,
                 qPrintable(QString("Type '%1' has LR=%2 which is not > 0").arg(t).arg(lr)));
        QVERIFY2(std::isfinite(lr),
                 qPrintable(QString("Type '%1' has non-finite LR").arg(t)));
        QVERIFY2(rel >= 0.0 && rel <= 1.0,
                 qPrintable(QString("Type '%1' reliability=%2 out of [0,1]").arg(t).arg(rel)));
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// 8. Map API with all types present raises posterior well above the prior
// ─────────────────────────────────────────────────────────────────────────────
void TestEvidenceScorerComprehensive::testMapAPIWithAllPresent()
{
    EvidenceScorer scorer;
    const double prior = 0.1;

    QStringList types = scorer.availableEvidenceTypes();
    QMap<QString, bool> allPresent;
    for (const auto& t : types)
        allPresent[t] = true;

    auto result = scorer.score(prior, allPresent);

    QVERIFY2(result.posteriorProbability > prior,
             qPrintable(QString("posterior=%1 should be > prior=%2 when all evidence present")
                        .arg(result.posteriorProbability).arg(prior)));
    QVERIFY2(result.posteriorProbability > 0.5,
             qPrintable(QString("posterior=%1 should be > 0.5 with overwhelming positive evidence")
                        .arg(result.posteriorProbability)));
}

// ─────────────────────────────────────────────────────────────────────────────
// 9. Map API with all types absent decreases posterior below the prior
// ─────────────────────────────────────────────────────────────────────────────
void TestEvidenceScorerComprehensive::testMapAPIWithNonePresent()
{
    EvidenceScorer scorer;
    const double prior = 0.9;

    QStringList types = scorer.availableEvidenceTypes();
    QMap<QString, bool> allAbsent;
    for (const auto& t : types)
        allAbsent[t] = false;

    auto result = scorer.score(prior, allAbsent);

    QVERIFY2(result.posteriorProbability < prior,
             qPrintable(QString("posterior=%1 should be < prior=%2 when all positive evidence absent")
                        .arg(result.posteriorProbability).arg(prior)));
}

// ─────────────────────────────────────────────────────────────────────────────
// 10. Result.contributions has exactly one entry per evidence type in the map
// ─────────────────────────────────────────────────────────────────────────────
void TestEvidenceScorerComprehensive::testContributions()
{
    EvidenceScorer scorer;

    QMap<QString, bool> presence;
    presence["fingerprint_match_10pt"]         = true;
    presence["eyewitness_identification_ideal"] = true;
    presence["alibi_strong"]                    = false;
    presence["cctv_clear_face"]                 = true;

    auto result = scorer.score(0.2, presence);

    QVERIFY2(static_cast<int>(result.contributions.size()) == presence.size(),
             qPrintable(QString("contributions.size()=%1 but expected %2")
                        .arg(result.contributions.size())
                        .arg(presence.size())));

    for (const auto& c : result.contributions) {
        QVERIFY2(!c.name.isEmpty(), "Contribution name must be non-empty");
        QVERIFY2(c.likelihoodRatio > 0.0,
                 qPrintable(QString("Contribution '%1' has LR=%2 which is not > 0")
                            .arg(c.name).arg(c.likelihoodRatio)));
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// 11. overallLikelihoodRatio equals the product of the per-type effective LRs
//     (effective LR = LR when present, 1/LR when absent)
// ─────────────────────────────────────────────────────────────────────────────
void TestEvidenceScorerComprehensive::testOverallLRProduct()
{
    EvidenceScorer scorer;

    QMap<QString, bool> presence;
    presence["fingerprint_match_10pt"] = true;
    presence["cctv_clear_face"]        = false;

    auto result = scorer.score(0.2, presence);

    // Compute expected product from individual contribution LRs
    double product = 1.0;
    for (const auto& c : result.contributions)
        product *= c.likelihoodRatio;

    QVERIFY2(std::abs(result.overallLikelihoodRatio - product) < 1e-6,
             qPrintable(QString("overallLR=%1 but product of individual LRs=%2")
                        .arg(result.overallLikelihoodRatio).arg(product)));
}

// ─────────────────────────────────────────────────────────────────────────────
// 12. Single evidence update follows the standard Bayesian formula:
//     posterior = LR * prior / (LR * prior + 1 − prior)
// ─────────────────────────────────────────────────────────────────────────────
void TestEvidenceScorerComprehensive::testBayesianUpdateFormula()
{
    EvidenceScorer scorer;
    const double prior = 0.25;
    const QString type = "cctv_clear_face";

    // Get the table LR for this type
    const auto [lr, rel] = scorer.getLRAndReliability(type);
    Q_UNUSED(rel)

    QMap<QString, bool> presence;
    presence[type] = true;

    auto result = scorer.score(prior, presence);

    const double expectedPosterior = (lr * prior) / (lr * prior + (1.0 - prior));

    QVERIFY2(std::abs(result.posteriorProbability - expectedPosterior) < 1e-6,
             qPrintable(QString("posterior=%1  expected=%2  (lr=%3, prior=%4)")
                        .arg(result.posteriorProbability)
                        .arg(expectedPosterior)
                        .arg(lr).arg(prior)));
}

// ─────────────────────────────────────────────────────────────────────────────
// 13. Multiple weak pieces of evidence combine to produce a stronger update
//     than any single piece on its own
// ─────────────────────────────────────────────────────────────────────────────
void TestEvidenceScorerComprehensive::testMultipleWeakEvidenceCombines()
{
    EvidenceScorer scorer;
    const double prior = 0.1;

    // Pick a weak evidence type (moderate MO match, LR ~ 3)
    QVector<EvidenceItem> singleEv = {{ "modus_operandi_match_moderate", true }};
    auto singleResult = scorer.score(singleEv, prior);
    QVERIFY(!singleResult.isEmpty());
    const double singlePosterior = singleResult.last().posteriorProbability;

    // Stack three copies — should produce a much higher posterior
    QVector<EvidenceItem> multiEv;
    for (int i = 0; i < 3; ++i)
        multiEv << EvidenceItem{ "modus_operandi_match_moderate", true };

    auto multiResult = scorer.score(multiEv, prior);
    QVERIFY(!multiResult.isEmpty());
    const double multiPosterior = multiResult.last().posteriorProbability;

    QVERIFY2(multiPosterior > singlePosterior,
             qPrintable(QString("multi-evidence posterior=%1 should exceed single posterior=%2")
                        .arg(multiPosterior).arg(singlePosterior)));
}

// ─────────────────────────────────────────────────────────────────────────────
// 14. A single very high-LR evidence type dominates: even from a low prior the
//     posterior should be substantially above 0.5
// ─────────────────────────────────────────────────────────────────────────────
void TestEvidenceScorerComprehensive::testStrongSingleEvidence()
{
    EvidenceScorer scorer;
    const double prior = 0.001;   // extremely low prior

    QVector<EvidenceItem> ev = {{ "fingerprint_match_10pt", true }};
    auto results = scorer.score(ev, prior);

    QVERIFY(!results.isEmpty());
    const double posterior = results.last().posteriorProbability;

    QVERIFY2(posterior > 0.5,
             qPrintable(QString("posterior=%1 should be > 0.5 with single very high-LR evidence "
                                "even from prior=%2").arg(posterior).arg(prior)));
}

// ─────────────────────────────────────────────────────────────────────────────

QTEST_MAIN(TestEvidenceScorerComprehensive)
#include "test_evidence_scorer_comprehensive.moc"
