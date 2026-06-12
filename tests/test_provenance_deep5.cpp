// test_provenance_deep5.cpp — Iteration 21 deep audit: ProvenanceLog chain ordering,
// formatChain/formatHtml output, clear/size, and DataExporter integration.
#include <QTest>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QDateTime>

#include "audit/ProvenanceLog.h"
#include "core/DataExporter.h"

class TestProvenanceDeep5 : public QObject
{
    Q_OBJECT

private slots:
    void testChainSortsByTimestampDespiteInsertOrder();
    void testFormatChainEmptyHashDashPlaceholder();
    void testFormatHtmlEmptyHashShowsEmDash();
    void testSizeEqualsCount();
    void testClearResetsCountAndExports();
    void testDataExporterProvenanceToJsonRoundTrip();
    void testChainUnknownEventIdEmpty();
    void testRecordMultipleStagesSameEventMaintainsOrder();
};

void TestProvenanceDeep5::testChainSortsByTimestampDespiteInsertOrder()
{
    ProvenanceLog log;
    const QString eventId = QStringLiteral("DEEP5-SORT");

    log.record(eventId, QStringLiteral("ingest"), QStringLiteral("first"),
               QStringLiteral("d1"), QStringLiteral("h1"));
    log.record(eventId, QStringLiteral("model"), QStringLiteral("second"),
               QStringLiteral("d2"), QStringLiteral("h2"));
    log.record(eventId, QStringLiteral("output"), QStringLiteral("third"),
               QStringLiteral("d3"), QStringLiteral("h3"));

    const auto chain = log.chain(eventId);
    QCOMPARE(chain.size(), 3);

    for (int i = 1; i < chain.size(); ++i) {
        QVERIFY2(chain[i].timestamp >= chain[i - 1].timestamp,
                 qPrintable(QStringLiteral("chain() must return entries sorted by timestamp")));
    }
}

void TestProvenanceDeep5::testFormatChainEmptyHashDashPlaceholder()
{
    ProvenanceLog log;
    log.record(QStringLiteral("DEEP5-FMT"), QStringLiteral("output"),
               QStringLiteral("export"), QStringLiteral("no hash row"));

    const QString text = log.formatChain(QStringLiteral("DEEP5-FMT"));
    QVERIFY(text.contains(QStringLiteral("DEEP5-FMT")));
    QVERIFY(text.contains(QStringLiteral("(hash:-)")));
    QVERIFY(text.contains(QStringLiteral("[output]")));
    QVERIFY(text.contains(QStringLiteral("export: no hash row")));
}

void TestProvenanceDeep5::testFormatHtmlEmptyHashShowsEmDash()
{
    ProvenanceLog log;
    log.record(QStringLiteral("DEEP5-HTML"), QStringLiteral("model"),
               QStringLiteral("fit"), QStringLiteral("trained"));

    const QString html = log.formatHtml(QStringLiteral("DEEP5-HTML"));
    QVERIFY(html.contains(QStringLiteral("&#8212;")));
    QVERIFY(!html.contains(QStringLiteral("hash:-")));
}

void TestProvenanceDeep5::testSizeEqualsCount()
{
    ProvenanceLog log;
    QCOMPARE(log.size(), 0);
    QCOMPARE(log.count(), 0);

    log.record(QStringLiteral("E1"), QStringLiteral("ingest"), QStringLiteral("a"),
               QStringLiteral("d1"), QStringLiteral("h1"));
    log.addEntry(QStringLiteral("src"), QStringLiteral("m"), QStringLiteral("b"),
                 QStringLiteral("d2"));

    QCOMPARE(log.size(), 2);
    QCOMPARE(log.count(), 2);
}

void TestProvenanceDeep5::testClearResetsCountAndExports()
{
    ProvenanceLog log;
    log.record(QStringLiteral("DEEP5-CLR"), QStringLiteral("ingest"),
               QStringLiteral("load"), QStringLiteral("rows"), QStringLiteral("abc"));
    log.clear();

    QCOMPARE(log.count(), 0);
    QCOMPARE(log.size(), 0);
    QVERIFY(log.exportToJson().contains(QStringLiteral("[]"))
            || QJsonDocument::fromJson(log.exportToJson().toUtf8()).array().isEmpty());
    QVERIFY(log.exportToCsv().endsWith(QStringLiteral("\n")));
    QCOMPARE(log.exportToCsv().count(QLatin1Char('\n')), 1);
}

void TestProvenanceDeep5::testDataExporterProvenanceToJsonRoundTrip()
{
    ProvenanceLog log;
    const QString eventId = QStringLiteral("DEEP5-EXP");
    log.record(eventId, QStringLiteral("inference"), QStringLiteral("score"),
               QStringLiteral("risk 0.82"), QStringLiteral("deadbeef"));

    const QJsonArray arr = DataExporter::provenanceToJson(log.chain(eventId));
    QCOMPARE(arr.size(), 1);

    const QJsonObject obj = arr[0].toObject();
    QCOMPARE(obj[QStringLiteral("eventId")].toString(), eventId);
    QCOMPARE(obj[QStringLiteral("stage")].toString(), QStringLiteral("inference"));
    QCOMPARE(obj[QStringLiteral("action")].toString(), QStringLiteral("score"));
    QCOMPARE(obj[QStringLiteral("detail")].toString(), QStringLiteral("risk 0.82"));
    QCOMPARE(obj[QStringLiteral("dataHash")].toString(), QStringLiteral("deadbeef"));
    QVERIFY(QDateTime::fromString(obj[QStringLiteral("timestamp")].toString(), Qt::ISODate).isValid());
}

void TestProvenanceDeep5::testChainUnknownEventIdEmpty()
{
    ProvenanceLog log;
    log.record(QStringLiteral("DEEP5-KNOWN"), QStringLiteral("ingest"),
               QStringLiteral("load"), QStringLiteral("x"));

    QVERIFY(log.chain(QStringLiteral("DEEP5-UNKNOWN")).isEmpty());
    QVERIFY(log.formatChain(QStringLiteral("DEEP5-UNKNOWN")).isEmpty());
    QVERIFY(log.formatHtml(QStringLiteral("DEEP5-UNKNOWN")).isEmpty());
}

void TestProvenanceDeep5::testRecordMultipleStagesSameEventMaintainsOrder()
{
    ProvenanceLog log;
    const QString eventId = QStringLiteral("DEEP5-PIPE");
    const QStringList stages = {
        QStringLiteral("ingest"), QStringLiteral("nlp"), QStringLiteral("model"),
        QStringLiteral("inference"), QStringLiteral("output")
    };

    for (const auto& stage : stages)
        log.record(eventId, stage, QStringLiteral("step"), stage + QStringLiteral(" detail"));

    const auto chain = log.chain(eventId);
    QCOMPARE(chain.size(), stages.size());
    for (int i = 0; i < stages.size(); ++i)
        QCOMPARE(chain[i].stage, stages[i]);

    const QString formatted = log.formatChain(eventId);
    for (const auto& stage : stages)
        QVERIFY2(formatted.contains(stage), qPrintable(stage));
}

QTEST_GUILESS_MAIN(TestProvenanceDeep5)
#include "test_provenance_deep5.moc"
