// test_mo_analyser_accuracy.cpp
// TF-IDF accuracy and ranking correctness tests for MOAnalyser.
#include <QTest>

#include "inference/MOAnalyser.h"
#include "core/CrimeEvent.h"

class MOAnalyserAccuracyTest : public QObject
{
    Q_OBJECT

    static MOCaseRecord makeCase(const QString& caseId, const QString& moText,
                                  bool resolved = false, int convYear = 0)
    {
        MOCaseRecord rec;
        rec.caseId         = caseId;
        rec.moText         = moText;
        rec.resolved       = resolved;
        rec.convictionYear = convYear;
        return rec;
    }

private slots:

    // ── 1. Identical text → similarity = 1.0 ─────────────────────────────────
    void testCosineSimilarityIdentical()
    {
        MOAnalyser ana;
        const QString text = "forced entry residential night cash jewellery solo";
        QVector<MOCaseRecord> cases = { makeCase("C1", text) };
        ana.fit(cases);

        auto results = ana.findSimilar(text, 1, 0.0);
        QVERIFY2(!results.isEmpty(), "Should find at least one match for identical text");
        QVERIFY2(results.first().similarityScore >= 0.95,
                 qPrintable(QStringLiteral("Identical text similarity should be ~1, got %1")
                    .arg(results.first().similarityScore)));
    }

    // ── 2. Completely different text → similarity near 0 ─────────────────────
    void testCosineSimilarityOrthogonal()
    {
        MOAnalyser ana;
        QVector<MOCaseRecord> cases = {
            makeCase("C1", "burglary forced window cash residential"),
        };
        ana.fit(cases);

        // Query with no word overlap
        auto results = ana.findSimilar("elephant symphony orchestra", 5, 0.0);
        if (!results.isEmpty()) {
            QVERIFY2(results.first().similarityScore < 0.2,
                     qPrintable(QStringLiteral("Orthogonal text similarity should be near 0, got %1")
                        .arg(results.first().similarityScore)));
        }
        // Empty result is also valid (minSimilarity filtering kicks in earlier)
    }

    // ── 3. Closest case ranks first ───────────────────────────────────────────
    void testRankingOrderCorrect()
    {
        MOAnalyser ana;
        QVector<MOCaseRecord> cases = {
            makeCase("CASE-A", "unrelated words elephant symphony orchid"),
            makeCase("CASE-B", "forced entry residential night cash jewellery"),
            makeCase("CASE-C", "daylight robbery shop victim weapon"),
        };
        ana.fit(cases);

        // Query closely matches CASE-B
        auto results = ana.findSimilar(
            "forced entry residential cash night", 3, 0.0);

        QVERIFY2(!results.isEmpty(), "findSimilar must return results");
        QCOMPARE(results.first().caseId, QStringLiteral("CASE-B"));
    }

    // ── 4. Resolved case gets boosted score ───────────────────────────────────
    void testResolvedCaseBoosted()
    {
        MOAnalyser ana;
        // Both cases share the same common tokens ("forced entry residential") with
        // the query, plus one unique token each. By symmetry their raw cosine
        // similarities are equal; the 1.2× resolved-case boost breaks the tie.
        QVector<MOCaseRecord> cases = {
            makeCase("UNRESOLVED", "forced entry residential tok_unresolved_xx", false, 0),
            makeCase("RESOLVED",   "forced entry residential tok_resolved_yy",   true,  2022),
        };
        ana.fit(cases);

        auto results = ana.findSimilar("forced entry residential", 2, 0.0);
        QVERIFY2(results.size() >= 2, "Both cases should be returned");

        // The resolved case should appear first (1.2× boost applied)
        QCOMPARE(results.first().caseId, QStringLiteral("RESOLVED"));
        QVERIFY2(results.first().resolved, "Top result must be the resolved case");
    }

    // ── 5. minSimilarity filters low-similarity results ───────────────────────
    void testMinSimilarityFilters()
    {
        MOAnalyser ana;
        QVector<MOCaseRecord> cases = {
            makeCase("C1", "forced entry residential night"),
            makeCase("C2", "vehicle crime car stolen"),
            makeCase("C3", "drug possession cannabis street"),
        };
        ana.fit(cases);

        // High minSimilarity: only very close match should return
        auto strict = ana.findSimilar("forced entry residential", 10, 0.8);
        for (const auto& r : std::as_const(strict)) {
            QVERIFY2(r.similarityScore >= 0.8,
                     qPrintable(QStringLiteral("All returned results must meet minSimilarity=0.8, got %1")
                        .arg(r.similarityScore)));
        }
    }

