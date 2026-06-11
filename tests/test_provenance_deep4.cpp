// test_provenance_deep4.cpp — Iteration 16 deep audit: audit provenance in
// src/audit/ (ProvenanceLog) and DataExporter::provenanceToJson integration.
// Note: no ProvenanceTracker class exists; audit trail is ProvenanceLog-based.
#include <QTest>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QDateTime>

#include "audit/ProvenanceLog.h"
#include "core/DataExporter.h"

class TestProvenanceDeep4 : public QObject
{
    Q_OBJECT

private slots:
    void testAuditPipelineFullChain();
    void testProvenanceToJsonIncludesAllRecordFields();
    void testProvenanceToJsonPreservesChainOrder();
    void testExportToJsonIncludesRecordFields();
    void testChainAfterClearIsEmpty();
    void testFilterByStageAcrossMultipleEvents();
    void testMixedRecordAndAddEntryIndependent();
    void testProvenanceToJsonEmptyChain();
};

void TestProvenanceDeep4::testAuditPipelineFullChain()
{
    ProvenanceLog log;
    const QString eventId = QStringLiteral("DEEP4-PIPE");

    log.record(eventId, QStringLiteral("ingest"),    QStringLiteral("import"),
               QStringLiteral("CSV 42 rows"),       QStringLiteral("ingest01"));
    log.record(eventId, QStringLiteral("nlp"),       QStringLiteral("classify"),
               QStringLiteral("burglary 0.88"),     QStringLiteral("nlp02"));
    log.record(eventId, QStringLiteral("model"),     QStringLiteral("fit"),
               QStringLiteral("Poisson lambda=1.2"), QStringLiteral("model03"));
    log.record(eventId, QStringLiteral("inference"), QStringLiteral("score"),
               QStringLiteral("risk=0.71"),          QStringLiteral("inf04"));
    log.record(eventId, QStringLiteral("output"),    QStringLiteral("export"),
               QStringLiteral("lead generated"),     QStringLiteral("out05"));

    const auto chain = log.chain(eventId);
    QCOMPARE(chain.size(), 5);

    const QStringList expectedStages = {
        QStringLiteral("ingest"), QStringLiteral("nlp"), QStringLiteral("model"),
        QStringLiteral("inference"), QStringLiteral("output")
    };
    for (int i = 0; i < expectedStages.size(); ++i)
        QCOMPARE(chain[i].stage, expectedStages[i]);

    const QString formatted = log.formatChain(eventId);
    QVERIFY(formatted.contains(QStringLiteral("ingest01")));
    QVERIFY(formatted.contains(QStringLiteral("out05")));
}

void TestProvenanceDeep4::testProvenanceToJsonIncludesAllRecordFields()
{
    ProvenanceLog log;
    log.record(QStringLiteral("DEEP4-JSON"), QStringLiteral("model"),
               QStringLiteral("predict"), QStringLiteral("zone A"),
               QStringLiteral("deadbeef"));

    const QJsonArray arr = DataExporter::provenanceToJson(log.chain(QStringLiteral("DEEP4-JSON")));
    QCOMPARE(arr.size(), 1);

    const QJsonObject obj = arr[0].toObject();
    QCOMPARE(obj[QStringLiteral("eventId")].toString(),  QStringLiteral("DEEP4-JSON"));
    QCOMPARE(obj[QStringLiteral("stage")].toString(),    QStringLiteral("model"));
    QCOMPARE(obj[QStringLiteral("action")].toString(),   QStringLiteral("predict"));
    QCOMPARE(obj[QStringLiteral("detail")].toString(),   QStringLiteral("zone A"));
    QCOMPARE(obj[QStringLiteral("dataHash")].toString(), QStringLiteral("deadbeef"));

    const QDateTime ts = QDateTime::fromString(
        obj[QStringLiteral("timestamp")].toString(), Qt::ISODate);
    QVERIFY2(ts.isValid(), qPrintable(obj[QStringLiteral("timestamp")].toString()));
}

void TestProvenanceDeep4::testProvenanceToJsonPreservesChainOrder()
{
    ProvenanceLog log;
    const QString eventId = QStringLiteral("DEEP4-ORD");
    const QDateTime t1 = QDateTime(QDate(2024, 9, 1), QTime(8, 0), Qt::UTC);
    const QDateTime t2 = QDateTime(QDate(2024, 9, 1), QTime(9, 0), Qt::UTC);
    const QDateTime t3 = QDateTime(QDate(2024, 9, 1), QTime(10, 0), Qt::UTC);

    log.addEntry(QStringLiteral("src"), QStringLiteral("m"),
                 QStringLiteral("late"),  QStringLiteral("third"),  t3);
    log.record(eventId, QStringLiteral("ingest"), QStringLiteral("early"),
               QStringLiteral("first"), QStringLiteral("h1"));
    log.addEntry(QStringLiteral("src"), QStringLiteral("m"),
                 QStringLiteral("mid"),   QStringLiteral("second"), t2);

    // Only record()-based entries belong to eventId chain; addEntry has no eventId.
    log.record(eventId, QStringLiteral("model"), QStringLiteral("middle"),
               QStringLiteral("second rec"), QStringLiteral("h2"));

    const QJsonArray arr = DataExporter::provenanceToJson(log.chain(eventId));
    QCOMPARE(arr.size(), 2);
    QCOMPARE(arr[0].toObject()[QStringLiteral("action")].toString(),
             QStringLiteral("early"));
    QCOMPARE(arr[1].toObject()[QStringLiteral("action")].toString(),
             QStringLiteral("middle"));
    Q_UNUSED(t1);
}

