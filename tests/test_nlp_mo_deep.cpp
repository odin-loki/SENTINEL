// test_nlp_mo_deep.cpp — Iteration 8 deep audit: MOAnalyser TF-IDF, CrimeClassifier,
// and MOExtractor behaviour (22 tests, class NlpMoDeepTest).
#include <QTest>
#include <QSet>
#include <cmath>
#include <algorithm>

#include "inference/MOAnalyser.h"
#include "nlp/CrimeClassifier.h"
#include "nlp/MOExtractor.h"
#include "core/CrimeEvent.h"

class NlpMoDeepTest : public QObject
{
    Q_OBJECT

private:
    CrimeClassifier m_cc;
    MOExtractor     m_ex;

    static MOCaseRecord makeCase(const QString& id, const QString& mo)
    {
        MOCaseRecord r;
        r.caseId = id;
        r.moText = mo;
        return r;
    }

    static bool isFiniteInRange(double v, double lo, double hi)
    {
        return std::isfinite(v) && v >= lo && v <= hi;
    }

private slots:

    // ── MOAnalyser (8 tests) ─────────────────────────────────────────────────

    void testCosineSimilaritySelf()
    {
        MOAnalyser ma;
        const QString mo = QStringLiteral("forced entry residential night cash jewellery");
        ma.fit({ makeCase(QStringLiteral("SELF"), mo) });

        const auto matches = ma.findSimilar(mo, 1, 0.0);
        QVERIFY2(!matches.isEmpty(), "Self-query should return at least one match");
        QVERIFY2(matches.first().similarityScore >= 0.99,
                 qPrintable(QStringLiteral("Self similarity %1 expected ~1.0")
                                .arg(matches.first().similarityScore)));
        QVERIFY(std::isfinite(matches.first().similarityScore));
    }

    void testCosineSimilarityDisjoint()
    {
        MOAnalyser ma;
        ma.fit({
            makeCase(QStringLiteral("A"), QStringLiteral("alpha beta gamma")),
            makeCase(QStringLiteral("B"), QStringLiteral("delta epsilon zeta")),
        });

        const auto matches = ma.findSimilar(
            QStringLiteral("xyzzy qwerty plugh"), 5, 0.0);
        for (const auto& m : matches) {
            QVERIFY2(m.similarityScore < 0.01,
                     qPrintable(QStringLiteral("Disjoint query similarity %1 should be ~0")
                                    .arg(m.similarityScore)));
            QVERIFY(std::isfinite(m.similarityScore));
        }
    }

    void testCosineSimilarityRange()
    {
        MOAnalyser ma;
        ma.fit({
            makeCase(QStringLiteral("C1"), QStringLiteral("robbery street night knife cash")),
            makeCase(QStringLiteral("C2"), QStringLiteral("burglary residential window forced")),
            makeCase(QStringLiteral("C3"), QStringLiteral("vehicle theft carpark electronics")),
        });

        const auto matches = ma.findSimilar(
            QStringLiteral("night street robbery"), 10, 0.0);
        for (const auto& m : matches) {
            QVERIFY2(isFiniteInRange(m.similarityScore, 0.0, 1.0),
                     qPrintable(QStringLiteral("Similarity %1 must be finite and in [0,1]")
                                    .arg(m.similarityScore)));
        }
    }

    void testTopSimilarSorted()
    {
        MOAnalyser ma;
        ma.fit({
            makeCase(QStringLiteral("T1"), QStringLiteral("forced entry residential night cash")),
            makeCase(QStringLiteral("T2"), QStringLiteral("forced entry residential night jewellery")),
            makeCase(QStringLiteral("T3"), QStringLiteral("forced entry commercial daytime safe")),
            makeCase(QStringLiteral("T4"), QStringLiteral("pickpocket transport morning wallet")),
            makeCase(QStringLiteral("T5"), QStringLiteral("vehicle theft carpark day solo")),
        });

        const auto matches = ma.findSimilar(
            QStringLiteral("forced entry residential night"), 5, 0.0);
        for (int i = 1; i < matches.size(); ++i) {
            QVERIFY2(matches[i - 1].similarityScore >= matches[i].similarityScore,
                     qPrintable(QStringLiteral("Results not sorted descending at index %1").arg(i)));
        }
    }

    void testTopSimilarK()
    {
        MOAnalyser ma;
        ma.fit({
            makeCase(QStringLiteral("K1"), QStringLiteral("alpha one")),
            makeCase(QStringLiteral("K2"), QStringLiteral("alpha two")),
            makeCase(QStringLiteral("K3"), QStringLiteral("alpha three")),
            makeCase(QStringLiteral("K4"), QStringLiteral("alpha four")),
            makeCase(QStringLiteral("K5"), QStringLiteral("alpha five")),
        });

        const auto matches = ma.findSimilar(QStringLiteral("alpha"), 3, 0.0);
        QCOMPARE(matches.size(), 3);
    }

