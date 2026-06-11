// test_provenance_deep2.cpp — Deep audit tests for ProvenanceLog
// Iteration 12 — covers append, chain filtering, clear, dataHash persistence,
// and large-volume stability (no ring-buffer capacity; log grows unbounded).
#include <QTest>
#include <QDateTime>
#include "audit/ProvenanceLog.h"

class TestProvenanceDeep2 : public QObject
{
    Q_OBJECT
private slots:
    void testAppendIncreasesCount();
    void testChainFiltersById();
    void testChainOnlyMatchingEventId();
    void testClearEmptiesLog();
    void testDataHashStoredAndRetrieved();
    void testFillManyNoCrash();
    void testChainReturnsEmpty_UnknownId();
};

// record() must increment count by one per call.
void TestProvenanceDeep2::testAppendIncreasesCount()
{
    ProvenanceLog log;
    QCOMPARE(log.count(), 0);

    log.record("EV-001", "ingest", "load", "CSV imported");
    QCOMPARE(log.count(), 1);

    log.record("EV-001", "nlp",    "parse", "NLP ran");
    QCOMPARE(log.count(), 2);

    log.record("EV-002", "model",  "fit",   "model updated");
    QCOMPARE(log.count(), 3);
}

// chain() must return entries for the requested eventId only, in timestamp order.
void TestProvenanceDeep2::testChainFiltersById()
{
    ProvenanceLog log;
    log.record("EV-A", "ingest",    "load",   "first");
    log.record("EV-B", "nlp",       "parse",  "other event");
    log.record("EV-A", "inference", "score",  "second");
    log.record("EV-B", "output",    "export", "other event 2");
    log.record("EV-A", "output",    "emit",   "third");

    const auto chainA = log.chain("EV-A");
    QCOMPARE(chainA.size(), 3);

    const auto chainB = log.chain("EV-B");
    QCOMPARE(chainB.size(), 2);

    for (const auto& e : chainA)
        QCOMPARE(e.eventId, QString("EV-A"));

    for (const auto& e : chainB)
        QCOMPARE(e.eventId, QString("EV-B"));
}

// chain() must contain only entries whose eventId matches exactly.
void TestProvenanceDeep2::testChainOnlyMatchingEventId()
{
    ProvenanceLog log;
    for (int i = 0; i < 10; ++i)
        log.record(QString("EV-%1").arg(i), "ingest", "load", "detail");

    // Requesting chain for EV-5 must yield exactly 1 entry.
    const auto chain5 = log.chain("EV-5");
    QCOMPARE(chain5.size(), 1);
    QCOMPARE(chain5.first().eventId, QString("EV-5"));

    // Chain for non-existent id must be empty.
    QVERIFY(log.chain("EV-99").isEmpty());
}

// clear() must empty the log and reset count to 0.
void TestProvenanceDeep2::testClearEmptiesLog()
{
    ProvenanceLog log;
    for (int i = 0; i < 50; ++i)
        log.record(QString("EV-%1").arg(i), "stage", "action", "detail");

    QCOMPARE(log.count(), 50);
    log.clear();
    QCOMPARE(log.count(), 0);
    QVERIFY(log.getEntries().isEmpty());

    // After clear, chain on any id must return empty.
    QVERIFY(log.chain("EV-0").isEmpty());
}

// The dataHash field passed to record() must be preserved in the returned chain.
void TestProvenanceDeep2::testDataHashStoredAndRetrieved()
{
    ProvenanceLog log;
    const QString hash1 = "abc123def456";
    const QString hash2 = "0000111122223333";

    log.record("EV-HASH", "ingest", "load",  "first entry",  hash1);
    log.record("EV-HASH", "model",  "score", "second entry", hash2);

    const auto chain = log.chain("EV-HASH");
    QCOMPARE(chain.size(), 2);

    // Sorted by timestamp; both hashes must be present (order may vary slightly
    // if timestamps tie, so check both hashes appear).
    QStringList hashes;
    for (const auto& e : chain) hashes << e.dataHash;
    QVERIFY2(hashes.contains(hash1), "hash1 must be stored in entry");
    QVERIFY2(hashes.contains(hash2), "hash2 must be stored in entry");

    // Calling again with the same arguments must produce identical hashes
    // (determinism of the stored field, not of any hash computation).
    ProvenanceLog log2;
    log2.record("EV-HASH", "ingest", "load",  "first entry",  hash1);
    log2.record("EV-HASH", "model",  "score", "second entry", hash2);
    const auto chain2 = log2.chain("EV-HASH");
    QCOMPARE(chain2.size(), 2);

    QStringList hashes2;
    for (const auto& e : chain2) hashes2 << e.dataHash;
    QCOMPARE(hashes, hashes2);
}

// Filling the log with a large number of entries must not crash.
// ProvenanceLog has no capacity limit — entries accumulate indefinitely.
void TestProvenanceDeep2::testFillManyNoCrash()
{
    ProvenanceLog log;
    const int N = 2000;
    for (int i = 0; i < N; ++i)
        log.record(QString("EV-%1").arg(i % 100), "ingest", "load",
                   QString("detail %1").arg(i));

    QCOMPARE(log.count(), N);

    // All entries are retained (no eviction).
    QVERIFY(log.count() >= N);

    // Clear must still work correctly after a large fill.
    log.clear();
    QCOMPARE(log.count(), 0);
}

// chain() for an id that was never recorded must return an empty vector.
void TestProvenanceDeep2::testChainReturnsEmpty_UnknownId()
{
    ProvenanceLog log;
    log.record("EV-X", "ingest", "load", "something");

    QVERIFY(log.chain("DOES_NOT_EXIST").isEmpty());
    QVERIFY(log.chain(QString()).isEmpty());
}

QTEST_GUILESS_MAIN(TestProvenanceDeep2)
#include "test_provenance_deep2.moc"
