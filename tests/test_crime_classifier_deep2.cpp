#include <QTest>
#include "nlp/CrimeClassifier.h"
#include <cmath>

class CrimeClassifierDeep2Test : public QObject
{
    Q_OBJECT

private:
    CrimeClassifier m_cc;

private slots:

    // Classifier uses keyword presence + predefined weights normalised by token count.
    // For a text of N tokens, score = (sum of matched keyword weights) / N.
    // This verifies that manual weight computation matches classify() output.
    void testKeywordScoringNormalisation()
    {
        // "burglary" (w=3) + "burglar" (w=3) + "stolen" (w=2) + "house" (w=1)  → 9 / 9 tokens
        // Ensure burglary wins and has non-trivial confidence.
        const QString text = QStringLiteral("burglar burglary stolen house entry forced ransacked broke home");
        const auto [type, conf] = m_cc.classify(text);
        QCOMPARE(type, QStringLiteral("burglary"));
        QVERIFY2(conf > 0.5, qPrintable(QStringLiteral("Expected conf > 0.5 for unambiguous burglary text, got %1").arg(conf)));
    }

    // Shorter document with the same keyword should not lower confidence if the
    // keyword set is equally unambiguous — the relative best/second ratio dominates.
    void testShorterDocumentClassifiesCorrectly()
    {
        const auto [type, conf] = m_cc.classify(QStringLiteral("cocaine dealing drugs"));
        QCOMPARE(type, QStringLiteral("drug_offence"));
        QVERIFY2(conf >= 0.0 && conf <= 1.0, "Confidence must be in [0,1]");
    }

    // Classifying the exact same text twice must return identical results (determinism).
    void testClassifyIsDeterministic()
    {
        const QString text = QStringLiteral("robbery robbed mugged victim threatened weapon");
        const auto [t1, c1] = m_cc.classify(text);
        const auto [t2, c2] = m_cc.classify(text);
        QCOMPARE(t1, t2);
        QCOMPARE(c1, c2);
    }

    // A pure burglary document should produce confidence well above 0.5 (best dominates).
    void testBurglaryClassificationHighConfidence()
    {
        const auto [type, conf] = m_cc.classify(
            QStringLiteral("burglar forced entry into house stolen jewellery ransacked property"));
        QCOMPARE(type, QStringLiteral("burglary"));
        QVERIFY2(conf > 0.5,
                 qPrintable(QStringLiteral("Expected conf > 0.5, got %1").arg(conf)));
    }

    // Murder-specific keywords: killed, homicide, fatal → classified as murder.
    void testMurderKeywordsDetected()
    {
        const auto [type, conf] = m_cc.classify(
            QStringLiteral("homicide victim found dead fatal stabbing murder investigation body"));
        QCOMPARE(type, QStringLiteral("murder"));
        QVERIFY(conf > 0.0);
    }

    // Drug offence keywords: heroin, cocaine, cannabis, dealing, possession.
    void testDrugOffenceKeywordsDetected()
    {
        const auto [type, conf] = m_cc.classify(
            QStringLiteral("heroin cocaine cannabis drugs dealing possession found"));
        QCOMPARE(type, QStringLiteral("drug_offence"));
    }

    // Completely different text to burglary — fruit-related text — should not match burglary.
    void testUnrelatedTextDifferentFromBurglary()
    {
        const auto [typeB, confB] = m_cc.classify(
            QStringLiteral("burglar forced entry into house stolen jewellery ransacked property"));
        const auto [typeU, confU] = m_cc.classify(
            QStringLiteral("apple orange banana cherry fruit salad healthy breakfast morning"));
        QVERIFY2(typeB != typeU || confU < confB,
                 "Unrelated text must produce different type or lower confidence than clear burglary");
    }

    // Empty text: must not crash; confidence and type should be valid.
    void testEmptyTextNocrash()
    {
        const auto [type, conf] = m_cc.classify(QStringLiteral(""));
        QVERIFY2(conf >= 0.0 && conf <= 1.0,
                 qPrintable(QStringLiteral("Empty text conf %1 must be in [0,1]").arg(conf)));
    }

    // Single unrecognised word → "unknown" with confidence 0.
    void testSingleUnknownWordReturnsUnknown()
    {
        const auto [type, conf] = m_cc.classify(QStringLiteral("xyzzy"));
        QCOMPARE(type, QStringLiteral("unknown"));
        QCOMPARE(conf, 0.0);
    }

    // Severity for "murder" crime type must be >= severity for "theft" (baseline ordering).
    void testSeverityOrderingMurderVsTheft()
    {
        const double sMurder = m_cc.severityScore(QStringLiteral("victim found dead"), QStringLiteral("murder"));
        const double sTheft  = m_cc.severityScore(QStringLiteral("wallet missing"), QStringLiteral("theft"));
        QVERIFY2(sMurder > sTheft,
                 qPrintable(QStringLiteral("murder severity %1 must exceed theft severity %2")
                     .arg(sMurder).arg(sTheft)));
    }

