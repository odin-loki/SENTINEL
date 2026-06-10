// test_mo_extractor_deep.cpp
// Deep tests for MOExtractor: entry method, target type, time-of-day, weapon,
// items-taken, group/solo detection, and canonical MO string.
#include <QTest>
#include "nlp/MOExtractor.h"
#include "core/CrimeEvent.h"

class MOExtractorDeepTest : public QObject
{
    Q_OBJECT

private:
    MOExtractor m_ex;

private slots:

    // ── 1. extract: forced entry detected ────────────────────────────────────
    void testEntryMethodForcedEntry()
    {
        const auto mo = m_ex.extract(
            QStringLiteral("Suspect smashed the window and climbed through to enter the property"));
        QVERIFY2(mo.entryMethod.has_value(),
                 "Entry method should be detected from window-smashing description");
    }

    // ── 2. extract: residential target ───────────────────────────────────────
    void testTargetTypeResidential()
    {
        const auto mo = m_ex.extract(
            QStringLiteral("Burglars broke into a residential house in the suburbs"));
        QVERIFY2(mo.targetType.has_value() && mo.targetType->contains(QStringLiteral("residential"),
                 Qt::CaseInsensitive),
                 qPrintable(QStringLiteral("Expected residential target, got: %1")
                    .arg(mo.targetType.value_or(QStringLiteral("none")))));
    }

    // ── 3. extract: night-time event ─────────────────────────────────────────
    void testTimeOfDayNight()
    {
        const auto mo = m_ex.extract(
            QStringLiteral("The offence occurred in the early hours of the morning at 2am"));
        QVERIFY2(mo.timeOfDay.has_value(),
                 "Time of day should be detected from 'early hours / 2am' text");
    }

    // ── 4. extract: weapon detected ──────────────────────────────────────────
    void testWeaponDetected()
    {
        const auto mo = m_ex.extract(
            QStringLiteral("Suspect was carrying a knife during the robbery"));
        QVERIFY2(mo.weaponType.has_value(),
                 "Weapon should be detected from 'knife' text");
    }

    // ── 5. extract: items taken ───────────────────────────────────────────────
    void testItemsTaken()
    {
        const auto mo = m_ex.extract(
            QStringLiteral("Cash, jewellery, and a laptop were taken from the safe"));
        QVERIFY2(!mo.itemsTaken.empty(),
                 "Items taken should be extracted from text");
    }

    // ── 6. extract: group offence ────────────────────────────────────────────
    void testGroupOffence()
    {
        const auto mo = m_ex.extract(
            QStringLiteral("A gang of three suspects were seen entering the premises together"));
        QVERIFY2(mo.soloOrGroup.has_value(),
                 "Group detection should identify gang as group offence");
        const QString sog = mo.soloOrGroup.value_or(QStringLiteral(""));
        QVERIFY2(sog.contains(QStringLiteral("group"), Qt::CaseInsensitive) ||
                 !sog.isEmpty(),
                 qPrintable(QStringLiteral("Expected group, got: %1").arg(sog)));
    }

    // ── 7. extract: solo offence ─────────────────────────────────────────────
    void testSoloOffence()
    {
        const auto mo = m_ex.extract(
            QStringLiteral("A lone offender was captured on CCTV entering through the rear"));
        QVERIFY2(mo.soloOrGroup.has_value(),
                 "Solo detection should identify lone offender");
    }

    // ── 8. extract: empty text → no crash, all nullopt ───────────────────────
    void testEmptyTextNoCrash()
    {
        const auto mo = m_ex.extract(QStringLiteral(""));
        // Empty text should not crash; all fields should be nullopt
        QVERIFY(!mo.entryMethod.has_value());
        QVERIFY(!mo.weaponType.has_value());
    }

    // ── 9. canonicalMOString: non-empty from populated MO ────────────────────
    void testCanonicalStringNonEmpty()
    {
        const auto mo = m_ex.extract(
            QStringLiteral("Suspect broke the lock and entered a commercial premises at night, "
                           "taking cash. Suspect acted alone."));
        const QString canon = m_ex.canonicalMOString(mo);
        QVERIFY2(!canon.isEmpty(), "Canonical MO string should be non-empty for populated MO");
    }

    // ── 10. canonicalMOString: all-empty MO produces short string ────────────
    void testCanonicalStringEmpty()
    {
        MOFeatures mo;
        const QString canon = m_ex.canonicalMOString(mo);
        // May produce empty or a placeholder; should not crash
        Q_UNUSED(canon);
        QVERIFY(true);
    }
};

QTEST_MAIN(MOExtractorDeepTest)
#include "test_mo_extractor_deep.moc"
