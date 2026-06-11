// test_nlp_comprehensive.cpp — Deep audit coverage for MOExtractor and CrimeClassifier.
// Fills gaps: all 6 MO features in one narrative, all 13 crime categories,
// threat keywords, confidence monotonicity, long text, and police-style reports.

#include <QTest>
#include <QCoreApplication>
#include <QString>
#include <QStringList>
#include <QMap>
#include <algorithm>

#include "nlp/MOExtractor.h"
#include "nlp/CrimeClassifier.h"
#include "core/CrimeEvent.h"

class NLPComprehensiveTest : public QObject
{
    Q_OBJECT

private:
    MOExtractor     m_extractor;
    CrimeClassifier m_classifier;

    void assertThreatTriggeredByKeyword(const QString& keyword)
    {
        const QString text = QStringLiteral(
            "violent attack murder stab victim criminal offender %1 crime weapon").arg(keyword);
        const double sent = m_classifier.sentiment(text);
        QVERIFY2(sent < -0.5,
                 qPrintable(QStringLiteral("Sentiment %1 must be < -0.5 for keyword '%2'")
                                .arg(sent).arg(keyword)));
        QVERIFY2(m_classifier.threatSignal(text, sent),
                 qPrintable(QStringLiteral("Threat keyword '%1' should trigger threatSignal")
                                .arg(keyword)));
    }

private slots:

    // ── MOExtractor: all six core features in one rich narrative ─────────────

    void testMOExtractorAllSixFeatures()
    {
        const QString text = QStringLiteral(
            "At approximately 2am a lone offender smashed the rear window of a residential "
            "house. The suspect was armed with a handgun and stole cash, jewellery, and a "
            "laptop before fleeing.");

        const MOFeatures mo = m_extractor.extract(text);

        QVERIFY2(mo.entryMethod.has_value(),
                 "entryMethod should be extracted from forced-entry narrative");
        QCOMPARE(*mo.entryMethod, QStringLiteral("forced_entry"));

        QVERIFY2(mo.targetType.has_value(),
                 "targetType should be extracted from residential narrative");
        QCOMPARE(*mo.targetType, QStringLiteral("residential"));

        QVERIFY2(mo.timeOfDay.has_value(),
                 "timeOfDay should be extracted from 2am narrative");
        QCOMPARE(*mo.timeOfDay, QStringLiteral("early_morning"));

        QVERIFY2(mo.weaponType.has_value(),
                 "weaponType should be extracted from handgun narrative");
        QCOMPARE(*mo.weaponType, QStringLiteral("firearm"));

        QVERIFY2(mo.itemsTaken.size() >= 3u,
                 qPrintable(QStringLiteral("Expected >= 3 items, got %1")
                                .arg(mo.itemsTaken.size())));
        const QStringList items(mo.itemsTaken.begin(), mo.itemsTaken.end());
        QVERIFY(items.contains(QStringLiteral("cash")));
        QVERIFY(items.contains(QStringLiteral("jewellery")));
        QVERIFY(items.contains(QStringLiteral("laptop")));

        QVERIFY2(mo.soloOrGroup.has_value(),
                 "soloOrGroup should be extracted from lone offender narrative");
        QCOMPARE(*mo.soloOrGroup, QStringLiteral("solo"));

        const QString canonical = m_extractor.canonicalMOString(mo);
        QVERIFY2(!canonical.isEmpty(), "canonicalMOString should be non-empty");
        QVERIFY(canonical.contains(QStringLiteral("forced_entry")));
        QVERIFY(canonical.contains(QStringLiteral("residential")));
        QVERIFY(canonical.contains(QStringLiteral("solo")));
    }

    // ── Entry method variants ────────────────────────────────────────────────

    void testMOExtractorEntryMethod()
    {
        struct Case { const char* text; const char* expected; };
        const Case cases[] = {
            { "Suspect smashed the window and forced entry",           "forced_entry" },
            { "The rear door was unlocked and left open",              "unlocked"     },
            { "Offender pretended to be a courier and tricked staff",  "deception"    },
            { "Suspect tailgated through the secure entrance",         "tailgating"   },
        };

        for (const auto& c : cases) {
            const MOFeatures mo = m_extractor.extract(QString::fromUtf8(c.text));
            QVERIFY2(mo.entryMethod.has_value(),
                     qPrintable(QStringLiteral("Entry method not detected for: %1").arg(c.text)));
            QCOMPARE(*mo.entryMethod, QString::fromUtf8(c.expected));
        }
    }

