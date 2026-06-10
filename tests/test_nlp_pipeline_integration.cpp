// test_nlp_pipeline_integration.cpp
// Comprehensive integration tests for the NLP pipeline (CrimeClassifier + MOExtractor).

#include <QTest>
#include <QCoreApplication>
#include <QString>
#include <QElapsedTimer>
#include <cmath>
#include <algorithm>

#include "nlp/CrimeClassifier.h"
#include "nlp/MOExtractor.h"
#include "core/CrimeEvent.h"

class TestNLPPipelineIntegration : public QObject
{
    Q_OBJECT
private slots:

    // ── Crime classification tests ───────────────────────────────────────────

    // "broke"(2.0) + "house"(1.0) = 3.0 for burglary vs stole(2.0) for robbery
    void testBurglaryClassification()
    {
        CrimeClassifier cc;
        const auto [type, conf] =
            cc.classify(QStringLiteral("suspect broke into house and stole laptop"));
        QCOMPARE(type, QStringLiteral("burglary"));
        QVERIFY(conf > 0.0 && conf <= 1.0);
    }

    // "robbed"(3.0) + "mugged"(3.0) + "demanded"(2.0) = 8.0 for robbery → clear winner
    void testRobberyClassification()
    {
        CrimeClassifier cc;
        const auto [type, conf] =
            cc.classify(QStringLiteral("victim was robbed and mugged demanded wallet"));
        QCOMPARE(type, QStringLiteral("robbery"));
        QVERIFY(conf > 0.0 && conf <= 1.0);
    }

    // "dead"(2.0) + "victim"(1.0) = 3.0 for murder → clear winner
    void testMurderClassification()
    {
        CrimeClassifier cc;
        const auto [type, conf] =
            cc.classify(QStringLiteral("victim was found dead after being shot"));
        QCOMPARE(type, QStringLiteral("murder"));
        QVERIFY(conf > 0.0 && conf <= 1.0);
    }

    // "sexual"(3.0) + "indecent"(3.0) + "touching"(2.0) + "assault"(1.0) = 9.0 → clear winner
    void testSexualOffenceClassification()
    {
        CrimeClassifier cc;
        const auto [type, conf] =
            cc.classify(QStringLiteral("victim reported sexual assault indecent touching"));
        QCOMPARE(type, QStringLiteral("sexual_offence"));
        QVERIFY(conf > 0.0 && conf <= 1.0);
    }

    // murder type has base severity 1.0 and multiple boost words present
    void testSeverityScoreHigh()
    {
        CrimeClassifier cc;
        const QString text = QStringLiteral("murder victim dead killed with gun");
        const auto [type, conf] = cc.classify(text);
        QCOMPARE(type, QStringLiteral("murder"));
        const double severity = cc.severityScore(text, type);
        QVERIFY2(severity > 0.8,
                 qPrintable(QStringLiteral("expected severity > 0.8, got %1").arg(severity)));
    }

    // theft type has base severity 0.3, no boost words in "minor theft"
    void testSeverityScoreLow()
    {
        CrimeClassifier cc;
        const QString text = QStringLiteral("minor theft");
        const double severity = cc.severityScore(text, QStringLiteral("theft"));
        QVERIFY2(severity < 0.5,
                 qPrintable(QStringLiteral("expected severity < 0.5, got %1").arg(severity)));
    }

    // ── MOExtractor tests ────────────────────────────────────────────────────

    // "forced" matches forced_entry pattern
    void testMOExtractionEntryMethod()
    {
        MOExtractor ex;
        const MOFeatures mo =
            ex.extract(QStringLiteral("suspect forced entry by smashing window"));
        QVERIFY(mo.entryMethod.has_value());
        QCOMPARE(*mo.entryMethod, QStringLiteral("forced_entry"));
    }

    // "knife" matches knife weapon pattern
    void testMOExtractionWeapon()
    {
        MOExtractor ex;
        const MOFeatures mo =
            ex.extract(QStringLiteral("suspect was armed with a knife"));
        QVERIFY(mo.weaponType.has_value());
        QCOMPARE(*mo.weaponType, QStringLiteral("knife"));
    }

    // "property" in residential target pattern
    void testMOExtractionTarget()
    {
        MOExtractor ex;
        const MOFeatures mo =
            ex.extract(QStringLiteral("residential property on High Street"));
        QVERIFY(mo.targetType.has_value());
        QCOMPARE(*mo.targetType, QStringLiteral("residential"));
    }

