// test_mo_analyser_tfidf.cpp — TF-IDF focused tests for MOAnalyser
//
// Covers: fit/isFitted state, caseCount, identical-MO high similarity,
// empty-MO low scores, minSimilarity threshold, topK limit, resolved-case
// fields returned correctly, descending-score ranking, unfitted empty results,
// and short (1-2 word) MO text handled gracefully.

#include <QTest>
#include <QCoreApplication>
#include <cmath>
#include <algorithm>

#include "inference/MOAnalyser.h"
#include "core/CrimeEvent.h"

namespace {

static MOCaseRecord makeCase(const QString& id, const QString& mo,
                              bool resolved = false,
                              const QString& outcome = {},
                              const QString& suspectProfile = {},
                              int convictionYear = 0)
{
    MOCaseRecord r;
    r.caseId          = id;
    r.moText          = mo;
    r.resolved        = resolved;
    r.outcome         = outcome.isEmpty()
                            ? (resolved ? QStringLiteral("resolved")
                                        : QStringLiteral("unresolved"))
                            : outcome;
    r.suspectProfile  = suspectProfile;
    r.convictionYear  = convictionYear;
    return r;
}

} // anonymous namespace

class TestMOAnalyserTFIDF : public QObject {
    Q_OBJECT

private slots:

    // ── 1. fit() makes isFitted() true ────────────────────────────────────────
    void testFitMakesIsFittedTrue()
    {
        MOAnalyser analyser;
        QVERIFY(!analyser.isFitted());

        QVector<MOCaseRecord> cases;
        cases << makeCase("c1", "robbery weapon nighttime victim street")
              << makeCase("c2", "forced entry window residential jewels");

        analyser.fit(cases);

        QVERIFY(analyser.isFitted());
    }

    // ── 2. caseCount() returns correct value ──────────────────────────────────
    void testCaseCountCorrect()
    {
        MOAnalyser analyser;
        QCOMPARE(analyser.caseCount(), 0);

        QVector<MOCaseRecord> cases;
        for (int i = 0; i < 7; ++i)
            cases << makeCase(QStringLiteral("c%1").arg(i),
                              QStringLiteral("theft robbery night variant_%1").arg(i));

        analyser.fit(cases);
        QCOMPARE(analyser.caseCount(), 7);
    }

    // ── 3. findSimilar() on identical MO text returns similarity > 0.9 ────────
    void testIdenticalMOHighSimilarity()
    {
        const QString moText =
            "forced entry night crowbar residential jewels gloves solo masked window";

        QVector<MOCaseRecord> cases;
        cases << makeCase("c_exact",   moText)
              << makeCase("c_unrelated", "arson petrol warehouse daytime fire ignition accelerant");

        MOAnalyser analyser;
        analyser.fit(cases);

        const auto results = analyser.findSimilar(moText, 5, 0.0);
        QVERIFY(!results.isEmpty());
        QCOMPARE(results.first().caseId, QStringLiteral("c_exact"));
        QVERIFY2(results.first().similarityScore > 0.9,
                 qPrintable(QStringLiteral("Expected > 0.9, got: %1")
                                .arg(results.first().similarityScore)));
    }

    // ── 4. findSimilar() on empty MO text returns empty or very low scores ─────
    void testEmptyMOTextReturnsLowScores()
    {
        QVector<MOCaseRecord> cases;
        cases << makeCase("c1", "robbery weapon victim nighttime street")
              << makeCase("c2", "forced entry residential window crowbar gloves");

        MOAnalyser analyser;
        analyser.fit(cases);

        const auto results = analyser.findSimilar("", 5, 0.0);
        // Either empty result set or all scores near zero
        for (const auto& m : results) {
            QVERIFY2(m.similarityScore < 1e-9,
                     qPrintable(QStringLiteral("Expected ~0 for empty query, got: %1")
                                    .arg(m.similarityScore)));
        }
    }

    // ── 5. findSimilar() respects minSimilarity threshold ─────────────────────
    void testMinSimilarityThreshold()
    {
        QVector<MOCaseRecord> cases;
        // c1 will match query well; c2 is unrelated
        cases << makeCase("c1", "burglary residential night window forced entry crowbar jewels")
              << makeCase("c2", "drug possession street arrest conviction caution");

        MOAnalyser analyser;
        analyser.fit(cases);

        const double threshold = 0.7;
        const auto results = analyser.findSimilar("burglary residential night", 5, threshold);

        for (const auto& m : results) {
            QVERIFY2(m.similarityScore >= threshold,
                     qPrintable(QStringLiteral("Score %1 below minSimilarity %2")
                                    .arg(m.similarityScore).arg(threshold)));
        }
    }

