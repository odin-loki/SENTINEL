#include <QtTest>
#include "inference/MOAnalyser.h"
#include "core/CrimeEvent.h"
#include <cmath>

namespace {

static MOCaseRecord makeCase(const QString& id, const QString& mo, bool resolved = false)
{
    MOCaseRecord r;
    r.caseId   = id;
    r.moText   = mo;
    r.resolved = resolved;
    r.outcome  = resolved ? QStringLiteral("resolved") : QStringLiteral("unresolved");
    return r;
}

} // namespace

class TestMOAnalyserDeep2 : public QObject {
    Q_OBJECT

private slots:

    void testTFIDF_repeatedTermHigherTF()
    {
        // "thief thief thief": TF("thief")=1.0
        // "thief burglar":     TF("thief")=0.5
        // Both unresolved (no boost). Query "thief" should rank c1 first.
        QVector<MOCaseRecord> cases;
        cases << makeCase("c1", "thief thief thief");
        cases << makeCase("c2", "thief burglar");

        MOAnalyser analyser;
        analyser.fit(cases);

        const auto results = analyser.findSimilar("thief", 5, 0.0);
        QVERIFY(!results.isEmpty());
        QCOMPARE(results.first().caseId, QStringLiteral("c1"));
    }

    void testCosineSimilarity_identicalDocument_isOne()
    {
        // Query identical to corpus doc → cosine = 1.0, boostedScore = 1.0 (capped)
        const QString moText = "forced entry night window residential crowbar gloves solo";

        QVector<MOCaseRecord> cases;
        cases << makeCase("exact", moText);
        cases << makeCase("other", "arson petrol warehouse fire accelerant ignition");

        MOAnalyser analyser;
        analyser.fit(cases);

        const auto results = analyser.findSimilar(moText, 5, 0.0);
        QVERIFY(!results.isEmpty());
        QCOMPARE(results.first().caseId, QStringLiteral("exact"));
        QVERIFY2(results.first().similarityScore >= 0.999,
                 qPrintable(QStringLiteral("Self-similarity should be ~1.0, got %1")
                                .arg(results.first().similarityScore)));
    }

    void testCosineSimilarity_orthogonalDocuments_isZero()
    {
        // Two docs with completely disjoint vocabularies.
        // Query on vocab-A terms should give 0 similarity with vocab-B doc.
        QVector<MOCaseRecord> cases;
        cases << makeCase("doc_a", "alpha beta gamma");
        cases << makeCase("doc_b", "delta epsilon zeta");

        MOAnalyser analyser;
        analyser.fit(cases);

        // Query only contains "alpha" — no shared terms with doc_b
        const auto results = analyser.findSimilar("alpha", 5, 0.0);
        for (const auto& m : results) {
            if (m.caseId == QStringLiteral("doc_b")) {
                QVERIFY2(m.similarityScore < 1e-9,
                         qPrintable(QStringLiteral("Orthogonal doc similarity should be 0, got %1")
                                        .arg(m.similarityScore)));
            }
        }
        // doc_a must be found with positive similarity
        const bool foundA = std::any_of(results.begin(), results.end(),
            [](const MOMatch& m){ return m.caseId == QStringLiteral("doc_a"); });
        QVERIFY2(foundA, "doc_a must be found when querying 'alpha'");
    }

    void testIDF_rareTermHigherThanCommonTerm()
    {
        // "unique_rare" appears in 1 of 3 docs; "common" appears in all 3.
        // IDF("unique_rare") = log(4/2)+1 ≈ 1.693
        // IDF("common")      = log(4/4)+1 = 1.0
        // Query "unique_rare" vs doc_a should score higher than
        // query "common" vs doc_a (same mixed doc in both cases).
        QVector<MOCaseRecord> cases;
        cases << makeCase("doc_a", "common unique_rare");
        cases << makeCase("doc_b", "common regular");
        cases << makeCase("doc_c", "common standard");

        MOAnalyser analyser;
        analyser.fit(cases);

        // Find sim("unique_rare", doc_a)
        double simRare = 0.0;
        {
            const auto r = analyser.findSimilar("unique_rare", 5, 0.0);
            for (const auto& m : r)
                if (m.caseId == QStringLiteral("doc_a")) { simRare = m.similarityScore; break; }
        }

        // Find sim("common", doc_a)
        double simCommon = 0.0;
        {
            const auto r = analyser.findSimilar("common", 5, 0.0);
            for (const auto& m : r)
                if (m.caseId == QStringLiteral("doc_a")) { simCommon = m.similarityScore; break; }
        }

        QVERIFY2(simRare > simCommon,
                 qPrintable(QStringLiteral("Rare-term query sim=%1 must be > common-term sim=%2")
                                .arg(simRare).arg(simCommon)));
    }

    void testFindSimilar_sortedDescending()
    {
        // Build corpus with varied similarity to query; results must be sorted descending.
        QVector<MOCaseRecord> cases;
        cases << makeCase("full",   "burglary window night residential crowbar jewels solo");
        cases << makeCase("partial","burglary window night");
        cases << makeCase("single", "burglary");
        cases << makeCase("unrelated", "fraud phone scam bank wire transfer");

        MOAnalyser analyser;
        analyser.fit(cases);

        const auto results = analyser.findSimilar(
            "burglary window night residential crowbar", 10, 0.0);

        for (int i = 1; i < results.size(); ++i) {
            QVERIFY2(results[i-1].similarityScore >= results[i].similarityScore - 1e-9,
                     qPrintable(QStringLiteral("Results not sorted at index %1: %2 < %3")
                                    .arg(i)
                                    .arg(results[i-1].similarityScore)
                                    .arg(results[i].similarityScore)));
        }
    }

    void testEmptyCorpus_returnsNoResults()
    {
        MOAnalyser analyser;
        QVERIFY(!analyser.isFitted());

        const auto results = analyser.findSimilar("robbery night weapon", 10, 0.0);
        QVERIFY2(results.isEmpty(),
                 qPrintable(QStringLiteral("Empty corpus should return 0 results, got %1")
                                .arg(results.size())));
    }

    void testSingleDocumentCorpus()
    {
        const QString moText = QStringLiteral("robbery weapon victim night street");
        QVector<MOCaseRecord> cases;
        cases << makeCase("only", moText);

        MOAnalyser analyser;
        analyser.fit(cases);
        QCOMPARE(analyser.caseCount(), 1);

        // Query with the exact same text → cosine = 1.0
        const auto results = analyser.findSimilar(moText, 5, 0.0);
        QVERIFY(!results.isEmpty());
        QCOMPARE(results.first().caseId, QStringLiteral("only"));
        QVERIFY(results.first().similarityScore >= 0.99);
    }

    void testDocumentNotInCorpus_queryStillWorks()
    {
        // Query text doesn't have to be in the corpus; OOV terms are ignored.
        QVector<MOCaseRecord> cases;
        cases << makeCase("c1", "robbery weapon victim");
        cases << makeCase("c2", "fraud scam elderly phone");

        MOAnalyser analyser;
        analyser.fit(cases);

        // "robbery" is in vocab; "zzzunknown" is not
        const auto results = analyser.findSimilar("robbery zzzunknown", 5, 0.0);
        QVERIFY(!results.isEmpty());
        QCOMPARE(results.first().caseId, QStringLiteral("c1"));
    }
};

QTEST_GUILESS_MAIN(TestMOAnalyserDeep2)
#include "test_mo_analyser_deep2.moc"
