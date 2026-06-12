// test_evidence_scorer_deep7.cpp — Deep audit iteration 20: EvidenceScorer
// Verifies: unknown types, absent inversion, empty map, reliability metadata, BF edge cases.

#include <QTest>
#include "inference/EvidenceScorer.h"
#include <cmath>

static constexpr double kEps = 1e-9;

class EvidenceScorerDeep7Test : public QObject
{
    Q_OBJECT

private:
    EvidenceScorer m_scorer;

private slots:

    void testUnknownEvidenceTypeNeutralLR()
    {
        constexpr double prior = 0.2;
        QMap<QString, bool> evidence;
        evidence[QStringLiteral("nonexistent_evidence_xyz")] = true;

        const auto result = m_scorer.score(prior, evidence);
        QCOMPARE(result.overallLikelihoodRatio, 1.0);
        QVERIFY2(std::abs(result.posteriorProbability - prior) < kEps,
                 "unknown evidence should leave posterior unchanged");

        const auto [lr, rel] = m_scorer.getLRAndReliability(
            QStringLiteral("nonexistent_evidence_xyz"));
        QCOMPARE(lr, 1.0);
        QCOMPARE(rel, 0.0);
    }

    void testAbsentEvidenceInvertsLR()
    {
        constexpr double prior = 0.1;
        const QString type = QStringLiteral("fingerprint_match_10pt");
        const auto [tableLR, rel] = m_scorer.getLRAndReliability(type);

        QMap<QString, bool> absent;
        absent[type] = false;
        const auto result = m_scorer.score(prior, absent);

        const double expectedLR = 1.0 / tableLR;
        QVERIFY2(std::abs(result.overallLikelihoodRatio - expectedLR) < 1e-12,
                 qPrintable(QStringLiteral("absent LR expected %1, got %2")
                     .arg(expectedLR).arg(result.overallLikelihoodRatio)));
        QVERIFY2(result.posteriorProbability < prior,
                 "absent incriminating evidence should lower posterior");
    }

    void testEmptyMapPreservesPrior()
    {
        const QVector<double> priors = {0.001, 0.05, 0.25, 0.75, 0.999};
        for (double prior : priors) {
            const auto result = m_scorer.score(prior, QMap<QString, bool>{});
            QCOMPARE(result.overallLikelihoodRatio, 1.0);
            QVERIFY(result.contributions.empty());
            QVERIFY2(std::abs(result.posteriorProbability - prior) < 1e-12,
                     qPrintable(QStringLiteral("empty map prior %1 -> posterior %2")
                         .arg(prior).arg(result.posteriorProbability)));
        }
    }

    void testEvidenceTypeCountMatchesAvailable()
    {
        const QStringList types = m_scorer.availableEvidenceTypes();
        QCOMPARE(m_scorer.evidenceTypeCount(), types.size());
        QVERIFY(types.size() >= 20);
        QVERIFY(types.contains(QStringLiteral("dna_match_full_profile")));
        QVERIFY(types.contains(QStringLiteral("alibi_strong")));
    }

    void testVectorReliabilityFromTable()
    {
        const QVector<EvidenceItem> items = {
            { QStringLiteral("cctv_clear_face"), true },
            { QStringLiteral("informant_tip_unreliable"), true },
            { QStringLiteral("unknown_type_xyz"), true },
        };

        const auto results = m_scorer.score(items, 0.1);
        QCOMPARE(results.size(), 3);

        const auto [cctvLR, cctvRel] = m_scorer.getLRAndReliability(
            QStringLiteral("cctv_clear_face"));
        const auto [tipLR, tipRel] = m_scorer.getLRAndReliability(
            QStringLiteral("informant_tip_unreliable"));

        QCOMPARE(results[0].reliability, cctvRel);
        QCOMPARE(results[1].reliability, tipRel);
        QCOMPARE(results[2].reliability, 0.0);
        QVERIFY2(results[0].reliability > results[1].reliability,
                 "CCTV reliability should exceed unreliable informant");
    }

    void testContributionWasPresentFlags()
    {
        QMap<QString, bool> evidence;
        evidence[QStringLiteral("vehicle_at_scene")] = true;
        evidence[QStringLiteral("alibi_strong")]     = false;

        const auto result = m_scorer.score(0.15, evidence);
        QCOMPARE(result.contributions.size(), 2u);

        bool sawPresent = false, sawAbsent = false;
        for (const auto& c : result.contributions) {
            if (c.name == QStringLiteral("vehicle_at_scene")) {
                QVERIFY(c.wasPresent);
                sawPresent = true;
            }
            if (c.name == QStringLiteral("alibi_strong")) {
                QVERIFY(!c.wasPresent);
                sawAbsent = true;
            }
        }
        QVERIFY(sawPresent);
        QVERIFY(sawAbsent);
    }

    void testBayesFactorZeroPriorReturnsOne()
    {
        QMap<QString, bool> evidence;
        evidence[QStringLiteral("dna_match_partial")] = true;

        const double bfZeroA = m_scorer.bayesFactor(0.0, 0.5, evidence);
        const double bfZeroB = m_scorer.bayesFactor(0.5, 0.0, evidence);
        const double bfBothZero = m_scorer.bayesFactor(0.0, 0.0, evidence);

        QCOMPARE(bfZeroA, 1.0);
        QCOMPARE(bfZeroB, 1.0);
        QCOMPARE(bfBothZero, 1.0);
    }

    void testExculpatoryOnlyLowersPosterior()
    {
        constexpr double prior = 0.4;
        QMap<QString, bool> exculpatory;
        exculpatory[QStringLiteral("alibi_strong")] = true;

        const auto result = m_scorer.score(prior, exculpatory);
        const auto [alibiLR, rel] = m_scorer.getLRAndReliability(
            QStringLiteral("alibi_strong"));

        QCOMPARE(result.overallLikelihoodRatio, alibiLR);
        QVERIFY2(result.posteriorProbability < prior,
                 qPrintable(QStringLiteral("exculpatory posterior %1 should be < prior %2")
                     .arg(result.posteriorProbability).arg(prior)));
        QVERIFY2(result.posteriorProbability > 0.0,
                 "exculpatory evidence should not zero posterior");

        const QVector<EvidenceItem> items = {
            { QStringLiteral("alibi_strong"), true }
        };
        const auto vecResults = m_scorer.score(items, prior);
        QCOMPARE(vecResults.size(), 1);
        QVERIFY(vecResults[0].notes.contains(QStringLiteral("Strong alibi")));
        QVERIFY2(std::abs(vecResults[0].posteriorProbability - result.posteriorProbability) < 1e-12,
                 "vector exculpatory posterior should match map API");
    }
};

QTEST_GUILESS_MAIN(EvidenceScorerDeep7Test)
#include "test_evidence_scorer_deep7.moc"
