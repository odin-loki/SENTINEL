// test_crime_classifier_deep4.cpp — Deep audit iteration 18: CrimeClassifier
// Probes: tie-breaking, tokenisation, threat boundary, severity defaults, category overlap.
#include <QTest>
#include <cmath>
#include "nlp/CrimeClassifier.h"

class CrimeClassifierDeep4Test : public QObject
{
    Q_OBJECT

private slots:

    void testVictimTokenTieBreaksToAssaultNotMurder()
    {
        // BUG: CrimeClassifier.cpp:190-198 — equal top scores keep first QMap key, not semantic priority.
        CrimeClassifier clf;
        const auto [type, conf] = clf.classify(QStringLiteral("victim"));
        QCOMPARE(type, QStringLiteral("assault"));
        QVERIFY(conf > 0.0);
    }

    void testThreatSignalRejectsSentimentExactlyMinus05()
    {
        CrimeClassifier clf;
        const QString text = QStringLiteral("threatened to kill the victim");
        QVERIFY(!clf.threatSignal(text, -0.5));
        const double sent = clf.sentiment(text);
        QVERIFY2(sent < -0.5,
                 qPrintable(QStringLiteral("Expected sentiment < -0.5, got %1").arg(sent)));
        QVERIFY(clf.threatSignal(text, sent));
    }

    void testSeverityUnknownCrimeTypeUsesDefaultBase()
    {
        CrimeClassifier clf;
        const double raw = clf.severityScore(
            QStringLiteral("minor incident"), QStringLiteral("nonexistent_type"));
        QCOMPARE(raw, 0.3);
    }

    void testPunctuationStrippedBeforeKeywordMatch()
    {
        CrimeClassifier clf;
        const auto [type, conf] = clf.classify(
            QStringLiteral("armed robbery, weapon demanded."));
        QCOMPARE(type, QStringLiteral("robbery"));
        QVERIFY(conf > 0.0);
    }

    void testHyphenatedBreakInMatchesVehicleCrime()
    {
        CrimeClassifier clf;
        const auto [type, conf] = clf.classify(
            QStringLiteral("vehicle break-in window smashed"));
        QCOMPARE(type, QStringLiteral("vehicle_crime"));
        QVERIFY(conf > 0.0);
    }

    void testSexualOffenceBeatsAssaultOnSharedKeyword()
    {
        CrimeClassifier clf;
        const auto [type, conf] = clf.classify(
            QStringLiteral("sexual assault indecent exposure"));
        QCOMPARE(type, QStringLiteral("sexual_offence"));
        QVERIFY(conf > 0.0);
    }

    void testSentimentZeroWhenNoPolarityTokens()
    {
        CrimeClassifier clf;
        const double sent = clf.sentiment(
            QStringLiteral("the investigation continues at the station"));
        QCOMPARE(sent, 0.0);
    }

    void testSingleKeywordConfidenceIsOne()
    {
        CrimeClassifier clf;
        const auto [type, conf] = clf.classify(QStringLiteral("burglary"));
        QCOMPARE(type, QStringLiteral("burglary"));
        QVERIFY2(std::abs(conf - 1.0) < 1e-9,
                 qPrintable(QStringLiteral("Single-category hit confidence %1 expected 1.0")
                                .arg(conf)));
    }
};

QTEST_GUILESS_MAIN(CrimeClassifierDeep4Test)
#include "test_crime_classifier_deep4.moc"
