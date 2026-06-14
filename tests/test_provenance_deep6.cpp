// test_provenance_deep6.cpp — Deep audit iteration 25: ProvenanceLog
// chain ordering, stage filter, JSON export, hash stability.
#include <QTest>
#include "audit/ProvenanceLog.h"

class TestProvenanceDeep6 : public QObject
{
    Q_OBJECT

private slots:

    void testChainPreservesInsertionOrder()
    {
        ProvenanceLog log;
        log.record(QStringLiteral("E1"), QStringLiteral("ingest"),
                   QStringLiteral("import"), QStringLiteral("csv row 1"), QStringLiteral("aaa"));
        log.record(QStringLiteral("E1"), QStringLiteral("nlp"),
                   QStringLiteral("classify"), QStringLiteral("burglary"), QStringLiteral("bbb"));
        log.record(QStringLiteral("E1"), QStringLiteral("inference"),
                   QStringLiteral("hint"), QStringLiteral("series link"), QStringLiteral("ccc"));

        const auto chain = log.chain(QStringLiteral("E1"));
        QCOMPARE(chain.size(), 3);
        QCOMPARE(chain[0].stage, QStringLiteral("ingest"));
        QCOMPARE(chain[1].stage, QStringLiteral("nlp"));
        QCOMPARE(chain[2].stage, QStringLiteral("inference"));
    }

    void testFilterByStage()
    {
        ProvenanceLog log;
        log.record(QStringLiteral("A"), QStringLiteral("model"), QStringLiteral("fit"), QStringLiteral("poisson"));
        log.record(QStringLiteral("B"), QStringLiteral("ingest"), QStringLiteral("fetch"), QStringLiteral("api"));

        const auto modelEntries = log.filterByStage(QStringLiteral("model"));
        QCOMPARE(modelEntries.size(), 1);
        QCOMPARE(modelEntries[0].eventId, QStringLiteral("A"));
    }

    void testExportJsonContainsEventId()
    {
        ProvenanceLog log;
        log.record(QStringLiteral("JSON-1"), QStringLiteral("output"),
                   QStringLiteral("export"), QStringLiteral("leads json"));

        const QString json = log.exportToJson();
        QVERIFY(json.contains(QStringLiteral("JSON-1")));
        QVERIFY(json.contains(QStringLiteral("export")));
    }

    void testFormatChainHumanReadable()
    {
        ProvenanceLog log;
        log.record(QStringLiteral("FMT-1"), QStringLiteral("ingest"),
                   QStringLiteral("load"), QStringLiteral("detail"));
        const QString formatted = log.formatChain(QStringLiteral("FMT-1"));
        QVERIFY(!formatted.isEmpty());
        QVERIFY(formatted.contains(QStringLiteral("ingest"))
                || formatted.contains(QStringLiteral("load")));
    }

    void testClearRemovesAll()
    {
        ProvenanceLog log;
        log.record(QStringLiteral("X"), QStringLiteral("ingest"), QStringLiteral("a"), QStringLiteral("d"));
        log.clear();
        QCOMPARE(log.count(), 0);
    }

    void testRecentLimitsCount()
    {
        ProvenanceLog log;
        for (int i = 0; i < 20; ++i)
            log.record(QStringLiteral("R%1").arg(i), QStringLiteral("ingest"),
                       QStringLiteral("act"), QStringLiteral("d"));

        const auto recent = log.recent(5);
        QCOMPARE(recent.size(), 5);
    }

    void testAddEntryAuditApi()
    {
        ProvenanceLog log;
        log.addEntry(QStringLiteral("uk_police"), QStringLiteral("CsvImporter"),
                     QStringLiteral("parse"), QStringLiteral("row 42"));
        QCOMPARE(log.size(), 1);
        const auto entries = log.filterBySource(QStringLiteral("uk_police"));
        QCOMPARE(entries.size(), 1);
    }
};

QTEST_GUILESS_MAIN(TestProvenanceDeep6)
#include "test_provenance_deep6.moc"
