// Deep audit iteration 14 — MOExtractor regex, 24h time, group precedence, truncation
#include <QTest>
#include "nlp/MOExtractor.h"
#include <algorithm>

class MOExtractorDeep3Test : public QObject
{
    Q_OBJECT

private:
    MOExtractor m_ex;

    static QString padding(int chars)
    {
        return QString(chars, QChar('x'));
    }

private slots:

    void testEntryForcedKicked()
    {
        const MOFeatures mo = m_ex.extract(
            QStringLiteral("suspect kicked the door open to enter the house"));
        QVERIFY(mo.entryMethod.has_value());
        QCOMPARE(*mo.entryMethod, QStringLiteral("forced_entry"));
    }

    void testEntryTailgating()
    {
        const MOFeatures mo = m_ex.extract(
            QStringLiteral("offender tailgated into the secure building"));
        QVERIFY(mo.entryMethod.has_value());
        QCOMPARE(*mo.entryMethod, QStringLiteral("tailgating"));
    }

    void testWeaponShotgun()
    {
        const MOFeatures mo = m_ex.extract(
            QStringLiteral("armed with a shotgun during the robbery"));
        QVERIFY(mo.weaponType.has_value());
        QCOMPARE(*mo.weaponType, QStringLiteral("firearm"));
    }

    void testWeaponAcidOther()
    {
        const MOFeatures mo = m_ex.extract(
            QStringLiteral("suspect threw acid at the victim"));
        QVERIFY(mo.weaponType.has_value());
        QCOMPARE(*mo.weaponType, QStringLiteral("other"));
    }

    void testTime24hBoundaries()
    {
        struct Case { const char* text; const char* bucket; };
        const Case cases[] = {
            { "incident at 00:15", "early_morning" },
            { "seen at 06:00",     "morning" },
            { "reported at 12:45", "afternoon" },
            { "occurred at 18:30", "evening" },
            { "stolen at 22:00",   "night" },
        };

        for (const auto& c : cases) {
            const MOFeatures mo = m_ex.extract(QString::fromLatin1(c.text));
            QVERIFY2(mo.timeOfDay.has_value(),
                     qPrintable(QStringLiteral("Time missing for '%1'").arg(c.text)));
            QCOMPARE(*mo.timeOfDay, QString::fromLatin1(c.bucket));
        }
    }

    void testTime24hSingleDigitHour()
    {
        const MOFeatures mo = m_ex.extract(QStringLiteral("attack at 9:30 in the morning"));
        QVERIFY(mo.timeOfDay.has_value());
        QCOMPARE(*mo.timeOfDay, QStringLiteral("morning"));
    }

    void testGroupOverSoloWhenAmbiguous()
    {
        const MOFeatures mo = m_ex.extract(
            QStringLiteral("two suspects acted alone according to witness"));
        QVERIFY(mo.soloOrGroup.has_value());
        QCOMPARE(*mo.soloOrGroup, QStringLiteral("group"));
    }

    void testSoloWhenOnlySoloKeyword()
    {
        const MOFeatures mo = m_ex.extract(
            QStringLiteral("the offender worked alone throughout"));
        QVERIFY(mo.soloOrGroup.has_value());
        QCOMPARE(*mo.soloOrGroup, QStringLiteral("solo"));
    }

    void testTruncation4096IgnoresContentBeyond()
    {
        // Weapon appears only after the 4096-char window — must not be extracted.
        QString longText = padding(4096) + QStringLiteral(" gun pistol rifle");
        const MOFeatures mo = m_ex.extract(longText);
        QVERIFY(!mo.weaponType.has_value());
    }

    void testTruncation4096KeepsContentWithin()
    {
        QString longText = QStringLiteral("gun used in robbery ") + padding(4070);
        const MOFeatures mo = m_ex.extract(longText);
        QVERIFY2(mo.weaponType.has_value(),
                 "Weapon within first 4096 chars must be extracted");
        QCOMPARE(*mo.weaponType, QStringLiteral("firearm"));
    }

    void testTruncationExactly4096Chars()
    {
        QString longText = padding(4090) + QStringLiteral(" knife");
        QCOMPARE(longText.size(), 4096);
        const MOFeatures mo = m_ex.extract(longText);
        QVERIFY2(mo.weaponType.has_value(),
                 "Keyword ending exactly at char 4096 must remain visible");
        QCOMPARE(*mo.weaponType, QStringLiteral("knife"));
    }

    void testCanonicalMOStringOrder()
    {
        const MOFeatures mo = m_ex.extract(
            QStringLiteral("group forced entry into house at 22:30 with knife stole cash"));
        const QString canonical = m_ex.canonicalMOString(mo);
        QVERIFY2(canonical.contains(QStringLiteral("forced_entry")),
                 qPrintable(QStringLiteral("Canonical: %1").arg(canonical)));
        QVERIFY(canonical.contains(QStringLiteral("group")));
        QVERIFY(canonical.contains(QStringLiteral("knife")));
        QVERIFY(canonical.contains(QStringLiteral("cash")));
    }
};

QTEST_GUILESS_MAIN(MOExtractorDeep3Test)
#include "test_mo_extractor_deep3.moc"
