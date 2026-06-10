// ─────────────────────────────────────────────────────────────────────────────
// TestEvidenceScorer — unit tests for EvidenceScorer
//
// Coverage:
//   Bayesian prior update (positive/negative evidence), multiple updates
//   converging to extremes, balanced evidence near 0.5, [0,1] bounds,
//   both score() overloads, EvidenceWeight struct fields, UI Result struct.
// ─────────────────────────────────────────────────────────────────────────────
#include <QTest>
#include <QCoreApplication>
#include <QMap>
#include <cmath>

#include "inference/EvidenceScorer.h"

class TestEvidenceScorer : public QObject {
    Q_OBJECT
private slots:
    void testConstruction();
    void testAvailableEvidenceTypesNotEmpty();
    void testPositiveEvidenceRaisesPosterior();
    void testNegativeEvidenceLowersPosterior();
    void testMultiplePositiveUpdatesConvergeToOne();
    void testMultipleNegativeUpdatesConvergeToZero();
    void testBalancedEvidenceStaysNearPrior();
    void testAllProbabilitiesInUnitInterval();
    void testScoreReturnsValidFloat();
    void testResultStructurePopulated();
    void testUIScoreOverloadPositiveEvidence();
    void testUIScoreOverloadNegativeEvidence();
    void testUIScorePosteriorInUnitInterval();
    void testGetLRAndReliability();
    void testUnknownTypeIsNeutral();
    void testAbsentEvidenceInvertsEffect();
    void testLowPriorWithStrongEvidenceSurpasses50pct();
    void testHighPriorWithStrongNegativeDropsBelow50pct();
};

// ─────────────────────────────────────────────────────────────────────────────
// 1. Construction succeeds without errors
// ─────────────────────────────────────────────────────────────────────────────
void TestEvidenceScorer::testConstruction()
{
    EvidenceScorer scorer;
    Q_UNUSED(scorer)
    QVERIFY(true);   // just reaches here without crashing
}

// ─────────────────────────────────────────────────────────────────────────────
// 2. availableEvidenceTypes() returns a non-empty list
// ─────────────────────────────────────────────────────────────────────────────
void TestEvidenceScorer::testAvailableEvidenceTypesNotEmpty()
{
    EvidenceScorer scorer;
    QVERIFY(!scorer.availableEvidenceTypes().isEmpty());
}

// ─────────────────────────────────────────────────────────────────────────────
// 3. Positive (inculpatory) evidence raises posterior above prior
// ─────────────────────────────────────────────────────────────────────────────
void TestEvidenceScorer::testPositiveEvidenceRaisesPosterior()
{
    EvidenceScorer scorer;
    const double prior = 0.10;
    QVector<EvidenceItem> ev = {{ "modus_operandi_match_high", true }};

    auto results = scorer.score(ev, prior);
    QVERIFY(!results.isEmpty());
    QVERIFY2(results.last().posteriorProbability > prior,
             qPrintable(QString("posterior=%1 should be > prior=%2")
                        .arg(results.last().posteriorProbability).arg(prior)));
}

// ─────────────────────────────────────────────────────────────────────────────
// 4. Negative (exculpatory) evidence lowers posterior below prior
//    alibi_strong has LR = 0.05 < 1
// ─────────────────────────────────────────────────────────────────────────────
void TestEvidenceScorer::testNegativeEvidenceLowersPosterior()
{
    EvidenceScorer scorer;
    const double prior = 0.50;
    QVector<EvidenceItem> ev = {{ "alibi_strong", true }};

    auto results = scorer.score(ev, prior);
    QVERIFY(!results.isEmpty());
    QVERIFY2(results.last().posteriorProbability < prior,
             qPrintable(QString("posterior=%1 should be < prior=%2")
                        .arg(results.last().posteriorProbability).arg(prior)));
}

// ─────────────────────────────────────────────────────────────────────────────
// 5. Stacking many strong positive pieces of evidence pushes posterior → 1
// ─────────────────────────────────────────────────────────────────────────────
void TestEvidenceScorer::testMultiplePositiveUpdatesConvergeToOne()
{
    EvidenceScorer scorer;
    const double prior = 0.05;

    // Repeat the highest-LR evidence type 3 times to push posterior high
    QVector<EvidenceItem> ev;
    for (int i = 0; i < 3; ++i)
        ev << EvidenceItem{ "fingerprint_match_10pt", true };

    auto results = scorer.score(ev, prior);
    QVERIFY(!results.isEmpty());
    QVERIFY2(results.last().posteriorProbability > 0.999,
             qPrintable(QString("posterior=%1 expected > 0.999")
                        .arg(results.last().posteriorProbability)));
}

