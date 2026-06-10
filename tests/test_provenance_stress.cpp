// test_provenance_stress.cpp
// Stress and correctness tests for ProvenanceLog.
#include <QTest>
#include <QElapsedTimer>

#include "audit/ProvenanceLog.h"

class ProvenanceStressTest : public QObject
{
    Q_OBJECT

private slots:

    // ── 1. Bulk insertion: 1000 entries, verified all stored ─────────────────
    void testBulkEntryInsertionSpeed()
    {
        ProvenanceLog log;
        QElapsedTimer t;
        t.start();

        for (int i = 0; i < 1000; ++i) {
            log.record(QStringLiteral("EVT-%1").arg(i),
                       QStringLiteral("ingest"),
                       QStringLiteral("import"),
                       QStringLiteral("Imported event %1").arg(i),
                       QStringLiteral("abc123"));
        }

        const qint64 ms = t.elapsed();
        QCOMPARE(log.count(), 1000);
        QVERIFY2(ms < 5000,
                 qPrintable(QStringLiteral("1000 insertions took %1 ms (> 5s limit)").arg(ms)));
    }

    // ── 2. Provenance chain ordered by timestamp ──────────────────────────────
    void testProvenanceChainIntegrity()
    {
        ProvenanceLog log;
        for (int i = 0; i < 10; ++i) {
            log.record(QStringLiteral("CHAIN-EVT"),
                       QStringLiteral("stage_%1").arg(i),
                       QStringLiteral("action_%1").arg(i),
                       QStringLiteral("detail %1").arg(i));
            QTest::qWait(2);  // small sleep to ensure distinct timestamps
        }

        auto chain = log.chain(QStringLiteral("CHAIN-EVT"));
        QCOMPARE(chain.size(), 10);

        for (int i = 1; i < chain.size(); ++i) {
            QVERIFY2(chain[i].timestamp >= chain[i-1].timestamp,
                     qPrintable(QStringLiteral("Entry %1 timestamp must be >= entry %2").arg(i).arg(i-1)));
        }
    }

    // ── 3. Multiple stages: 100 total entries across 4 stages ─────────────────
    void testMultipleStages()
    {
        ProvenanceLog log;
        const QStringList stages = { "ingest", "nlp", "model", "inference" };

        for (int i = 0; i < 100; ++i) {
            const QString stage = stages[i % 4];
            log.record(QStringLiteral("MULTI-EVT-%1").arg(i),
                       stage,
                       QStringLiteral("process"),
                       QStringLiteral("step %1").arg(i));
        }

        QCOMPARE(log.count(), 100);

        // Each stage should have 25 entries
        for (const QString& stage : stages) {
            const auto entries = log.filterByStage(stage);
            QCOMPARE(entries.size(), 25);
            for (const auto& e : entries)
                QCOMPARE(e.stage, stage);
        }
    }

    // ── 4. recent(n) returns last n in newest-first order ─────────────────────
    void testRecentNEntriesOrder()
    {
        ProvenanceLog log;
        for (int i = 0; i < 20; ++i) {
            log.record(QStringLiteral("REC-EVT"),
                       QStringLiteral("stage"),
                       QStringLiteral("action"),
                       QStringLiteral("detail %1").arg(i));
        }

        const auto last5 = log.recent(5);
        QCOMPARE(last5.size(), 5);

        // recent() returns newest-first; verify detail contains 19, 18, 17, 16, 15
        for (int i = 0; i < 5; ++i) {
            const int expected = 19 - i;
            QVERIFY2(last5[i].detail.contains(QString::number(expected)),
                     qPrintable(QStringLiteral("recent()[%1] should have detail %2, got: %3")
                        .arg(i).arg(expected).arg(last5[i].detail)));
        }
    }

