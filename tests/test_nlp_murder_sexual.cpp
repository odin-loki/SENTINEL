// test_nlp_murder_sexual.cpp — Tests for murder/sexual_offence classification
// and extended MOExtractor features (victim profiles, new items, weapons)

#include <QTest>
#include <QCoreApplication>
#include <QString>
#include <cmath>
#include <algorithm>

#include "nlp/CrimeClassifier.h"
#include "nlp/MOExtractor.h"
#include "core/CrimeEvent.h"

class NLPMurderSexualTest : public QObject
{
    Q_OBJECT
private slots:

    void testMurderClassification()
    {
        CrimeClassifier cc;
        const auto [type, conf] = cc.classify(
            QStringLiteral("victim was killed in a homicide, body found"));
        QCOMPARE(type, QStringLiteral("murder"));
        QVERIFY2(conf > 0.5,
                 qPrintable(QStringLiteral("Expected confidence > 0.5, got %1").arg(conf)));
    }

    void testSexualOffenceClassification()
    {
        CrimeClassifier cc;
        const auto [type, conf] = cc.classify(
            QStringLiteral("indecent sexual assault on the person"));
        QCOMPARE(type, QStringLiteral("sexual_offence"));
        QVERIFY2(conf > 0.5,
                 qPrintable(QStringLiteral("Expected confidence > 0.5, got %1").arg(conf)));
    }

    void testMurderSeverityScore()
    {
        CrimeClassifier cc;
        const QString text = QStringLiteral("victim was killed in a homicide, body found");
        const auto [type, conf] = cc.classify(text);
        const double score = cc.severityScore(text, type);
        QVERIFY2(score >= 0.9,
                 qPrintable(QStringLiteral("Expected severity >= 0.9, got %1").arg(score)));
    }

    void testVictimProfileElderly()
    {
        MOExtractor ex;
        const MOFeatures mo = ex.extract(QStringLiteral("an elderly pensioner was robbed"));
        QVERIFY2(mo.victimProfile.has_value(), "victimProfile should be set for elderly text");
        QCOMPARE(*mo.victimProfile, QStringLiteral("elderly"));
    }

    void testVictimProfileChild()
    {
        MOExtractor ex;
        const MOFeatures mo = ex.extract(QStringLiteral("a child teenager was the target"));
        QVERIFY2(mo.victimProfile.has_value(), "victimProfile should be set for child text");
        QCOMPARE(*mo.victimProfile, QStringLiteral("child"));
    }

    void testVictimProfileFemale()
    {
        MOExtractor ex;
        const MOFeatures mo = ex.extract(QStringLiteral("the woman was assaulted"));
        QVERIFY2(mo.victimProfile.has_value(), "victimProfile should be set for female text");
        QCOMPARE(*mo.victimProfile, QStringLiteral("female"));
    }

    void testMoreItemsExtraction()
    {
        MOExtractor ex;
        const MOFeatures mo = ex.extract(QStringLiteral("suspect stole a bicycle and a tablet"));
        const auto bicycleIt = std::find(mo.itemsTaken.begin(), mo.itemsTaken.end(),
                                          QStringLiteral("bicycle"));
        const auto tabletIt  = std::find(mo.itemsTaken.begin(), mo.itemsTaken.end(),
                                          QStringLiteral("tablet"));
        QVERIFY2(bicycleIt != mo.itemsTaken.end(), "bicycle should be in itemsTaken");
        QVERIFY2(tabletIt  != mo.itemsTaken.end(), "tablet should be in itemsTaken");
    }

    void testPassportExtraction()
    {
        MOExtractor ex;
        const MOFeatures mo = ex.extract(QStringLiteral("passport and documents were stolen"));
        const auto it = std::find(mo.itemsTaken.begin(), mo.itemsTaken.end(),
                                   QStringLiteral("passport"));
        QVERIFY2(it != mo.itemsTaken.end(), "passport should be in itemsTaken");
    }

    void testMacheteWeapon()
    {
        MOExtractor ex;
        const MOFeatures mo = ex.extract(
            QStringLiteral("suspect threatened victim with a machete"));
        QVERIFY2(mo.weaponType.has_value(), "weaponType should be set for machete");
        QCOMPARE(*mo.weaponType, QStringLiteral("knife"));
    }

    void testCrowbarWeapon()
    {
        MOExtractor ex;
        const MOFeatures mo = ex.extract(
            QStringLiteral("forced entry using crowbar"));
        QVERIFY2(mo.weaponType.has_value(), "weaponType should be set for crowbar");
        QCOMPARE(*mo.weaponType, QStringLiteral("blunt"));
    }
};

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    NLPMurderSexualTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_nlp_murder_sexual.moc"
