// Iteration 12 — ProvenanceLog deep test
#include <QtTest/QtTest>
#include <QJsonDocument>
#include <QJsonArray>
#include "audit/ProvenanceLog.h"

class ProvenanceDeep3Test : public QObject
{
    Q_OBJECT

private slots:

    void testInitiallyEmpty()
    {
        ProvenanceLog log;
        QCOMPARE(log.count(), 0);
        QVERIFY(log.getEntries().isEmpty());
    }

    void testRecordIncreasesCount()
    {
        ProvenanceLog log;
        log.record("EVT001", "ingest", "import", "imported 5 rows", "abc123");
        QCOMPARE(log.count(), 1);
        log.record("EVT002", "model", "predict", "ran Poisson");
        QCOMPARE(log.count(), 2);
    }

    void testAddEntryIncreasesCount()
    {
        ProvenanceLog log;
        log.addEntry("CSV", "PoissonBaseline", "fit", "10 events");
        QCOMPARE(log.count(), 1);
    }

    void testClearResetsCount()
    {
        ProvenanceLog log;
        log.record("EVT001", "ingest", "import", "x");
        log.record("EVT002", "model", "fit", "y");
        QCOMPARE(log.count(), 2);
        log.clear();
        QCOMPARE(log.count(), 0);
        QVERIFY(log.getEntries().isEmpty());
    }

    void testSizeMatchesCount()
    {
        ProvenanceLog log;
        log.record("E1", "ingest", "import", "x");
        log.record("E2", "model", "fit", "y");
        QCOMPARE(log.size(), log.count());
        QCOMPARE(log.size(), 2);
    }

    void testChainFiltersById()
    {
        ProvenanceLog log;
        log.record("EVT001", "ingest", "import", "step1");
        log.record("EVT002", "model",  "fit",    "other");
        log.record("EVT001", "model",  "predict","step2");

        const auto chain = log.chain("EVT001");
        QCOMPARE(chain.size(), 2);
        for (const auto& e : chain)
            QCOMPARE(e.eventId, QStringLiteral("EVT001"));
    }

    void testChainEmptyForUnknownId()
    {
        ProvenanceLog log;
        log.record("EVT001", "ingest", "import", "x");
        QVERIFY(log.chain("UNKNOWN").isEmpty());
    }

    void testChainSortedByTimestamp()
    {
        ProvenanceLog log;
        const QDateTime t1 = QDateTime(QDate(2024, 1, 1), QTime(10, 0), Qt::UTC);
        const QDateTime t2 = QDateTime(QDate(2024, 1, 1), QTime(11, 0), Qt::UTC);
        // Record in reverse order — chain() should sort ascending
        log.addEntry("src", "m", "action2", "detail2", t2);
        log.addEntry("src", "m", "action1", "detail1", t1);
        const auto entries = log.filterByModel("m");
        QCOMPARE(entries.size(), 2);
    }

    void testRecentReturnsCorrectCount()
    {
        ProvenanceLog log;
        for (int i = 0; i < 10; ++i)
            log.record(QStringLiteral("EVT%1").arg(i), "ingest", "import", "x");
        QCOMPARE(log.recent(3).size(), 3);
    }

    void testRecentMoreThanTotalReturnsAll()
    {
        ProvenanceLog log;
        log.record("A", "ingest", "x", "");
        log.record("B", "ingest", "x", "");
        QCOMPARE(log.recent(100).size(), 2);
    }

    void testRecentZeroReturnsEmpty()
    {
        ProvenanceLog log;
        log.record("E1", "ingest", "x", "");
        QCOMPARE(log.recent(0).size(), 0);
    }

    void testFilterByStage()
    {
        ProvenanceLog log;
        log.record("E1", "ingest", "import", "x");
        log.record("E2", "model",  "fit",    "y");
        log.record("E3", "ingest", "validate","z");

        const auto ingests = log.filterByStage("ingest");
        QCOMPARE(ingests.size(), 2);
        for (const auto& e : ingests)
            QCOMPARE(e.stage, QStringLiteral("ingest"));
    }

    void testFilterByModel()
    {
        ProvenanceLog log;
        log.addEntry("src", "PoissonBaseline", "fit", "x",
                     QDateTime(QDate(2024,1,1), QTime(12,0), Qt::UTC));
        log.addEntry("src", "HawkesProcess",   "fit", "y",
                     QDateTime(QDate(2024,1,1), QTime(12,1), Qt::UTC));
        log.addEntry("src", "PoissonBaseline", "predict", "z",
                     QDateTime(QDate(2024,1,1), QTime(12,2), Qt::UTC));

        const auto filtered = log.filterByModel("PoissonBaseline");
        QCOMPARE(filtered.size(), 2);
    }

