// test_provenance_log_deep3.cpp — Iteration 12 deep audit: append-only log
// behaviour, entry field population, and JSON export.
#include <QTest>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QDateTime>

#include "audit/ProvenanceLog.h"

class TestProvenanceLogDeep3 : public QObject
{
    Q_OBJECT

private slots:
    void testAppendOnlyPreservesEarlierEntries();
    void testRecentActsAsSlidingWindow();
    void testRecentReturnsNewestFirst();
    void testLargeAppendDoesNotEvictEntries();
    void testRecordPopulatesAllFields();
    void testAddEntryPopulatesAllFields();
    void testExportToJsonContainsCorrectFields();
    void testExportToJsonParsesAsValidArray();
    void testExportToJsonEmptyLog();
};

void TestProvenanceLogDeep3::testAppendOnlyPreservesEarlierEntries()
{
    ProvenanceLog log;
    const QDateTime t1 = QDateTime(QDate(2024, 3, 1), QTime(9, 0), Qt::UTC);
    const QDateTime t2 = QDateTime(QDate(2024, 3, 1), QTime(10, 0), Qt::UTC);

    log.addEntry("CSV", "Poisson", "fit", "first entry",  t1);
    log.addEntry("API", "Hawkes",  "fit", "second entry", t2);

    const auto csvEntries = log.filterBySource("CSV");
    QCOMPARE(csvEntries.size(), 1);
    QCOMPARE(csvEntries[0].detail, QStringLiteral("first entry"));
    QCOMPARE(csvEntries[0].timestamp, t1);

    // Appending more must not mutate the first entry
    log.addEntry("CSV", "Poisson", "predict", "third entry", t2.addSecs(3600));
    const auto csvAfter = log.filterBySource("CSV");
    QCOMPARE(csvAfter.size(), 2);
    QCOMPARE(csvAfter[0].detail, QStringLiteral("first entry"));
    QCOMPARE(log.count(), 3);
}

void TestProvenanceLogDeep3::testRecentActsAsSlidingWindow()
{
    ProvenanceLog log;
    for (int i = 0; i < 20; ++i) {
        log.addEntry("src", "model",
                     QStringLiteral("action_%1").arg(i),
                     QStringLiteral("detail_%1").arg(i),
                     QDateTime(QDate(2024, 1, 1), QTime(i % 24, 0), Qt::UTC));
    }

    const auto window = log.recent(5);
    QCOMPARE(window.size(), 5);

    // Full log retains all 20 — append-only, no eviction
    QCOMPARE(log.count(), 20);
    QCOMPARE(log.filterBySource("src").size(), 20);
}

void TestProvenanceLogDeep3::testRecentReturnsNewestFirst()
{
    ProvenanceLog log;
    log.addEntry("src", "m", "first",  "d1",
                 QDateTime(QDate(2024, 6, 1), QTime(8, 0), Qt::UTC));
    log.addEntry("src", "m", "second", "d2",
                 QDateTime(QDate(2024, 6, 1), QTime(9, 0), Qt::UTC));
    log.addEntry("src", "m", "third",  "d3",
                 QDateTime(QDate(2024, 6, 1), QTime(10, 0), Qt::UTC));

    const auto recent2 = log.recent(2);
    QCOMPARE(recent2.size(), 2);
    QCOMPARE(recent2[0].action, QStringLiteral("third"));
    QCOMPARE(recent2[1].action, QStringLiteral("second"));
}

void TestProvenanceLogDeep3::testLargeAppendDoesNotEvictEntries()
{
    ProvenanceLog log;
    constexpr int N = 1500;
    for (int i = 0; i < N; ++i)
        log.record(QStringLiteral("EV-%1").arg(i), "ingest", "load",
                   QStringLiteral("detail %1").arg(i));

    QCOMPARE(log.count(), N);
    QCOMPARE(log.chain(QStringLiteral("EV-0")).size(), 1);
    QCOMPARE(log.chain(QStringLiteral("EV-999")).size(), 1);
}