    // ── 6. topK limits result count ───────────────────────────────────────────
    void testTopKLimitsResults()
    {
        MOAnalyser ana;
        QVector<MOCaseRecord> cases;
        for (int i = 0; i < 10; ++i) {
            cases.append(makeCase(QStringLiteral("CASE-%1").arg(i),
                                  QStringLiteral("burglary forced residential cash night")));
        }
        ana.fit(cases);

        auto results = ana.findSimilar("burglary forced residential", 3, 0.0);
        QVERIFY2(results.size() <= 3,
                 qPrintable(QStringLiteral("topK=3 must limit results to <= 3, got %1")
                    .arg(results.size())));
    }

    // ── 7. Shared features extracted for matching tokens ─────────────────────
    void testSharedFeaturesExtracted()
    {
        MOAnalyser ana;
        QVector<MOCaseRecord> cases = {
            makeCase("CASE-SHARED", "forced entry residential night cash jewellery"),
        };
        ana.fit(cases);

        auto results = ana.findSimilar(
            "forced entry residential cash", 1, 0.0);
        QVERIFY2(!results.isEmpty(), "Should find at least one match");
        QVERIFY2(!results.first().sharedFeatures.isEmpty(),
                 "Shared features must not be empty for matching text");

        // At minimum, "forced", "entry", "residential", or "cash" should appear
        const QStringList shared = results.first().sharedFeatures;
        bool hasExpected = shared.contains("forced") || shared.contains("entry")
                        || shared.contains("residential") || shared.contains("cash");
        QVERIFY2(hasExpected,
                 qPrintable(QStringLiteral("Shared features should contain at least one known word. Got: %1")
                    .arg(shared.join(", "))));
    }

    // ── 8. Empty database → returns empty, no crash ───────────────────────────
    void testEmptyDatabaseReturnsEmpty()
    {
        MOAnalyser ana;
        // No fit() called — should return empty gracefully
        auto results = ana.findSimilar("burglary forced entry", 5, 0.0);
        QCOMPARE(results.size(), 0);
    }

    // ── 9. caseId, resolved, outcome fields populated in result ──────────────
    void testResultFieldsPopulated()
    {
        MOAnalyser ana;
        QVector<MOCaseRecord> cases = {
            makeCase("CASE-XYZ", "forced residential night", true, 2021),
        };
        cases[0].outcome = "Convicted";
        cases[0].suspectProfile = "Adult male";
        ana.fit(cases);

        auto results = ana.findSimilar("forced residential", 1, 0.0);
        QVERIFY2(!results.isEmpty(), "Should return at least one result");

        const auto& r = results.first();
        QCOMPARE(r.caseId,    QStringLiteral("CASE-XYZ"));
        QVERIFY2(r.resolved,  "resolved flag must be set");
        QCOMPARE(r.convictionYear, 2021);
        QCOMPARE(r.outcome,   QStringLiteral("Convicted"));
        QCOMPARE(r.suspectProfile, QStringLiteral("Adult male"));
    }

    // ── 10. Multiple fits reset state ────────────────────────────────────────
    void testRefitResetsState()
    {
        MOAnalyser ana;

        // First fit: case about burglary
        ana.fit({ makeCase("OLD", "burglary residential forced") });
        auto first = ana.findSimilar("burglary", 5, 0.0);
        const bool foundOld = std::any_of(first.begin(), first.end(),
            [](const MOMatch& m){ return m.caseId == "OLD"; });
        QVERIFY2(foundOld, "OLD case should be found after first fit");

        // Second fit: completely different cases, no burglary
        ana.fit({ makeCase("NEW1", "vehicle crime car stolen"),
                  makeCase("NEW2", "drug possession cannabis") });

        auto second = ana.findSimilar("burglary", 5, 0.0);
        const bool foundOldAfterRefit = std::any_of(second.begin(), second.end(),
            [](const MOMatch& m){ return m.caseId == "OLD"; });
        QVERIFY2(!foundOldAfterRefit, "OLD case must not appear after refit");
    }
};

QTEST_MAIN(MOAnalyserAccuracyTest)
#include "test_mo_analyser_accuracy.moc"