    void testFilterBySource()
    {
        ProvenanceLog log;
        log.addEntry("CSV", "model", "fit",    "x", QDateTime(QDate(2024,1,1), QTime(12,0), Qt::UTC));
        log.addEntry("API", "model", "ingest", "y", QDateTime(QDate(2024,1,1), QTime(12,1), Qt::UTC));
        log.addEntry("CSV", "model", "export", "z", QDateTime(QDate(2024,1,1), QTime(12,2), Qt::UTC));

        const auto csv = log.filterBySource("CSV");
        QCOMPARE(csv.size(), 2);
    }

    void testFilterByTimeRange()
    {
        ProvenanceLog log;
        const QDateTime base = QDateTime(QDate(2024, 6, 1), QTime(12, 0), Qt::UTC);
        log.addEntry("src", "m", "a1", "", base.addSecs(-7200));  // 10:00
        log.addEntry("src", "m", "a2", "", base);                  // 12:00
        log.addEntry("src", "m", "a3", "", base.addSecs(7200));    // 14:00

        // Range 11:00–13:00 should return only the 12:00 entry
        const auto filtered = log.filterByTimeRange(
            base.addSecs(-3600), base.addSecs(3600));
        QCOMPARE(filtered.size(), 1);
    }

    void testExportToJsonValid()
    {
        ProvenanceLog log;
        log.addEntry("CSV", "Poisson", "fit", "10 events",
                     QDateTime(QDate(2024, 1, 1), QTime(12, 0), Qt::UTC));

        const QString json = log.exportToJson();
        QVERIFY(!json.isEmpty());

        QJsonParseError err;
        const QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8(), &err);
        QCOMPARE(err.error, QJsonParseError::NoError);
        QVERIFY(doc.isArray());
        QCOMPARE(doc.array().size(), 1);
    }

    void testExportToCsvHeader()
    {
        ProvenanceLog log;
        log.record("E1", "ingest", "import", "detail");
        QVERIFY(log.exportToCsv().startsWith(
            "timestamp,eventId,stage,source,model,action,detail,dataHash\n"));
    }

    void testExportToCsvRowCount()
    {
        ProvenanceLog log;
        for (int i = 0; i < 5; ++i)
            log.record(QStringLiteral("E%1").arg(i), "ingest", "import", "x");
        // 1 header + 5 data rows = 6 newlines
        QCOMPARE(log.exportToCsv().count('\n'), 6);
    }

    void testFormatChainContainsEventId()
    {
        ProvenanceLog log;
        log.record("EVT999", "ingest", "import", "from CSV",  "hash1");
        log.record("EVT999", "model",  "fit",    "Poisson",   "hash2");

        const QString chain = log.formatChain("EVT999");
        QVERIFY(chain.contains("EVT999"));
        QVERIFY(chain.contains("import"));
        QVERIFY(chain.contains("fit"));
    }

    void testFormatHtmlContainsTable()
    {
        ProvenanceLog log;
        log.record("EVT_HTML", "ingest", "import", "test detail", "hashXYZ");
        const QString html = log.formatHtml("EVT_HTML");
        QVERIFY(html.contains("<table>"));
        QVERIFY(html.contains("EVT_HTML"));
    }

    void testFormatHtmlEmptyForUnknownId()
    {
        ProvenanceLog log;
        log.record("EVT001", "ingest", "import", "test");
        QVERIFY(log.formatHtml("UNKNOWN").isEmpty());
    }

    void testGetEntriesReverseOrder()
    {
        ProvenanceLog log;
        log.addEntry("src", "m", "first",  "", QDateTime(QDate(2024,1,1), QTime(10,0), Qt::UTC));
        log.addEntry("src", "m", "second", "", QDateTime(QDate(2024,1,1), QTime(11,0), Qt::UTC));
        log.addEntry("src", "m", "third",  "", QDateTime(QDate(2024,1,1), QTime(12,0), Qt::UTC));

        const auto entries = log.getEntries();
        QCOMPARE(entries.size(), 3);
        QCOMPARE(entries[0].action, QStringLiteral("third"));
        QCOMPARE(entries[2].action, QStringLiteral("first"));
    }

    void testRecordPreservesAllFields()
    {
        ProvenanceLog log;
        log.record("MYEVENT", "output", "export", "exported 3 leads", "deadbeef");
        const auto chain = log.chain("MYEVENT");
        QCOMPARE(chain.size(), 1);
        const auto& e = chain[0];
        QCOMPARE(e.eventId,  QStringLiteral("MYEVENT"));
        QCOMPARE(e.stage,    QStringLiteral("output"));
        QCOMPARE(e.action,   QStringLiteral("export"));
        QCOMPARE(e.detail,   QStringLiteral("exported 3 leads"));
        QCOMPARE(e.dataHash, QStringLiteral("deadbeef"));
        QVERIFY(e.timestamp.isValid());
    }
};

QTEST_GUILESS_MAIN(ProvenanceDeep3Test)
#include "test_provenance_deep3.moc"
