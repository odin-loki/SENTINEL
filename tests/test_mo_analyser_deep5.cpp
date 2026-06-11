// test_mo_analyser_deep5.cpp — Deep audit iteration 18: MOAnalyser
// Probes: punctuation tokenisation, minSimilarity vs boost, OOV terms, tie-breaks.
#include <QtTest/QtTest>
#include <cmath>
#include <algorithm>
#include "inference/MOAnalyser.h"
#include "core/CrimeEvent.h"

class MOAnalyserDeep5Test : public QObject
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

    void testPunctuationAttachedTokensReduceSimilarity()
    {
        // BUG: MOAnalyser.cpp:13-14 — whitespace split leaves trailing punctuation on tokens.
        const QString corpus = QStringLiteral("forced entry residential window crowbar");
        MOAnalyser ma;
        ma.fit({ rec(QStringLiteral("C1"), corpus) });

        const auto clean = ma.findSimilar(corpus, 1, 0.0);
        const auto punct = ma.findSimilar(
            QStringLiteral("forced, entry residential window crowbar"), 1, 0.0);

        QVERIFY2(!clean.isEmpty(), "Clean query must match");
        QVERIFY2(!punct.isEmpty(), "Punctuated query still returns a result");
        QVERIFY2(clean.first().similarityScore > punct.first().similarityScore,
                 qPrintable(QStringLiteral(
                     "Punctuation lowers similarity: clean=%1 punct=%2")
                     .arg(clean.first().similarityScore)
                     .arg(punct.first().similarityScore)));
        QVERIFY(!punct.first().sharedFeatures.contains(QStringLiteral("forced")));
    }

    void testMatchMetadataPropagatedFromCaseRecord()
    {
        MOCaseRecord r;
        r.caseId         = QStringLiteral("META");
        r.moText         = QStringLiteral("alpha beta gamma");
        r.resolved       = true;
        r.outcome        = QStringLiteral("convicted");
        r.suspectProfile = QStringLiteral("male 30s");
        r.convictionYear = 2019;

        MOAnalyser ma;
        ma.fit({ r });

        const auto matches = ma.findSimilar(r.moText, 1, 0.0);
        QVERIFY(!matches.isEmpty());
        QCOMPARE(matches.first().outcome, QStringLiteral("convicted"));
        QCOMPARE(matches.first().suspectProfile, QStringLiteral("male 30s"));
        QCOMPARE(matches.first().convictionYear, 2019);
        QVERIFY(matches.first().resolved);
    }

    void testResolvedBoostCappedAtOne()
    {
        MOAnalyser ma;
        const QString mo = QStringLiteral("identical tokens only here");
        ma.fit({ rec(QStringLiteral("R"), mo, true) });

        const auto matches = ma.findSimilar(mo, 1, 0.0);
        QVERIFY(!matches.isEmpty());
        QVERIFY2(matches.first().similarityScore <= 1.0,
                 qPrintable(QStringLiteral("Boost must cap at 1.0, got %1")
                                .arg(matches.first().similarityScore)));
    }

    void testIdenticalScoreTiePrefersResolved()
    {
        const QString mo = QStringLiteral("rear window crowbar entry night");
        MOAnalyser ma;
        ma.fit({
            rec(QStringLiteral("UNRES"), mo, false),
            rec(QStringLiteral("RES"),   mo, true),
        });

        const auto matches = ma.findSimilar(mo, 2, 0.0);
        QCOMPARE(matches.size(), 2);
        QCOMPARE(matches.first().caseId, QStringLiteral("RES"));
    }

    void testOutOfVocabularyQueryTermsIgnored()
    {
        MOAnalyser ma;
        ma.fit({
            rec(QStringLiteral("A"), QStringLiteral("alpha beta gamma")),
            rec(QStringLiteral("B"), QStringLiteral("alpha beta delta")),
        });

        const auto withOov = ma.findSimilar(
            QStringLiteral("alpha beta zeta omega phi"), 2, 0.0);
        const auto withoutOov = ma.findSimilar(
            QStringLiteral("alpha beta"), 2, 0.0);

        QVERIFY2(!withOov.isEmpty() && !withoutOov.isEmpty(),
                 "Partial overlap should still match");
        QVERIFY2(withOov.first().similarityScore <= withoutOov.first().similarityScore,
                 "OOV tokens dilute TF and should not increase similarity");
    }

    void testSharedFeaturesPreservesQueryOrder()
    {
        MOAnalyser ma;
        ma.fit({ rec(QStringLiteral("C1"), QStringLiteral("gamma beta alpha")) });

        const auto matches = ma.findSimilar(
            QStringLiteral("alpha beta gamma"), 1, 0.0);
        QVERIFY(!matches.isEmpty());
        const QStringList& shared = matches.first().sharedFeatures;
        QCOMPARE(shared.size(), 3);
        QCOMPARE(shared.at(0), QStringLiteral("alpha"));
        QCOMPARE(shared.at(1), QStringLiteral("beta"));
        QCOMPARE(shared.at(2), QStringLiteral("gamma"));
    }

    void testMultipleWhitespaceDelimitersTokenised()
    {
        MOAnalyser ma;
        ma.fit({ rec(QStringLiteral("C1"), QStringLiteral("one two three")) });

        const auto matches = ma.findSimilar(
            QStringLiteral("one\t\ntwo   three"), 1, 0.0);
        QVERIFY(!matches.isEmpty());
        QVERIFY2(matches.first().similarityScore >= 0.99,
                 "Tabs/newlines should tokenise like spaces");
    }

    void testFitEmptyCorpusNotFitted()
    {
        MOAnalyser ma;
        ma.fit({});
        QVERIFY(!ma.isFitted());
        QVERIFY(ma.findSimilar(QStringLiteral("anything"), 5, 0.0).isEmpty());
    }
};

QTEST_GUILESS_MAIN(MOAnalyserDeep5Test)
#include "test_mo_analyser_deep5.moc"