    // ── 5. filterByStage returns only matching stage ───────────────────────────
    void testFilterByStageCorrectness()
    {
        ProvenanceLog log;
        log.record("E1", "ingest",    "import", "imported");
        log.record("E2", "nlp",       "extract", "extracted");
        log.record("E3", "model",     "fit",    "fitted");
        log.record("E4", "ingest",    "validate", "validated");
        log.record("E5", "inference", "score", "scored");
        log.record("E6", "nlp",       "classify", "classified");

        const auto ingestEntries = log.filterByStage("ingest");
        QCOMPARE(ingestEntries.size(), 2);
        for (const auto& e : ingestEntries)
            QCOMPARE(e.stage, QStringLiteral("ingest"));

        const auto nlpEntries = log.filterByStage("nlp");
        QCOMPARE(nlpEntries.size(), 2);

        const auto modelEntries = log.filterByStage("model");
        QCOMPARE(modelEntries.size(), 1);

        const auto unknownEntries = log.filterByStage("nonexistent");
        QCOMPARE(unknownEntries.size(), 0);
    }

    // ── 6. HTML export contains all expected content ──────────────────────────
    void testFormatHtml()
    {
        ProvenanceLog log;
        const QStringList stages = { "ingest", "nlp", "model", "inference", "output" };
        for (const QString& stage : stages) {
            log.record("HTML-EVT", stage, "test_action",
                       QStringLiteral("detail for ") + stage, "hash123");
        }

        const QString html = log.formatHtml("HTML-EVT");
        QVERIFY2(!html.isEmpty(), "HTML must not be empty");
        QVERIFY2(html.contains("<table"), "HTML must contain a <table> element");
        QVERIFY2(html.contains("HTML-EVT"), "HTML must contain the event ID");

        for (const QString& stage : stages) {
            QVERIFY2(html.contains(stage),
                     qPrintable(QStringLiteral("HTML must contain stage: %1").arg(stage)));
        }
        QVERIFY2(html.contains("hash123"), "HTML must contain the data hash");
    }

    // ── 7. formatChain produces text with all stages ──────────────────────────
    void testFormatChain()
    {
        ProvenanceLog log;
        log.record("CHAIN-EVT2", "ingest",    "import",   "step 1");
        log.record("CHAIN-EVT2", "nlp",       "classify", "step 2");
        log.record("CHAIN-EVT2", "inference", "score",    "step 3");

        const QString text = log.formatChain("CHAIN-EVT2");
        QVERIFY2(!text.isEmpty(), "formatChain must not be empty");
        QVERIFY2(text.contains("ingest"),    "chain text must mention 'ingest'");
        QVERIFY2(text.contains("nlp"),       "chain text must mention 'nlp'");
        QVERIFY2(text.contains("inference"), "chain text must mention 'inference'");
    }

    // ── 8. clear() empties the log ────────────────────────────────────────────
    void testClear()
    {
        ProvenanceLog log;
        for (int i = 0; i < 50; ++i)
            log.record("EVT", "ingest", "act", "detail");

        QCOMPARE(log.count(), 50);
        log.clear();
        QCOMPARE(log.count(), 0);

        // After clear, chain() returns empty
        const auto chain = log.chain("EVT");
        QCOMPARE(chain.size(), 0);

        // After clear, filterByStage returns empty
        const auto filtered = log.filterByStage("ingest");
        QCOMPARE(filtered.size(), 0);
    }

    // ── 9. dataHash stored and retrieved correctly ────────────────────────────
    void testDataHashRoundtrip()
    {
        ProvenanceLog log;
        const QString hash = QStringLiteral("abcdef1234567890");
        log.record("HASH-EVT", "model", "train", "fitted model", hash);

        const auto chain = log.chain("HASH-EVT");
        QCOMPARE(chain.size(), 1);
        QCOMPARE(chain.first().dataHash, hash);
    }

    // ── 10. chain() returns empty for unknown event ID ─────────────────────────
    void testChainUnknownEvent()
    {
        ProvenanceLog log;
        log.record("KNOWN-EVT", "ingest", "import", "detail");

        const auto chain = log.chain("UNKNOWN-EVT-XYZ");
        QCOMPARE(chain.size(), 0);
    }
};

QTEST_APPLESS_MAIN(ProvenanceStressTest)
#include "test_provenance_stress.moc"
