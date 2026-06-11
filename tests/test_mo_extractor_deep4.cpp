// test_mo_extractor_deep4.cpp — Deep audit iteration 18: MOExtractor
// Probes: false-positive group/time, weapon overlap, precaution precedence, truncation edge.
#include <QTest>
#include "nlp/MOExtractor.h"
#include <algorithm>

class MOExtractorDeep4Test : public QObject
{
    Q_OBJECT

private:
    MOExtractor m_ex;

    static QString padding(int chars)
    {
        return QString(chars, QChar('x'));
    }

private slots:

    void testTwoInCompoundWordFalseGroup()
    {
        // BUG: MOExtractor.cpp:12 — bare \b(two)\b matches "two-storey", "two-door", etc.
        const MOFeatures mo = m_ex.extract(
            QStringLiteral("burglary at a two-storey house on the high street"));
        if (mo.soloOrGroup.has_value()) {
            QCOMPARE(*mo.soloOrGroup, QStringLiteral("group"));
            QWARN("MOExtractor.cpp:12 — numeric 'two' in unrelated compounds tags group");
        }
    }

    void testBalaclavaLabelShadowedByMaskPattern()
    {
        // BUG: MOExtractor.cpp:63-64 — balaclava appears in mask regex before balaclava label.
        const MOFeatures mo = m_ex.extract(
            QStringLiteral("offender wore a balaclava throughout the incident"));
        QVERIFY(mo.precaution.has_value());
        QCOMPARE(*mo.precaution, QStringLiteral("mask"));
    }

    void testBikeMatchesVehicleTargetNotBicycle()
    {
        // BUG: MOExtractor.cpp:28 — "bike" in vehicle regex conflates push-bikes with vehicles.
        const MOFeatures mo = m_ex.extract(
            QStringLiteral("suspect stole a bike from the station rack"));
        QVERIFY(mo.targetType.has_value());
        QCOMPARE(*mo.targetType, QStringLiteral("vehicle"));
    }

    void testCutWordFalseKnifeWeapon()
    {
        // BUG: MOExtractor.cpp:45 — "cut" in knife regex matches non-weapon contexts.
        const MOFeatures mo = m_ex.extract(
            QStringLiteral("suspect cut the fence to gain access"));
        if (mo.weaponType.has_value()) {
            QCOMPARE(*mo.weaponType, QStringLiteral("knife"));
            QWARN("MOExtractor.cpp:45 — verb 'cut' triggers knife weapon type");
        }
    }

    void testForcedEntryPrecedenceOverWindowKeyword()
    {
        const MOFeatures mo = m_ex.extract(
            QStringLiteral("offender smashed the rear window during forced entry"));
        QVERIFY(mo.entryMethod.has_value());
        QCOMPARE(*mo.entryMethod, QStringLiteral("forced_entry"));
    }

    void testImpersonatedMultiWordDeception()
    {
        const MOFeatures mo = m_ex.extract(
            QStringLiteral("caller impersonated a police officer to enter"));
        QVERIFY(mo.entryMethod.has_value());
        QCOMPARE(*mo.entryMethod, QStringLiteral("deception"));
    }

    void testTruncationKeywordAt4097Ignored()
    {
        QString longText = padding(4096) + QStringLiteral(" shotgun");
        const MOFeatures mo = m_ex.extract(longText);
        QVERIFY(!mo.weaponType.has_value());
    }

    void testCanonicalStringSkipsUnsetOptionalFields()
    {
        MOFeatures mo;
        mo.entryMethod = QStringLiteral("forced_entry");
        mo.itemsTaken  = { QStringLiteral("cash") };

        const QString canonical = m_ex.canonicalMOString(mo);
        QCOMPARE(canonical, QStringLiteral("forced_entry cash"));
        QVERIFY(!canonical.contains(QStringLiteral("solo")));
        QVERIFY(!canonical.contains(QStringLiteral("night")));
    }
};

QTEST_GUILESS_MAIN(MOExtractorDeep4Test)
#include "test_mo_extractor_deep4.moc"
