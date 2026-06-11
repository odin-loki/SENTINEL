// test_provenance_log_deep5.cpp — Iteration 19 deep audit: CSV/JSON export,
// filter APIs, getEntries ordering, addEntry timestamps, and HTML escaping.
#include <QTest>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QDateTime>

#include "audit/ProvenanceLog.h"

class TestProvenanceLogDeep5 : public QObject
{
    Q_OBJECT

private slots:
    void testExportToCsvEscapesCommaQuoteAndNewline();
    void testExportToJsonOmitsEmptyOptionalFields();
    void testFilterByStageModelAndSourceIsolation();
    void testFilterByTimeRangeInclusiveBoundaries();
    void testGetEntriesReturnsNewestFirst();
    void testRecentZeroReturnsEmptyRecentLargeReturnsAll();
    void testAddEntryInvalidTimestampUsesCurrentUtc();
    void testFormatHtmlEscapesDetailAndReturnsEmptyForUnknown();
};

void TestProvenanceLogDeep5::testExportToCsvEscapesCommaQuoteAndNewline()
{
    ProvenanceLog log;
    log.record(QStringLiteral("DEEP5-CSV"), QStringLiteral("ingest"),
               QStringLiteral("load,init"), QStringLiteral("row \"1\"\nline2"),
               QStringLiteral("hash\"x"));

    const QString csv = log.exportToCsv();
    QVERIFY(csv.startsWith(QStringLiteral("timestamp,eventId,stage,source,model,action,detail,dataHash\n")));
    QVERIFY2(csv.contains(QStringLiteral("\"load,init\"")),
             "Comma in action must be CSV-quoted");
    QVERIFY2(csv.contains(QStringLiteral("\"row \"\"1\"\"\nline2\"")),
             "Quotes and newline in detail must be escaped");
    QVERIFY2(csv.contains(QStringLiteral("\"hash\"\"x\"")),
             "Quote in dataHash must be doubled");
    // Embedded newline in detail is inside a quoted field — do not count physical line breaks as rows.
    QVERIFY(csv.trimmed().endsWith(QStringLiteral("\"hash\"\"x\"")));
}

void TestProvenanceLogDeep5::testExportToJsonOmitsEmptyOptionalFields()
{
    ProvenanceLog log;
    log.record(QStringLiteral("DEEP5-JSON"), QStringLiteral("model"),
               QStringLiteral("fit"), QStringLiteral("trained"),
               QStringLiteral("abc123"));

    const QJsonDocument doc = QJsonDocument::fromJson(log.exportToJson().toUtf8());
    QVERIFY(doc.isArray());
    const QJsonObject obj = doc.array().first().toObject();

    QCOMPARE(obj[QStringLiteral("eventId")].toString(), QStringLiteral("DEEP5-JSON"));
    QCOMPARE(obj[QStringLiteral("stage")].toString(),    QStringLiteral("model"));
    QCOMPARE(obj[QStringLiteral("action")].toString(),   QStringLiteral("fit"));
    QCOMPARE(obj[QStringLiteral("detail")].toString(),   QStringLiteral("trained"));
    QCOMPARE(obj[QStringLiteral("dataHash")].toString(), QStringLiteral("abc123"));
    QVERIFY(!obj.contains(QStringLiteral("source")));
    QVERIFY(!obj.contains(QStringLiteral("model")));

    log.addEntry(QStringLiteral("uk_police"), QStringLiteral("Poisson"),
                 QStringLiteral("predict"), QStringLiteral("zone forecast"),
                 QDateTime(QDate(2024, 7, 1), QTime(12, 0), Qt::UTC));

    const QJsonObject auditObj = QJsonDocument::fromJson(log.exportToJson().toUtf8())
                                     .array().last().toObject();
    QCOMPARE(auditObj[QStringLiteral("source")].toString(), QStringLiteral("uk_police"));
    QCOMPARE(auditObj[QStringLiteral("model")].toString(),  QStringLiteral("Poisson"));
    QVERIFY(!auditObj.contains(QStringLiteral("eventId")));
    QVERIFY(!auditObj.contains(QStringLiteral("stage")));
}

void TestProvenanceLogDeep5::testFilterByStageModelAndSourceIsolation()
{
    ProvenanceLog log;
    log.record(QStringLiteral("A"), QStringLiteral("ingest"), QStringLiteral("load"),
               QStringLiteral("csv"), QStringLiteral("h1"));
    log.record(QStringLiteral("B"), QStringLiteral("model"), QStringLiteral("fit"),
               QStringLiteral("poisson"), QStringLiteral("h2"));
    log.addEntry(QStringLiteral("api"), QStringLiteral("Hawkes"),
                 QStringLiteral("fit"), QStringLiteral("hawkes fit"),
                 QDateTime(QDate(2024, 5, 1), QTime(8, 0), Qt::UTC));
    log.addEntry(QStringLiteral("csv"), QStringLiteral("Poisson"),
                 QStringLiteral("predict"), QStringLiteral("csv predict"),
                 QDateTime(QDate(2024, 5, 2), QTime(9, 0), Qt::UTC));

    QCOMPARE(log.filterByStage(QStringLiteral("ingest")).size(), 1);
    QCOMPARE(log.filterByStage(QStringLiteral("model")).size(), 1);
    QCOMPARE(log.filterByModel(QStringLiteral("Hawkes")).size(), 1);
    QCOMPARE(log.filterByModel(QStringLiteral("Poisson")).size(), 1);
    QCOMPARE(log.filterBySource(QStringLiteral("csv")).size(), 1);
    QCOMPARE(log.filterBySource(QStringLiteral("api")).size(), 1);
    QVERIFY(log.filterByStage(QStringLiteral("output")).isEmpty());
}