    // laptop, phone, cash are all in the items pattern
    void testMOExtractionItems()
    {
        MOExtractor ex;
        const MOFeatures mo =
            ex.extract(QStringLiteral("stole laptop phone and cash"));
        QVERIFY2(!mo.itemsTaken.empty(), "expected non-empty itemsTaken");
        const auto hasLaptop = std::find(mo.itemsTaken.begin(), mo.itemsTaken.end(),
                                         QStringLiteral("laptop")) != mo.itemsTaken.end();
        QVERIFY(hasLaptop);
    }

    // "elderly" matches elderly victim profile pattern
    void testMOExtractionVictimProfile()
    {
        MOExtractor ex;
        const MOFeatures mo =
            ex.extract(QStringLiteral("elderly woman attacked"));
        QVERIFY(mo.victimProfile.has_value());
        QVERIFY2(mo.victimProfile->contains(QStringLiteral("elderly")),
                 qPrintable(QStringLiteral("victim profile '%1' should contain 'elderly'")
                                .arg(*mo.victimProfile)));
    }

    // ── Threat / sentiment tests ─────────────────────────────────────────────

    // "kill" is a negative word AND a threat keyword; sentiment < -0.5 triggers signal
    void testThreatDetection()
    {
        CrimeClassifier cc;
        const QString text = QStringLiteral("I will kill you");
        const double sent = cc.sentiment(text);
        QVERIFY2(sent < -0.5,
                 qPrintable(QStringLiteral("expected sentiment < -0.5 for threat text, got %1")
                                .arg(sent)));
        QVERIFY2(cc.threatSignal(text, sent),
                 "expected threatSignal=true for text containing 'kill' with sentiment < -0.5");
    }

    // Dense negative-word crime narrative → sentiment clearly negative
    void testSentimentNegative()
    {
        CrimeClassifier cc;
        const double sent = cc.sentiment(
            QStringLiteral("violent attack murder stab shoot kill harm damage destroy"));
        QVERIFY2(sent < 0.0,
                 qPrintable(QStringLiteral("expected negative sentiment, got %1").arg(sent)));
    }

    // ── Edge-case / robustness tests ─────────────────────────────────────────

    void testEmptyInput()
    {
        CrimeClassifier cc;
        MOExtractor ex;

        // classify: empty string → "unknown", no crash
        const auto [type, conf] = cc.classify(QStringLiteral(""));
        QVERIFY2(!type.isEmpty(), "classify('') must not return empty type");
        QVERIFY(conf >= 0.0 && conf <= 1.0);

        // severityScore: no crash, in [0,1]
        const double sev = cc.severityScore(QStringLiteral(""), QStringLiteral("unknown"));
        QVERIFY(sev >= 0.0 && sev <= 1.0);

        // sentiment: no negative/positive words → 0.0
        QCOMPARE(cc.sentiment(QStringLiteral("")), 0.0);

        // threatSignal with 0.0 sentiment (>= -0.5) → false
        QVERIFY(!cc.threatSignal(QStringLiteral(""), 0.0));

        // MOExtractor: all optional fields absent, items empty
        const MOFeatures mo = ex.extract(QStringLiteral(""));
        QVERIFY(!mo.entryMethod.has_value());
        QVERIFY(!mo.targetType.has_value());
        QVERIFY(!mo.timeOfDay.has_value());
        QVERIFY(!mo.weaponType.has_value());
        QVERIFY(mo.itemsTaken.empty());
        QVERIFY(!mo.soloOrGroup.has_value());
        QVERIFY(!mo.victimProfile.has_value());
    }

    // 100 KB of crime text must complete classify + sentiment + extract in < 3 seconds.
    // MOExtractor truncates at 4096 chars; CrimeClassifier processes all tokens but
    // the word-set is bounded so the loop stays sub-second even for large inputs.
    void testLongInputNoHang()
    {
        CrimeClassifier cc;
        MOExtractor ex;

        const QString chunk =
            QStringLiteral("suspect broke into house and stole laptop victim was threatened ");
        QString longText;
        longText.reserve(102400);
        while (longText.size() < 102400)
            longText += chunk;

        QElapsedTimer timer;
        timer.start();

        cc.classify(longText);
        cc.sentiment(longText);
        ex.extract(longText);

        const qint64 elapsedMs = timer.elapsed();
        QVERIFY2(elapsedMs < 3000,
                 qPrintable(QStringLiteral("long input took %1 ms, expected < 3000 ms")
                                .arg(elapsedMs)));
    }
};

QTEST_MAIN(TestNLPPipelineIntegration)
#include "test_nlp_pipeline_integration.moc"