    // ── Target types ─────────────────────────────────────────────────────────

    void testMOExtractorTargetTypes()
    {
        struct Case { const char* text; const char* expected; };
        const Case cases[] = {
            { "Burglars broke into the house late at night",     "residential" },
            { "Offender robbed the shop at closing time",          "commercial"  },
            { "The car was broken into on the high street",        "vehicle"     },
            { "A pedestrian was accosted near the station",        "person"      },
        };

        for (const auto& c : cases) {
            const MOFeatures mo = m_extractor.extract(QString::fromUtf8(c.text));
            QVERIFY2(mo.targetType.has_value(),
                     qPrintable(QStringLiteral("Target type not detected for: %1").arg(c.text)));
            QCOMPARE(*mo.targetType, QString::fromUtf8(c.expected));
        }
    }

    // ── Time of day bins ─────────────────────────────────────────────────────

    void testMOExtractorTimeOfDay()
    {
        struct Case { const char* text; const char* expected; };
        const Case cases[] = {
            { "The break-in occurred at 3am before dawn",       "early_morning" },
            { "The incident was reported at 9am in the morning",  "morning"       },
            { "Offence took place at 2pm in the afternoon",     "afternoon"     },
            { "Approached the victim at dusk near the park",    "evening"       },
            { "Entry was gained at midnight in the dark",       "night"         },
        };

        for (const auto& c : cases) {
            const MOFeatures mo = m_extractor.extract(QString::fromUtf8(c.text));
            QVERIFY2(mo.timeOfDay.has_value(),
                     qPrintable(QStringLiteral("Time of day not detected for: %1").arg(c.text)));
            QCOMPARE(*mo.timeOfDay, QString::fromUtf8(c.expected));
        }
    }

    // ── Weapon types ─────────────────────────────────────────────────────────

    void testMOExtractorWeapons()
    {
        {
            const MOFeatures mo = m_extractor.extract(
                QStringLiteral("The offender brandished a knife during the robbery"));
            QVERIFY(mo.weaponType.has_value());
            QCOMPARE(*mo.weaponType, QStringLiteral("knife"));
        }
        {
            const MOFeatures mo = m_extractor.extract(
                QStringLiteral("Suspect pulled out a gun and demanded valuables"));
            QVERIFY(mo.weaponType.has_value());
            QCOMPARE(*mo.weaponType, QStringLiteral("firearm"));
        }
        {
            const MOFeatures mo = m_extractor.extract(
                QStringLiteral("Offender was armed with a firearm at the scene"));
            QVERIFY(mo.weaponType.has_value());
            QCOMPARE(*mo.weaponType, QStringLiteral("firearm"));
        }
        {
            const MOFeatures mo = m_extractor.extract(
                QStringLiteral("Victim was threatened with a blade"));
            QVERIFY(mo.weaponType.has_value());
            QCOMPARE(*mo.weaponType, QStringLiteral("knife"));
        }
    }

    // ── Multiple items taken ─────────────────────────────────────────────────

    void testMOExtractorItemsTaken()
    {
        const MOFeatures mo = m_extractor.extract(
            QStringLiteral("Offender stole cash, phone, wallet, watch, and electronics"));

        QVERIFY2(mo.itemsTaken.size() >= 5u,
                 qPrintable(QStringLiteral("Expected >= 5 items, got %1")
                                .arg(mo.itemsTaken.size())));

        const QStringList items(mo.itemsTaken.begin(), mo.itemsTaken.end());
        for (const QString& expected :
             { QStringLiteral("cash"), QStringLiteral("phone"), QStringLiteral("wallet"),
               QStringLiteral("watch"), QStringLiteral("electronics") }) {
            QVERIFY2(items.contains(expected),
                     qPrintable(QStringLiteral("Missing item: %1").arg(expected)));
        }
    }

    // ── Solo vs group ────────────────────────────────────────────────────────

    void testMOExtractorSoloVsGroup()
    {
        {
            const MOFeatures mo = m_extractor.extract(
                QStringLiteral("The offender was acting alone near the property"));
            QVERIFY(mo.soloOrGroup.has_value());
            QCOMPARE(*mo.soloOrGroup, QStringLiteral("solo"));
        }
        {
            const MOFeatures mo = m_extractor.extract(
                QStringLiteral("A group of three entered through the rear door"));
            QVERIFY(mo.soloOrGroup.has_value());
            QCOMPARE(*mo.soloOrGroup, QStringLiteral("group"));
        }
        {
            const MOFeatures mo = m_extractor.extract(
                QStringLiteral("He was not alone; the group split up before entry"));
            QVERIFY(mo.soloOrGroup.has_value());
            QCOMPARE(*mo.soloOrGroup, QStringLiteral("group"));
        }
    }

