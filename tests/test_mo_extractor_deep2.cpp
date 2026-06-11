#include <QTest>
#include "nlp/MOExtractor.h"
#include <QString>
#include <QStringList>
#include <algorithm>

class MOExtractorDeep2Test : public QObject
{
    Q_OBJECT

private:
    MOExtractor m_ex;

private slots:

    // ── Weapon pattern tests ──────────────────────────────────────────────────

    void testWeaponFirearmDetected()
    {
        const MOFeatures mo = m_ex.extract(QStringLiteral("suspect was seen holding a gun"));
        QVERIFY2(mo.weaponType.has_value(), "Firearm keyword 'gun' should set weaponType");
        QCOMPARE(*mo.weaponType, QStringLiteral("firearm"));
    }

    void testWeaponPistolDetected()
    {
        const MOFeatures mo = m_ex.extract(QStringLiteral("offender threatened victim with a pistol"));
        QVERIFY(mo.weaponType.has_value());
        QCOMPARE(*mo.weaponType, QStringLiteral("firearm"));
    }

    void testWeaponKnifeDetected()
    {
        const MOFeatures mo = m_ex.extract(QStringLiteral("victim was stabbed with a knife during the altercation"));
        QVERIFY(mo.weaponType.has_value());
        QCOMPARE(*mo.weaponType, QStringLiteral("knife"));
    }

    void testWeaponBladeDetected()
    {
        const MOFeatures mo = m_ex.extract(QStringLiteral("suspect wielded a blade"));
        QVERIFY(mo.weaponType.has_value());
        QCOMPARE(*mo.weaponType, QStringLiteral("knife"));
    }

    void testWeaponBluntBatDetected()
    {
        const MOFeatures mo = m_ex.extract(QStringLiteral("attacker struck victim with a bat"));
        QVERIFY(mo.weaponType.has_value());
        QCOMPARE(*mo.weaponType, QStringLiteral("blunt"));
    }

    void testWeaponAbsentWhenNoKeyword()
    {
        const MOFeatures mo = m_ex.extract(QStringLiteral("suspect ran away on foot without confronting anyone"));
        QVERIFY(!mo.weaponType.has_value());
    }

    // ── Entry method pattern tests ────────────────────────────────────────────

    void testEntryForcedBroke()
    {
        const MOFeatures mo = m_ex.extract(QStringLiteral("the suspect broke a window to enter the house"));
        QVERIFY2(mo.entryMethod.has_value(), "Entry keyword 'broke' should set entryMethod");
        QCOMPARE(*mo.entryMethod, QStringLiteral("forced_entry"));
    }

    void testEntryForcedSmashed()
    {
        const MOFeatures mo = m_ex.extract(QStringLiteral("the rear door was smashed open"));
        QVERIFY(mo.entryMethod.has_value());
        QCOMPARE(*mo.entryMethod, QStringLiteral("forced_entry"));
    }

    void testEntryUnlocked()
    {
        const MOFeatures mo = m_ex.extract(QStringLiteral("offender entered through an unlocked back door"));
        QVERIFY(mo.entryMethod.has_value());
        QCOMPARE(*mo.entryMethod, QStringLiteral("unlocked"));
    }

    void testEntryDeception()
    {
        const MOFeatures mo = m_ex.extract(QStringLiteral("the suspect pretended to be a utility worker"));
        QVERIFY(mo.entryMethod.has_value());
        QCOMPARE(*mo.entryMethod, QStringLiteral("deception"));
    }

    void testEntryAbsentWhenNoKeyword()
    {
        const MOFeatures mo = m_ex.extract(QStringLiteral("a laptop was taken from a desk in the office"));
        QVERIFY(!mo.entryMethod.has_value());
    }

    // ── Target type pattern tests ─────────────────────────────────────────────

    void testTargetResidential()
    {
        const MOFeatures mo = m_ex.extract(QStringLiteral("suspects targeted a house on the main road"));
        QVERIFY(mo.targetType.has_value());
        QCOMPARE(*mo.targetType, QStringLiteral("residential"));
    }

    void testTargetCommercial()
    {
        const MOFeatures mo = m_ex.extract(QStringLiteral("the shop was broken into overnight"));
        QVERIFY(mo.targetType.has_value());
        QCOMPARE(*mo.targetType, QStringLiteral("commercial"));
    }

    void testTargetVehicle()
    {
        const MOFeatures mo = m_ex.extract(QStringLiteral("the car window was smashed and items taken"));
        QVERIFY(mo.targetType.has_value());
        QCOMPARE(*mo.targetType, QStringLiteral("vehicle"));
    }