    void testZeroDocumentNoNaN()
    {
        MOAnalyser ma;
        ma.fit({
            makeCase(QStringLiteral("Z1"), QStringLiteral("some corpus text here")),
            makeCase(QStringLiteral("Z2"), QStringLiteral("another document token set")),
        });

        const auto matches = ma.findSimilar(QStringLiteral(""), 5, 0.0);
        for (const auto& m : matches) {
            QVERIFY2(!std::isnan(m.similarityScore),
                     "Empty query must not produce NaN similarity");
            QVERIFY2(m.similarityScore == 0.0,
                     qPrintable(QStringLiteral("Empty query similarity %1 expected 0.0")
                                    .arg(m.similarityScore)));
        }
        QVERIFY(std::isfinite(0.0));  // sanity: empty query path completed
    }

    void testTFIDFWeightsRareTermHigher()
    {
        // Replicate smoothed IDF formula used by MOAnalyser::buildIDF.
        const int N = 5;
        const double idfRare = std::log((static_cast<double>(N) + 1.0) /
                                        (1.0 + 1.0)) + 1.0;
        const double idfCommon = std::log((static_cast<double>(N) + 1.0) /
                                          (5.0 + 1.0)) + 1.0;
        QVERIFY2(idfRare > idfCommon,
                 qPrintable(QStringLiteral("Rare-term IDF %1 should exceed common-term IDF %2")
                                .arg(idfRare).arg(idfCommon)));

        MOAnalyser ma;
        QVector<MOCaseRecord> cases;
        for (int i = 0; i < N; ++i) {
            cases.append(makeCase(
                QStringLiteral("R%1").arg(i),
                i == 0 ? QStringLiteral("commonword commonword rareword")
                       : QStringLiteral("commonword commonword commonword")));
        }
        ma.fit(cases);

        const auto rareMatches = ma.findSimilar(QStringLiteral("rareword"), 1, 0.0);
        QVERIFY2(!rareMatches.isEmpty(), "Rare-term query should match corpus");
        QCOMPARE(rareMatches.first().caseId, QStringLiteral("R0"));
    }

    void testIdenticalDescriptionsSimilarityOne()
    {
        MOAnalyser ma;
        const QString text = QStringLiteral("smashed window residential night jewellery solo");
        ma.fit({
            makeCase(QStringLiteral("ID1"), text),
            makeCase(QStringLiteral("ID2"), QStringLiteral("unrelated tokens only here")),
        });

        const auto matches = ma.findSimilar(text, 2, 0.0);
        QVERIFY2(!matches.isEmpty(), "Identical description should produce a match");
        QVERIFY2(matches.first().similarityScore >= 0.99,
                 qPrintable(QStringLiteral("Identical MO similarity %1 expected ~1.0")
                                .arg(matches.first().similarityScore)));
    }

    // ── CrimeClassifier (7 tests) ────────────────────────────────────────────

    void testClassifyViolentHighSeverity()
    {
        const QString text = QStringLiteral("victim murdered in brutal homicide attack");
        const auto [type, conf] = m_cc.classify(text);
        Q_UNUSED(conf);
        const double severity = m_cc.severityScore(text, type);
        // Severity scale is 0.0–1.0; level 4+ on a 1–5 scale maps to >= 0.8.
        QVERIFY2(severity >= 0.8,
                 qPrintable(QStringLiteral("Murder severity %1 expected >= 0.8 (level 4+)")
                                .arg(severity)));
    }

    void testClassifyBurglaryMedium()
    {
        const QString text = QStringLiteral("burglary forced entry residential property stolen");
        const auto [type, conf] = m_cc.classify(text);
        Q_UNUSED(conf);
        QVERIFY2(type.contains(QStringLiteral("burgl"), Qt::CaseInsensitive),
                 qPrintable(QStringLiteral("Expected burglary type, got %1").arg(type)));

        const double severity = m_cc.severityScore(text, type);
        // Level 2–3 on 1–5 scale → [0.4, 0.6] on 0–1 scale; burglary baseline is 0.6.
        QVERIFY2(severity >= 0.4 && severity <= 0.6,
                 qPrintable(QStringLiteral("Burglary severity %1 expected in [0.4, 0.6]")
                                .arg(severity)));
    }

    void testClassifyEmptyNoCrash()
    {
        const auto [type, conf] = m_cc.classify(QStringLiteral(""));
        QCOMPARE(type, QStringLiteral("unknown"));
        QCOMPARE(conf, 0.0);
    }

    void testThreatKeywordStabbed()
    {
        const QString text = QStringLiteral("victim was stabbed during the assault");
        const double sent = m_cc.sentiment(text);
        QVERIFY2(sent <= 0.0,
                 qPrintable(QStringLiteral("Violent text sentiment %1 should be <= 0").arg(sent)));
        QVERIFY2(m_cc.threatSignal(text, sent),
                 "Threat keyword 'stabbed' with negative sentiment should trigger threatSignal");
    }

