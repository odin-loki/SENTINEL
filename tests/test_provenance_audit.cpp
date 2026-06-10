// test_provenance_audit.cpp — Focused audit tests for ProvenanceLog
#include <QTest>
#include <QCoreApplication>
#include "audit/ProvenanceLog.h"

class TestProvenanceAudit : public QObject {
    Q_OBJECT

private slots:

    // 1 ── Record 5 entries for the same eventId; chain returns all 5
    void testRecordAndRetrieve()
    {
        ProvenanceLog log;
        for (int i = 0; i < 5; ++i) {
            log.record("evtA", "ingest", QString("action%1").arg(i),
                       QString("detail%1").arg(i));
        }
        const auto chain = log.chain("evtA");
        QCOMPARE(chain.size(), 5);
        for (const auto& e : chain)
            QCOMPARE(e.eventId, QStringLiteral("evtA"));
    }

    // 2 ── Chain entries are returned in chronological (insertion) order
    void testChainOrdering()
    {
        ProvenanceLog log;
        for (int i = 0; i < 5; ++i) {
            log.record("evtOrder", "ingest", QString("action%1").arg(i),
                       QString("step%1").arg(i));
            QTest::qWait(20);   // ensure distinct ms-precision timestamps
        }
        const auto chain = log.chain("evtOrder");
        QCOMPARE(chain.size(), 5);
        for (int i = 0; i < chain.size() - 1; ++i)
            QVERIFY(chain[i].timestamp <= chain[i + 1].timestamp);
    }

    // 3 ── Records for two different eventIds don't mix
    void testMultipleEventIds()
    {
        ProvenanceLog log;
        log.record("alpha", "ingest",    "import",   "alpha detail 1");
        log.record("beta",  "nlp",       "classify", "beta detail 1");
        log.record("alpha", "nlp",       "classify", "alpha detail 2");
        log.record("beta",  "inference", "score",    "beta detail 2");
        log.record("alpha", "output",    "export",   "alpha detail 3");

        const auto chainA = log.chain("alpha");
        const auto chainB = log.chain("beta");

        QCOMPARE(chainA.size(), 3);
        QCOMPARE(chainB.size(), 2);

        for (const auto& e : chainA)
            QCOMPARE(e.eventId, QStringLiteral("alpha"));
        for (const auto& e : chainB)
            QCOMPARE(e.eventId, QStringLiteral("beta"));
    }

    // 4 ── recent(3) returns the 3 most-recent entries across all events
    void testRecentN()
    {
        ProvenanceLog log;
        for (int i = 0; i < 10; ++i)
            log.record(QString("evt%1").arg(i % 3), "ingest",
                       "action", QString("detail%1").arg(i));

        const auto recent = log.recent(3);
        QCOMPARE(recent.size(), 3);
    }

    // 5 ── recent(1000) returns all entries when fewer than 1000 exist
    void testRecentAll()
    {
        ProvenanceLog log;
        const int total = 42;
        for (int i = 0; i < total; ++i)
            log.record("evtAll", "ingest", "action", QString("detail%1").arg(i));

        const auto all = log.recent(1000);
        QCOMPARE(all.size(), total);
    }

    // 6 ── clear() empties the log; chain returns an empty vector
    void testClearAll()
    {
        ProvenanceLog log;
        log.record("evtX", "ingest",    "import",   "detail1");
        log.record("evtY", "nlp",       "classify", "detail2");
        log.record("evtX", "inference", "score",    "detail3");

        QVERIFY(!log.chain("evtX").isEmpty());
        QVERIFY(!log.recent(100).isEmpty());

        log.clear();

        QVERIFY(log.chain("evtX").isEmpty());
        QVERIFY(log.chain("evtY").isEmpty());
        QVERIFY(log.recent(100).isEmpty());
    }

    // 7 ── formatChain() returns a non-empty string containing stage and action text
    void testFormatChain()
    {
        ProvenanceLog log;
        log.record("evtFmt", "nlp", "classify", "Crime type detected");
        log.record("evtFmt", "inference", "score", "Confidence 0.91");

        const QString text = log.formatChain("evtFmt");

        QVERIFY(!text.isEmpty());
        QVERIFY(text.contains("nlp") || text.contains("classify"));
        QVERIFY(text.contains("inference") || text.contains("score"));
    }

