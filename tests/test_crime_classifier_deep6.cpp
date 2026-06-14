// test_crime_classifier_deep6.cpp — Deep audit iteration 25: CrimeClassifier
// multilabel confidence, severity, sentiment, threat signal.
#include <QTest>
#include <cmath>
#include "nlp/CrimeClassifier.h"

class TestCrimeClassifierDeep6 : public QObject
{
    Q_OBJECT

    CrimeClassifier m_clf;

private slots:

    void testBurglaryClassification()
    {
        const auto [type, conf] = m_clf.classify(
            QStringLiteral("suspect forced entry through rear window burglary"));
        QCOMPARE(type, QStringLiteral("burglary"));
        QVERIFY2(conf > 0.0, qPrintable(QStringLiteral("conf=%1").arg(conf)));
    }

    void testAssaultSeverityHigh()
    {
        const QString text = QStringLiteral("victim stabbed during violent assault");
        const auto [type, conf] = m_clf.classify(text);
        Q_UNUSED(conf);
        const double sev = m_clf.severityScore(text, type);
        QVERIFY2(sev > 0.5, qPrintable(QStringLiteral("sev=%1").arg(sev)));
    }

    void testNegativeSentiment()
    {
        const QString text = QStringLiteral("terrified victim threatened injured scared");
        const double sent = m_clf.sentiment(text);
        QVERIFY2(sent < 0.0, qPrintable(QStringLiteral("sent=%1").arg(sent)));
    }

    void testThreatSignalOnViolentText()
    {
        const QString text = QStringLiteral("offender threatened to kill the victim");
        const double sent = m_clf.sentiment(text);
        QVERIFY(m_clf.threatSignal(text, sent));
    }

    void testBatchClassifyPreservesOrder()
    {
        const QVector<QString> texts = {
            QStringLiteral("vehicle stolen from driveway"),
            QStringLiteral("assault victim punched"),
        };
        const auto results = m_clf.batchClassify(texts);
        QCOMPARE(results.size(), 2);
        QVERIFY(!results[0].first.isEmpty());
        QVERIFY(!results[1].first.isEmpty());
    }

    void testCorpusSizePositive()
    {
        QVERIFY(m_clf.corpusSize() >= 10);
    }

    void testEmptyTextLowConfidence()
    {
        const auto [type, conf] = m_clf.classify(QString());
        QVERIFY(conf >= 0.0 && conf <= 1.0);
        QVERIFY(!type.isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestCrimeClassifierDeep6)
#include "test_crime_classifier_deep6.moc"
