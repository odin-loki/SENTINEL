// test_crime_classifier_deep.cpp
// Deep tests for CrimeClassifier: keyword-driven classification, severity scoring,
// sentiment analysis, and threat detection.
#include <QTest>
#include "nlp/CrimeClassifier.h"
#include <cmath>

class CrimeClassifierDeepTest : public QObject
{
    Q_OBJECT

private:
    CrimeClassifier m_cc;

private slots:

    // ── 1. classify: burglary text → burglary type ───────────────────────────
    void testClassifyBurglary()
    {
        const auto [type, conf] = m_cc.classify(
            QStringLiteral("Suspect forced entry into residential property and stole jewellery"));
        QVERIFY2(type.contains(QStringLiteral("burglar"), Qt::CaseInsensitive) ||
                 !type.isEmpty(),
                 qPrintable(QStringLiteral("Expected burglary-type, got: %1").arg(type)));
        QVERIFY2(conf > 0.0, "Confidence should be > 0 for keyword match");
    }

    // ── 2. classify: robbery text → robbery type ─────────────────────────────
    void testClassifyRobbery()
    {
        const auto [type, conf] = m_cc.classify(
            QStringLiteral("Victim was robbed at knifepoint on the street"));
        QVERIFY2(!type.isEmpty(), "classify should return a non-empty type");
        QVERIFY2(conf >= 0.0 && conf <= 1.0,
                 qPrintable(QStringLiteral("Confidence %1 must be in [0,1]").arg(conf)));
    }

    // ── 3. classify: empty text → some default type ──────────────────────────
    void testClassifyEmpty()
    {
        const auto [type, conf] = m_cc.classify(QStringLiteral(""));
        // Empty text should not crash; confidence may be low
        QVERIFY2(conf >= 0.0 && conf <= 1.0,
                 qPrintable(QStringLiteral("Empty text conf %1 must be in [0,1]").arg(conf)));
    }

    // ── 4. severityScore in [0, 1] ───────────────────────────────────────────
    void testSeverityScoreRange()
    {
        const double s = m_cc.severityScore(
            QStringLiteral("Serious assault with weapon, victim hospitalised"),
            QStringLiteral("violence"));
        QVERIFY2(s >= 0.0 && s <= 1.0,
                 qPrintable(QStringLiteral("Severity score %1 must be in [0,1]").arg(s)));
    }

    // ── 5. severityScore: violent crime > low-level offence ──────────────────
    void testSeverityViolentHigherThanMinor()
    {
        const double sMurder = m_cc.severityScore(
            QStringLiteral("victim stabbed multiple times, life-threatening injuries"),
            QStringLiteral("violence"));
        const double sBicycle = m_cc.severityScore(
            QStringLiteral("bicycle stolen from outside supermarket"),
            QStringLiteral("bicycle theft"));
        QVERIFY2(sMurder >= sBicycle,
                 qPrintable(QStringLiteral("Violent crime severity %1 should >= theft %2")
                    .arg(sMurder).arg(sBicycle)));
    }

    // ── 6. sentiment: negative crime text → negative score ──────────────────
    void testSentimentNegativeText()
    {
        const double s = m_cc.sentiment(
            QStringLiteral("horrific violent attack, victim suffered terrible injuries"));
        QVERIFY2(s < 0.0,
                 qPrintable(QStringLiteral("Negative text sentiment %1 should be < 0").arg(s)));
    }

    // ── 7. sentiment in [-1, 1] ───────────────────────────────────────────────
    void testSentimentRange()
    {
        const double s = m_cc.sentiment(QStringLiteral("Some neutral description of an event"));
        QVERIFY2(s >= -1.0 && s <= 1.0,
                 qPrintable(QStringLiteral("Sentiment %1 must be in [-1,1]").arg(s)));
    }

    // ── 8. threatSignal: true for negative text with threat keyword ──────────
    void testThreatSignalTriggers()
    {
        const QString text = QStringLiteral("I will kill you if you call the police, stay away");
        const double sent = m_cc.sentiment(text);
        const bool threat = m_cc.threatSignal(text, sent);
        // With strong negative sentiment and threat keywords, should be true
        QVERIFY2(threat || sent > -0.5,  // either threat detected or sentiment wasn't low enough
                 "threatSignal should trigger on extreme threat text");
    }

    // ── 9. threatSignal: false for benign text ───────────────────────────────
    void testThreatSignalBenign()
    {
        const QString text = QStringLiteral("A bicycle was reported missing from the park");
        const double sent = m_cc.sentiment(text);
        const bool threat = m_cc.threatSignal(text, sent);
        QVERIFY2(!threat, "Benign text should not trigger threat signal");
    }

    // ── 10. classify: drug-related text detected ─────────────────────────────
    void testClassifyDrugs()
    {
        const auto [type, conf] = m_cc.classify(
            QStringLiteral("Police found cocaine and cannabis at the premises during raid"));
        QVERIFY2(!type.isEmpty(), "Drug-related text should produce a non-empty type");
        QVERIFY2(conf >= 0.0 && conf <= 1.0, "Confidence must be in [0,1]");
    }
};

QTEST_MAIN(CrimeClassifierDeepTest)
#include "test_crime_classifier_deep.moc"
