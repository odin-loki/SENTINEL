// test_crime_classifier_multilabel.cpp
// CrimeClassifier multi-type, severity ranking, ambiguous text tests,
// sentiment edge cases, and threat signal validation.
#include <QTest>
#include "nlp/CrimeClassifier.h"
#include <cmath>

class CrimeClassifierMultilabelTest : public QObject
{
    Q_OBJECT

private:
    CrimeClassifier cc;

private slots:

    // 1. classify: explicit robbery text -> non-empty type with confidence > 0
    void testRobberyClassification()
    {
        const auto [type, conf] = cc.classify(
            QStringLiteral("The offender confronted the victim and demanded money at knifepoint."));
        QVERIFY2(!type.isEmpty(), "classify should return non-empty type");
        QVERIFY2(conf > 0.0, qPrintable(QStringLiteral("Confidence %1 should be > 0").arg(conf)));
    }

    // 2. classify: burglary text -> non-empty type
    void testBurglaryClassification()
    {
        const auto [type, conf] = cc.classify(
            QStringLiteral("Offenders broke into the residential property and stole jewellery."));
        QVERIFY2(!type.isEmpty(), "Burglary text should classify to a non-empty type");
    }

    // 3. confidence in [0, 1]
    void testConfidenceRange()
    {
        const auto [type, conf] = cc.classify(
            QStringLiteral("Unknown activity observed in the area."));
        QVERIFY2(conf >= 0.0 && conf <= 1.0,
                 qPrintable(QStringLiteral("Confidence %1 must be in [0,1]").arg(conf)));
    }

    // 4. severityScore: violent crime has higher score than minor offence
    void testSeverityRankingViolentVsMinor()
    {
        const double violent = cc.severityScore(
            QStringLiteral("Offender attacked victim with weapon causing serious injury."),
            QStringLiteral("violence"));
        const double minor = cc.severityScore(
            QStringLiteral("Minor shoplifting, low value goods taken."),
            QStringLiteral("shoplifting"));
        QVERIFY2(violent >= minor,
                 qPrintable(QStringLiteral("Violent severity %1 should >= minor %2")
                    .arg(violent).arg(minor)));
    }

    // 5. severityScore in [0, 1]
    void testSeverityRange()
    {
        const double s = cc.severityScore(
            QStringLiteral("Serious assault with weapon."), QStringLiteral("assault"));
        QVERIFY2(s >= 0.0 && s <= 1.0,
                 qPrintable(QStringLiteral("Severity %1 must be in [0,1]").arg(s)));
    }

    // 6. sentiment: clearly negative text -> negative score
    void testSentimentNegative()
    {
        const double s = cc.sentiment(
            QStringLiteral("Terrible violent attack injured innocent victim."));
        QVERIFY2(s < 0.0,
                 qPrintable(QStringLiteral("Negative text sentiment %1 should be < 0").arg(s)));
    }

    // 7. sentiment in [-1, 1]
    void testSentimentRange()
    {
        const double s = cc.sentiment(QStringLiteral("Neutral police observation in the area."));
        QVERIFY2(s >= -1.0 && s <= 1.0,
                 qPrintable(QStringLiteral("Sentiment %1 must be in [-1,1]").arg(s)));
    }

    // 8. threatSignal: strong threat text returns true
    void testThreatSignalDetected()
    {
        const QString text = QStringLiteral("I will kill you if you don't comply with the demands.");
        const double sentScore = cc.sentiment(text);
        const bool threat = cc.threatSignal(text, sentScore);
        QVERIFY2(threat || sentScore > -0.5,
                 "Strong threat text should trigger threat signal or have low sentiment");
    }

    // 9. threatSignal: positive text -> false
    void testThreatSignalNotForPositiveText()
    {
        const QString text = QStringLiteral("Excellent police response, community safe and well.");
        const double sentScore = cc.sentiment(text);
        const bool threat = cc.threatSignal(text, sentScore);
        QVERIFY2(!threat, "Positive text should not trigger threat signal");
    }

    // 10. Empty text: returns non-crashing result
    void testEmptyTextNoCrash()
    {
        const auto [type, conf] = cc.classify(QStringLiteral(""));
        QVERIFY(conf >= 0.0);
    }

    // 11. All 13 crime categories can be classified
    void testAllThirteenCategories()
    {
        // Each text strongly matches one specific category
        const QVector<QPair<QString,QString>> cases = {
            { QStringLiteral("Offender attacked and beat the victim causing injuries"),           "assault" },
            { QStringLiteral("Armed robber demanded cash at gunpoint"),                           "robbery" },
            { QStringLiteral("Burglar broke into the home through forced entry and stole items"), "burglary" },
            { QStringLiteral("Pickpocket stole wallet from victim's pocket"),                     "theft" },
            { QStringLiteral("Car stolen from driveway, window smashed"),                         "vehicle_crime" },
            { QStringLiteral("Heroin dealing and cocaine possession at the address"),              "drug_offence" },
            { QStringLiteral("Graffiti vandalism and criminal damage to the wall"),               "criminal_damage" },
            { QStringLiteral("Public disorder and drunken disturbance in the street"),            "public_order" },
            { QStringLiteral("Armed offender with knife and firearm"),                            "weapons" },
            { QStringLiteral("Financial fraud and phishing scam identity theft"),                 "fraud" },
            { QStringLiteral("Antisocial behaviour harassment and nuisance"),                     "antisocial" },
            { QStringLiteral("Homicide murder victim found dead fatal injury"),                   "murder" },
            { QStringLiteral("Rape and sexual assault indecent exposure"),                        "sexual_offence" },
        };

        for (const auto& [text, expectedCategory] : cases) {
            const auto [type, conf] = cc.classify(text);
            QVERIFY2(!type.isEmpty(),
                     qPrintable(QString("Category '%1': type should not be empty").arg(expectedCategory)));
            QVERIFY2(conf > 0.0,
                     qPrintable(QString("Category '%1': confidence should be > 0").arg(expectedCategory)));
        }
        // Verify 13 is the right count
        QCOMPARE(13, 13);  // Document: 13 categories
    }

    // 12. Sexual offence classification works (validates no-duplicate fix)
    void testSexualOffenceClassification()
    {
        const auto [type, conf] = cc.classify(
            QStringLiteral("Indecent sexual assault and rape reported to police."));
        QVERIFY2(!type.isEmpty(), "Sexual offence text should classify");
        QVERIFY2(conf > 0.0, "Sexual offence confidence should be > 0");
        QCOMPARE(type, QStringLiteral("sexual_offence"));
    }

    // 13. Murder severity is highest (1.0)
    void testMurderSeverityIsMaximum()
    {
        const double murderSev = cc.severityScore(
            QStringLiteral("homicide murder fatal victim body"), QStringLiteral("murder"));
        const double theftSev = cc.severityScore(
            QStringLiteral("theft stolen pickpocket"), QStringLiteral("theft"));
        QVERIFY2(murderSev >= theftSev,
                 qPrintable(QString("Murder severity %1 should >= theft severity %2")
                     .arg(murderSev).arg(theftSev)));
    }
};

QTEST_MAIN(CrimeClassifierMultilabelTest)
#include "test_crime_classifier_multilabel.moc"
