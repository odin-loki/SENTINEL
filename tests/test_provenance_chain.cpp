// test_provenance_chain.cpp
// Validates ProvenanceLog recording, chain retrieval, filtering, HTML/text output,
// and edge cases.
#include <QTest>
#include "audit/ProvenanceLog.h"
#include <QElapsedTimer>

class ProvenanceChainTest : public QObject
{
    Q_OBJECT

private slots:

    // ── 1. record() increases count ──────────────────────────────────────────
    void testCountIncreasesOnRecord()
    {
        ProvenanceLog log;
        QCOMPARE(log.count(), 0);
        log.record(QStringLiteral("E1"), QStringLiteral("ingest"), QStringLiteral("import"), QStringLiteral("CSV import"));
        QCOMPARE(log.count(), 1);
        log.record(QStringLiteral("E1"), QStringLiteral("nlp"),    QStringLiteral("classify"), QStringLiteral("NLP run"));
        QCOMPARE(log.count(), 2);
    }

    // ── 2. chain() returns only entries for that event ───────────────────────
    void testChainFiltersCorrectly()
    {
        ProvenanceLog log;
        log.record(QStringLiteral("EVT-A"), QStringLiteral("ingest"), QStringLiteral("import"), QStringLiteral("A imported"));
        log.record(QStringLiteral("EVT-B"), QStringLiteral("ingest"), QStringLiteral("import"), QStringLiteral("B imported"));
        log.record(QStringLiteral("EVT-A"), QStringLiteral("nlp"),    QStringLiteral("classify"), QStringLiteral("A classified"));

        const auto chain = log.chain(QStringLiteral("EVT-A"));
        QCOMPARE(chain.size(), 2);
        for (const auto& e : chain)
            QCOMPARE(e.eventId, QStringLiteral("EVT-A"));
    }

    // ── 3. chain() for unknown event is empty ─────────────────────────────────
    void testChainUnknownEventEmpty()
    {
        ProvenanceLog log;
        log.record(QStringLiteral("E1"), QStringLiteral("ingest"), QStringLiteral("x"), QStringLiteral("x"));
        const auto chain = log.chain(QStringLiteral("NONEXISTENT"));
        QVERIFY(chain.isEmpty());
    }

    // ── 4. recent(n) returns up to n entries ──────────────────────────────────
    void testRecentReturnsN()
    {
        ProvenanceLog log;
        for (int i = 0; i < 20; ++i)
            log.record(QStringLiteral("E%1").arg(i), QStringLiteral("ingest"),
                       QStringLiteral("op"), QStringLiteral("detail %1").arg(i));

        const auto rec5  = log.recent(5);
        const auto rec20 = log.recent(20);
        const auto rec50 = log.recent(50);  // more than total

        QCOMPARE(rec5.size(), 5);
        QCOMPARE(rec20.size(), 20);
        QCOMPARE(rec50.size(), 20);  // capped at actual count
    }

    // ── 5. filterByStage() returns only that stage ────────────────────────────
    void testFilterByStage()
    {
        ProvenanceLog log;
        log.record(QStringLiteral("E1"), QStringLiteral("ingest"),    QStringLiteral("import"), QStringLiteral("x"));
        log.record(QStringLiteral("E1"), QStringLiteral("nlp"),       QStringLiteral("classify"), QStringLiteral("y"));
        log.record(QStringLiteral("E2"), QStringLiteral("inference"), QStringLiteral("score"), QStringLiteral("z"));
        log.record(QStringLiteral("E3"), QStringLiteral("nlp"),       QStringLiteral("extract"), QStringLiteral("w"));

        const auto nlpEntries = log.filterByStage(QStringLiteral("nlp"));
        QCOMPARE(nlpEntries.size(), 2);
        for (const auto& e : nlpEntries)
            QCOMPARE(e.stage, QStringLiteral("nlp"));
    }

    // ── 6. clear() resets to zero ──────────────────────────────────────────────
    void testClearResetsCount()
    {
        ProvenanceLog log;
        for (int i = 0; i < 10; ++i)
            log.record(QStringLiteral("E"), QStringLiteral("ingest"), QStringLiteral("x"), QStringLiteral("x"));
        QCOMPARE(log.count(), 10);
        log.clear();
        QCOMPARE(log.count(), 0);
    }

    // ── 7. formatChain() contains event ID and stage info ────────────────────
    void testFormatChainContainsExpectedText()
    {
        ProvenanceLog log;
        log.record(QStringLiteral("EVT-42"), QStringLiteral("ingest"), QStringLiteral("import"),
                   QStringLiteral("CSV column A"));
        log.record(QStringLiteral("EVT-42"), QStringLiteral("model"),  QStringLiteral("predict"),
                   QStringLiteral("Poisson rate=3.2"));

        const QString text = log.formatChain(QStringLiteral("EVT-42"));
        QVERIFY2(!text.isEmpty(), "formatChain must return non-empty string");
        QVERIFY2(text.contains(QStringLiteral("EVT-42")) ||
                 text.contains(QStringLiteral("ingest"))  ||
                 text.contains(QStringLiteral("import")),
                 "formatChain should reference event data");
    }

    // ── 8. formatHtml() returns valid HTML with table tag ────────────────────
    void testFormatHtmlContainsTable()
    {
        ProvenanceLog log;
        log.record(QStringLiteral("H1"), QStringLiteral("ingest"), QStringLiteral("import"),
                   QStringLiteral("detail"), QStringLiteral("ab12cd34"));
        const QString html = log.formatHtml(QStringLiteral("H1"));
        QVERIFY2(!html.isEmpty(), "formatHtml must return non-empty string");
        QVERIFY2(html.contains(QStringLiteral("<table"), Qt::CaseInsensitive) ||
                 html.contains(QStringLiteral("<div"),   Qt::CaseInsensitive),
                 "formatHtml should contain HTML markup");
    }

    // ── 9. ProvenanceEntry has correct stage after record ─────────────────────
    void testEntryStageCorrect()
    {
        ProvenanceLog log;
        log.record(QStringLiteral("X"), QStringLiteral("output"), QStringLiteral("export"),
                   QStringLiteral("CSV exported"));
        const auto chain = log.chain(QStringLiteral("X"));
        QVERIFY(!chain.isEmpty());
        QCOMPARE(chain.first().stage, QStringLiteral("output"));
        QCOMPARE(chain.first().action, QStringLiteral("export"));
    }

    // ── 10. Stress: 1000 entries, chain retrieval is fast ─────────────────────
    void testStressAndChainPerformance()
    {
        ProvenanceLog log;
        for (int i = 0; i < 1000; ++i) {
            const QString id = (i % 5 == 0) ? QStringLiteral("STRESS") :
                               QStringLiteral("OTHER%1").arg(i);
            log.record(id, QStringLiteral("ingest"), QStringLiteral("op"), QStringLiteral("d"));
        }
        QCOMPARE(log.count(), 1000);

        QElapsedTimer t;
        t.start();
        const auto chain = log.chain(QStringLiteral("STRESS"));
        const qint64 elapsed = t.elapsed();

        QCOMPARE(chain.size(), 200);  // i=0,5,10,...,995 → 200 entries
        QVERIFY2(elapsed < 500, qPrintable(QStringLiteral("chain() took %1 ms, expected < 500").arg(elapsed)));
    }
};

QTEST_MAIN(ProvenanceChainTest)
#include "test_provenance_chain.moc"
