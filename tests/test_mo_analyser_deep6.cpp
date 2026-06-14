// test_mo_analyser_deep6.cpp — Deep audit iteration 23: MOAnalyser
// Probes: empty corpus, identical/orthogonal similarity, topK cap, TF-IDF
// frequency weighting, and incremental resolved-case indexing via fit().

#include <QtTest/QtTest>
#include <cmath>
#include <algorithm>
#include "inference/MOAnalyser.h"
#include "core/CrimeEvent.h"

class MOAnalyserDeep6Test : public QObject
{
    Q_OBJECT

    static MOCaseRecord rec(const QString& id, const QString& mo, bool resolved = false)
    {
        MOCaseRecord r;
        r.caseId   = id;
        r.moText   = mo;
        r.resolved = resolved;
        return r;
    }

private slots:
    void testEmptyResolvedDatabaseReturnsNoMatches();
    void testIdenticalMoStringsScoreNearOne();
    void testOrthogonalMoStringsScoreLow();
    void testTopKRespectsLimit();
    void testTfidfWeightsDifferByTermFrequency();
    void testAddResolvedCaseThenMatchFindsIt();
    void testMinSimilarityFiltersWeakOrthogonalMatches();
};

// ─── Tests ───────────────────────────────────────────────────────────────────

void MOAnalyserDeep6Test::testEmptyResolvedDatabaseReturnsNoMatches()
{
    MOAnalyser ma;
    ma.fit({});
    QVERIFY(!ma.isFitted());

    const auto matches = ma.findSimilar(
        QStringLiteral("forced entry residential night crowbar"), 5, 0.0);
    QVERIFY2(matches.isEmpty(),
             "Empty corpus must return no matches");
}

void MOAnalyserDeep6Test::testIdenticalMoStringsScoreNearOne()
{
    const QString mo = QStringLiteral("smash grab retail display glass daytime escape vehicle");
    MOAnalyser ma;
    ma.fit({
        rec(QStringLiteral("REF"), mo),
        rec(QStringLiteral("OTHER"), QStringLiteral("arson warehouse petrol accelerant ignition")),
    });

    const auto matches = ma.findSimilar(mo, 1, 0.0);
    QVERIFY(!matches.isEmpty());
    QCOMPARE(matches.first().caseId, QStringLiteral("REF"));
    QVERIFY2(matches.first().similarityScore >= 0.99,
             qPrintable(QStringLiteral("Identical MO expected ~1.0, got %1")
                            .arg(matches.first().similarityScore)));
}

void MOAnalyserDeep6Test::testOrthogonalMoStringsScoreLow()
{
    MOAnalyser ma;
    ma.fit({
        rec(QStringLiteral("FRAUD"), QStringLiteral("email phishing bank transfer impersonation")),
    });

    const auto matches = ma.findSimilar(
        QStringLiteral("forced entry residential night jewels crowbar"), 5, 0.0);
    QVERIFY(!matches.isEmpty());
    QVERIFY2(matches.first().similarityScore < 0.35,
             qPrintable(QStringLiteral("Orthogonal MO pair expected low score, got %1")
                            .arg(matches.first().similarityScore)));
}

void MOAnalyserDeep6Test::testTopKRespectsLimit()
{
    MOAnalyser ma;
    QVector<MOCaseRecord> corpus;
    for (int i = 0; i < 15; ++i) {
        corpus.append(rec(QStringLiteral("C%1").arg(i),
                          QStringLiteral("theft vehicle break window variant%1").arg(i)));
    }
    ma.fit(corpus);

    const int k = 4;
    const auto matches = ma.findSimilar(
        QStringLiteral("theft vehicle break window"), k, 0.0);
    QCOMPARE(matches.size(), k);
}

void MOAnalyserDeep6Test::testTfidfWeightsDifferByTermFrequency()
{
    // Same vocabulary, but "alpha" appears twice in A and once in B.
    MOAnalyser ma;
    ma.fit({
        rec(QStringLiteral("A"), QStringLiteral("alpha alpha beta gamma")),
        rec(QStringLiteral("B"), QStringLiteral("alpha beta gamma delta")),
    });

    const auto queryAlphaHeavy = ma.findSimilar(QStringLiteral("alpha alpha"), 2, 0.0);
    const auto queryBetaHeavy  = ma.findSimilar(QStringLiteral("beta beta"), 2, 0.0);

    QVERIFY2(!queryAlphaHeavy.isEmpty() && !queryBetaHeavy.isEmpty(),
             "Both queries should match corpus");

    QCOMPARE(queryAlphaHeavy.first().caseId, QStringLiteral("A"));
    QVERIFY2(queryAlphaHeavy.first().similarityScore
                 > queryBetaHeavy.first().similarityScore,
             qPrintable(QStringLiteral(
                 "Repeated-term TF should dominate: alpha-query=%1 beta-query=%2")
                            .arg(queryAlphaHeavy.first().similarityScore)
                            .arg(queryBetaHeavy.first().similarityScore)));
}

void MOAnalyserDeep6Test::testAddResolvedCaseThenMatchFindsIt()
{
    // MOAnalyser has no addResolvedCase(); refit corpus after adding a resolved record.
    MOAnalyser ma;
    QVector<MOCaseRecord> corpus;

    MOCaseRecord resolved;
    resolved.caseId   = QStringLiteral("RES-NEW");
    resolved.moText   = QStringLiteral("rear entry glass cut alarm disabled gloves");
    resolved.resolved = true;
    resolved.outcome  = QStringLiteral("convicted");
    corpus.append(resolved);

    ma.fit(corpus);
    QVERIFY(ma.isFitted());
    QCOMPARE(ma.caseCount(), 1);

    const auto matches = ma.findSimilar(resolved.moText, 1, 0.0);
    QVERIFY(!matches.isEmpty());
    QCOMPARE(matches.first().caseId, QStringLiteral("RES-NEW"));
    QVERIFY(matches.first().resolved);
    QCOMPARE(matches.first().outcome, QStringLiteral("convicted"));
}

void MOAnalyserDeep6Test::testMinSimilarityFiltersWeakOrthogonalMatches()
{
    MOAnalyser ma;
    ma.fit({
        rec(QStringLiteral("X"), QStringLiteral("warehouse arson petrol ignition accelerant")),
    });

    const auto loose = ma.findSimilar(
        QStringLiteral("online phishing credit card fraud"), 5, 0.0);
    const auto strict = ma.findSimilar(
        QStringLiteral("online phishing credit card fraud"), 5, 0.5);

    QVERIFY2(!loose.isEmpty(), "Low threshold may return weak match");
    QVERIFY2(strict.isEmpty(),
             "High minSimilarity should filter orthogonal MO matches");
}

QTEST_GUILESS_MAIN(MOAnalyserDeep6Test)
#include "test_mo_analyser_deep6.moc"
