// test_evidence_calibration.cpp
// Comprehensive Qt Test suite for EvidenceScorer and CalibrationAnalyser.

#include <QTest>
#include <QCoreApplication>
#include <QMap>
#include <cmath>

#include "inference/EvidenceScorer.h"
#include "benchmark/CalibrationAnalyser.h"
#include "core/CrimeEvent.h"

class TestEvidenceCalibration : public QObject
{
    Q_OBJECT
private slots:
    // ── EvidenceScorer ──────────────────────────────────────────────────────
    void testPriorAndSingleEvidence();
    void testSequentialUpdating();
    void testNeutralEvidence();
    void testStrongEvidence();
    void testWeakEvidence();
    void testReliabilityWeighting();
    void testResultStructure();
    void testSymmetryBetweenLikelihoods();

    // ── CalibrationAnalyser ─────────────────────────────────────────────────
    void testPerfectCalibration();
    void testOverconfidentModel();
    void testUnderconfidentModel();
    void testBinCoverage();
    void testECERange();
    void testBrierScore();
    void testCalibrationCurveMonotonicity();
    void testEmptyInput();
    void testSinglePrediction();
};

// ═══════════════════════════════════════════════════════════════════════════════
// EvidenceScorer tests
// ═══════════════════════════════════════════════════════════════════════════════

// 1 ── prior=0.5, modus_operandi_match_high (LR=8.0)
//      priorOdds = 1.0  →  posterior odds = 8.0  →  posterior = 8/9 ≈ 0.8889
void TestEvidenceCalibration::testPriorAndSingleEvidence()
{
    EvidenceScorer scorer;
    QVector<EvidenceItem> ev = {{ "modus_operandi_match_high", true }};
    auto results = scorer.score(ev, 0.5);

    QVERIFY(!results.isEmpty());
    const double expected = 8.0 / 9.0;
    QVERIFY(qAbs(results.last().posteriorProbability - expected) < 0.001);
}

// 2 ── Sequential: prior=0.1, eyewitness_ideal (LR=4) then mo_moderate (LR=3)
//      priorOdds = 1/9, combined odds = 12/9 = 4/3  →  posterior = 4/7 ≈ 0.5714
void TestEvidenceCalibration::testSequentialUpdating()
{
    EvidenceScorer scorer;
    QVector<EvidenceItem> ev = {
        { "eyewitness_identification_ideal", true },
        { "modus_operandi_match_moderate",   true }
    };
    auto results = scorer.score(ev, 0.1);

    QCOMPARE(results.size(), 2);

    const double expectedPosterior = (4.0 / 3.0) / (1.0 + 4.0 / 3.0);   // 4/7
    QVERIFY(qAbs(results.last().posteriorProbability - expectedPosterior) < 0.001);

    // Each successive inculpatory item must increase the running posterior
    QVERIFY(results[1].posteriorProbability > results[0].posteriorProbability);
}

// 3 ── Unknown type → LR=1 → posterior stays equal to prior
void TestEvidenceCalibration::testNeutralEvidence()
{
    EvidenceScorer scorer;
    const double prior = 0.35;
    QVector<EvidenceItem> ev = {{ "completely_unknown_evidence_xyz", true }};
    auto results = scorer.score(ev, prior);

    QVERIFY(!results.isEmpty());
    QVERIFY(qAbs(results.last().posteriorProbability - prior) < 0.001);
}

// 4 ── fingerprint_match_10pt (LR=1e6), low prior=0.001 → posterior approaches 1
void TestEvidenceCalibration::testStrongEvidence()
{
    EvidenceScorer scorer;
    QVector<EvidenceItem> ev = {{ "fingerprint_match_10pt", true }};
    auto results = scorer.score(ev, 0.001);

    QVERIFY(!results.isEmpty());
    QVERIFY(results.last().posteriorProbability > 0.999);
}

// 5 ── alibi_strong (LR=0.05, exculpatory), prior=0.5
//      posterior odds = 0.05  →  posterior = 0.05/1.05 ≈ 0.0476
void TestEvidenceCalibration::testWeakEvidence()
{
    EvidenceScorer scorer;
    QVector<EvidenceItem> ev = {{ "alibi_strong", true }};
    auto results = scorer.score(ev, 0.5);

    QVERIFY(!results.isEmpty());
    const double expected = 0.05 / 1.05;
    QVERIFY(qAbs(results.last().posteriorProbability - expected) < 0.001);
    QVERIFY(results.last().posteriorProbability < 0.1);
}

