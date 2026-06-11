// Deep audit iteration 14 — CrimeClassifier keyword scoring, severity 1–10, batch consistency
#include <QTest>
#include <cmath>
#include "nlp/CrimeClassifier.h"

class CrimeClassifierDeep3Test : public QObject
{
    Q_OBJECT

    static int severity1to10(double rawScore)
    {
        return static_cast<int>(std::round(rawScore * 9.0)) + 1;
    }

private slots:

    void testClassifyMurderKeywords()
    {
        CrimeClassifier clf;
        const auto [type, conf] = clf.classify(
            QStringLiteral("victim murdered homicide dead body fatal"));
        QCOMPARE(type, QStringLiteral("murder"));
        QVERIFY2(conf > 0.5,
                 qPrintable(QStringLiteral("Murder confidence %1 expected > 0.5").arg(conf)));
    }

    void testClassifyBurglaryKeywords()
    {
        CrimeClassifier clf;
        const auto [type, conf] = clf.classify(
            QStringLiteral("burglar forced entry house ransacked stolen"));
        QCOMPARE(type, QStringLiteral("burglary"));
        QVERIFY(conf > 0.0);
    }

    void testClassifyUnknownWhenNoKeywords()
    {
        CrimeClassifier clf;
        const auto [type, conf] = clf.classify(
            QStringLiteral("xyzzy plugh lorem ipsum delta"));
        QCOMPARE(type, QStringLiteral("unknown"));
        QCOMPARE(conf, 0.0);
    }

    void testSeverityMurderMapsToNineOrTen()
    {
        CrimeClassifier clf;
        const QString text = QStringLiteral("murder killed dead gun shot armed");
        const double raw = clf.severityScore(text, QStringLiteral("murder"));
        const int sev = severity1to10(raw);
        QVERIFY2(sev >= 9 && sev <= 10,
                 qPrintable(QStringLiteral("Murder severity %1 (raw %2) expected 9–10")
                                .arg(sev).arg(raw)));
    }

    void testSeverityTheftMapsLowOn1to10Scale()
    {
        CrimeClassifier clf;
        const QString text = QStringLiteral("bicycle stolen from outside shop");
        const double raw = clf.severityScore(text, QStringLiteral("theft"));
        const int sev = severity1to10(raw);
        QVERIFY2(sev >= 1 && sev <= 5,
                 qPrintable(QStringLiteral("Theft severity %1 expected <= 5").arg(sev)));
    }

    void testSeverityRawScoreInUnitInterval()
    {
        CrimeClassifier clf;
        const QString text = QStringLiteral("assault attack punch victim injured");
        const double raw = clf.severityScore(text, QStringLiteral("assault"));
        QVERIFY2(raw >= 0.0 && raw <= 1.0,
                 qPrintable(QStringLiteral("Raw severity %1 must be in [0,1]").arg(raw)));
    }

    void testSentimentNegativeForViolentText()
    {
        CrimeClassifier clf;
        const double sent = clf.sentiment(
            QStringLiteral("violent attack murder stab victim injured criminal"));
        QVERIFY2(sent < 0.0,
                 qPrintable(QStringLiteral("Violent sentiment %1 should be negative").arg(sent)));
        QVERIFY2(sent >= -1.0 && sent <= 1.0,
                 "Sentiment must stay in [-1, 1]");
    }

    void testSentimentPositiveWhenResolvedLanguage()
    {
        CrimeClassifier clf;
        const double sent = clf.sentiment(
            QStringLiteral("police arrested suspect case resolved victim safe"));
        QVERIFY2(sent > 0.0,
                 qPrintable(QStringLiteral("Positive outcome sentiment %1 should be > 0")
                                .arg(sent)));
    }

    void testThreatSignalStrictSentimentThreshold()
    {
        CrimeClassifier clf;
        const QString text = QStringLiteral("suspect threatened to kill victim");
        QVERIFY(!clf.threatSignal(text, -0.5));
        const double sent = clf.sentiment(text);
        QVERIFY(clf.threatSignal(text, sent));
    }

    void testBatchClassifyMatchesIndividual()
    {
        CrimeClassifier clf;
        const QVector<QString> texts = {
            QStringLiteral("robbery mugged weapon demanded"),
            QStringLiteral("burglary broke entry forced house"),
            QStringLiteral("cannabis drugs dealing possession"),
            QStringLiteral(""),
            QStringLiteral("knife gun armed weapon blade"),
        };

        const auto batch = clf.batchClassify(texts);
        QCOMPARE(batch.size(), texts.size());

        for (int i = 0; i < texts.size(); ++i) {
            const auto single = clf.classify(texts[i]);
            QCOMPARE(batch[i].first, single.first);
            QVERIFY(std::abs(batch[i].second - single.second) < 1e-12);
        }
    }

    void testBatchClassifyEmptyInput()
    {
        CrimeClassifier clf;
        const auto batch = clf.batchClassify({});
        QVERIFY(batch.isEmpty());
    }

    void testCorpusSizeMatchesCategoryCount()
    {
        CrimeClassifier clf;
        QVERIFY(clf.corpusSize() >= 12);
    }
};

QTEST_GUILESS_MAIN(CrimeClassifierDeep3Test)
#include "test_crime_classifier_deep3.moc"
