// test_crime_classifier_deep2.cpp — Second-pass deep tests for CrimeClassifier
#include <QTest>
#include "nlp/CrimeClassifier.h"
#include <cmath>

class TestCrimeClassifierDeep2 : public QObject
{
    Q_OBJECT

private slots:

    // ── 1. classify() returns confidence in [0, 1] ────────────────────────────
    void testClassifyConfidenceRange()
    {
        CrimeClassifier clf;
        const QStringList texts = {
            QStringLiteral("man was stabbed and attacked violently"),
            QStringLiteral("car window smashed vehicle stolen"),
            QStringLiteral("cannabis drugs dealing possession"),
            QStringLiteral("unknown gibberish zyxwv"),
        };
        for (const QString& text : texts) {
            auto [type, conf] = clf.classify(text);
            QVERIFY2(conf >= 0.0 && conf <= 1.0,
                     qPrintable(QStringLiteral("confidence %1 for '%2' must be in [0,1]")
                        .arg(conf).arg(text)));
        }
    }

    // ── 2. classify() on empty string doesn't crash ───────────────────────────
    void testClassifyEmptyString()
    {
        CrimeClassifier clf;
        auto [type, conf] = clf.classify(QString());
        // Must not crash; confidence should be 0 for unknown
        QVERIFY(conf >= 0.0 && conf <= 1.0);
        // Type should be "unknown" when no keywords match
        QCOMPARE(type, QStringLiteral("unknown"));
    }

    // ── 3. batchClassify(N) returns exactly N results ─────────────────────────
    void testBatchClassifyCount()
    {
        CrimeClassifier clf;
        const QVector<QString> texts = {
            QStringLiteral("robbery mugged demanded"),
            QStringLiteral("burglary broke entry forced"),
            QStringLiteral("knife weapon armed blade"),
            QStringLiteral("fraud scam phishing identity"),
            QStringLiteral(""),
        };
        const auto results = clf.batchClassify(texts);
        QCOMPARE(results.size(), texts.size());
    }

    // ── 4. batchClassify results match individual classify() calls ────────────
    void testBatchClassifyMatchesIndividual()
    {
        CrimeClassifier clf;
        const QVector<QString> texts = {
            QStringLiteral("murder killed homicide dead victim"),
            QStringLiteral("assault attack punch hit"),
            QStringLiteral("theft stolen pickpocket shoplifting"),
        };
        const auto batchResults = clf.batchClassify(texts);
        QCOMPARE(batchResults.size(), texts.size());
        for (int i = 0; i < texts.size(); ++i) {
            auto [type, conf] = clf.classify(texts[i]);
            QCOMPARE(batchResults[i].first,  type);
            QVERIFY(std::abs(batchResults[i].second - conf) < 1e-12);
        }
    }

    // ── 5. Severity maps to [1, 10] scale for known violent crime ─────────────
    void testSeverityRangeViolentCrime()
    {
        CrimeClassifier clf;
        const QString text  = QStringLiteral("murder killed dead gun shot armed");
        const QString ctype = QStringLiteral("murder");

        const double rawScore = clf.severityScore(text, ctype);
        // rawScore is [0,1]; map to [1,10]: severity = round(rawScore * 9) + 1
        const int severity = static_cast<int>(std::round(rawScore * 9.0)) + 1;

        QVERIFY2(severity >= 1 && severity <= 10,
                 qPrintable(QStringLiteral("severity %1 (raw %2) must be in [1,10]")
                    .arg(severity).arg(rawScore)));
        // Murder with boost words should be near the top of the scale
        QVERIFY2(severity >= 9,
                 qPrintable(QStringLiteral("murder severity %1 should be >= 9").arg(severity)));
    }

    // ── 6. corpusSize() > 0 after construction ────────────────────────────────
    void testCorpusSizePositive()
    {
        CrimeClassifier clf;
        const int size = clf.corpusSize();
        QVERIFY2(size > 0,
                 qPrintable(QStringLiteral("corpusSize() %1 should be > 0").arg(size)));
        // Should have at least all standard crime categories
        QVERIFY2(size >= 10,
                 qPrintable(QStringLiteral("corpusSize() %1 should be >= 10 categories").arg(size)));
    }
};

QTEST_GUILESS_MAIN(TestCrimeClassifierDeep2)
#include "test_crime_classifier_deep2.moc"
