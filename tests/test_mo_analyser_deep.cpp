// test_mo_analyser_deep.cpp — Deep TF-IDF + cosine similarity tests for MOAnalyser
#include <QTest>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <cmath>
#include <algorithm>

#include "inference/MOAnalyser.h"
#include "core/CrimeEvent.h"

// ── Helpers ───────────────────────────────────────────────────────────────────

namespace {

static MOCaseRecord makeCase(const QString& id, const QString& mo,
                              bool resolved = false,
                              const QString& outcome = {})
{
    MOCaseRecord r;
    r.caseId   = id;
    r.moText   = mo;
    r.resolved = resolved;
    r.outcome  = outcome.isEmpty()
                     ? (resolved ? QStringLiteral("resolved") : QStringLiteral("unresolved"))
                     : outcome;
    return r;
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// TestMOAnalyserDeep
// ─────────────────────────────────────────────────────────────────────────────

class TestMOAnalyserDeep : public QObject {
    Q_OBJECT

private slots:

    // ── Basic state ───────────────────────────────────────────────────────────
    void testNotFittedInitially()
    {
        MOAnalyser analyser;
        QVERIFY(!analyser.isFitted());
        QCOMPARE(analyser.caseCount(), 0);
    }

    void testFittedAfterFit()
    {
        MOAnalyser analyser;
        QVector<MOCaseRecord> cases;
        cases << makeCase("c1", "robbery weapon victim nighttime");
        analyser.fit(cases);
        QVERIFY(analyser.isFitted());
        QCOMPARE(analyser.caseCount(), 1);
    }

    // ── 1. TF-IDF weighting: rare features get higher IDF than common ones ────
    void testRareFeaturesRankHigher()
    {
        // "rare_token" appears in only 1/5 docs; "common" appears in 4/5 docs.
        // IDF(rare_token) > IDF(common) → query on rare_token should rank c1 first.
        QVector<MOCaseRecord> cases;
        cases << makeCase("c1", "forced entry rare_token weapon night residential");
        cases << makeCase("c2", "common theft vehicle daytime road");
        cases << makeCase("c3", "common theft residential morning property");
        cases << makeCase("c4", "common property store afternoon cash");
        cases << makeCase("c5", "common robbery street evening victim");

        MOAnalyser analyser;
        analyser.fit(cases);
        QCOMPARE(analyser.caseCount(), 5);

        auto results = analyser.findSimilar("rare_token forced entry", 5, 0.0);
        QVERIFY(!results.isEmpty());
        QCOMPARE(results.first().caseId, QStringLiteral("c1"));
    }

    void testRareTokenHigherSimilarityThanCommonToken()
    {
        // Build corpus where doc A has a rare token and doc B shares only a common token.
        // Query with the rare token should score doc A higher.
        QVector<MOCaseRecord> cases;
        cases << makeCase("A", "jewellery theft night window unique_signature_alpha");
        cases << makeCase("B", "jewellery theft night window street");
        cases << makeCase("C", "jewellery theft day door store");
        cases << makeCase("D", "jewellery theft evening shop display");

        MOAnalyser analyser;
        analyser.fit(cases);

        // Query with unique_signature_alpha (appears in A only)
        auto results = analyser.findSimilar("unique_signature_alpha", 4, 0.0);
        QVERIFY(!results.isEmpty());
        QCOMPARE(results.first().caseId, QStringLiteral("A"));
    }

    void testCommonTermsSimilarScoresAcrossDocs()
    {
        // All docs share "theft"; querying with only "theft" should give similar
        // scores across docs (within a reasonable range).
        QVector<MOCaseRecord> cases;
        cases << makeCase("c1", "theft safebox jewels nighttime gloves mask");
        cases << makeCase("c2", "theft purse wallet daylight crowd");
        cases << makeCase("c3", "theft vehicle parts garage isolated");
        cases << makeCase("c4", "theft phone street runner escaped");
        cases << makeCase("c5", "theft cash register shopfront distract");

        MOAnalyser analyser;
        analyser.fit(cases);

        auto results = analyser.findSimilar("theft", 5, 0.0);
        if (results.size() >= 2) {
            const double diff = std::abs(results[0].similarityScore -
                                         results[results.size()-1].similarityScore);
            QVERIFY2(diff < 0.5,
                     qPrintable(QStringLiteral("Score spread for common token: %1").arg(diff)));
        }
    }

    // ── 2. Cosine similarity: identical → 1.0, orthogonal → 0.0 ──────────────
    void testIdenticalMOScoresOne()
    {
        const QString moText =
            "forced entry night crowbar residential jewels gloves solo masked";

        QVector<MOCaseRecord> cases;
        cases << makeCase("c_exact", moText);
        cases << makeCase("c_other", "completely different arson vehicle petrol ignition");

        MOAnalyser analyser;
        analyser.fit(cases);

        auto results = analyser.findSimilar(moText, 5, 0.0);
        QVERIFY(!results.isEmpty());
        QCOMPARE(results.first().caseId, QStringLiteral("c_exact"));
        QVERIFY2(results.first().similarityScore >= 0.99,
                 qPrintable(QStringLiteral("Identical query score: %1")
                                .arg(results.first().similarityScore)));
    }

    void testOutOfVocabQueryScoresZero()
    {
        QVector<MOCaseRecord> cases;
        cases << makeCase("c1", "arson petrol ignition vehicle");
        cases << makeCase("c2", "robbery weapon intimidation victim");

        MOAnalyser analyser;
        analyser.fit(cases);

        // Query tokens are completely absent from the vocabulary
        auto results = analyser.findSimilar("zzz_nonexistent_xyz_qwerty", 5, 0.0);
        for (const auto& m : results) {
            QVERIFY2(m.similarityScore < 1e-9,
                     qPrintable(QStringLiteral("Expected ~0 score, got: %1")
                                    .arg(m.similarityScore)));
        }
    }

    void testTotallyDifferentMOsLowSimilarity()
    {
        QVector<MOCaseRecord> cases;
        cases << makeCase("c1", "forced entry window residential night jewels crowbar solo");
        cases << makeCase("c2", "arson petrol commercial warehouse daytime group fire");

        MOAnalyser analyser;
        analyser.fit(cases);

        auto results = analyser.findSimilar("forced entry window residential", 5, 0.0);
        double c2Score = 0.0;
        bool foundC1 = false;
        for (const auto& m : results) {
            if (m.caseId == "c1") foundC1 = true;
            if (m.caseId == "c2") c2Score = m.similarityScore;
        }
        QVERIFY(foundC1);
        QVERIFY2(c2Score < 0.3,
                 qPrintable(QStringLiteral("c2 score should be low: %1").arg(c2Score)));
    }

    // ── 3. Similar MOs → high score; dissimilar → low score ──────────────────
    void testSimilarMOHighScore()
    {
        QVector<MOCaseRecord> cases;
        cases << makeCase("c_similar",
                          "smashed window night residential jewels solo tool crowbar");
        cases << makeCase("c_dissimilar",
                          "phishing email fraud online banking transfer credentials");

        MOAnalyser analyser;
        analyser.fit(cases);

        auto results = analyser.findSimilar(
            "window night residential jewelry crowbar solo broke", 5, 0.0);

        double simScore = 0.0, disScore = 0.0;
        for (const auto& m : results) {
            if (m.caseId == "c_similar")    simScore = m.similarityScore;
            if (m.caseId == "c_dissimilar") disScore = m.similarityScore;
        }
        QVERIFY2(simScore > disScore,
                 qPrintable(QStringLiteral("sim=%1 dis=%2").arg(simScore).arg(disScore)));
        QVERIFY(simScore > 0.3);
    }

    void testDissimilarMOsLowOrAbsent()
    {
        QVector<MOCaseRecord> cases;
        cases << makeCase("c1", "bank robbery gun hostage vault guard forced");
        cases << makeCase("c2", "cyber fraud identity theft credit card online phishing");
        cases << makeCase("c3", "homicide blunt force domestic scene victim weapon");

        MOAnalyser analyser;
        analyser.fit(cases);

        // Query strongly matches c1 only
        auto results = analyser.findSimilar("bank robbery gun hostage vault", 3, 0.0);
        if (!results.isEmpty()) {
            QCOMPARE(results.first().caseId, QStringLiteral("c1"));
            if (results.size() >= 2) {
                QVERIFY(results[0].similarityScore > results[1].similarityScore);
            }
        }
    }

    // ── 4. Diverse corpus: top-k correctly ranked ─────────────────────────────
    void testTopKRankingInDiverseCorpus()
    {
        QVector<MOCaseRecord> cases;
        // Burglary cluster — should match query
        cases << makeCase("burg1", "forced entry window residential night jewels crowbar solo");
        cases << makeCase("burg2", "smashed window home night gold rings gloves entry");
        cases << makeCase("burg3", "door forced lock home jewelry wristwatch night entry");
        // Fraud cluster — should NOT match query
        cases << makeCase("fraud1", "phone scam elderly victim bank transfer money");
        cases << makeCase("fraud2", "email phishing credit card online fraud money");
        // Arson — should NOT match query
        cases << makeCase("arson1", "petrol ignition commercial premises fire night");

        MOAnalyser analyser;
        analyser.fit(cases);

        auto results = analyser.findSimilar(
            "forced entry residential night crowbar jewelry window", 3, 0.0);

        QVERIFY(results.size() >= 2);
        for (int i = 0; i < std::min(3, static_cast<int>(results.size())); ++i) {
            const QString& cid = results[i].caseId;
            QVERIFY2(cid.startsWith("burg"),
                     qPrintable(QStringLiteral("Rank %1: expected burg*, got %2")
                                    .arg(i).arg(cid)));
        }
    }

    void testTopKRespectsBound()
    {
        QVector<MOCaseRecord> cases;
        for (int i = 0; i < 20; ++i) {
            cases << makeCase(QStringLiteral("c%1").arg(i),
                              QStringLiteral("theft robbery weapon night variant_%1").arg(i));
        }

        MOAnalyser analyser;
        analyser.fit(cases);

        const auto results = analyser.findSimilar("theft robbery weapon night", 5, 0.0);
        QVERIFY2(results.size() <= 5,
                 qPrintable(QStringLiteral("Got %1 results, expected ≤ 5")
                                .arg(results.size())));
    }

    void testTopKResultsSortedDescending()
    {
        QVector<MOCaseRecord> cases;
        for (int i = 0; i < 10; ++i) {
            cases << makeCase(QStringLiteral("c%1").arg(i),
                              QStringLiteral("burglary night window residential %1").arg(i));
        }

        MOAnalyser analyser;
        analyser.fit(cases);

        const auto results = analyser.findSimilar("burglary night window", 10, 0.0);
        for (int i = 1; i < results.size(); ++i) {
            QVERIFY(results[i-1].similarityScore >= results[i].similarityScore);
        }
    }

    // ── 5. Precision@K: same-series events rank higher than cross-series ──────
    void testPrecisionAtK_SameSeriesRanksHigher()
    {
        // Series A: residential burglary at night via forced window
        // Series B: vehicle theft from parking lot during daytime
        QVector<MOCaseRecord> cases;
        cases << makeCase("A1", "forced entry window residential night jewels gloves crowbar seriesA");
        cases << makeCase("A2", "smashed window home night gold gloves tool seriesA");
        cases << makeCase("A3", "broken window house night jewelry mask solo seriesA");
        cases << makeCase("B1", "vehicle parking lot daytime jemmy boot isolated seriesB");
        cases << makeCase("B2", "car boot jemmy parking ramp daylight tools seriesB");
        cases << makeCase("B3", "stolen vehicle lot afternoon jemmy door seriesB");

        MOAnalyser analyser;
        analyser.fit(cases);

        // Query similar to Series A
        auto results = analyser.findSimilar(
            "window residential night jewels gloves crowbar forced", 6, 0.0);

        // Find last A-rank and first B-rank
        int lastARank = -1, firstBRank = -1;
        for (int i = 0; i < results.size(); ++i) {
            if (results[i].caseId.startsWith('A')) lastARank  = i;
            if (results[i].caseId.startsWith('B') && firstBRank < 0) firstBRank = i;
        }

        QVERIFY2(lastARank >= 0, "At least one A-series case must be found");
        if (firstBRank >= 0) {
            QVERIFY2(lastARank < firstBRank,
                     qPrintable(QStringLiteral("A-series last rank=%1, B-series first rank=%2")
                                    .arg(lastARank).arg(firstBRank)));
        }
    }

    void testPrecisionAtK_ThreeSeriesCluster()
    {
        // Three distinct MO clusters; query on cluster 1 should return cluster-1 cases first.
        QVector<MOCaseRecord> cases;
        // Cluster 1: burglary
        for (int i = 0; i < 4; ++i)
            cases << makeCase(QStringLiteral("burg_%1").arg(i),
                              QStringLiteral("burglary window night residential jewel "
                                             "gloves crowbar solo entry forced %1").arg(i));
        // Cluster 2: fraud
        for (int i = 0; i < 4; ++i)
            cases << makeCase(QStringLiteral("fraud_%1").arg(i),
                              QStringLiteral("fraud elderly phone scam bank transfer "
                                             "money wire impersonation %1").arg(i));
        // Cluster 3: assault
        for (int i = 0; i < 4; ++i)
            cases << makeCase(QStringLiteral("assault_%1").arg(i),
                              QStringLiteral("assault victim weapon street intimidation "
                                             "gang confrontation injury %1").arg(i));

        MOAnalyser analyser;
        analyser.fit(cases);

        auto results = analyser.findSimilar(
            "burglary window residential night jewels crowbar forced solo", 4, 0.0);

        QVERIFY(!results.isEmpty());
        // Top result must be a burglary case
        QVERIFY2(results.first().caseId.startsWith("burg"),
                 qPrintable(QStringLiteral("Top result: %1").arg(results.first().caseId)));
    }

    // ── 6. Edge cases ─────────────────────────────────────────────────────────
    void testEmptyMOTextInCorpus()
    {
        QVector<MOCaseRecord> cases;
        cases << makeCase("c_normal", "robbery weapon night victim street");
        cases << makeCase("c_empty",  "");

        MOAnalyser analyser;
        analyser.fit(cases);
        QVERIFY(analyser.isFitted());
        QCOMPARE(analyser.caseCount(), 2);

        // Should not crash; c_normal should be found
        auto results = analyser.findSimilar("robbery weapon night", 5, 0.0);
        QVERIFY(!results.isEmpty());
        QCOMPARE(results.first().caseId, QStringLiteral("c_normal"));
    }

    void testEmptyQueryText()
    {
        QVector<MOCaseRecord> cases;
        cases << makeCase("c1", "robbery weapon night residential");
        MOAnalyser analyser;
        analyser.fit(cases);

        // Empty query → zero vector → all cosine scores 0.0
        auto results = analyser.findSimilar("", 5, 0.0);
        for (const auto& m : results) {
            QVERIFY2(m.similarityScore < 1e-9,
                     qPrintable(QStringLiteral("Expected 0, got %1").arg(m.similarityScore)));
        }
    }

    void testSingleWordMO()
    {
        QVector<MOCaseRecord> cases;
        cases << makeCase("c1", "robbery");
        cases << makeCase("c2", "robbery nighttime residential window");

        MOAnalyser analyser;
        analyser.fit(cases);

        auto results = analyser.findSimilar("robbery", 5, 0.0);
        QVERIFY(!results.isEmpty());

        bool foundC1 = false;
        for (const auto& m : results) {
            if (m.caseId == "c1") {
                foundC1 = true;
                QVERIFY(m.similarityScore > 0.0);
            }
        }
        QVERIFY(foundC1);
    }

    void testVeryLongMOText()
    {
        // Construct a long MO with 100 repetitions of 10 distinct tokens
        QStringList words = {"forced", "entry", "night", "residential", "window",
                              "crowbar", "jewels", "gloves", "solo", "masked"};
        QString longMO;
        for (int i = 0; i < 100; ++i)
            for (const auto& w : words)
                longMO += w + QLatin1Char(' ');
        longMO = longMO.trimmed();

        QVector<MOCaseRecord> cases;
        cases << makeCase("c_long",  longMO);
        cases << makeCase("c_short", "robbery weapon victim");

        MOAnalyser analyser;
        analyser.fit(cases);
        QVERIFY(analyser.isFitted());

        auto results = analyser.findSimilar(
            "forced entry night residential window", 5, 0.0);
        QVERIFY(!results.isEmpty());
        QCOMPARE(results.first().caseId, QStringLiteral("c_long"));
    }

    void testUnicodeText()
    {
        QVector<MOCaseRecord> cases;
        // German
        cases << makeCase("c_de", QString::fromUtf8("Einbruch Nacht Fenster Wohnung Schmuck"));
        // Arabic
        cases << makeCase("c_ar", QString::fromUtf8("\u0633\u0631\u0642\u0629 \u0644\u064a\u0644"));
        // Japanese
        cases << makeCase("c_ja", QString::fromUtf8("\u5f37\u76d7 \u591c\u9593 \u4f4f\u5b85"));

        MOAnalyser analyser;
        analyser.fit(cases);
        QVERIFY(analyser.isFitted());
        QCOMPARE(analyser.caseCount(), 3);

        // Query in German should match the German record
        auto results = analyser.findSimilar(
            QString::fromUtf8("Einbruch Nacht Fenster"), 3, 0.0);
        QVERIFY(!results.isEmpty());
        QCOMPARE(results.first().caseId, QStringLiteral("c_de"));
    }

    void testAllIdenticalMOs()
    {
        // All docs have the same text → equal scores
        QVector<MOCaseRecord> cases;
        for (int i = 0; i < 5; ++i)
            cases << makeCase(QStringLiteral("c%1").arg(i),
                              "robbery weapon victim street night");

        MOAnalyser analyser;
        analyser.fit(cases);

        auto results = analyser.findSimilar("robbery weapon victim", 5, 0.0);
        QVERIFY(results.size() == 5);
        for (int i = 1; i < results.size(); ++i) {
            const double diff = std::abs(results[0].similarityScore -
                                          results[i].similarityScore);
            QVERIFY2(diff < 1e-9,
                     qPrintable(QStringLiteral("Score not equal for identical docs: diff=%1")
                                    .arg(diff)));
        }
    }

    void testSingleDocCorpus()
    {
        QVector<MOCaseRecord> cases;
        cases << makeCase("c1", "robbery weapon victim");

        MOAnalyser analyser;
        analyser.fit(cases);
        QCOMPARE(analyser.caseCount(), 1);

        auto results = analyser.findSimilar("robbery weapon victim", 5, 0.0);
        QVERIFY(!results.isEmpty());
        QCOMPARE(results.first().caseId, QStringLiteral("c1"));
        QVERIFY(results.first().similarityScore >= 0.99);
    }

    // ── 7. Performance: index 500 cases, query in < 100 ms ───────────────────
    void testPerformance500Cases()
    {
        QVector<MOCaseRecord> cases;
        cases.reserve(500);

        const QStringList templates = {
            "forced entry %1 residential night crowbar jewels gloves solo window",
            "vehicle theft %1 parking lot daytime jemmy boot isolated tools",
            "fraud %1 elderly phone scam bank transfer wire money impersonation",
            "robbery %1 weapon victim street nighttime group intimidation",
            "arson %1 petrol commercial premises fire accelerant ignition",
        };

        for (int i = 0; i < 500; ++i) {
            cases << makeCase(
                QStringLiteral("perf_%1").arg(i),
                templates[i % templates.size()].arg(
                    QStringLiteral("variant_%1").arg(i))
            );
        }

        MOAnalyser analyser;
        analyser.fit(cases);
        QVERIFY(analyser.isFitted());
        QCOMPARE(analyser.caseCount(), 500);

        // Time 10 queries; total must be < 1000 ms (avg < 100 ms each)
        QElapsedTimer timer;
        timer.start();
        for (int q = 0; q < 10; ++q) {
            const auto results = analyser.findSimilar(
                "forced entry residential night crowbar jewels window", 10, 0.0);
            QVERIFY(!results.isEmpty());
        }
        const qint64 totalMs = timer.elapsed();
        QVERIFY2(totalMs < 1000,
                 qPrintable(QStringLiteral("10 queries took %1 ms (limit 1000 ms)")
                                .arg(totalMs)));
    }

    // ── Resolved case boost ───────────────────────────────────────────────────
    void testResolvedCasesBoosted()
    {
        // c_resolved has ALL query tokens (exact match → cosine = 1.0 → boosted to 1.0)
        // c_unresolved is missing one query-specific token → cosine < 1.0
        // After boost, c_resolved should rank first.
        QVector<MOCaseRecord> cases;
        cases << makeCase("c_resolved",
                          "robbery weapon victim street nighttime unique_resolved_tag",
                          true);
        cases << makeCase("c_unresolved",
                          "robbery weapon victim street nighttime",
                          false);

        MOAnalyser analyser;
        analyser.fit(cases);

        // Query matches c_resolved perfectly (includes unique_resolved_tag)
        const auto results = analyser.findSimilar(
            "robbery weapon victim street nighttime unique_resolved_tag", 5, 0.0);
        QVERIFY(results.size() >= 2);
        QCOMPARE(results.first().caseId, QStringLiteral("c_resolved"));
        QVERIFY(results.first().resolved == true);
    }

    void testResolvedBoostCappedAtOne()
    {
        const QString moText = "robbery weapon nighttime victim";
        QVector<MOCaseRecord> cases;
        cases << makeCase("c1", moText, true);

        MOAnalyser analyser;
        analyser.fit(cases);

        auto results = analyser.findSimilar(moText, 5, 0.0);
        QVERIFY(!results.isEmpty());
        QVERIFY2(results.first().similarityScore <= 1.0,
                 qPrintable(QStringLiteral("Score exceeds 1.0: %1")
                                .arg(results.first().similarityScore)));
    }

    // ── Shared features ───────────────────────────────────────────────────────
    void testSharedFeaturesReturned()
    {
        QVector<MOCaseRecord> cases;
        cases << makeCase("c1", "forced entry night residential crowbar jewels");

        MOAnalyser analyser;
        analyser.fit(cases);

        auto results = analyser.findSimilar("forced entry night crowbar", 5, 0.0);
        QVERIFY(!results.isEmpty());
        const auto& shared = results.first().sharedFeatures;
        QVERIFY(shared.contains("forced"));
        QVERIFY(shared.contains("entry"));
        QVERIFY(shared.contains("night"));
        QVERIFY(shared.contains("crowbar"));
    }

    void testSharedFeaturesNoDuplicates()
    {
        QVector<MOCaseRecord> cases;
        cases << makeCase("c1", "robbery robbery robbery night night victim");

        MOAnalyser analyser;
        analyser.fit(cases);

        auto results = analyser.findSimilar("robbery robbery night", 5, 0.0);
        QVERIFY(!results.isEmpty());
        const auto& shared = results.first().sharedFeatures;
        // Each token should appear at most once in sharedFeatures
        QCOMPARE(shared.count("robbery"), 1);
        QCOMPARE(shared.count("night"),   1);
    }

    // ── minSimilarity filter ─────────────────────────────────────────────────
    void testMinSimilarityFilter()
    {
        QVector<MOCaseRecord> cases;
        cases << makeCase("c1", "burglary residential night window forced entry");
        cases << makeCase("c2", "drug possession street arrest");

        MOAnalyser analyser;
        analyser.fit(cases);

        const auto results = analyser.findSimilar("burglary residential", 5, 0.8);
        for (const auto& m : results) {
            QVERIFY2(m.similarityScore >= 0.8,
                     qPrintable(QStringLiteral("Score %1 below minSimilarity 0.8")
                                    .arg(m.similarityScore)));
        }
    }

    void testMinSimilarityZeroReturnsAll()
    {
        QVector<MOCaseRecord> cases;
        for (int i = 0; i < 5; ++i)
            cases << makeCase(QStringLiteral("c%1").arg(i),
                              QStringLiteral("alpha beta gamma %1").arg(i));

        MOAnalyser analyser;
        analyser.fit(cases);

        // minSimilarity=0.0 should return all matches that share at least one token
        const auto results = analyser.findSimilar("alpha beta gamma", 10, 0.0);
        QVERIFY(results.size() >= 1);
    }

    // ── Refitting ─────────────────────────────────────────────────────────────
    void testRefitReplacesOldIndex()
    {
        MOAnalyser analyser;
        QVector<MOCaseRecord> cases1;
        cases1 << makeCase("old1", "robbery weapon victim street");
        analyser.fit(cases1);
        QCOMPARE(analyser.caseCount(), 1);

        QVector<MOCaseRecord> cases2;
        for (int i = 0; i < 5; ++i)
            cases2 << makeCase(QStringLiteral("new%1").arg(i),
                               QStringLiteral("fraud phone scam bank %1").arg(i));
        analyser.fit(cases2);
        QCOMPARE(analyser.caseCount(), 5);

        // Old corpus should be gone; query on old vocab yields no old IDs
        const auto results = analyser.findSimilar("fraud phone scam", 5, 0.0);
        for (const auto& m : results) {
            QVERIFY(m.caseId.startsWith("new"));
        }
    }
};

QTEST_MAIN(TestMOAnalyserDeep)
#include "test_mo_analyser_deep.moc"
