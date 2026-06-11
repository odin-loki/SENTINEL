// test_provenance_log_deep4.cpp — Iteration 16 deep audit: record, formatChain,
// clear, dataHash, and multi-event chain isolation.
#include <QTest>
#include <QDateTime>

#include "audit/ProvenanceLog.h"

class TestProvenanceLogDeep4 : public QObject
{
    Q_OBJECT

private slots:
    void testRecordPopulatesEventIdStageAndHash();
    void testFormatChainIncludesHashAndStages();
    void testFormatChainEmptyHashShowsDash();
    void testFormatChainChronologicalOrder();
    void testClearRemovesAllChainsAndHashes();
    void testMultiEventChainsStayIsolated();
    void testLargeMultiStageChainNoCrash();
    void testRecordAfterClearStartsFreshChain();
};

void TestProvenanceLogDeep4::testRecordPopulatesEventIdStageAndHash()
{
    ProvenanceLog log;
    log.record(QStringLiteral("DEEP4-EVT"), QStringLiteral("ingest"),
               QStringLiteral("import"), QStringLiteral("loaded 12 rows"),
               QStringLiteral("cafebabe"));

    const auto chain = log.chain(QStringLiteral("DEEP4-EVT"));
    QCOMPARE(chain.size(), 1);

    const ProvenanceEntry& e = chain[0];
    QCOMPARE(e.eventId,  QStringLiteral("DEEP4-EVT"));
    QCOMPARE(e.stage,    QStringLiteral("ingest"));
    QCOMPARE(e.action,   QStringLiteral("import"));
    QCOMPARE(e.detail,   QStringLiteral("loaded 12 rows"));
    QCOMPARE(e.dataHash, QStringLiteral("cafebabe"));
    QVERIFY(e.timestamp.isValid());
}

void TestProvenanceLogDeep4::testFormatChainIncludesHashAndStages()
{
    ProvenanceLog log;
    log.record(QStringLiteral("DEEP4-FMT"), QStringLiteral("ingest"),
               QStringLiteral("import"), QStringLiteral("from CSV"), QStringLiteral("aaa111"));
    log.record(QStringLiteral("DEEP4-FMT"), QStringLiteral("model"),
               QStringLiteral("fit"), QStringLiteral("Poisson fit"), QStringLiteral("bbb222"));

    const QString formatted = log.formatChain(QStringLiteral("DEEP4-FMT"));
    QVERIFY(formatted.contains(QStringLiteral("DEEP4-FMT")));
    QVERIFY(formatted.contains(QStringLiteral("[ingest]")));
    QVERIFY(formatted.contains(QStringLiteral("[model]")));
    QVERIFY(formatted.contains(QStringLiteral("import")));
    QVERIFY(formatted.contains(QStringLiteral("fit")));
    QVERIFY(formatted.contains(QStringLiteral("(hash:aaa111)")));
    QVERIFY(formatted.contains(QStringLiteral("(hash:bbb222)")));
}

void TestProvenanceLogDeep4::testFormatChainEmptyHashShowsDash()
{
    ProvenanceLog log;
    log.record(QStringLiteral("DEEP4-NOHASH"), QStringLiteral("output"),
               QStringLiteral("export"), QStringLiteral("leads written"));

    const QString formatted = log.formatChain(QStringLiteral("DEEP4-NOHASH"));
    QVERIFY(formatted.contains(QStringLiteral("(hash:-)")));
    QVERIFY(!formatted.contains(QStringLiteral("(hash:)")));
}

void TestProvenanceLogDeep4::testFormatChainChronologicalOrder()
{
    ProvenanceLog log;
    // record() timestamps follow wall-clock insertion order; chain() sorts ascending.
    log.record(QStringLiteral("DEEP4-ORDER"), QStringLiteral("ingest"),
               QStringLiteral("step1"), QStringLiteral("first"), QStringLiteral("h1"));
    log.record(QStringLiteral("DEEP4-ORDER"), QStringLiteral("model"),
               QStringLiteral("step2"), QStringLiteral("second"), QStringLiteral("h2"));
    log.record(QStringLiteral("DEEP4-ORDER"), QStringLiteral("output"),
               QStringLiteral("step3"), QStringLiteral("third"), QStringLiteral("h3"));

    const auto chain = log.chain(QStringLiteral("DEEP4-ORDER"));
    QCOMPARE(chain.size(), 3);
    QVERIFY(chain[0].timestamp <= chain[1].timestamp);
    QVERIFY(chain[1].timestamp <= chain[2].timestamp);
    QCOMPARE(chain[0].action, QStringLiteral("step1"));
    QCOMPARE(chain[2].action, QStringLiteral("step3"));

    const QString formatted = log.formatChain(QStringLiteral("DEEP4-ORDER"));
    const int posStep1 = formatted.indexOf(QStringLiteral("step1"));
    const int posStep2 = formatted.indexOf(QStringLiteral("step2"));
    const int posStep3 = formatted.indexOf(QStringLiteral("step3"));
    QVERIFY(posStep1 >= 0 && posStep2 >= 0 && posStep3 >= 0);
    QVERIFY(posStep1 < posStep2);
    QVERIFY(posStep2 < posStep3);
}