    // ── 6. findSimilar() respects topK limit ──────────────────────────────────
    void testTopKLimit()
    {
        QVector<MOCaseRecord> cases;
        for (int i = 0; i < 20; ++i)
            cases << makeCase(QStringLiteral("c%1").arg(i),
                              QStringLiteral("theft robbery weapon night variant_%1").arg(i));

        MOAnalyser analyser;
        analyser.fit(cases);

        const int K = 5;
        const auto results = analyser.findSimilar("theft robbery weapon night", K, 0.0);
        QVERIFY2(results.size() <= K,
                 qPrintable(QStringLiteral("Got %1 results, expected <= %2")
                                .arg(results.size()).arg(K)));
    }

    // ── 7. findSimilar() returns resolved case fields correctly ────────────────
    void testResolvedCaseFieldsReturned()
    {
        QVector<MOCaseRecord> cases;
        cases << makeCase("c_resolved",
                          "robbery weapon victim street nighttime unique_alpha_tag",
                          /*resolved=*/true,
                          /*outcome=*/"resolved",
                          /*suspectProfile=*/"male 30-40 local offender",
                          /*convictionYear=*/2020);

        MOAnalyser analyser;
        analyser.fit(cases);

        const auto results = analyser.findSimilar(
            "robbery weapon victim street nighttime unique_alpha_tag", 5, 0.0);

        QVERIFY(!results.isEmpty());
        const auto& match = results.first();
        QCOMPARE(match.caseId,         QStringLiteral("c_resolved"));
        QVERIFY (match.resolved);
        QCOMPARE(match.outcome,        QStringLiteral("resolved"));
        QCOMPARE(match.suspectProfile, QStringLiteral("male 30-40 local offender"));
        QCOMPARE(match.convictionYear, 2020);
    }

    // ── 8. Multiple cases ranked by decreasing similarity ─────────────────────
    void testResultsDescendingOrder()
    {
        QVector<MOCaseRecord> cases;
        // Burglary cluster — should rank higher than fraud cluster
        cases << makeCase("burg1", "forced entry window residential night jewels crowbar solo")
              << makeCase("burg2", "smashed window home night gold rings gloves entry")
              << makeCase("burg3", "door forced lock home jewelry wristwatch night entry")
              << makeCase("fraud1", "phone scam elderly bank transfer wire impersonation money")
              << makeCase("fraud2", "email phishing credit card online fraud account theft");

        MOAnalyser analyser;
        analyser.fit(cases);

        const auto results = analyser.findSimilar(
            "forced entry residential night crowbar jewelry window", 10, 0.0);

        // Verify scores are in non-increasing order
        for (int i = 1; i < results.size(); ++i) {
            QVERIFY2(results[i-1].similarityScore >= results[i].similarityScore,
                     qPrintable(QStringLiteral("Not sorted at rank %1: %2 < %3")
                                    .arg(i)
                                    .arg(results[i-1].similarityScore)
                                    .arg(results[i].similarityScore)));
        }
    }

    // ── 9. Unfitted analyser returns empty results ─────────────────────────────
    void testUnfittedAnalyserReturnsEmpty()
    {
        MOAnalyser analyser;
        QVERIFY(!analyser.isFitted());

        const auto results = analyser.findSimilar("robbery weapon nighttime victim", 5, 0.0);
        QVERIFY2(results.isEmpty(),
                 qPrintable(QStringLiteral("Unfitted analyser returned %1 results")
                                .arg(results.size())));
    }

    // ── 10. Very short MO text (1-2 words) handled gracefully ─────────────────
    void testShortMOTextHandledGracefully()
    {
        QVector<MOCaseRecord> cases;
        cases << makeCase("c1", "robbery")
              << makeCase("c2", "robbery nighttime residential window forced entry crowbar");

        MOAnalyser analyser;
        analyser.fit(cases);
        QVERIFY(analyser.isFitted());
        QCOMPARE(analyser.caseCount(), 2);

        // Single-word corpus entry
        {
            const auto results = analyser.findSimilar("robbery", 5, 0.0);
            // Should not crash and should return at least one result
            QVERIFY(!results.isEmpty());
            bool found = false;
            for (const auto& m : results) {
                if (m.caseId == "c1") {
                    found = true;
                    QVERIFY2(m.similarityScore > 0.0,
                             "Single-word corpus entry should have positive similarity");
                }
            }
            QVERIFY2(found, "c1 (single-word MO) not found in results");
        }

        // Two-word query
        {
            const auto results2 = analyser.findSimilar("forced entry", 5, 0.0);
            QVERIFY(!results2.isEmpty());
            QCOMPARE(results2.first().caseId, QStringLiteral("c2"));
        }
    }
};

QTEST_GUILESS_MAIN(TestMOAnalyserTFIDF)
#include "test_mo_analyser_tfidf.moc"