    // ── Time-of-day pattern tests ─────────────────────────────────────────────

    void testTimeNight()
    {
        const MOFeatures mo = m_ex.extract(QStringLiteral("the incident occurred at midnight"));
        QVERIFY(mo.timeOfDay.has_value());
        QCOMPARE(*mo.timeOfDay, QStringLiteral("night"));
    }

    void testTimeEarlyMorning()
    {
        const MOFeatures mo = m_ex.extract(QStringLiteral("suspect seen at 3am leaving the property"));
        QVERIFY(mo.timeOfDay.has_value());
        QCOMPARE(*mo.timeOfDay, QStringLiteral("early_morning"));
    }

    void testTimeEvening()
    {
        const MOFeatures mo = m_ex.extract(QStringLiteral("the robbery happened at dusk"));
        QVERIFY(mo.timeOfDay.has_value());
        QCOMPARE(*mo.timeOfDay, QStringLiteral("evening"));
    }

    // ── Victim profile tests ──────────────────────────────────────────────────

    void testVictimElderly()
    {
        const MOFeatures mo = m_ex.extract(QStringLiteral("an elderly pensioner was targeted at their home"));
        QVERIFY(mo.victimProfile.has_value());
        QCOMPARE(*mo.victimProfile, QStringLiteral("elderly"));
    }

    void testVictimFemale()
    {
        const MOFeatures mo = m_ex.extract(QStringLiteral("the woman was approached by two suspects"));
        QVERIFY(mo.victimProfile.has_value());
        QCOMPARE(*mo.victimProfile, QStringLiteral("female"));
    }

    void testVictimChild()
    {
        const MOFeatures mo = m_ex.extract(QStringLiteral("a juvenile was reported missing"));
        QVERIFY(mo.victimProfile.has_value());
        QCOMPARE(*mo.victimProfile, QStringLiteral("child"));
    }

    // ── Precaution tests ──────────────────────────────────────────────────────

    void testPrecautionGloves()
    {
        const MOFeatures mo = m_ex.extract(QStringLiteral("suspect was wearing gloves throughout"));
        QVERIFY(mo.precaution.has_value());
        QCOMPARE(*mo.precaution, QStringLiteral("gloves"));
    }

    void testPrecautionMask()
    {
        const MOFeatures mo = m_ex.extract(QStringLiteral("the masked offender entered the premises"));
        QVERIFY(mo.precaution.has_value());
        QCOMPARE(*mo.precaution, QStringLiteral("mask"));
    }

    // ── Items taken tests ─────────────────────────────────────────────────────

    void testItemsCashDetected()
    {
        const MOFeatures mo = m_ex.extract(QStringLiteral("approximately five hundred pounds in cash was stolen"));
        const auto& items = mo.itemsTaken;
        QVERIFY2(std::find(items.begin(), items.end(), QStringLiteral("cash")) != items.end(),
                 "Expected 'cash' in itemsTaken");
    }

    void testItemsJewelleryNormalised()
    {
        // Both "jewelry" (US) and "jewellery" (UK) should normalise to "jewellery"
        const MOFeatures moUK = m_ex.extract(QStringLiteral("jewellery and a watch were taken"));
        const MOFeatures moUS = m_ex.extract(QStringLiteral("jewelry and a watch were taken"));

        auto contains = [](const std::vector<QString>& v, const QString& s) {
            return std::find(v.begin(), v.end(), s) != v.end();
        };
        QVERIFY(contains(moUK.itemsTaken, QStringLiteral("jewellery")));
        QVERIFY(contains(moUS.itemsTaken, QStringLiteral("jewellery")));
    }

    void testItemsMultipleDistinct()
    {
        const MOFeatures mo = m_ex.extract(
            QStringLiteral("suspect stole cash jewellery laptop and a watch from the property"));
        QVERIFY2(mo.itemsTaken.size() >= 3,
                 qPrintable(QStringLiteral("Expected >= 3 items, got %1").arg((int)mo.itemsTaken.size())));
    }

    void testItemsNoDuplicates()
    {
        const MOFeatures mo = m_ex.extract(
            QStringLiteral("cash was in the drawer and more cash was under the bed"));
        int cashCount = 0;
        for (const auto& item : mo.itemsTaken) {
            if (item == QStringLiteral("cash")) ++cashCount;
        }
        QCOMPARE(cashCount, 1);
    }

    // ── Solo / group tests ───────────────────────────────────────────────────