    // ── Empty input safety ───────────────────────────────────────────────────

    void testMOExtractorEmptyText()
    {
        const MOFeatures mo = m_extractor.extract(QStringLiteral(""));
        QVERIFY(!mo.entryMethod.has_value());
        QVERIFY(!mo.targetType.has_value());
        QVERIFY(!mo.timeOfDay.has_value());
        QVERIFY(!mo.weaponType.has_value());
        QVERIFY(!mo.victimProfile.has_value());
        QVERIFY(!mo.soloOrGroup.has_value());
        QVERIFY(mo.itemsTaken.empty());

        const QString canonical = m_extractor.canonicalMOString(mo);
        QVERIFY2(canonical.isEmpty(), "Empty MOFeatures should produce empty canonical string");
    }

    // ── All 13 crime categories ──────────────────────────────────────────────

    void testClassifierAllThirteenCategories()
    {
        const QMap<QString, QString> texts = {
            { QStringLiteral("assault"),
              QStringLiteral("assault attack punch beat fight violence victim") },
            { QStringLiteral("robbery"),
              QStringLiteral("robbery robbed mugged demanded weapon threatened") },
            { QStringLiteral("burglary"),
              QStringLiteral("burglar burglary ransacked forced entry stolen house") },
            { QStringLiteral("theft"),
              QStringLiteral("theft stolen pickpocket shoplifting missing") },
            { QStringLiteral("vehicle_crime"),
              QStringLiteral("stolen vehicle car break-in window vandalised tyre") },
            { QStringLiteral("drug_offence"),
              QStringLiteral("drugs heroin cocaine cannabis dealing possession") },
            { QStringLiteral("criminal_damage"),
              QStringLiteral("vandalism damage graffiti smashed destroyed fire") },
            { QStringLiteral("public_order"),
              QStringLiteral("disorder disturbance fighting drunk threatening") },
            { QStringLiteral("weapons"),
              QStringLiteral("knife gun firearm weapon blade armed") },
            { QStringLiteral("fraud"),
              QStringLiteral("fraud scam phishing identity impersonation fake") },
            { QStringLiteral("antisocial"),
              QStringLiteral("antisocial nuisance noise harassment loitering abusive") },
            { QStringLiteral("murder"),
              QStringLiteral("murder killed homicide manslaughter dead body fatal") },
            { QStringLiteral("sexual_offence"),
              QStringLiteral("rape sexual indecent assault touching exposure") },
        };

        QCOMPARE(texts.size(), 13);

        for (auto it = texts.constBegin(); it != texts.constEnd(); ++it) {
            const auto [type, conf] = m_classifier.classify(it.value());
            QVERIFY2(type == it.key(),
                     qPrintable(QStringLiteral("For '%1' expected '%2', got '%3' (conf=%4)")
                                    .arg(it.value(), it.key(), type).arg(conf)));
            QVERIFY2(conf > 0.0,
                     qPrintable(QStringLiteral("Confidence should be > 0 for '%1'").arg(it.key())));
        }
    }

    // ── Threat keywords: bomb, execute, shoot, kill ─────────────────────────

    void testClassifierThreatKeywords()
    {
        for (const QString& kw :
             { QStringLiteral("bomb"), QStringLiteral("execute"),
               QStringLiteral("shoot"), QStringLiteral("kill") }) {
            assertThreatTriggeredByKeyword(kw);
        }
    }

    // ── Sentiment: negative scores lower than positive ───────────────────────

    void testClassifierSentimentNegativeVsPositive()
    {
        const double negative = m_classifier.sentiment(
            QStringLiteral("violent attack murder stab shot dead beaten criminal"));
        const double positive = m_classifier.sentiment(
            QStringLiteral("arrested caught prevented rescued apprehended convicted safe"));

        QVERIFY2(negative < 0.0,
                 qPrintable(QStringLiteral("Negative text sentiment %1 should be < 0")
                                .arg(negative)));
        QVERIFY2(positive > 0.0,
                 qPrintable(QStringLiteral("Positive text sentiment %1 should be > 0")
                                .arg(positive)));
        QVERIFY2(negative < positive,
                 qPrintable(QStringLiteral("Negative %1 should be < positive %2")
                                .arg(negative).arg(positive)));
    }

