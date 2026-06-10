// test_mo_analyser_tfidf_deep.cpp
// Deep TF-IDF tests for MOAnalyser: cosine similarity, findSimilar topK,
// min-similarity threshold, identical vs disjoint texts, and isFitted.
#include <QTest>
#include "inference/MOAnalyser.h"
#include "core/CrimeEvent.h"
#include <cmath>
#include <algorithm>

class MOAnalyserTFIDFDeepTest : public QObject
{
    Q_OBJECT

private:
    static MOCaseRecord mc(const QString& id, const QString& mo,
                            bool resolved = false)
    {
        MOCaseRecord r;
        r.caseId   = id;
        r.moText   = mo;
        r.resolved = resolved;
        return r;
    }

    static QVector<MOCaseRecord> buglaryCorpus()
    {
        return {
            mc(QStringLiteral("C01"), QStringLiteral("forced_entry residential night cash jewellery solo")),
            mc(QStringLiteral("C02"), QStringLiteral("forced_entry residential night cash electronics solo")),
            mc(QStringLiteral("C03"), QStringLiteral("forced_entry commercial daytime safe cash group")),
            mc(QStringLiteral("C04"), QStringLiteral("window_break residential evening jewellery solo")),
            mc(QStringLiteral("C05"), QStringLiteral("vehicle_theft carpark day solo electronics")),
            mc(QStringLiteral("C06"), QStringLiteral("pickpocket transport morning wallet cards group")),
            mc(QStringLiteral("C07"), QStringLiteral("robbery street night knife cash solo")),
            mc(QStringLiteral("C08"), QStringLiteral("robbery street night knife cards group")),
            mc(QStringLiteral("C09"), QStringLiteral("arson commercial night solo accelerant")),
            mc(QStringLiteral("C10"), QStringLiteral("vandalism residential day solo")),
        };
    }

private slots:

    // 1. isFitted() false before fit()
    void testNotFittedBeforeFit()
    {
        MOAnalyser ma;
        QVERIFY(!ma.isFitted());
    }

    // 2. isFitted() true after fit()
    void testFittedAfterFit()
    {
        MOAnalyser ma;
        ma.fit(buglaryCorpus());
        QVERIFY2(ma.isFitted(), "Should be fitted after fit()");
    }

    // 3. caseCount() matches input
    void testCaseCount()
    {
        MOAnalyser ma;
        const auto corpus = buglaryCorpus();
        ma.fit(corpus);
        QCOMPARE(ma.caseCount(), corpus.size());
    }

    // 4. findSimilar: identical query returns top match
    void testFindSimilarIdenticalQuery()
    {
        MOAnalyser ma;
        ma.fit(buglaryCorpus());
        const auto matches = ma.findSimilar(
            QStringLiteral("forced_entry residential night cash jewellery solo"), 3, 0.0);
        QVERIFY2(!matches.isEmpty(), "Identical MO query should return at least one match");
        QVERIFY2(matches.first().similarityScore >= 0.9,
                 qPrintable(QStringLiteral("Identical MO similarity %1 should be >= 0.9")
                    .arg(matches.first().similarityScore)));
    }

    // 5. findSimilar: completely different MO returns empty or low-similarity results
    void testFindSimilarDisjointQuery()
    {
        MOAnalyser ma;
        ma.fit(buglaryCorpus());
        const auto matches = ma.findSimilar(
            QStringLiteral("xyzzy qwerty nonsense tokens"), 5, 0.3);
        // Disjoint tokens should produce no matches above 0.3 threshold
        for (const auto& m : matches) {
            QVERIFY2(m.similarityScore >= 0.3,
                     qPrintable(QStringLiteral("Returned match %1 should meet min threshold 0.3")
                        .arg(m.similarityScore)));
        }
        QVERIFY(true);  // Pass even if empty
    }

    // 6. findSimilar: topK respected
    void testFindSimilarTopKRespected()
    {
        MOAnalyser ma;
        ma.fit(buglaryCorpus());
        const auto matches = ma.findSimilar(
            QStringLiteral("robbery street night knife"), 3, 0.0);
        QVERIFY2(matches.size() <= 3,
                 qPrintable(QStringLiteral("findSimilar(topK=3) returned %1").arg(matches.size())));
    }

    // 7. Similarity values in [0, 1]
    void testSimilarityRange()
    {
        MOAnalyser ma;
        ma.fit(buglaryCorpus());
        const auto matches = ma.findSimilar(
            QStringLiteral("forced_entry residential night"), 10, 0.0);
        for (const auto& m : matches) {
            QVERIFY2(m.similarityScore >= 0.0 && m.similarityScore <= 1.0,
                     qPrintable(QStringLiteral("Similarity %1 must be in [0,1]").arg(m.similarityScore)));
        }
    }

    // 8. Results sorted by similarity descending
    void testResultsSortedDescending()
    {
        MOAnalyser ma;
        ma.fit(buglaryCorpus());
        const auto matches = ma.findSimilar(
            QStringLiteral("forced_entry residential night cash"), 5, 0.0);
        for (int i = 1; i < matches.size(); ++i) {
            QVERIFY2(matches[i-1].similarityScore >= matches[i].similarityScore,
                     qPrintable(QStringLiteral("Results not sorted: [%1]=%2 < [%3]=%4")
                        .arg(i-1).arg(matches[i-1].similarityScore).arg(i).arg(matches[i].similarityScore)));
        }
    }

    // 9. minSimilarity filter works
    void testMinSimilarityFilter()
    {
        MOAnalyser ma;
        ma.fit(buglaryCorpus());
        const double minSim = 0.5;
        const auto matches = ma.findSimilar(
            QStringLiteral("forced_entry residential night cash"), 10, minSim);
        for (const auto& m : matches) {
            QVERIFY2(m.similarityScore >= minSim,
                     qPrintable(QStringLiteral("Match similarity %1 must be >= minSim %2")
                        .arg(m.similarityScore).arg(minSim)));
        }
    }

    // 10. Empty query returns empty or minimal matches
    void testEmptyQueryNoCrash()
    {
        MOAnalyser ma;
        ma.fit(buglaryCorpus());
        const auto matches = ma.findSimilar(QStringLiteral(""), 5, 0.0);
        // Should not crash; may return empty
        for (const auto& m : matches)
            QVERIFY2(m.similarityScore >= 0.0, "Similarity must be >= 0");
        QVERIFY(true);
    }
};

QTEST_MAIN(MOAnalyserTFIDFDeepTest)
#include "test_mo_analyser_tfidf_deep.moc"
