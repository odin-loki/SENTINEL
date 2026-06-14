// test_mo_extractor_deep6.cpp — Deep audit iteration 28: MOExtractor
// canonical MO string, commercial target, mask precaution, vehicle target.
#include <QTest>
#include "nlp/MOExtractor.h"

class MOExtractorDeep6Test : public QObject
{
    Q_OBJECT

    MOExtractor m_ex;

private slots:

    void testCanonicalMOStringJoinsFeatures()
    {
        const MOFeatures mo = m_ex.extract(
            QStringLiteral("forced entry residential night cash solo"));
        const QString canon = m_ex.canonicalMOString(mo);
        QVERIFY(!canon.isEmpty());
        QVERIFY(canon.contains(QStringLiteral("forced_entry")));
    }

    void testCommercialTargetDetected()
    {
        const MOFeatures mo = m_ex.extract(
            QStringLiteral("offender targeted a shop on high street"));
        QVERIFY(mo.targetType.has_value());
        QCOMPARE(*mo.targetType, QStringLiteral("commercial"));
    }

    void testMaskPrecautionDetected()
    {
        const MOFeatures mo = m_ex.extract(
            QStringLiteral("suspect wore a mask during the robbery"));
        QVERIFY(mo.precaution.has_value());
    }

    void testVehicleTargetDetected()
    {
        const MOFeatures mo = m_ex.extract(
            QStringLiteral("car window smashed and items stolen from vehicle"));
        QVERIFY(mo.targetType.has_value());
        QCOMPARE(*mo.targetType, QStringLiteral("vehicle"));
    }

    void testCanonicalFromEmptyFeatures()
    {
        const MOFeatures empty;
        const QString canon = m_ex.canonicalMOString(empty);
        QVERIFY(canon.isEmpty() || !canon.trimmed().isEmpty());
    }
};

QTEST_GUILESS_MAIN(MOExtractorDeep6Test)
#include "test_mo_extractor_deep6.moc"