// 6 ── Reliability metadata is stored; high-reliability types carry higher LRs
//      and therefore shift the posterior more than low-reliability types.
void TestEvidenceCalibration::testReliabilityWeighting()
{
    EvidenceScorer scorer;
    const double prior = 0.1;

    // fingerprint_match_10pt: LR=1e6, reliability=0.99
    QVector<EvidenceItem> evFP = {{ "fingerprint_match_10pt",         true }};
    // eyewitness_identification_poor: LR=1.5, reliability=0.40
    QVector<EvidenceItem> evEW = {{ "eyewitness_identification_poor", true }};

    auto rFP = scorer.score(evFP, prior);
    auto rEW = scorer.score(evEW, prior);

    QVERIFY(!rFP.isEmpty());
    QVERIFY(!rEW.isEmpty());

    // High-reliability evidence (fingerprint) shifts posterior far more
    QVERIFY(rFP.last().posteriorProbability > rEW.last().posteriorProbability);

    // Reliability field is correctly populated from the LR table
    QVERIFY(qAbs(rFP.last().reliability - 0.99) < 0.001);
    QVERIFY(qAbs(rEW.last().reliability - 0.40) < 0.001);
}

// 7 ── All EvidenceWeight output fields are populated for a known type
void TestEvidenceCalibration::testResultStructure()
{
    EvidenceScorer scorer;
    QVector<EvidenceItem> ev = {{ "cctv_clear_face", true }};   // LR=50, rel=0.85
    auto results = scorer.score(ev, 0.2);

    QVERIFY(!results.isEmpty());
    const auto& r = results.first();

    QVERIFY(!r.evidenceType.isEmpty());
    QVERIFY(r.likelihoodRatio   > 1.0);   // cctv_clear_face LR = 50
    QVERIFY(r.posteriorOdds     > 0.0);
    QVERIFY(r.posteriorProbability > 0.0 && r.posteriorProbability < 1.0);
    QVERIFY(r.reliability       > 0.0);
    QVERIFY(!r.notes.isEmpty());
}

// 8 ── With prior=0.5: P(H|E present) + P(H|E absent) = 1.0 for any LR.
//      Proof: priorOdds=1  →  present = LR/(1+LR), absent = 1/(1+LR), sum = 1.
void TestEvidenceCalibration::testSymmetryBetweenLikelihoods()
{
    EvidenceScorer scorer;

    QVector<EvidenceItem> evPresent = {{ "cctv_clear_face", true  }};  // LR=50
    QVector<EvidenceItem> evAbsent  = {{ "cctv_clear_face", false }};  // effective LR=1/50

    auto rPresent = scorer.score(evPresent, 0.5);
    auto rAbsent  = scorer.score(evAbsent,  0.5);

    QVERIFY(!rPresent.isEmpty());
    QVERIFY(!rAbsent.isEmpty());

    const double pPresent = rPresent.last().posteriorProbability;
    const double pAbsent  = rAbsent.last().posteriorProbability;

    // Symmetry property
    QVERIFY(qAbs((pPresent + pAbsent) - 1.0) < 1e-9);

    // Direction sanity: inculpatory evidence present pushes above prior,
    // absent (i.e. fingerprint NOT found) pushes below prior
    QVERIFY(pPresent > 0.5);
    QVERIFY(pAbsent  < 0.5);
}

// ═══════════════════════════════════════════════════════════════════════════════
// CalibrationAnalyser tests
// ═══════════════════════════════════════════════════════════════════════════════

// 1 ── Perfect calibration: pred=actual for every sample → ECE ≈ 0
void TestEvidenceCalibration::testPerfectCalibration()
{
    CalibrationAnalyser ca(10);
    QVector<QPair<double,double>> data;
    for (int i = 0; i < 50; ++i) data.append({0.0, 0.0});
    for (int i = 0; i < 50; ++i) data.append({1.0, 1.0});

    auto result = ca.analyse(data);

    QVERIFY(result.ece < 0.01);
    QCOMPARE(result.nSamples, 100);
}

// 2 ── Overconfident: all predictions=0.9, actual rate=0.5 → ECE = 0.4 > 0.3
void TestEvidenceCalibration::testOverconfidentModel()
{
    CalibrationAnalyser ca(10);
    QVector<QPair<double,double>> data;
    for (int i = 0; i < 100; ++i)
        data.append({0.9, (i < 50) ? 1.0 : 0.0});

    auto result = ca.analyse(data);

    QVERIFY(result.ece > 0.3);
    QVERIFY(result.mce > 0.3);
}

// 3 ── Underconfident: all predictions=0.5, all outcomes=1.0 → ECE = 0.5 > 0
void TestEvidenceCalibration::testUnderconfidentModel()
{
    CalibrationAnalyser ca(10);
    QVector<QPair<double,double>> data;
    for (int i = 0; i < 100; ++i) data.append({0.5, 1.0});

    auto result = ca.analyse(data);

    QVERIFY(result.ece > 0.4);
}

