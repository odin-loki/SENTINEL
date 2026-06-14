// test_mo_analyser_deep7.cpp — Deep audit iteration 26: MOAnalyser
// topK limit, minSimilarity filter, identical MO match, caseCount.
#include <QtTest/QtTest>
#include "inference/MOAnalyser.h"
#include "core/CrimeEvent.h"

class MOAnalyserDeep7Test : public QObject
{
    Q_OBJECT

    static MOCaseRecord rec(const QString& id, const QString& mo)
    {
        MOCaseRecord r;
        r.caseId = id;
        r.moText = mo;
        return r;
    }

private slots:

    void testTopKLimitsResults()
    {
        MOAnalyser ma;
        ma.fit({
            rec(QStringLiteral("A"), QStringLiteral("forced entry window smash")),
            rec(QStringLiteral("B"), QStringLiteral("forced entry door kick")),
            rec(QStringLiteral("C"), QStringLiteral("forced entry rear door")),
            rec(QStringLiteral("D"), QStringLiteral("shoplifting concealment")),
            rec(QStringLiteral("E"), QStringLiteral("vehicle break in")),
        });

        const auto matches = ma.findSimilar(
            QStringLiteral("forced entry window"), 2, 0.0);
        QCOMPARE(matches.size(), 2);
    }

    void testMinSimilarityFiltersWeakMatches()
    {
        MOAnalyser ma;
        ma.fit({
            rec(QStringLiteral("X"), QStringLiteral("night burglary alarm disabled")),
            rec(QStringLiteral("Y"), QStringLiteral("daytime shop theft")),
        });

        const auto strict = ma.findSimilar(
            QStringLiteral("night burglary"), 5, 0.9);
        const auto loose = ma.findSimilar(
            QStringLiteral("night burglary"), 5, 0.0);
        QVERIFY(loose.size() >= strict.size());
    }

    void testIdenticalMOScoresHighly()
    {
        const QString mo = QStringLiteral("rear access forced entry tools");
        MOAnalyser ma;
        ma.fit({
            rec(QStringLiteral("M1"), mo),
            rec(QStringLiteral("M2"), QStringLiteral("unrelated fraud")),
        });

        const auto matches = ma.findSimilar(mo, 1, 0.0);
        QVERIFY(!matches.isEmpty());
        QCOMPARE(matches.first().caseId, QStringLiteral("M1"));
        QVERIFY(matches.first().similarityScore > 0.5);
    }

    void testCaseCountAfterFit()
    {
        MOAnalyser ma;
        QVERIFY(!ma.isFitted());
        ma.fit({ rec(QStringLiteral("1"), QStringLiteral("a")),
                 rec(QStringLiteral("2"), QStringLiteral("b")) });
        QVERIFY(ma.isFitted());
        QCOMPARE(ma.caseCount(), 2);
    }

    void testEmptyQueryStillReturnsMatchesAtZeroThreshold()
    {
        MOAnalyser ma;
        ma.fit({ rec(QStringLiteral("Z"), QStringLiteral("text")) });
        const auto matches = ma.findSimilar(QString{}, 5, 0.0);
        QVERIFY(!matches.isEmpty());
    }
};

QTEST_GUILESS_MAIN(MOAnalyserDeep7Test)
#include "test_mo_analyser_deep7.moc"
