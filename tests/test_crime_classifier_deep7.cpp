// test_crime_classifier_deep7.cpp — Deep audit iteration 29: CrimeClassifier
// corpus size, vehicle theft, positive sentiment, severity baseline.
#include <QTest>
#include "nlp/CrimeClassifier.h"

class TestCrimeClassifierDeep7 : public QObject
{
    Q_OBJECT

    CrimeClassifier m_clf;

private slots:

    void testCorpusSizePositive()
    {
        QVERIFY(m_clf.corpusSize() > 5);
    }

    void testVehicleTheftClassification()
    {
        const auto [type, conf] = m_clf.classify(
            QStringLiteral("car stolen from driveway vehicle theft"));
        QCOMPARE(type, QStringLiteral("theft"));
        QVERIFY(conf > 0.0);
    }

    void testPositiveSentimentOnBenignText()
    {
        const QString text = QStringLiteral("peaceful safe community helpful officers");
        const double sent = m_clf.sentiment(text);
        QVERIFY(sent > 0.0);
    }

    void testRobberySeverityHigherThanTheft()
    {
        const QString robText = QStringLiteral("armed robbery at bank");
        const QString theftText = QStringLiteral("bicycle stolen from rack");
        const auto [robType, _] = m_clf.classify(robText);
        const auto [theftType, __] = m_clf.classify(theftText);
        QVERIFY(m_clf.severityScore(robText, robType)
                > m_clf.severityScore(theftText, theftType));
    }

    void testBatchClassifySameLength()
    {
        const QVector<QString> texts = {
            QStringLiteral("burglary forced entry"),
            QStringLiteral("assault victim punched"),
            QStringLiteral("arson building fire"),
        };
        const auto results = m_clf.batchClassify(texts);
        QCOMPARE(results.size(), texts.size());
    }
};

QTEST_GUILESS_MAIN(TestCrimeClassifierDeep7)
#include "test_crime_classifier_deep7.moc"