void TestProvenanceLogDeep4::testClearRemovesAllChainsAndHashes()
{
    ProvenanceLog log;
    log.record(QStringLiteral("DEEP4-A"), QStringLiteral("ingest"), QStringLiteral("load"),
               QStringLiteral("a"), QStringLiteral("hashA"));
    log.record(QStringLiteral("DEEP4-B"), QStringLiteral("model"), QStringLiteral("fit"),
               QStringLiteral("b"), QStringLiteral("hashB"));
    QCOMPARE(log.count(), 2);

    log.clear();
    QCOMPARE(log.count(), 0);
    QVERIFY(log.chain(QStringLiteral("DEEP4-A")).isEmpty());
    QVERIFY(log.chain(QStringLiteral("DEEP4-B")).isEmpty());
    QVERIFY(log.formatChain(QStringLiteral("DEEP4-A")).isEmpty());
}

void TestProvenanceLogDeep4::testMultiEventChainsStayIsolated()
{
    ProvenanceLog log;
    for (int ev = 0; ev < 5; ++ev) {
        const QString id = QStringLiteral("DEEP4-MULTI-%1").arg(ev);
        log.record(id, QStringLiteral("ingest"), QStringLiteral("import"),
                   QStringLiteral("detail %1").arg(ev), QStringLiteral("hash%1").arg(ev));
        log.record(id, QStringLiteral("inference"), QStringLiteral("score"),
                   QStringLiteral("score %1").arg(ev), QStringLiteral("hash%1b").arg(ev));
    }

    QCOMPARE(log.count(), 10);
    for (int ev = 0; ev < 5; ++ev) {
        const QString id = QStringLiteral("DEEP4-MULTI-%1").arg(ev);
        const auto chain = log.chain(id);
        QCOMPARE(chain.size(), 2);
        for (const auto& e : chain)
            QCOMPARE(e.eventId, id);
    }

    QVERIFY(log.chain(QStringLiteral("DEEP4-MULTI-99")).isEmpty());
}

void TestProvenanceLogDeep4::testLargeMultiStageChainNoCrash()
{
    ProvenanceLog log;
    constexpr int stages = 200;
    for (int i = 0; i < stages; ++i) {
        log.record(QStringLiteral("DEEP4-BIG"), QStringLiteral("stage"),
                   QStringLiteral("action_%1").arg(i),
                   QStringLiteral("detail %1").arg(i),
                   QStringLiteral("%1").arg(i, 8, 16, QLatin1Char('0')));
    }

    QCOMPARE(log.chain(QStringLiteral("DEEP4-BIG")).size(), stages);
    const QString formatted = log.formatChain(QStringLiteral("DEEP4-BIG"));
    QVERIFY(formatted.count(QLatin1Char('\n')) >= stages);
    // Last stage index is stages-1 (199 → 0x000000c7).
    QVERIFY(formatted.contains(QStringLiteral("(hash:000000c7)")));
}

void TestProvenanceLogDeep4::testRecordAfterClearStartsFreshChain()
{
    ProvenanceLog log;
    log.record(QStringLiteral("DEEP4-RESET"), QStringLiteral("ingest"),
               QStringLiteral("old"), QStringLiteral("stale"), QStringLiteral("oldhash"));
    log.clear();

    log.record(QStringLiteral("DEEP4-RESET"), QStringLiteral("ingest"),
               QStringLiteral("new"), QStringLiteral("fresh"), QStringLiteral("newhash"));

    const auto chain = log.chain(QStringLiteral("DEEP4-RESET"));
    QCOMPARE(chain.size(), 1);
    QCOMPARE(chain[0].action,   QStringLiteral("new"));
    QCOMPARE(chain[0].dataHash, QStringLiteral("newhash"));
    QCOMPARE(log.count(), 1);
}

QTEST_GUILESS_MAIN(TestProvenanceLogDeep4)
#include "test_provenance_log_deep4.moc"