    // 8 ── formatHtml() returns a string containing <table and </table>
    void testFormatHtml()
    {
        ProvenanceLog log;
        log.record("evtHtml", "ingest", "import", "CSV data loaded", "ab12cd34");

        const QString html = log.formatHtml("evtHtml");

        QVERIFY(!html.isEmpty());
        QVERIFY(html.contains("<table"));
        QVERIFY(html.contains("</table>"));
    }

    // 9 ── If dataHash is provided it appears in the chain entries
    void testDataHashStored()
    {
        ProvenanceLog log;
        const QString hash = QStringLiteral("deadbeef0102030405060708");
        log.record("evtHash", "ingest", "import", "CSV row data", hash);

        const auto chain = log.chain("evtHash");
        QCOMPARE(chain.size(), 1);
        QCOMPARE(chain[0].dataHash, hash);
    }

    // 10 ── All five pipeline stages can be recorded and retrieved
    void testAllStages()
    {
        ProvenanceLog log;
        const QString evId = QStringLiteral("evtPipeline");
        const QStringList stages = { "ingest", "nlp", "model", "inference", "output" };
        for (const QString& s : stages)
            log.record(evId, s, "process", "step detail");

        const auto chain = log.chain(evId);
        QCOMPARE(chain.size(), 5);

        QSet<QString> found;
        for (const auto& e : chain)
            found.insert(e.stage);

        for (const QString& s : stages)
            QVERIFY(found.contains(s));
    }

    // 11 ── Timestamps in chain entries are valid QDateTime objects
    void testTimestampRecorded()
    {
        ProvenanceLog log;
        log.record("evtTs", "ingest", "import", "Row A");
        log.record("evtTs", "nlp",    "parse",  "Row B");

        const auto chain = log.chain("evtTs");
        QCOMPARE(chain.size(), 2);
        for (const auto& e : chain)
            QVERIFY(e.timestamp.isValid());
    }

    // 12 ── chain("") returns an empty vector; no crash
    void testEmptyEventId()
    {
        ProvenanceLog log;
        log.record("realEvent", "ingest", "import", "some data");

        const auto chain = log.chain({});
        QVERIFY(chain.isEmpty());
    }

    // 13 ── Large log: 10 000 entries across 100 events; recent(50) works
    void testLargeLog()
    {
        ProvenanceLog log;
        const int numEvents    = 100;
        const int perEvent     = 100;          // 10 000 total

        for (int e = 0; e < numEvents; ++e) {
            for (int i = 0; i < perEvent; ++i) {
                log.record(QString("event%1").arg(e),
                           "ingest", "action", QString("detail%1_%2").arg(e).arg(i));
            }
        }

        const auto recent = log.recent(50);
        QCOMPARE(recent.size(), 50);

        // Spot-check a single event chain
        const auto chain0 = log.chain("event0");
        QCOMPARE(chain0.size(), perEvent);
    }

    // 14 ── Narrative containing <script> is HTML-escaped in formatHtml()
    void testFormatHtmlEscaping()
    {
        ProvenanceLog log;
        log.record("evtXSS", "ingest", "import",
                   "<script>alert('xss')</script>", {});

        const QString html = log.formatHtml("evtXSS");

        QVERIFY(!html.isEmpty());
        QVERIFY(!html.contains("<script>alert"));         // raw tag must not appear
        QVERIFY(html.contains("&lt;script&gt;"));         // must be escaped
    }

    // 15 ── Calling chain() for events with different record counts; no crash
    void testMultipleChainsConcurrent()
    {
        ProvenanceLog log;

        // Three events with very different record counts
        for (int i = 0; i < 20; ++i)
            log.record("bigEvent",   "ingest", "action", "d");
        for (int i = 0; i < 5; ++i)
            log.record("midEvent",   "nlp",    "action", "d");
        for (int i = 0; i < 1; ++i)
            log.record("smallEvent", "output", "action", "d");

        const auto c1 = log.chain("bigEvent");
        const auto c2 = log.chain("midEvent");
        const auto c3 = log.chain("smallEvent");
        const auto c4 = log.chain("missingEvent");

        QCOMPARE(c1.size(), 20);
        QCOMPARE(c2.size(), 5);
        QCOMPARE(c3.size(), 1);
        QVERIFY(c4.isEmpty());
    }
};

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    TestProvenanceAudit t;
    return QTest::qExec(&t, argc, argv);
}

#include "test_provenance_audit.moc"
