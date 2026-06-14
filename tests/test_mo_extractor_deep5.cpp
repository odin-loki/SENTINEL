// test_mo_extractor_deep5.cpp — Deep audit iteration 24: MOExtractor
// entry method, weapons, group size, time of day, items, empty narrative.
#include <QTest>
#include "nlp/MOExtractor.h"

class MOExtractorDeep5Test : public QObject
{
    Q_OBJECT

    MOExtractor m_ex;

private slots:

    void testForcedEntryDetected()
    {
        const MOFeatures mo = m_ex.extract(
            QStringLiteral("offender forced entry through rear door"));
        QVERIFY(mo.entryMethod.has_value());
        QCOMPARE(*mo.entryMethod, QStringLiteral("forced_entry"));
    }

    void testWeaponGunDetected()
    {
        const MOFeatures mo = m_ex.extract(
            QStringLiteral("suspect threatened victim with a handgun"));
        QVERIFY(mo.weaponType.has_value());
        QCOMPARE(*mo.weaponType, QStringLiteral("firearm"));
    }

    void testSoloOffenderDetected()
    {
        const MOFeatures mo = m_ex.extract(
            QStringLiteral("lone offender entered the premises alone"));
        QVERIFY(mo.soloOrGroup.has_value());
        QCOMPARE(*mo.soloOrGroup, QStringLiteral("solo"));
    }

    void testGroupOffenderDetected()
    {
        const MOFeatures mo = m_ex.extract(
            QStringLiteral("three suspects working together"));
        QVERIFY(mo.soloOrGroup.has_value());
        QCOMPARE(*mo.soloOrGroup, QStringLiteral("group"));
    }

    void testNightTimeOfDay()
    {
        const MOFeatures mo = m_ex.extract(
            QStringLiteral("incident occurred at night in darkness"));
        QVERIFY(mo.timeOfDay.has_value());
        QCOMPARE(*mo.timeOfDay, QStringLiteral("night"));
    }

    void testItemsTakenCash()
    {
        const MOFeatures mo = m_ex.extract(
            QStringLiteral("offender took cash and jewellery from drawer"));
        QVERIFY(!mo.itemsTaken.empty());
        bool hasCash = false;
        for (const auto& item : mo.itemsTaken) {
            if (item.contains(QStringLiteral("cash"), Qt::CaseInsensitive))
                hasCash = true;
        }
        QVERIFY(hasCash);
    }

    void testEmptyNarrativeReturnsEmptyMO()
    {
        const MOFeatures mo = m_ex.extract(QString());
        QVERIFY(!mo.entryMethod.has_value());
        QVERIFY(!mo.weaponType.has_value());
        QVERIFY(mo.itemsTaken.empty());
    }
};

QTEST_GUILESS_MAIN(MOExtractorDeep5Test)
#include "test_mo_extractor_deep5.moc"
