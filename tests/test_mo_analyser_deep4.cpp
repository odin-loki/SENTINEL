// Deep audit iteration 14 — MOAnalyser TF-IDF, IDF smoothing, findSimilar ordering
#include <QtTest/QtTest>
#include <cmath>
#include <algorithm>
#include "inference/MOAnalyser.h"
#include "core/CrimeEvent.h"

class MOAnalyserDeep4Test : public QObject
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

    static double smoothedIdf(int N, int df)
    {
        return std::log((static_cast<double>(N) + 1.0) /
                        (static_cast<double>(df) + 1.0)) + 1.0;
    }

private slots:

    void testSingleDocumentCorpusSelfSimilarityOne()
    {
        const QString mo = QStringLiteral("forced entry residential night cash");
        MOAnalyser ma;
        ma.fit({ rec(QStringLiteral("SOLO"), mo) });

        QVERIFY(ma.isFitted());
        QCOMPARE(ma.caseCount(), 1);

        const auto matches = ma.findSimilar(mo, 5, 0.0);
        QVERIFY2(!matches.isEmpty(), "Single-doc self-query must return a match");
        QVERIFY2(matches.first().similarityScore >= 0.99,
                 qPrintable(QStringLiteral("Self similarity %1 expected ~1.0")
                                .arg(matches.first().similarityScore)));
    }

    void testSingleDocumentCorpusIdfAlwaysPositive()
    {
        const int N = 1;
        const double idf = smoothedIdf(N, 1);
        QVERIFY2(idf > 0.0,
                 qPrintable(QStringLiteral("Smoothed IDF %1 must be > 0 for N=1").arg(idf)));
        QVERIFY2(std::abs(idf - 1.0) < 1e-12,
                 "Single-doc term IDF should be log(2/2)+1 = 1.0");

        MOAnalyser ma;
        ma.fit({ rec(QStringLiteral("ONLY"), QStringLiteral("alpha beta gamma")) });
        const auto matches = ma.findSimilar(QStringLiteral("alpha beta gamma"), 1, 0.0);
        QVERIFY2(!matches.isEmpty(), "Single-doc corpus must still match");
        QVERIFY(matches.first().similarityScore >= 0.99);
    }

    void testIdfSmoothingRareTermOutranksCommon()
    {
        MOAnalyser ma;
        ma.fit({
            rec(QStringLiteral("RARE"),  QStringLiteral("common rareword token")),
            rec(QStringLiteral("DOC2"),  QStringLiteral("common token alpha")),
            rec(QStringLiteral("DOC3"),  QStringLiteral("common token beta")),
        });

        const auto matches = ma.findSimilar(QStringLiteral("rareword common"), 3, 0.0);
        QVERIFY2(!matches.isEmpty(), "Query should match corpus");
        QCOMPARE(matches.first().caseId, QStringLiteral("RARE"));
    }

    void testUniversalTermIdfNotZero()
    {
        const int N = 4;
        const double universalIdf = smoothedIdf(N, N);
        QVERIFY2(universalIdf > 0.0,
                 qPrintable(QStringLiteral("Universal-term IDF %1 must stay positive")
                                .arg(universalIdf)));
        QVERIFY2(universalIdf < smoothedIdf(N, 1),
                 "Universal terms should have lower IDF than rare terms");
    }

    void testFindSimilarSortedDescending()
    {
        MOAnalyser ma;
        ma.fit({
            rec(QStringLiteral("HIGH"), QStringLiteral("window crowbar forced entry residential")),
            rec(QStringLiteral("MED"),  QStringLiteral("window entry residential")),
            rec(QStringLiteral("LOW"),  QStringLiteral("vehicle theft carpark electronics")),
        });

        const auto matches = ma.findSimilar(
            QStringLiteral("window crowbar forced entry residential night"), 10, 0.0);
        for (int i = 1; i < matches.size(); ++i) {
            QVERIFY2(matches[i - 1].similarityScore >= matches[i].similarityScore,
                     qPrintable(QStringLiteral("Not sorted: [%1]=%2 vs [%3]=%4")
                                    .arg(i - 1).arg(matches[i - 1].similarityScore)
                                    .arg(i).arg(matches[i].similarityScore)));
        }
    }

    void testResolvedBoostCanReorderResults()
    {
        const QString baseMo = QStringLiteral("rear window crowbar entry");
        MOAnalyser ma;
        ma.fit({
            rec(QStringLiteral("UNRES"), baseMo, false),
            rec(QStringLiteral("RES"),   baseMo, true),
        });

        const auto matches = ma.findSimilar(baseMo, 5, 0.0);
        QVERIFY2(matches.size() >= 2, "Both cases should match identical MO");
        QCOMPARE(matches.first().caseId, QStringLiteral("RES"));
        QVERIFY2(matches.first().similarityScore >= matches[1].similarityScore,
                 "Resolved boost should rank RES first");
    }

    void testMinSimilarityAppliedToRawCosine()
    {
        MOAnalyser ma;
        ma.fit({
            rec(QStringLiteral("CLOSE"), QStringLiteral("alpha beta gamma delta")),
            rec(QStringLiteral("FAR"),   QStringLiteral("completely unrelated tokens here")),
        });

        const auto strict = ma.findSimilar(QStringLiteral("alpha beta"), 5, 0.85);
        for (const auto& m : strict) {
            QVERIFY2(m.similarityScore >= 0.85,
                     qPrintable(QStringLiteral("Returned score %1 below minSimilarity 0.85")
                                    .arg(m.similarityScore)));
        }
    }

    void testTopKZeroReturnsEmpty()
    {
        MOAnalyser ma;
        ma.fit({
            rec(QStringLiteral("A"), QStringLiteral("alpha beta")),
            rec(QStringLiteral("B"), QStringLiteral("alpha gamma")),
        });
        QVERIFY(ma.findSimilar(QStringLiteral("alpha"), 0, 0.0).isEmpty());
    }

    void testTopKNegativeReturnsEmpty()
    {
        MOAnalyser ma;
        ma.fit({ rec(QStringLiteral("A"), QStringLiteral("alpha beta gamma")) });
        QVERIFY(ma.findSimilar(QStringLiteral("alpha"), -1, 0.0).isEmpty());
    }

    void testEmptyQueryZeroSimilarity()
    {
        MOAnalyser ma;
        ma.fit({ rec(QStringLiteral("A"), QStringLiteral("token one two three")) });
        const auto matches = ma.findSimilar(QStringLiteral(""), 5, 0.0);
        for (const auto& m : matches) {
            QVERIFY2(!std::isnan(m.similarityScore), "Empty query must not produce NaN");
            QCOMPARE(m.similarityScore, 0.0);
        }
    }

    void testSharedFeaturesOnlyOverlap()
    {
        MOAnalyser ma;
        ma.fit({ rec(QStringLiteral("C1"), QStringLiteral("alpha beta gamma")) });

        const auto matches = ma.findSimilar(QStringLiteral("alpha delta"), 1, 0.0);
        QVERIFY(!matches.isEmpty());
        const QStringList& shared = matches.first().sharedFeatures;
        QVERIFY(shared.contains(QStringLiteral("alpha")));
        QVERIFY(!shared.contains(QStringLiteral("delta")));
        QVERIFY(!shared.contains(QStringLiteral("beta")));
    }

    void testRefitReplacesCorpus()
    {
        MOAnalyser ma;
        ma.fit({ rec(QStringLiteral("OLD"), QStringLiteral("alpha beta gamma")) });
        ma.fit({ rec(QStringLiteral("NEW"), QStringLiteral("delta epsilon zeta")) });

        QCOMPARE(ma.caseCount(), 1);
        const auto matches = ma.findSimilar(QStringLiteral("alpha beta"), 5, 0.01);
        QVERIFY(matches.isEmpty());
    }
};

QTEST_GUILESS_MAIN(MOAnalyserDeep4Test)
#include "test_mo_analyser_deep4.moc"