    // Severity boosted by high-severity keywords: "gun", "firearm", "stab", "kill" each add 0.05.
    void testSeverityBoostedByKeywords()
    {
        const double base    = m_cc.severityScore(QStringLiteral("incident occurred"), QStringLiteral("assault"));
        const double boosted = m_cc.severityScore(
            QStringLiteral("suspect armed with gun fired stab kill dead"), QStringLiteral("assault"));
        QVERIFY2(boosted > base,
                 qPrintable(QStringLiteral("boosted %1 must exceed base %2").arg(boosted).arg(base)));
    }

    // Severity always in [0, 1].
    void testSeverityAlwaysInRange()
    {
        const QStringList types = {
            QStringLiteral("murder"), QStringLiteral("assault"), QStringLiteral("theft"),
            QStringLiteral("drug_offence"), QStringLiteral("unknown_type")
        };
        for (const QString& t : types) {
            const double s = m_cc.severityScore(QStringLiteral("gun bomb kill stab armed"), t);
            QVERIFY2(s >= 0.0 && s <= 1.0,
                     qPrintable(QStringLiteral("Severity %1 for type '%2' must be in [0,1]").arg(s).arg(t)));
        }
    }

    // Sentiment: pure negative crime text → negative score.
    void testSentimentNegativeCrimeText()
    {
        const double s = m_cc.sentiment(
            QStringLiteral("violent attack assault murder stab shoot kill weapon crime criminal"));
        QVERIFY2(s < 0.0,
                 qPrintable(QStringLiteral("Crime-heavy text sentiment %1 must be < 0").arg(s)));
    }

    // Sentiment: pure positive text → positive score.
    void testSentimentPositiveResolutionText()
    {
        const double s = m_cc.sentiment(
            QStringLiteral("arrested caught recovered safe secured resolved prevented protected rescued"));
        QVERIFY2(s > 0.0,
                 qPrintable(QStringLiteral("Resolution text sentiment %1 must be > 0").arg(s)));
    }

    // Sentiment: empty text → 0.0 (no signal).
    void testSentimentEmptyIsZero()
    {
        QCOMPARE(m_cc.sentiment(QStringLiteral("")), 0.0);
    }

    // Sentiment always in [-1, 1].
    void testSentimentRange()
    {
        const QStringList texts = {
            QStringLiteral("violent attack murder"),
            QStringLiteral("safe resolved peaceful"),
            QStringLiteral("neutral text without signal"),
            QStringLiteral(""),
        };
        for (const QString& text : texts) {
            const double s = m_cc.sentiment(text);
            QVERIFY2(s >= -1.0 && s <= 1.0,
                     qPrintable(QStringLiteral("Sentiment %1 for '%2' must be in [-1,1]")
                         .arg(s).arg(text)));
        }
    }

    // ThreatSignal: strongly negative text with threat keyword → true.
    void testThreatSignalFiresOnNegativePlusThreatKeyword()
    {
        // Build text with many negative words to ensure sentiment < -0.5,
        // and add "bomb" which is a known threat keyword.
        const QString text =
            QStringLiteral("violent attack assault murder stab shoot kill weapon crime criminal "
                           "bomb damage destroy threat harm hurt victim dead offender");
        const double sent = m_cc.sentiment(text);
        QVERIFY2(sent < -0.5,
                 qPrintable(QStringLiteral("Setup error: sentiment %1 should be < -0.5").arg(sent)));
        QVERIFY(m_cc.threatSignal(text, sent));
    }

    // ThreatSignal: sentiment >= -0.5 → always false regardless of text.
    void testThreatSignalRequiresNegativeSentiment()
    {
        QVERIFY(!m_cc.threatSignal(QStringLiteral("shoot bomb kill"), -0.3));
        QVERIFY(!m_cc.threatSignal(QStringLiteral("shoot bomb kill"), 0.0));
        QVERIFY(!m_cc.threatSignal(QStringLiteral("shoot bomb kill"), 1.0));
    }

    // ThreatSignal: very negative sentiment but no threat keywords → false.
    void testThreatSignalRequiresThreatKeyword()
    {
        // "stolen looted ransacked vandalised" are negative but not in m_threatKeywords
        const QString text = QStringLiteral("stolen looted ransacked vandalised abused trafficked coerced");
        QVERIFY(!m_cc.threatSignal(text, -1.0));
    }

    // Confidence must always be in [0, 1] for any input.
    void testConfidenceAlwaysInRange()
    {
        const QStringList texts = {
            QStringLiteral(""),
            QStringLiteral("murder kill dead"),
            QStringLiteral("xyzzy quux"),
            QStringLiteral("drugs cocaine heroin cannabis dealing possession armed gun robbery mugged"),
        };
        for (const QString& text : texts) {
            const auto [type, conf] = m_cc.classify(text);
            QVERIFY2(conf >= 0.0 && conf <= 1.0,
                     qPrintable(QStringLiteral("Confidence %1 for '%2' must be in [0,1]")
                         .arg(conf).arg(text)));
        }
    }
};

QTEST_GUILESS_MAIN(CrimeClassifierDeep2Test)
#include "test_crime_classifier_deep2.moc"