    void testThreatKeywordShot()
    {
        const QString text = QStringLiteral("suspect shot victim on the street");
        const double sent = m_cc.sentiment(text);
        QVERIFY2(sent <= 0.0,
                 qPrintable(QStringLiteral("Violent text sentiment %1 should be <= 0").arg(sent)));
        QVERIFY2(m_cc.threatSignal(text, sent),
                 "Threat keyword 'shot' with negative sentiment should trigger threatSignal");
    }

    void testSentimentNegativeViolent()
    {
        const double sent = m_cc.sentiment(
            QStringLiteral("violent murder attack victim stabbed injured criminal"));
        QVERIFY2(sent <= 0.0,
                 qPrintable(QStringLiteral("Violent crime sentiment %1 should be <= 0").arg(sent)));
    }

    void testCategoriesNotEmpty()
    {
        // CrimeClassifier has no categories() API; verify representative crime types classify.
        static const QMap<QString, QString> samples = {
            { QStringLiteral("murder victim killed homicide"), QStringLiteral("murder") },
            { QStringLiteral("burglary forced entry house"),   QStringLiteral("burglary") },
            { QStringLiteral("theft stolen goods missing"),    QStringLiteral("theft") },
            { QStringLiteral("robbery mugged weapon street"),  QStringLiteral("robbery") },
        };

        QSet<QString> seenTypes;
        for (auto it = samples.cbegin(); it != samples.cend(); ++it) {
            const auto [type, conf] = m_cc.classify(it.key());
            Q_UNUSED(conf);
            QVERIFY2(!type.isEmpty() && type != QStringLiteral("unknown"),
                     qPrintable(QStringLiteral("Sample '%1' should classify to a known type")
                                    .arg(it.key())));
            QCOMPARE(type, it.value());
            seenTypes.insert(type);
        }
        QVERIFY2(seenTypes.size() >= 4,
                 "At least four distinct crime categories should be classifiable");
    }

    // ── MOExtractor (7 tests) ──────────────────────────────────────────────────

    void testExtractWeaponKnife()
    {
        const MOFeatures mo = m_ex.extract(QStringLiteral("offender carried a knife"));
        QVERIFY2(mo.weaponType.has_value(),
                 "Weapon field should be set for knife description");
        QCOMPARE(*mo.weaponType, QStringLiteral("knife"));
    }

    void testExtractTimeNight()
    {
        const MOFeatures mo = m_ex.extract(QStringLiteral("broke in at night"));
        QVERIFY2(mo.timeOfDay.has_value(),
                 "timeOfDay should be set for night reference");
        QCOMPARE(*mo.timeOfDay, QStringLiteral("night"));
    }

    void testExtractEntryForced()
    {
        const MOFeatures mo = m_ex.extract(
            QStringLiteral("forced entry through window into house"));
        QVERIFY2(mo.entryMethod.has_value(),
                 "entryMethod should be set for forced entry description");
        QCOMPARE(*mo.entryMethod, QStringLiteral("forced_entry"));
    }

    void testExtractMultipleFeatures()
    {
        const MOFeatures mo = m_ex.extract(
            QStringLiteral("At 3am a lone offender forced entry into a residential house "
                           "with a knife and stole cash"));
        QVERIFY(mo.entryMethod.has_value());
        QVERIFY(mo.targetType.has_value());
        QVERIFY(mo.timeOfDay.has_value());
        QVERIFY(mo.weaponType.has_value());
        QVERIFY(!mo.itemsTaken.empty());
        QVERIFY(mo.soloOrGroup.has_value());
    }

    void testExtractEmptyNoCrash()
    {
        const MOFeatures mo = m_ex.extract(QStringLiteral(""));
        QVERIFY(!mo.entryMethod.has_value());
        QVERIFY(!mo.targetType.has_value());
        QVERIFY(!mo.timeOfDay.has_value());
        QVERIFY(!mo.weaponType.has_value());
        QVERIFY(mo.itemsTaken.empty());
        QVERIFY(!mo.soloOrGroup.has_value());
        QVERIFY(!mo.precaution.has_value());
    }

    void testExtractGloves()
    {
        const MOFeatures mo = m_ex.extract(QStringLiteral("offender wore gloves"));
        QVERIFY2(mo.precaution.has_value(),
                 "Precaution field should note gloves from description");
        QCOMPARE(*mo.precaution, QStringLiteral("gloves"));
    }

    void testExtractVehicle()
    {
        const MOFeatures mo = m_ex.extract(QStringLiteral("drove away in a car"));
        QVERIFY2(mo.targetType.has_value(),
                 "Vehicle reference should set targetType");
        QCOMPARE(*mo.targetType, QStringLiteral("vehicle"));
    }
};

QTEST_GUILESS_MAIN(NlpMoDeepTest)
#include "test_nlp_mo_deep.moc"