// 4 ── Dense, evenly spread predictions → all 10 bins have at least one sample
void TestEvidenceCalibration::testBinCoverage()
{
    CalibrationAnalyser ca(10);
    QVector<QPair<double,double>> data;
    for (int i = 0; i < 100; ++i) {
        const double pred = (i + 0.5) / 100.0;
        data.append({pred, (i % 2 == 0) ? 1.0 : 0.0});
    }

    auto result = ca.analyse(data);

    QCOMPARE(result.nBins, 10);
    int occupiedBins = 0;
    for (const auto& bin : result.bins)
        if (bin.count > 0) ++occupiedBins;
    QCOMPARE(occupiedBins, 10);
}

// 5 ── ECE must always lie in [0, 1]
void TestEvidenceCalibration::testECERange()
{
    CalibrationAnalyser ca(10);

    // Worst-case: all predict 1.0, all actual 0.0
    QVector<QPair<double,double>> worst;
    for (int i = 0; i < 50; ++i) worst.append({1.0, 0.0});
    auto r1 = ca.analyse(worst);
    QVERIFY(r1.ece >= 0.0 && r1.ece <= 1.0);

    // Near-perfect
    QVector<QPair<double,double>> perfect;
    for (int i = 0; i < 50; ++i) perfect.append({0.5, (i % 2 == 0) ? 1.0 : 0.0});
    auto r2 = ca.analyse(perfect);
    QVERIFY(r2.ece >= 0.0 && r2.ece <= 1.0);
}

// 6 ── Brier score = mean((pred - actual)^2)
//      100 samples with pred=0.7, act=1.0 → BS = (0.7 - 1.0)^2 = 0.09
void TestEvidenceCalibration::testBrierScore()
{
    CalibrationAnalyser ca(10);
    QVector<QPair<double,double>> data;
    for (int i = 0; i < 100; ++i) data.append({0.7, 1.0});

    auto result = ca.analyse(data);

    const double expected = (0.7 - 1.0) * (0.7 - 1.0);   // 0.09
    QVERIFY(qAbs(result.brierScore - expected) < 1e-9);
}

// 7 ── Well-calibrated data: each bin's avgPred ≈ its empirical outcome rate
void TestEvidenceCalibration::testCalibrationCurveMonotonicity()
{
    CalibrationAnalyser ca(10);
    QVector<QPair<double,double>> data;

    // For each bin b, predict at the bin centre and set the positive rate to match.
    // bin b: pred = (b + 0.5) / 10, nPos = round(pred * 20).
    for (int b = 0; b < 10; ++b) {
        const double pred = (b + 0.5) / 10.0;
        const int nPos = static_cast<int>(pred * 20);
        const int nNeg = 20 - nPos;
        for (int i = 0; i < nPos; ++i) data.append({pred, 1.0});
        for (int i = 0; i < nNeg; ++i) data.append({pred, 0.0});
    }

    auto result = ca.analyse(data);

    // For this idealised calibrated dataset, ECE should be very small
    QVERIFY(result.ece < 0.1);

    // Each occupied bin should have avgPred close to empirical
    for (const auto& bin : result.bins) {
        if (bin.count > 0)
            QVERIFY(bin.error < 0.15);
    }
}

// 8 ── Empty input: no crash, returns default (zero) result
void TestEvidenceCalibration::testEmptyInput()
{
    CalibrationAnalyser ca(10);
    auto result = ca.analyse({});

    QCOMPARE(result.nSamples, 0);
    QVERIFY(result.ece        >= 0.0);
    QVERIFY(result.mce        >= 0.0);
    QVERIFY(result.brierScore >= 0.0);
}

// 9 ── Single prediction: no crash, all numeric fields are finite and in range
void TestEvidenceCalibration::testSinglePrediction()
{
    CalibrationAnalyser ca(10);
    auto result = ca.analyse({{ 0.8, 1.0 }});

    QCOMPARE(result.nSamples, 1);
    QVERIFY(result.ece >= 0.0 && result.ece <= 1.0);
    QVERIFY(result.mce >= 0.0 && result.mce <= 1.0);
    QVERIFY(std::isfinite(result.brierScore));
    QVERIFY(std::isfinite(result.logLoss));

    // Brier: (0.8 - 1.0)^2 = 0.04
    QVERIFY(qAbs(result.brierScore - 0.04) < 1e-9);
}

QTEST_MAIN(TestEvidenceCalibration)
#include "test_evidence_calibration.moc"