// ─────────────────────────────────────────────────────────────────────────────
// 6. Stacking many negative pieces of evidence pushes posterior → 0
// ─────────────────────────────────────────────────────────────────────────────
void TestEvidenceScorer::testMultipleNegativeUpdatesConvergeToZero()
{
    EvidenceScorer scorer;
    const double prior = 0.80;

    // alibi_strong LR=0.05 applied several times
    QVector<EvidenceItem> ev;
    for (int i = 0; i < 5; ++i)
        ev << EvidenceItem{ "alibi_strong", true };

    auto results = scorer.score(ev, prior);
    QVERIFY(!results.isEmpty());
    QVERIFY2(results.last().posteriorProbability < 0.01,
             qPrintable(QString("posterior=%1 expected < 0.01")
                        .arg(results.last().posteriorProbability)));
}

// ─────────────────────────────────────────────────────────────────────────────
// 7. Balanced evidence (one inculpatory + one equally strong exculpatory,
//    starting from prior=0.5) keeps posterior near 0.5.
//    modus_operandi_match_moderate LR=3, and its absent counterpart ~1/3 cancel.
// ─────────────────────────────────────────────────────────────────────────────
void TestEvidenceScorer::testBalancedEvidenceStaysNearPrior()
{
    EvidenceScorer scorer;
    const double prior = 0.5;

    // present (LR=3) followed by absent (effective LR=1/3) on same type
    QVector<EvidenceItem> ev = {
        { "modus_operandi_match_moderate", true  },   // LR=3
        { "modus_operandi_match_moderate", false }    // LR=1/3
    };

    auto results = scorer.score(ev, prior);
    QVERIFY(!results.isEmpty());
    const double posterior = results.last().posteriorProbability;
    QVERIFY2(std::abs(posterior - 0.5) < 0.05,
             qPrintable(QString("posterior=%1 expected near 0.5").arg(posterior)));
}