    // ── Long text (500 words) must not crash ─────────────────────────────────

    void testClassifierLongText()
    {
        QString narrative;
        narrative.reserve(6000);
        const QString sentence =
            QStringLiteral("The suspect broke into the house and stole cash from the premises. ");
        while (narrative.count(QChar::Space) + 1 < 500) {
            narrative += sentence;
        }

        const auto [type, conf] = m_classifier.classify(narrative);
        QVERIFY2(!type.isEmpty(), "Long text classification must return a type");
        QVERIFY2(conf >= 0.0 && conf <= 1.0,
                 qPrintable(QStringLiteral("Confidence %1 must be in [0,1]").arg(conf)));

        const MOFeatures mo = m_extractor.extract(narrative);
        const QString canonical = m_extractor.canonicalMOString(mo);
        Q_UNUSED(canonical);
    }

    // ── More keywords → higher confidence ────────────────────────────────────

    void testClassifierConfidenceMonotonicity()
    {
        // Use texts that share a second-place competitor so confidence reflects
        // how decisively the top category wins (not the single-keyword 1.0 artefact).
        const auto [typeSparse, confSparse] = m_classifier.classify(
            QStringLiteral("burglar house stolen"));
        const auto [typeRich, confRich] = m_classifier.classify(
            QStringLiteral("burglar burglary broke entry house home stolen forced ransacked"));

        QCOMPARE(typeSparse, QStringLiteral("burglary"));
        QCOMPARE(typeRich, QStringLiteral("burglary"));
        QVERIFY2(confRich > confSparse,
                 qPrintable(QStringLiteral("Rich text conf %1 should exceed sparse conf %2")
                                .arg(confRich).arg(confSparse)));
    }

    // ── Full police-style narrative ──────────────────────────────────────────

    void testMOExtractorRichPoliceReport()
    {
        const QString report = QStringLiteral(
            "INCIDENT REPORT — Ref: CR/2025/08471\n"
            "At 11pm on 14 March 2025, attending officers responded to a burglary at the "
            "victim's house on Oakfield Road. The victim, an elderly pensioner, reported that "
            "a group of three males had forced entry by kicking the rear patio door. One "
            "offender was observed carrying a crowbar. Items reported stolen include cash, "
            "jewellery, a passport, and a tablet. CCTV captured the offenders fleeing on foot. "
            "Victim declined medical treatment. Scene preserved for SOCO.");

        const MOFeatures mo = m_extractor.extract(report);

        QVERIFY2(mo.entryMethod.has_value(), "Police report should yield entry method");
        QCOMPARE(*mo.entryMethod, QStringLiteral("forced_entry"));

        QVERIFY2(mo.targetType.has_value(), "Police report should yield target type");
        QCOMPARE(*mo.targetType, QStringLiteral("residential"));

        QVERIFY2(mo.timeOfDay.has_value(), "Police report should yield time of day");
        QCOMPARE(*mo.timeOfDay, QStringLiteral("night"));

        QVERIFY2(mo.weaponType.has_value(), "Police report should yield weapon type");
        QCOMPARE(*mo.weaponType, QStringLiteral("blunt"));

        QVERIFY2(mo.soloOrGroup.has_value(), "Police report should yield solo/group");
        QCOMPARE(*mo.soloOrGroup, QStringLiteral("group"));

        QVERIFY2(mo.victimProfile.has_value(), "Police report should yield victim profile");
        QCOMPARE(*mo.victimProfile, QStringLiteral("elderly"));

        const QStringList items(mo.itemsTaken.begin(), mo.itemsTaken.end());
        QVERIFY2(items.size() >= 4u,
                 qPrintable(QStringLiteral("Expected >= 4 stolen items, got %1")
                                .arg(items.size())));
        QVERIFY(items.contains(QStringLiteral("cash")));
        QVERIFY(items.contains(QStringLiteral("jewellery")));
        QVERIFY(items.contains(QStringLiteral("passport")));
        QVERIFY(items.contains(QStringLiteral("tablet")));

        const QString canonical = m_extractor.canonicalMOString(mo);
        QVERIFY2(!canonical.isEmpty(), "Police report canonical MO must be non-empty");
        QVERIFY(canonical.contains(QStringLiteral("forced_entry")));
        QVERIFY(canonical.contains(QStringLiteral("residential")));
        QVERIFY(canonical.contains(QStringLiteral("group")));
        QVERIFY(canonical.contains(QStringLiteral("elderly")));
    }
};

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    NLPComprehensiveTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_nlp_comprehensive.moc"