void TestProvenanceLogDeep3::testRecordPopulatesAllFields()
{
    ProvenanceLog log;
    const QDateTime before = QDateTime::currentDateTimeUtc();
    log.record(QStringLiteral("EVT-REC"), QStringLiteral("nlp"),
               QStringLiteral("classify"), QStringLiteral("burglary 0.91"),
               QStringLiteral("aabbccdd"));
    const QDateTime after = QDateTime::currentDateTimeUtc();

    const auto chain = log.chain(QStringLiteral("EVT-REC"));
    QCOMPARE(chain.size(), 1);

    const ProvenanceEntry& e = chain[0];
    QCOMPARE(e.eventId,  QStringLiteral("EVT-REC"));
    QCOMPARE(e.stage,    QStringLiteral("nlp"));
    QCOMPARE(e.action,   QStringLiteral("classify"));
    QCOMPARE(e.detail,   QStringLiteral("burglary 0.91"));
    QCOMPARE(e.dataHash, QStringLiteral("aabbccdd"));
    QVERIFY(e.timestamp >= before);
    QVERIFY(e.timestamp <= after);
}

void TestProvenanceLogDeep3::testAddEntryPopulatesAllFields()
{
    ProvenanceLog log;
    const QDateTime ts = QDateTime(QDate(2024, 7, 4), QTime(14, 30), Qt::UTC);
    log.addEntry(QStringLiteral("UKPolice"), QStringLiteral("CSVImporter"),
                 QStringLiteral("ingest"), QStringLiteral("loaded 42 rows"), ts);

    const auto entries = log.filterByModel(QStringLiteral("CSVImporter"));
    QCOMPARE(entries.size(), 1);

    const ProvenanceEntry& e = entries[0];
    QCOMPARE(e.source,    QStringLiteral("UKPolice"));
    QCOMPARE(e.model,     QStringLiteral("CSVImporter"));
    QCOMPARE(e.action,    QStringLiteral("ingest"));
    QCOMPARE(e.detail,    QStringLiteral("loaded 42 rows"));
    QCOMPARE(e.timestamp, ts);
}

void TestProvenanceLogDeep3::testExportToJsonContainsCorrectFields()
{
    ProvenanceLog log;
    const QDateTime ts = QDateTime(QDate(2024, 5, 20), QTime(16, 0), Qt::UTC);
    log.addEntry(QStringLiteral("Socrata"), QStringLiteral("RiskForecaster"),
                 QStringLiteral("predict"), QStringLiteral("zone risk computed"), ts);

    const QJsonDocument doc = QJsonDocument::fromJson(log.exportToJson().toUtf8());
    QVERIFY(doc.isArray());
    QCOMPARE(doc.array().size(), 1);

    const QJsonObject obj = doc.array().first().toObject();
    QCOMPARE(obj[QStringLiteral("source")].toString(), QStringLiteral("Socrata"));
    QCOMPARE(obj[QStringLiteral("model")].toString(),  QStringLiteral("RiskForecaster"));
    QCOMPARE(obj[QStringLiteral("action")].toString(), QStringLiteral("predict"));
    QCOMPARE(obj[QStringLiteral("detail")].toString(), QStringLiteral("zone risk computed"));
    QCOMPARE(obj[QStringLiteral("timestamp")].toString(),
             ts.toString(Qt::ISODate));
}

void TestProvenanceLogDeep3::testExportToJsonParsesAsValidArray()
{
    ProvenanceLog log;
    log.record(QStringLiteral("E1"), QStringLiteral("ingest"), QStringLiteral("import"), QStringLiteral("a"));
    log.record(QStringLiteral("E2"), QStringLiteral("model"),  QStringLiteral("fit"),    QStringLiteral("b"));
    log.addEntry(QStringLiteral("src"), QStringLiteral("m"), QStringLiteral("export"), QStringLiteral("c"));

    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(log.exportToJson().toUtf8(), &err);
    QCOMPARE(err.error, QJsonParseError::NoError);
    QVERIFY(doc.isArray());
    QCOMPARE(doc.array().size(), 3);
}

void TestProvenanceLogDeep3::testExportToJsonEmptyLog()
{
    ProvenanceLog log;
    const QJsonDocument doc = QJsonDocument::fromJson(log.exportToJson().toUtf8());
    QVERIFY(doc.isArray());
    QCOMPARE(doc.array().size(), 0);
}

QTEST_GUILESS_MAIN(TestProvenanceLogDeep3)
#include "test_provenance_log_deep3.moc"