    void testSoloOffender()
    {
        const MOFeatures mo = m_ex.extract(QStringLiteral("the suspect acted alone and was seen leaving solo"));
        QVERIFY(mo.soloOrGroup.has_value());
        QCOMPARE(*mo.soloOrGroup, QStringLiteral("solo"));
    }

    void testGroupOffender()
    {
        const MOFeatures mo = m_ex.extract(QStringLiteral("a group of three individuals entered the property"));
        QVERIFY(mo.soloOrGroup.has_value());
        QCOMPARE(*mo.soloOrGroup, QStringLiteral("group"));
    }

    void testAmbiguousSoloAndGroupResolvesToGroup()
    {
        const MOFeatures mo = m_ex.extract(
            QStringLiteral("the gang worked alone according to the witness"));
        QVERIFY(mo.soloOrGroup.has_value());
        QCOMPARE(*mo.soloOrGroup, QStringLiteral("group"));
    }

    // ── Empty text: all fields absent ────────────────────────────────────────

    void testEmptyTextAllFieldsAbsent()
    {
        const MOFeatures mo = m_ex.extract(QStringLiteral(""));
        QVERIFY(!mo.entryMethod.has_value());
        QVERIFY(!mo.targetType.has_value());
        QVERIFY(!mo.timeOfDay.has_value());
        QVERIFY(!mo.weaponType.has_value());
        QVERIFY(!mo.victimProfile.has_value());
        QVERIFY(!mo.soloOrGroup.has_value());
        QVERIFY(!mo.precaution.has_value());
        QVERIFY(mo.itemsTaken.empty());
    }

    // Text with no MO indicator words also produces empty features.
    void testNoMOIndicatorsAllAbsent()
    {
        const MOFeatures mo = m_ex.extract(
            QStringLiteral("the sky is blue and the weather is pleasant today"));
        QVERIFY(!mo.entryMethod.has_value());
        QVERIFY(!mo.weaponType.has_value());
        QVERIFY(!mo.targetType.has_value());
        QVERIFY(mo.itemsTaken.empty());
    }

    // ── Very long text does not crash ────────────────────────────────────────

    void testVeryLongTextNoCrash()
    {
        // Build ~50 000-character string (well over 4096 truncation threshold).
        // The extractor must not crash or hang; it truncates at 4096 chars internally.
        QString longText;
        longText.reserve(55000);
        for (int i = 0; i < 1000; ++i) {
            longText += QStringLiteral("the suspect approached the victim and demanded money ");
        }
        // Extract must simply return without crashing.
        const MOFeatures mo = m_ex.extract(longText);
        // itemsTaken is a vector — just verify the call completed.
        Q_UNUSED(mo);
        QVERIFY(true);
    }

    void testVeryLongTextWithWeaponInFirstSegment()
    {
        // Gun keyword appears well within the 4096-char truncation window.
        QString longText = QStringLiteral("suspect used a gun during the robbery. ");
        for (int i = 0; i < 500; ++i) {
            longText += QStringLiteral("the police arrived shortly after the incident was reported. ");
        }
        const MOFeatures mo = m_ex.extract(longText);
        QVERIFY2(mo.weaponType.has_value(), "Weapon 'gun' in first segment should be extracted");
        QCOMPARE(*mo.weaponType, QStringLiteral("firearm"));
    }

    // ── Canonical MO string ──────────────────────────────────────────────────

    void testCanonicalMOStringIncludesExtractedFields()
    {
        const MOFeatures mo = m_ex.extract(
            QStringLiteral("burglar forced entry into house at night stole cash alone"));
        const QString canonical = m_ex.canonicalMOString(mo);
        QVERIFY2(!canonical.isEmpty(), "Canonical MO string should be non-empty for rich text");
        // Should contain extracted features as tokens
        QVERIFY2(canonical.contains(QStringLiteral("forced_entry")),
                 qPrintable(QStringLiteral("Canonical: '%1'").arg(canonical)));
        QVERIFY2(canonical.contains(QStringLiteral("residential")),
                 qPrintable(QStringLiteral("Canonical: '%1'").arg(canonical)));
        QVERIFY2(canonical.contains(QStringLiteral("cash")),
                 qPrintable(QStringLiteral("Canonical: '%1'").arg(canonical)));
    }

    void testCanonicalMOStringEmptyForEmptyFeatures()
    {
        const MOFeatures mo = m_ex.extract(QStringLiteral(""));
        QVERIFY(m_ex.canonicalMOString(mo).isEmpty());
    }
};

QTEST_GUILESS_MAIN(MOExtractorDeep2Test)
#include "test_mo_extractor_deep2.moc"
