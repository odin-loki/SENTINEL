// Iteration 12 — MOAnalyser deep test
#include <QtTest/QtTest>
#include <cmath>
#include "inference/MOAnalyser.h"
#include "core/CrimeEvent.h"

class MOAnalyserDeep2Test : public QObject
{
    Q_OBJECT

    static MOCaseRecord makeRecord(const QString& caseId, const QString& moText,
                                   bool resolved = false)
    {
        MOCaseRecord r;
        r.caseId  = caseId;
        r.moText  = moText;
        r.resolved = resolved;
        return r;
    }

private slots:

    // ─── Fit / state ──────────────────────────────────────────────────────

    void testUnfittedReturnsEmpty()
    {
        MOAnalyser ma;
        QVERIFY(!ma.isFitted());
        const auto matches = ma.findSimilar("test query");
        QVERIFY(matches.isEmpty());
    }

    void testFitSetsIsFitted()
    {
        MOAnalyser ma;
        ma.fit({ makeRecord("C1", "entered via rear window with crowbar") });
        QVERIFY(ma.isFitted());
        QCOMPARE(ma.caseCount(), 1);
    }

    void testFitEmptyDoesNotCrash()
    {
        MOAnalyser ma;
        ma.fit({});
        QCOMPARE(ma.caseCount(), 0);
        QVERIFY(ma.findSimilar("test").isEmpty());
    }

    // ─── Cosine similarity properties ─────────────────────────────────────

    void testIdenticalQueryReturnsSimilarityOne()
    {
        const QString moText = "entered through rear window with crowbar at night";
        MOAnalyser ma;
        ma.fit({ makeRecord("C1", moText) });

        const auto matches = ma.findSimilar(moText, 5, 0.0);
        QVERIFY(!matches.isEmpty());
        // Exact same text: cos sim = 1.0 (possibly boosted to 1.2 for resolved, capped at 1.0)
        QVERIFY2(matches.first().similarityScore > 0.9,
                 qPrintable(QStringLiteral("Expected sim > 0.9, got %1").arg(matches.first().similarityScore)));
    }

    void testDifferentTextLowSimilarity()
    {
        MOAnalyser ma;
        ma.fit({ makeRecord("C1", "burglary rear window crowbar") });

        // Completely unrelated MO text — shouldn't appear in results with default threshold 0.3
        const auto matches = ma.findSimilar("arson vehicle fire accelerant", 5, 0.3);
        // Should return empty or very low similarity
        for (const auto& m : matches) {
            QVERIFY2(m.similarityScore < 0.5,
                     qPrintable(QStringLiteral("Unrelated text matched too high: %1").arg(m.similarityScore)));
        }
    }

    void testSimilarTextHigherRankThanDissimilar()
    {
        MOAnalyser ma;
        ma.fit({
            makeRecord("SIMILAR", "entered rear window crowbar nighttime"),
            makeRecord("DISSIMILAR", "arson vehicle accelerant fire daytime")
        });

        const auto matches = ma.findSimilar("rear window crowbar break-in", 10, 0.0);
        // SIMILAR case should appear before DISSIMILAR
        if (matches.size() >= 2) {
            bool similarFirst = false;
            for (int i = 0; i < matches.size(); ++i) {
                if (matches[i].caseId == "SIMILAR") { similarFirst = true; break; }
                if (matches[i].caseId == "DISSIMILAR") break;
            }
            QVERIFY2(similarFirst, "SIMILAR case should rank before DISSIMILAR case");
        }
    }

    // ─── Resolved case boosting ───────────────────────────────────────────

    void testResolvedCaseBoosted()
    {
        const QString moText = "entry through window crowbar";
        MOAnalyser ma;
        ma.fit({
            makeRecord("RESOLVED",   moText, true),   // resolved → boosted
            makeRecord("UNRESOLVED", moText, false)   // same text, not boosted
        });

        const auto matches = ma.findSimilar(moText, 10, 0.0);
        // Both should match; RESOLVED should appear first
        QVERIFY(!matches.isEmpty());
        // The resolved case gets sim * 1.2 (capped at 1.0)
        bool resolvedFirst = !matches.isEmpty() &&
                             matches.first().caseId == QStringLiteral("RESOLVED");
        QVERIFY2(resolvedFirst, "Resolved case should rank first due to boosting");
    }

    // ─── Shared features extraction ───────────────────────────────────────

    void testSharedFeaturesCorrect()
    {
        MOAnalyser ma;
        ma.fit({ makeRecord("C1", "entry rear window crowbar nighttime dark") });

        const auto matches = ma.findSimilar("rear window crowbar", 5, 0.0);
        QVERIFY(!matches.isEmpty());

        // Shared features should contain "rear", "window", "crowbar"
        const QStringList& shared = matches.first().sharedFeatures;
        QVERIFY2(shared.contains("rear"),   "Expected 'rear' in shared features");
        QVERIFY2(shared.contains("window"), "Expected 'window' in shared features");
        QVERIFY2(shared.contains("crowbar"),"Expected 'crowbar' in shared features");
    }