void TestProvenanceLogDeep5::testFilterByTimeRangeInclusiveBoundaries()
{
    ProvenanceLog log;
    const QDateTime t1 = QDateTime(QDate(2024, 4, 10), QTime(10, 0), Qt::UTC);
    const QDateTime t2 = QDateTime(QDate(2024, 4, 10), QTime(12, 0), Qt::UTC);
    const QDateTime t3 = QDateTime(QDate(2024, 4, 10), QTime(14, 0), Qt::UTC);

    log.addEntry(QStringLiteral("s"), QStringLiteral("m"), QStringLiteral("a1"),
                 QStringLiteral("d1"), t1);
    log.addEntry(QStringLiteral("s"), QStringLiteral("m"), QStringLiteral("a2"),
                 QStringLiteral("d2"), t2);
    log.addEntry(QStringLiteral("s"), QStringLiteral("m"), QStringLiteral("a3"),
                 QStringLiteral("d3"), t3);

    const auto window = log.filterByTimeRange(t1, t2);
    QCOMPARE(window.size(), 2);
    QCOMPARE(window[0].timestamp, t1);
    QCOMPARE(window[1].timestamp, t2);

    QCOMPARE(log.filterByTimeRange(t2, t2).size(), 1);
    QVERIFY(log.filterByTimeRange(t3.addSecs(1), t3.addDays(1)).isEmpty());
}

void TestProvenanceLogDeep5::testGetEntriesReturnsNewestFirst()
{
    ProvenanceLog log;
    log.addEntry(QStringLiteral("s"), QStringLiteral("m"), QStringLiteral("first"),
                 QStringLiteral("d1"),
                 QDateTime(QDate(2024, 1, 1), QTime(8, 0), Qt::UTC));
    log.addEntry(QStringLiteral("s"), QStringLiteral("m"), QStringLiteral("second"),
                 QStringLiteral("d2"),
                 QDateTime(QDate(2024, 1, 1), QTime(9, 0), Qt::UTC));
    log.addEntry(QStringLiteral("s"), QStringLiteral("m"), QStringLiteral("third"),
                 QStringLiteral("d3"),
                 QDateTime(QDate(2024, 1, 1), QTime(10, 0), Qt::UTC));

    const auto entries = log.getEntries();
    QCOMPARE(entries.size(), 3);
    QCOMPARE(entries[0].action, QStringLiteral("third"));
    QCOMPARE(entries[2].action, QStringLiteral("first"));
}

void TestProvenanceLogDeep5::testRecentZeroReturnsEmptyRecentLargeReturnsAll()
{
    ProvenanceLog log;
    for (int i = 0; i < 4; ++i)
        log.record(QStringLiteral("DEEP5-REC"), QStringLiteral("ingest"),
                   QStringLiteral("a%1").arg(i), QStringLiteral("d%1").arg(i));

    QCOMPARE(log.recent(0).size(), 0);

    const auto all = log.recent(100);
    QCOMPARE(all.size(), 4);
    QCOMPARE(all[0].action, QStringLiteral("a3"));
    QCOMPARE(all[3].action, QStringLiteral("a0"));
}

void TestProvenanceLogDeep5::testAddEntryInvalidTimestampUsesCurrentUtc()
{
    ProvenanceLog log;
    const QDateTime before = QDateTime::currentDateTimeUtc();
    log.addEntry(QStringLiteral("src"), QStringLiteral("model"),
                 QStringLiteral("auto_ts"), QStringLiteral("no timestamp"),
                 QDateTime());
    const QDateTime after = QDateTime::currentDateTimeUtc();

    const auto entries = log.getEntries();
    QCOMPARE(entries.size(), 1);
    QVERIFY(entries[0].timestamp.isValid());
    QVERIFY(entries[0].timestamp >= before);
    QVERIFY(entries[0].timestamp <= after);
}

void TestProvenanceLogDeep5::testFormatHtmlEscapesDetailAndReturnsEmptyForUnknown()
{
    ProvenanceLog log;
    log.record(QStringLiteral("DEEP5-HTML"), QStringLiteral("output"),
               QStringLiteral("export"), QStringLiteral("<script>alert(1)</script>"),
               QStringLiteral("hash&val"));

    const QString html = log.formatHtml(QStringLiteral("DEEP5-HTML"));
    QVERIFY(html.startsWith(QStringLiteral("<!DOCTYPE html>")));
    QVERIFY(html.contains(QStringLiteral("&lt;script&gt;")));
    QVERIFY(!html.contains(QStringLiteral("<script>alert")));
    QVERIFY(html.contains(QStringLiteral("hash&amp;val")));
    QVERIFY(log.formatHtml(QStringLiteral("NO-SUCH-EVENT")).isEmpty());
}

QTEST_GUILESS_MAIN(TestProvenanceLogDeep5)
#include "test_provenance_log_deep5.moc"
