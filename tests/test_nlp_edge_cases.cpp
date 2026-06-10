// test_nlp_edge_cases.cpp — NLP edge-case unit tests for SENTINEL
// Tests MOExtractor pattern matching and CrimeClassifier scoring under unusual inputs.

#include <QTest>
#include <QCoreApplication>
#include <QString>
#include <QStringList>
#include <QMap>

#include "nlp/MOExtractor.h"
#include "nlp/CrimeClassifier.h"
#include "core/CrimeEvent.h"

class TestNLPEdgeCases : public QObject
{
    Q_OBJECT

private:
    MOExtractor     m_ex;
    CrimeClassifier m_clf;

private slots:

    // ── MOExtractor edge cases ──────────────────────────────────────────────

    void testMOExtractEmptyString()
    {
        const MOFeatures mo = m_ex.extract(QStringLiteral(""));
        QVERIFY(!mo.entryMethod.has_value());
        QVERIFY(!mo.timeOfDay.has_value());
        QVERIFY(!mo.targetType.has_value());
        QVERIFY(!mo.weaponType.has_value());
        QVERIFY(!mo.soloOrGroup.has_value());
        QVERIFY(mo.itemsTaken.empty());
    }

    void testMOExtractOnlySpaces()
    {
        const MOFeatures mo = m_ex.extract(QStringLiteral("     \t\n  "));
        QVERIFY(!mo.entryMethod.has_value());
        QVERIFY(!mo.timeOfDay.has_value());
        QVERIFY(!mo.targetType.has_value());
        QVERIFY(!mo.weaponType.has_value());
        QVERIFY(!mo.soloOrGroup.has_value());
        QVERIFY(mo.itemsTaken.empty());
    }

    void testMOExtractMixedCaseKeywords()
    {
        // Entry patterns use std::regex::icase — "FORCED" must still match
        const MOFeatures mo = m_ex.extract(QStringLiteral("FORCED ENTRY REAR WINDOW"));
        QVERIFY(mo.entryMethod.has_value());
        QCOMPARE(*mo.entryMethod, QStringLiteral("forced_entry"));
    }

    void testMOExtractMultipleItems()
    {
        const MOFeatures mo = m_ex.extract(
            QStringLiteral("suspect stole cash, jewellery and electronics from the property"));
        const QStringList items(mo.itemsTaken.begin(), mo.itemsTaken.end());
        QVERIFY(items.contains(QStringLiteral("cash")));
        QVERIFY(items.contains(QStringLiteral("jewellery")));
        QVERIFY(items.contains(QStringLiteral("electronics")));
        QVERIFY(mo.itemsTaken.size() >= 3u);
    }

    void testMOExtractSoloKeyword()
    {
        const MOFeatures mo = m_ex.extract(
            QStringLiteral("the offender acted alone at the property"));
        QVERIFY(mo.soloOrGroup.has_value());
        QCOMPARE(*mo.soloOrGroup, QStringLiteral("solo"));
    }

    void testMOExtractGroupKeyword()
    {
        const MOFeatures mo = m_ex.extract(
            QStringLiteral("two suspects entered through the back door"));
        QVERIFY(mo.soloOrGroup.has_value());
        QCOMPARE(*mo.soloOrGroup, QStringLiteral("group"));
    }

    void testMOExtractNightTimeKeyword()
    {
        // "midnight" maps to the "night" time bin
        const MOFeatures mo = m_ex.extract(
            QStringLiteral("the incident occurred at midnight"));
        QVERIFY(mo.timeOfDay.has_value());
        QCOMPARE(*mo.timeOfDay, QStringLiteral("night"));
    }

    void testMOExtractWeaponKeywords()
    {
        // knife keyword → "knife" weapon type
        {
            const MOFeatures mo = m_ex.extract(
                QStringLiteral("the offender brandished a knife"));
            QVERIFY(mo.weaponType.has_value());
            QCOMPARE(*mo.weaponType, QStringLiteral("knife"));
        }
        // firearm keyword → "firearm" type
        {
            const MOFeatures mo = m_ex.extract(
                QStringLiteral("suspect was armed with a firearm"));
            QVERIFY(mo.weaponType.has_value());
            QCOMPARE(*mo.weaponType, QStringLiteral("firearm"));
        }
        // gun also maps to "firearm" (firearm pattern is first in list)
        {
            const MOFeatures mo = m_ex.extract(
                QStringLiteral("offender pulled out a gun"));
            QVERIFY(mo.weaponType.has_value());
            QCOMPARE(*mo.weaponType, QStringLiteral("firearm"));
        }
    }

    void testMOExtractCanonicalString()
    {
        // A narrative with at least one extractable feature must produce
        // a non-empty canonical MO string.
        const MOFeatures mo = m_ex.extract(
            QStringLiteral("the offender forced the door alone at night and stole cash"));
        const QString canonical = m_ex.canonicalMOString(mo);
        QVERIFY(!canonical.isEmpty());
    }

    // ── CrimeClassifier ────────────────────────────────────────────────────

