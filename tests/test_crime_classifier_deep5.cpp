// test_crime_classifier_deep5.cpp — Deep audit iteration 22: CrimeClassifier
// murder severity, empty unknown, threat/sentiment, multilabel confidence,
// burglary vs theft disambiguation, severity monotonicity.

#include <QTest>
#include <cmath>
#include "nlp/CrimeClassifier.h"

class CrimeClassifierDeep5Test : public QObject
{
    Q_OBJECT

private slots:

    void testMurderKeywordsHighestSeverity()
    {
        CrimeClassifier clf;
        const QString text = QStringLiteral("murder homicide killed dead fatal victim body");
        const double murderSev = clf.severityScore(text, QStringLiteral("murder"));
        const double assaultSev = clf.severityScore(text, QStringLiteral("assault"));
        const double theftSev = clf.severityScore(text, QStringLiteral("theft"));

        QVERIFY2(murderSev >= assaultSev,
                 qPrintable(QStringLiteral("Murder severity %1 should >= assault %2")
                                .arg(murderSev).arg(assaultSev)));
        QVERIFY2(murderSev >= theftSev,
                 qPrintable(QStringLiteral("Murder severity %1 should >= theft %2")
                                .arg(murderSev).arg(theftSev)));
        QCOMPARE(murderSev, 1.0);
    }

    void testEmptyNarrativeReturnsUnknownType()
    {
        CrimeClassifier clf;
        const auto empty = clf.classify(QStringLiteral(""));
        QCOMPARE(empty.first, QStringLiteral("unknown"));
        QCOMPARE(empty.second, 0.0);

        const auto noise = clf.classify(
            QStringLiteral("xyzzy plugh lorem ipsum delta"));
        QCOMPARE(noise.first, QStringLiteral("unknown"));
        QCOMPARE(noise.second, 0.0);
    }

    void testThreatSignalDetectedOnViolentThreatText()
    {
        CrimeClassifier clf;
        const QString text = QStringLiteral("suspect threatened to kill the victim");
        const double sent = clf.sentiment(text);
        QVERIFY2(sent < -0.5,
                 qPrintable(QStringLiteral("Threat text sentiment %1 expected < -0.5").arg(sent)));
        QVERIFY(clf.threatSignal(text, sent));
        QVERIFY(!clf.threatSignal(text, -0.5));
    }

    void testSentimentNegativeOnViolentText()
    {
        CrimeClassifier clf;
        const double sent = clf.sentiment(
            QStringLiteral("violent attack murder stab victim injured criminal"));
        QVERIFY2(sent < 0.0,
                 qPrintable(QStringLiteral("Violent sentiment %1 should be negative").arg(sent)));
        QVERIFY2(sent >= -1.0 && sent <= 1.0,
                 "Sentiment must stay in [-1, 1]");
    }

    void testMultilabelKeywordsLowerConfidence()
    {
        CrimeClassifier clf;
        const auto single = clf.classify(QStringLiteral("burglary"));
        const auto multi = clf.classify(
            QStringLiteral("burglary broke entry house stolen forced"));
        QCOMPARE(single.first, QStringLiteral("burglary"));
        QCOMPARE(multi.first, QStringLiteral("burglary"));
        QVERIFY2(multi.second < single.second,
                 qPrintable(QStringLiteral("Multi-category keywords confidence %1 should be "
                                            "< single-hit confidence %2")
                                .arg(multi.second).arg(single.second)));
        QVERIFY2(multi.second > 0.0 && multi.second < 1.0,
                 "Competing keyword families should yield fractional confidence");
    }

    void testBurglaryVsTheftDisambiguation()
    {
        CrimeClassifier clf;
        const auto burglary = clf.classify(
            QStringLiteral("burglar forced entry house home ransacked broke"));
        QCOMPARE(burglary.first, QStringLiteral("burglary"));
        QVERIFY(burglary.second > 0.0);

        const auto theft = clf.classify(
            QStringLiteral("theft pickpocket shoplifting stolen missing"));
        QCOMPARE(theft.first, QStringLiteral("theft"));
        QVERIFY(theft.second > 0.0);
    }

    void testSeverityMonotonicMurderGreaterThanTheft()
    {
        CrimeClassifier clf;
        const QString murderText = QStringLiteral("murder killed dead homicide fatal");
        const QString theftText = QStringLiteral("theft stolen pickpocket missing");

        const double murderSev = clf.severityScore(murderText, QStringLiteral("murder"));
        const double theftSev = clf.severityScore(theftText, QStringLiteral("theft"));

        QVERIFY2(murderSev > theftSev,
                 qPrintable(QStringLiteral("Murder severity %1 must exceed theft %2")
                                .arg(murderSev).arg(theftSev)));
    }
};

QTEST_GUILESS_MAIN(CrimeClassifierDeep5Test)

#include "test_crime_classifier_deep5.moc"