void TestProvenanceDeep4::testExportToJsonIncludesRecordFields()
{
    ProvenanceLog log;
    log.record(QStringLiteral("DEEP4-BUG"), QStringLiteral("ingest"),
               QStringLiteral("import"), QStringLiteral("rows"), QStringLiteral("abc"));

    const QJsonDocument doc = QJsonDocument::fromJson(log.exportToJson().toUtf8());
    QVERIFY(doc.isArray());
    QCOMPARE(doc.array().size(), 1);

    const QJsonObject obj = doc.array().first().toObject();
    QCOMPARE(obj[QStringLiteral("eventId")].toString(),  QStringLiteral("DEEP4-BUG"));
    QCOMPARE(obj[QStringLiteral("stage")].toString(),    QStringLiteral("ingest"));
    QCOMPARE(obj[QStringLiteral("dataHash")].toString(), QStringLiteral("abc"));
    QCOMPARE(obj[QStringLiteral("action")].toString(),   QStringLiteral("import"));
}

void TestProvenanceDeep4::testChainAfterClearIsEmpty()
{
    ProvenanceLog log;
    log.record(QStringLiteral("DEEP4-CLR"), QStringLiteral("ingest"),
               QStringLiteral("load"), QStringLiteral("x"), QStringLiteral("h"));
    log.clear();

    QVERIFY(log.chain(QStringLiteral("DEEP4-CLR")).isEmpty());
    QVERIFY(DataExporter::provenanceToJson(log.chain(QStringLiteral("DEEP4-CLR"))).isEmpty());
    QVERIFY(log.formatHtml(QStringLiteral("DEEP4-CLR")).isEmpty());
}

void TestProvenanceDeep4::testFilterByStageAcrossMultipleEvents()
{
    ProvenanceLog log;
    log.record(QStringLiteral("DEEP4-E1"), QStringLiteral("ingest"), QStringLiteral("a"), QStringLiteral("d1"));
    log.record(QStringLiteral("DEEP4-E2"), QStringLiteral("model"),  QStringLiteral("b"), QStringLiteral("d2"));
    log.record(QStringLiteral("DEEP4-E1"), QStringLiteral("model"),  QStringLiteral("c"), QStringLiteral("d3"));
    log.record(QStringLiteral("DEEP4-E3"), QStringLiteral("ingest"), QStringLiteral("e"), QStringLiteral("d4"));

    const auto modelEntries = log.filterByStage(QStringLiteral("model"));
    QCOMPARE(modelEntries.size(), 2);
    for (const auto& e : modelEntries)
        QCOMPARE(e.stage, QStringLiteral("model"));

    const auto ingestEntries = log.filterByStage(QStringLiteral("ingest"));
    QCOMPARE(ingestEntries.size(), 2);
}

void TestProvenanceDeep4::testMixedRecordAndAddEntryIndependent()
{
    ProvenanceLog log;
    log.record(QStringLiteral("DEEP4-MIX"), QStringLiteral("ingest"),
               QStringLiteral("record_action"), QStringLiteral("rec detail"), QStringLiteral("rh"));
    log.addEntry(QStringLiteral("UKPolice"), QStringLiteral("CSVImporter"),
                 QStringLiteral("add_action"), QStringLiteral("add detail"));

    QCOMPARE(log.count(), 2);
    QCOMPARE(log.chain(QStringLiteral("DEEP4-MIX")).size(), 1);
    QCOMPARE(log.filterBySource(QStringLiteral("UKPolice")).size(), 1);
    QCOMPARE(log.filterByModel(QStringLiteral("CSVImporter")).size(), 1);

    const auto recChain = log.chain(QStringLiteral("DEEP4-MIX"));
    QCOMPARE(recChain[0].source, QString());
    QCOMPARE(recChain[0].model,    QString());

    const auto addEntries = log.filterBySource(QStringLiteral("UKPolice"));
    QCOMPARE(addEntries[0].eventId, QString());
    QCOMPARE(addEntries[0].stage,    QString());
}

void TestProvenanceDeep4::testProvenanceToJsonEmptyChain()
{
    ProvenanceLog log;
    const QJsonArray arr = DataExporter::provenanceToJson(log.chain(QStringLiteral("NONE")));
    QVERIFY(arr.isEmpty());
}

QTEST_GUILESS_MAIN(TestProvenanceDeep4)
#include "test_provenance_deep4.moc"