// ─────────────────────────────────────────────────────────────────────────────
// 8. All posteriors returned by score() are in [0, 1]
// ─────────────────────────────────────────────────────────────────────────────
void TestEvidenceScorer::testAllProbabilitiesInUnitInterval()
{
    EvidenceScorer scorer;
    QVector<EvidenceItem> ev = {
        { "fingerprint_match_10pt",         true  },
        { "eyewitness_identification_ideal", true  },
        { "alibi_strong",                   true  },
        { "cctv_clear_face",                false }
    };

    auto results = scorer.score(ev, 0.3);
    QVERIFY(!results.isEmpty());

    for (const auto& r : results) {
        QVERIFY2(r.posteriorProbability >= 0.0 && r.posteriorProbability <= 1.0,
                 qPrintable(QString("posteriorProbability=%1 out of [0,1]")
                            .arg(r.posteriorProbability)));
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// 9. score() returns a non-empty vector for any recognised evidence type
// ─────────────────────────────────────────────────────────────────────────────
void TestEvidenceScorer::testScoreReturnsValidFloat()
{
    EvidenceScorer scorer;
    QVector<EvidenceItem> ev = {{ "cctv_clear_face", true }};

    auto results = scorer.score(ev, 0.2);
    QVERIFY(!results.isEmpty());
    QVERIFY(std::isfinite(results.last().posteriorProbability));
}

// ─────────────────────────────────────────────────────────────────────────────
// 10. EvidenceWeight struct has all fields populated for a known type
// ─────────────────────────────────────────────────────────────────────────────
void TestEvidenceScorer::testResultStructurePopulated()
{
    EvidenceScorer scorer;
    QVector<EvidenceItem> ev = {{ "cctv_clear_face", true }};

    auto results = scorer.score(ev, 0.15);
    QVERIFY(!results.isEmpty());

    const auto& r = results.first();
    QVERIFY(!r.evidenceType.isEmpty());
    QVERIFY(r.likelihoodRatio > 0.0);
    QVERIFY(r.posteriorOdds  > 0.0);
    QVERIFY(r.posteriorProbability > 0.0 && r.posteriorProbability <= 1.0);
    QVERIFY(r.reliability >= 0.0 && r.reliability <= 1.0);
}

// ─────────────────────────────────────────────────────────────────────────────
// 11. UI overload: present evidence → posteriorProbability > prior
// ─────────────────────────────────────────────────────────────────────────────
void TestEvidenceScorer::testUIScoreOverloadPositiveEvidence()
{
    EvidenceScorer scorer;
    const double prior = 0.1;

    QMap<QString, bool> presence;
    presence["modus_operandi_match_high"] = true;

    auto result = scorer.score(prior, presence);
    QVERIFY2(result.posteriorProbability > prior,
             qPrintable(QString("posterior=%1  prior=%2")
                        .arg(result.posteriorProbability).arg(prior)));
    QVERIFY(result.overallLikelihoodRatio > 1.0);
    QVERIFY(!result.contributions.empty());
}

// ─────────────────────────────────────────────────────────────────────────────
// 12. UI overload: absent exculpatory evidence → posteriorProbability < prior
//    alibi_strong absent (LR inverted) should lower posterior
// ─────────────────────────────────────────────────────────────────────────────
void TestEvidenceScorer::testUIScoreOverloadNegativeEvidence()
{
    EvidenceScorer scorer;
    const double prior = 0.8;

    // alibi_strong present → LR = 0.05 < 1 → exculpatory
    QMap<QString, bool> presence;
    presence["alibi_strong"] = true;

    auto result = scorer.score(prior, presence);
    QVERIFY2(result.posteriorProbability < prior,
             qPrintable(QString("posterior=%1  prior=%2")
                        .arg(result.posteriorProbability).arg(prior)));
}

// ─────────────────────────────────────────────────────────────────────────────
// 13. UI overload: posteriorProbability always in [0, 1]
// ─────────────────────────────────────────────────────────────────────────────
void TestEvidenceScorer::testUIScorePosteriorInUnitInterval()
{
    EvidenceScorer scorer;

    QMap<QString, bool> presence;
    presence["fingerprint_match_10pt"]         = true;
    presence["eyewitness_identification_ideal"] = true;
    presence["alibi_strong"]                   = false;

    const double posterior = scorer.score(0.05, presence).posteriorProbability;
    QVERIFY2(posterior >= 0.0 && posterior <= 1.0,
             qPrintable(QString("posterior=%1").arg(posterior)));
}

// ─────────────────────────────────────────────────────────────────────────────
// 14. getLRAndReliability() returns sensible values for a known type
// ─────────────────────────────────────────────────────────────────────────────
void TestEvidenceScorer::testGetLRAndReliability()
{
    EvidenceScorer scorer;

    const auto [lr, rel] = scorer.getLRAndReliability("fingerprint_match_10pt");
    QVERIFY(lr  > 1.0);
    QVERIFY(rel > 0.0 && rel <= 1.0);

    // Unknown type → default LR=1, reliability=0 (or similar neutral sentinel)
    const auto [lrUnk, relUnk] = scorer.getLRAndReliability("DOES_NOT_EXIST_XYZ");
    QVERIFY(std::isfinite(lrUnk));
    QVERIFY(std::isfinite(relUnk));
}

// ─────────────────────────────────────────────────────────────────────────────
// 15. Completely unknown evidence type is neutral (posterior ≈ prior)
// ─────────────────────────────────────────────────────────────────────────────
void TestEvidenceScorer::testUnknownTypeIsNeutral()
{
    EvidenceScorer scorer;
    const double prior = 0.35;
    QVector<EvidenceItem> ev = {{ "completely_unknown_xyz_12345", true }};

    auto results = scorer.score(ev, prior);
    QVERIFY(!results.isEmpty());
    QVERIFY2(std::abs(results.last().posteriorProbability - prior) < 0.001,
             qPrintable(QString("posterior=%1  prior=%2")
                        .arg(results.last().posteriorProbability).arg(prior)));
}

// ─────────────────────────────────────────────────────────────────────────────
// 16. Absent evidence inverts the direction versus present evidence (prior=0.5)
//     cctv_clear_face present → posterior > 0.5; absent → posterior < 0.5
// ─────────────────────────────────────────────────────────────────────────────
void TestEvidenceScorer::testAbsentEvidenceInvertsEffect()
{
    EvidenceScorer scorer;
    const double prior = 0.5;

    auto rPresent = scorer.score({ { "cctv_clear_face", true  } }, prior);
    auto rAbsent  = scorer.score({ { "cctv_clear_face", false } }, prior);

    QVERIFY(!rPresent.isEmpty());
    QVERIFY(!rAbsent.isEmpty());

    QVERIFY(rPresent.last().posteriorProbability > prior);
    QVERIFY(rAbsent.last().posteriorProbability  < prior);
}

// ─────────────────────────────────────────────────────────────────────────────
// 17. Low prior (0.001) + single strong positive evidence → posterior > 0.5
// ─────────────────────────────────────────────────────────────────────────────
void TestEvidenceScorer::testLowPriorWithStrongEvidenceSurpasses50pct()
{
    EvidenceScorer scorer;
    QVector<EvidenceItem> ev = {{ "fingerprint_match_10pt", true }};

    auto results = scorer.score(ev, 0.001);
    QVERIFY(!results.isEmpty());
    QVERIFY2(results.last().posteriorProbability > 0.5,
             qPrintable(QString("posterior=%1")
                        .arg(results.last().posteriorProbability)));
}

// ─────────────────────────────────────────────────────────────────────────────
// 18. High prior (0.99) + strong exculpatory evidence → posterior < 0.5
// ─────────────────────────────────────────────────────────────────────────────
void TestEvidenceScorer::testHighPriorWithStrongNegativeDropsBelow50pct()
{
    EvidenceScorer scorer;
    // Apply alibi_strong (LR=0.05) multiple times from a high prior
    QVector<EvidenceItem> ev;
    for (int i = 0; i < 3; ++i)
        ev << EvidenceItem{ "alibi_strong", true };

    auto results = scorer.score(ev, 0.99);
    QVERIFY(!results.isEmpty());
    QVERIFY2(results.last().posteriorProbability < 0.5,
             qPrintable(QString("posterior=%1")
                        .arg(results.last().posteriorProbability)));
}

// ─────────────────────────────────────────────────────────────────────────────

QTEST_MAIN(TestEvidenceScorer)
#include "test_evidence_scorer.moc"