    void testCrimeClassifyEmptyString()
    {
        const auto [type, conf] = m_clf.classify(QStringLiteral(""));
        QVERIFY(!type.isEmpty());   // must return a defined type, not crash
        QVERIFY(conf >= 0.0);
    }

    void testCrimeClassifyUnknownText()
    {
        // Completely unrelated text → zero keyword matches → low/zero confidence
        const auto [type, conf] = m_clf.classify(
            QStringLiteral("xylophone catapult nebula quasar trombone glacier"));
        QVERIFY(conf <= 0.5);
    }

    void testCrimeClassifyBurglaryKeywords()
    {
        // Strong burglary keywords dominate; vehicle_crime allowed due to "window"
        const auto [type, conf] = m_clf.classify(
            QStringLiteral("broken window forced entry residential dwelling"));
        QVERIFY(type == QStringLiteral("burglary") ||
                type == QStringLiteral("vehicle_crime"));
        QVERIFY(conf >= 0.5);
    }

    void testCrimeClassifyRobberyKeywords()
    {
        const auto [type, conf] = m_clf.classify(
            QStringLiteral("victim robbed threatened demanded weapon mugged"));
        QVERIFY(type == QStringLiteral("robbery") ||
                type == QStringLiteral("assault"));
        QVERIFY(conf >= 0.5);
    }

    void testSentimentNegativeText()
    {
        const double sent = m_clf.sentiment(
            QStringLiteral("violent attack victim murder dead beaten"));
        QVERIFY(sent <= -0.3);
    }

    void testSentimentNeutralText()
    {
        // No sentiment-signal words → returns 0
        const double sent = m_clf.sentiment(
            QStringLiteral("report submitted to police station today"));
        QVERIFY(qAbs(sent) <= 0.1);
    }

    void testThreatSignalWithExplicitThreat()
    {
        const QString text = QStringLiteral(
            "violent murder kill attack threat weapon stab crime criminal offender");
        const double sentScore = m_clf.sentiment(text);
        QVERIFY(sentScore < -0.5);                        // gate: must be sufficiently negative
        QVERIFY(m_clf.threatSignal(text, sentScore));     // threat keyword present
    }

    void testSeverityHighForViolentCrime()
    {
        // assault baseline = 0.8, well above 0.6
        const double sev = m_clf.severityScore(
            QStringLiteral("violent assault with a knife victim injured"),
            QStringLiteral("assault"));
        QVERIFY(sev >= 0.6);
    }

    void testSeverityLowForMinorCrime()
    {
        // criminal_damage baseline = 0.4, below 0.5
        const double sev = m_clf.severityScore(
            QStringLiteral("minor vandalism scratched the fence"),
            QStringLiteral("criminal_damage"));
        QVERIFY(sev <= 0.5);
    }

    void testClassifyAllCrimeTypes()
    {
        // One clear text per crime type — top predicted type must match expected
        const QMap<QString, QString> texts = {
            { QStringLiteral("assault"),
              QStringLiteral("assault attack punch beat fight victim") },
            { QStringLiteral("robbery"),
              QStringLiteral("robbery robbed mugged demanded weapon") },
            { QStringLiteral("burglary"),
              QStringLiteral("burglar burglary ransacked forced entry stolen") },
            { QStringLiteral("theft"),
              QStringLiteral("theft stolen pickpocket shoplifting") },
            { QStringLiteral("vehicle_crime"),
              QStringLiteral("stolen vehicle car break-in window vandalised") },
            { QStringLiteral("drug_offence"),
              QStringLiteral("drugs heroin cocaine cannabis dealing possession") },
            { QStringLiteral("criminal_damage"),
              QStringLiteral("vandalism damage graffiti smashed destroyed") },
            { QStringLiteral("public_order"),
              QStringLiteral("disorder disturbance fighting drunk threatening") },
            { QStringLiteral("weapons"),
              QStringLiteral("knife gun firearm weapon blade armed") },
            { QStringLiteral("fraud"),
              QStringLiteral("fraud scam phishing identity impersonation") },
            { QStringLiteral("antisocial"),
              QStringLiteral("antisocial nuisance noise harassment loitering") },
        };

        for (auto it = texts.constBegin(); it != texts.constEnd(); ++it) {
            const auto [type, conf] = m_clf.classify(it.value());
            QVERIFY2(type == it.key(),
                     qPrintable(QString("For '%1' expected '%2', got '%3'")
                                .arg(it.value(), it.key(), type)));
        }
    }

    void testVeryLongNarrative()
    {
        // 5000-char narrative must complete without crash, confidence in [0,1]
        QString narrative;
        narrative.reserve(5100);
        const QString base =
            QStringLiteral("The suspect broke into the house and stole cash from the premises. ");
        while (narrative.size() < 5000) narrative += base;
        narrative.truncate(5000);

        const auto [type, conf] = m_clf.classify(narrative);
        QVERIFY(!type.isEmpty());
        QVERIFY(conf >= 0.0 && conf <= 1.0);
    }
};

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    TestNLPEdgeCases t;
    QStringList args = { "test", "-o", "nlp_edge_cases.txt,txt" };
    return QTest::qExec(&t, args);
}

#include "test_nlp_edge_cases.moc"
