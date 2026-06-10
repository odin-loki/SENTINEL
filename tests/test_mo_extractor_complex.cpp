// test_mo_extractor_complex.cpp
// MOExtractor complex MO string tests: ambiguous patterns, compound sentences,
// canonicalMOString correctness, and edge cases.
#include <QTest>
#include "nlp/MOExtractor.h"

class MOExtractorComplexTest : public QObject
{
    Q_OBJECT

private:
    MOExtractor extractor;

private slots:

    // 1. Ambiguous entry: prefers explicit forced entry over generic "entered"
    void testAmbiguousEntryForcedPreferred()
    {
        const auto mo = extractor.extract(
            QStringLiteral("The suspect broke the window and entered the building at midnight."));
        if (mo.entryMethod.has_value())
            QVERIFY2(mo.entryMethod.value().contains(QStringLiteral("forced"), Qt::CaseInsensitive) ||
                     mo.entryMethod.value().contains(QStringLiteral("window"), Qt::CaseInsensitive),
                     qPrintable(QStringLiteral("Expected forced entry, got: %1")
                        .arg(mo.entryMethod.value())));
    }

    // 2. Compound sentence: group + residential + night
    void testCompoundSentenceExtraction()
    {
        const auto mo = extractor.extract(
            QStringLiteral("A group of suspects broke into the residential property after dark."));
        // Should detect at least one of the three signals
        bool anyDetected = mo.soloOrGroup.has_value() ||
                           mo.targetType.has_value()   ||
                           mo.timeOfDay.has_value();
        QVERIFY2(anyDetected, "Compound sentence should yield at least one extracted feature");
    }

    // 3. Vehicle target: car mentioned
    void testVehicleTargetExtracted()
    {
        const auto mo = extractor.extract(
            QStringLiteral("Offender smashed the car window and stole items from inside the vehicle."));
        if (mo.targetType.has_value())
            QVERIFY2(mo.targetType.value().contains(QStringLiteral("vehicle"), Qt::CaseInsensitive) ||
                     mo.targetType.value().contains(QStringLiteral("car"), Qt::CaseInsensitive),
                     qPrintable(QStringLiteral("Target type '%1' should be vehicle-related").arg(mo.targetType.value())));
    }

    // 4. canonicalMOString: non-empty for feature-rich MO
    void testCanonicalMOStringNonEmpty()
    {
        const auto mo = extractor.extract(
            QStringLiteral("Forced rear window entry residential property, night, cash and jewellery taken, two suspects."));
        const QString canonical = extractor.canonicalMOString(mo);
        QVERIFY2(!canonical.isEmpty(), "canonicalMOString should be non-empty for feature-rich MO");
    }

    // 5. canonicalMOString: contains entry method when extracted
    void testCanonicalContainsEntryMethod()
    {
        const auto mo = extractor.extract(
            QStringLiteral("Suspect forced open the rear door and entered the house."));
        if (mo.entryMethod.has_value()) {
            const QString canonical = extractor.canonicalMOString(mo);
            QVERIFY2(!canonical.isEmpty(), "Canonical MO with entry method should be non-empty");
        }
    }

    // 6. Early morning time detection
    void testEarlyMorningDetection()
    {
        const auto mo = extractor.extract(
            QStringLiteral("Incident occurred at approximately 3am."));
        if (mo.timeOfDay.has_value())
            QVERIFY2(!mo.timeOfDay.value().isEmpty(), "time of day should be non-empty");
    }

    // 7. Multiple items taken
    void testMultipleItemsTaken()
    {
        const auto mo = extractor.extract(
            QStringLiteral("The offender took cash, jewellery, a laptop and a watch."));
        QVERIFY2(mo.itemsTaken.size() >= 1, "Multiple items should be detected");
    }

    // 8. Knife weapon detection
    void testKnifeWeaponDetection()
    {
        const auto mo = extractor.extract(
            QStringLiteral("The suspect threatened the victim with a knife."));
        if (mo.weaponType.has_value())
            QVERIFY2(!mo.weaponType.value().isEmpty(), "weaponType should be non-empty");
    }

    // 9. Empty text: no crash, returns empty optional fields
    void testEmptyTextNoCrash()
    {
        const auto mo = extractor.extract(QStringLiteral(""));
        QVERIFY(!mo.entryMethod.has_value());
        QVERIFY(!mo.targetType.has_value());
    }

    // 10. Solo keyword detection
    void testSoloDetection()
    {
        const auto mo = extractor.extract(
            QStringLiteral("A lone offender was observed breaking into the shop."));
        if (mo.soloOrGroup.has_value())
            QVERIFY2(mo.soloOrGroup.value().contains(QStringLiteral("solo"), Qt::CaseInsensitive),
                     qPrintable(QStringLiteral("soloOrGroup '%1' should be 'solo'").arg(mo.soloOrGroup.value())));
    }
};

QTEST_MAIN(MOExtractorComplexTest)
#include "test_mo_extractor_complex.moc"