    void testSharedFeaturesNoDuplicates()
    {
        MOAnalyser ma;
        ma.fit({ makeRecord("C1", "window window window door") });

        const auto matches = ma.findSimilar("window window", 5, 0.0);
        if (!matches.isEmpty()) {
            const QStringList& shared = matches.first().sharedFeatures;
            // No duplicates (removeDuplicates is called)
            int windowCount = shared.count("window");
            QVERIFY2(windowCount <= 1,
                     qPrintable(QStringLiteral("'window' appears %1 times in shared, expected ≤ 1").arg(windowCount)));
        }
    }

    // ─── Top-K bounding ───────────────────────────────────────────────────

    void testTopKBounded()
    {
        MOAnalyser ma;
        QVector<MOCaseRecord> cases;
        for (int i = 1; i <= 20; ++i)
            cases.append(makeRecord(QStringLiteral("C%1").arg(i),
                                    QStringLiteral("rear window crowbar entry crime %1").arg(i)));
        ma.fit(cases);

        const auto matches = ma.findSimilar("rear window crowbar", 5, 0.0);
        QVERIFY2(matches.size() <= 5,
                 qPrintable(QStringLiteral("Expected ≤ 5 results, got %1").arg(matches.size())));
    }

    // ─── Minimum similarity threshold ─────────────────────────────────────

    void testMinSimilarityFiltersResults()
    {
        MOAnalyser ma;
        ma.fit({
            makeRecord("C1", "burglary rear window crowbar"),
            makeRecord("C2", "completely unrelated different content here")
        });

        // High threshold should filter out low-similarity cases
        const auto matches = ma.findSimilar("rear window crowbar", 5, 0.9);
        for (const auto& m : matches) {
            QVERIFY2(m.similarityScore >= 0.9,
                     qPrintable(QStringLiteral("Similarity %1 below threshold 0.9").arg(m.similarityScore)));
        }
    }

    // ─── SimilarityScore in [0,1] ─────────────────────────────────────────

    void testSimilarityScoreInRange()
    {
        MOAnalyser ma;
        ma.fit({
            makeRecord("C1", "burglary rear window crowbar", true),
            makeRecord("C2", "theft pickpocket city centre"),
            makeRecord("C3", "assault bar fight night")
        });

        const auto matches = ma.findSimilar("rear window entry crowbar", 10, 0.0);
        for (const auto& m : matches) {
            QVERIFY2(m.similarityScore >= 0.0 && m.similarityScore <= 1.0,
                     qPrintable(QStringLiteral("similarityScore out of [0,1]: %1").arg(m.similarityScore)));
        }
    }

    // ─── Sorted by similarity descending ─────────────────────────────────

    void testResultsSortedBySimilarityDesc()
    {
        MOAnalyser ma;
        ma.fit({
            makeRecord("C1", "rear window crowbar entry forced"),
            makeRecord("C2", "door lock pick jimmy crowbar"),
            makeRecord("C3", "completely different text"),
            makeRecord("C4", "window glass smash crowbar entry")
        });

        const auto matches = ma.findSimilar("rear window crowbar forced entry", 10, 0.0);
        for (int i = 1; i < matches.size(); ++i) {
            QVERIFY2(matches[i-1].similarityScore >= matches[i].similarityScore,
                     qPrintable(QStringLiteral("Results not sorted: [%1]=%2 vs [%3]=%4")
                                .arg(i-1).arg(matches[i-1].similarityScore)
                                .arg(i).arg(matches[i].similarityScore)));
        }
    }

    // ─── Case ID preserved ────────────────────────────────────────────────

    void testCaseIdPreserved()
    {
        MOAnalyser ma;
        ma.fit({ makeRecord("CASE_XYZ_789", "burglary window entry forced") });

        const auto matches = ma.findSimilar("window entry forced", 5, 0.0);
        QVERIFY(!matches.isEmpty());
        QCOMPARE(matches.first().caseId, QStringLiteral("CASE_XYZ_789"));
    }

    // ─── IDF property: rare terms have higher weight ─────────────────────

    void testRareTermsRankedHigher()
    {
        // "crowbar" appears only in C1; "entry" appears in all three
        // → C1 should rank higher than C2/C3 for query "entry crowbar"
        MOAnalyser ma;
        ma.fit({
            makeRecord("C1", "entry crowbar window"),  // has crowbar (rare)
            makeRecord("C2", "entry door lock"),       // no crowbar
            makeRecord("C3", "entry forced pry")       // no crowbar
        });

        const auto matches = ma.findSimilar("entry crowbar", 5, 0.0);
        QVERIFY(!matches.isEmpty());
        QCOMPARE(matches.first().caseId, QStringLiteral("C1"));
    }
};

QTEST_GUILESS_MAIN(MOAnalyserDeep2Test)
#include "test_mo_analyser_deep2.moc"
