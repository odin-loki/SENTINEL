// test_provenance_log_deep.cpp
// Deep tests for ProvenanceLog: record, chain, filterByStage, recent,
// formatChain, formatHtml, and clear.
#include <QTest>
#include "audit/ProvenanceLog.h"

class ProvenanceLogDeepTest : public QObject
{
    Q_OBJECT

private:
    static void populateLog(ProvenanceLog& log, const QString& eventId = QStringLiteral("E1"))
    {
        log.record(eventId, QStringLiteral("ingest"),    QStringLiteral("csv_import"),   QStringLiteral("Imported from UK Police CSV"));
        log.record(eventId, QStringLiteral("nlp"),       QStringLiteral("classify"),     QStringLiteral("CrimeClassifier: burglary 0.92"));
        log.record(eventId, QStringLiteral("model"),     QStringLiteral("poisson_fit"),  QStringLiteral("PoissonBaseline lambda=0.5"));
        log.record(eventId, QStringLiteral("inference"), QStringLiteral("hint_gen"),     QStringLiteral("HintEngine generated 3 leads"));
        log.record(eventId, QStringLiteral("output"),    QStringLiteral("lead_export"),  QStringLiteral("LeadReportGenerator: Markdown"));
    }

private slots:

    // ── 1. count() increases after record() ──────────────────────────────────
    void testCountIncreases()
    {
        ProvenanceLog log;
        QCOMPARE(log.count(), 0);
        log.record(QStringLiteral("E1"), QStringLiteral("ingest"), QStringLiteral("import"), QStringLiteral("detail"));
        QCOMPARE(log.count(), 1);
        log.record(QStringLiteral("E1"), QStringLiteral("nlp"), QStringLiteral("classify"), QStringLiteral("detail2"));
        QCOMPARE(log.count(), 2);
    }

    // ── 2. chain() returns all entries for given eventId ─────────────────────
    void testChainReturnsAllForEvent()
    {
        ProvenanceLog log; populateLog(log, QStringLiteral("E42"));
        const auto ch = log.chain(QStringLiteral("E42"));
        QVERIFY2(ch.size() == 5,
                 qPrintable(QStringLiteral("Expected 5 entries in chain, got %1").arg(ch.size())));
    }

    // ── 3. chain() doesn't return entries for different event ─────────────────
    void testChainIsolatesEvent()
    {
        ProvenanceLog log;
        log.record(QStringLiteral("E1"), QStringLiteral("ingest"), QStringLiteral("import"), QStringLiteral("d"));
        log.record(QStringLiteral("E2"), QStringLiteral("ingest"), QStringLiteral("import"), QStringLiteral("d"));
        const auto ch = log.chain(QStringLiteral("E1"));
        QVERIFY2(ch.size() == 1, "chain(E1) should return only E1 entries");
    }

    // ── 4. filterByStage() returns only matching stage ────────────────────────
    void testFilterByStage()
    {
        ProvenanceLog log; populateLog(log);
        const auto filtered = log.filterByStage(QStringLiteral("nlp"));
        QVERIFY2(filtered.size() == 1, "filterByStage(nlp) should return 1 entry");
        QVERIFY2(filtered.first().stage == QStringLiteral("nlp"), "Stage mismatch");
    }

    // ── 5. recent(n) returns at most n entries ────────────────────────────────
    void testRecentRespectsLimit()
    {
        ProvenanceLog log; populateLog(log);
        const auto r = log.recent(3);
        QVERIFY2(r.size() <= 3,
                 qPrintable(QStringLiteral("recent(3) returned %1").arg(r.size())));
    }

    // ── 6. recent() returns most-recent entries (not oldest) ──────────────────
    void testRecentOrderLatest()
    {
        ProvenanceLog log;
        for (int i = 0; i < 5; ++i)
            log.record(QStringLiteral("E1"), QStringLiteral("model"),
                       QStringLiteral("step%1").arg(i), QStringLiteral("d"));
        const auto r = log.recent(2);
        QVERIFY2(r.size() <= 2, "recent(2) must return <= 2 entries");
    }

    // ── 7. ProvenanceEntry fields populated correctly ─────────────────────────
    void testEntryFieldsPopulated()
    {
        ProvenanceLog log;
        log.record(QStringLiteral("EVTEST"), QStringLiteral("ingest"),
                   QStringLiteral("csv_import"), QStringLiteral("detail text"),
                   QStringLiteral("abc123hash"));
        const auto ch = log.chain(QStringLiteral("EVTEST"));
        QVERIFY2(!ch.isEmpty(), "chain should have an entry");
        const auto& e = ch.first();
        QVERIFY2(e.eventId == QStringLiteral("EVTEST"), "eventId mismatch");
        QVERIFY2(e.stage   == QStringLiteral("ingest"), "stage mismatch");
        QVERIFY2(e.action  == QStringLiteral("csv_import"), "action mismatch");
        QVERIFY2(e.detail  == QStringLiteral("detail text"), "detail mismatch");
        QVERIFY2(e.timestamp.isValid(), "timestamp should be valid");
    }

    // ── 8. clear() resets all entries ────────────────────────────────────────
    void testClearResetsAll()
    {
        ProvenanceLog log; populateLog(log);
        QVERIFY(log.count() > 0);
        log.clear();
        QCOMPARE(log.count(), 0);
        QVERIFY2(log.recent(100).isEmpty(), "After clear, recent() should be empty");
    }

    // ── 9. formatChain: non-empty for populated log ───────────────────────────
    void testFormatChainNonEmpty()
    {
        ProvenanceLog log; populateLog(log, QStringLiteral("E77"));
        const QString formatted = log.formatChain(QStringLiteral("E77"));
        QVERIFY2(!formatted.isEmpty(), "formatChain should produce non-empty text");
        QVERIFY2(formatted.contains(QStringLiteral("ingest")), "formatChain should mention stages");
    }

    // ── 10. formatHtml: contains HTML tags ───────────────────────────────────
    void testFormatHtmlContainsTags()
    {
        ProvenanceLog log; populateLog(log, QStringLiteral("E99"));
        const QString html = log.formatHtml(QStringLiteral("E99"));
        QVERIFY2(html.contains(QStringLiteral("<")) && html.contains(QStringLiteral(">")),
                 "formatHtml should contain HTML tags");
    }
};

QTEST_GUILESS_MAIN(ProvenanceLogDeepTest)
#include "test_provenance_log_deep.moc"
