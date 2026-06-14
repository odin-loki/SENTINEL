// test_mo_analyser_deep8.cpp — Deep audit iteration 30: MOAnalyser
// resolved outcome propagation, score ordering, unrelated query, vocab growth.
#include <QtTest/QtTest>
#include "inference/MOAnalyser.h"

class MOAnalyserDeep8Test : public QObject
{
    Q_OBJECT

    static MOCaseRecord rec(const QString& id, const QString& mo,
                            bool resolved = false, const QString& outcome = {})
    {
        MOCaseRecord r;
        r.caseId   = id;
        r.moText   = mo;
        r.resolved = resolved;
        r.outcome  = outcome;
        return r;
    }

private slots:

    void testResolvedOutcomePropagatesToMatch()
    {
        MOAnalyser ma;
        ma.fit({ rec(QStringLiteral("RES-8"), QStringLiteral("forced entry rear door"),
                     true, QStringLiteral("convicted")) });

        const auto matches = ma.findSimilar(
            QStringLiteral("forced entry rear"), 1, 0.0);
        QVERIFY(!matches.isEmpty());
        QVERIFY(matches.first().resolved);
        QCOMPARE(matches.first().outcome, QStringLiteral("convicted"));
    }

    void testResultsSortedByDescendingScore()
    {
        MOAnalyser ma;
        ma.fit({
            rec(QStringLiteral("A"), QStringLiteral("window smash forced entry")),
            rec(QStringLiteral("B"), QStringLiteral("window smash forced entry tools")),
            rec(QStringLiteral("C"), QStringLiteral("unrelated fraud paperwork")),
        });

        const auto matches = ma.findSimilar(
            QStringLiteral("window smash forced entry"), 3, 0.0);
        QVERIFY(matches.size() >= 2);
        QVERIFY(matches.first().similarityScore
                >= matches.last().similarityScore);
    }

    void testUnrelatedQueryYieldsLowScores()
    {
        MOAnalyser ma;
        ma.fit({ rec(QStringLiteral("BUR"), QStringLiteral("night burglary alarm")) });

        const auto matches = ma.findSimilar(
            QStringLiteral("daytime shoplifting"), 5, 0.5);
        QVERIFY(matches.isEmpty());
    }

    void testCaseCountTracksCorpus()
    {
        MOAnalyser ma;
        ma.fit({
            rec(QStringLiteral("1"), QStringLiteral("a")),
            rec(QStringLiteral("2"), QStringLiteral("b")),
            rec(QStringLiteral("3"), QStringLiteral("c")),
        });
        QCOMPARE(ma.caseCount(), 3);
    }

    void testSharedFeaturesPopulatedForOverlap()
    {
        MOAnalyser ma;
        ma.fit({
            rec(QStringLiteral("F1"), QStringLiteral("rear door forced crowbar")),
            rec(QStringLiteral("F2"), QStringLiteral("rear window forced")),
        });

        const auto matches = ma.findSimilar(
            QStringLiteral("rear forced entry"), 1, 0.0);
        QVERIFY(!matches.isEmpty());
        QVERIFY(!matches.first().sharedFeatures.isEmpty());
    }
};

QTEST_GUILESS_MAIN(MOAnalyserDeep8Test)
#include "test_mo_analyser_deep8.moc"
