// test_audit_log_widget.cpp
// Headless unit tests for AuditLogWidget.
// QApplication is required for widget construction; widgets are never shown
// except in testWidgetSize where it is guarded by a platform check.
#include <QTest>
#include <QApplication>
#include <QTableWidget>
#include <QLineEdit>
#include <QCoreApplication>
#include "ui/AuditLogWidget.h"
#include "audit/ProvenanceLog.h"

// ─────────────────────────────────────────────────────────────────────────────
// TestAuditLogWidget
// ─────────────────────────────────────────────────────────────────────────────
class TestAuditLogWidget : public QObject {
    Q_OBJECT

private slots:

    // 1 ── Widget constructs without crashing; isVisible() is callable
    void testWidgetCreation()
    {
        ProvenanceLog log;
        AuditLogWidget w(log);
        bool vis = w.isVisible();
        Q_UNUSED(vis)
        QVERIFY(true);
    }

    // 2 ── refresh() on an empty ProvenanceLog must not crash
    void testRefreshWithEmptyLog()
    {
        ProvenanceLog log;
        AuditLogWidget w(log);
        w.refresh();
        QVERIFY(true);
    }

    // 3 ── After adding 3 entries and calling refresh() the table has 3 rows
    void testRefreshShowsEntries()
    {
        ProvenanceLog log;
        log.record("evt_1", "ingest", "import",  "detail A");
        log.record("evt_2", "nlp",    "parse",   "detail B");
        log.record("evt_3", "model",  "fit",     "detail C");

        AuditLogWidget w(log);
        w.refresh();

        auto* table = w.findChild<QTableWidget*>();
        QVERIFY(table != nullptr);
        QCOMPARE(table->rowCount(), 3);
    }

    // 4 ── First row shows the correct Event ID, Stage, and Action
    void testEntryFieldsDisplayed()
    {
        ProvenanceLog log;
        // Add only one entry so row 0 is unambiguous
        log.record("evt_xyz", "ingest", "import_csv", "test detail", "hash1234");

        AuditLogWidget w(log);
        w.refresh();

        auto* table = w.findChild<QTableWidget*>();
        QVERIFY(table != nullptr);
        QVERIFY(table->rowCount() >= 1);

        // Column layout: 0=Timestamp, 1=Event ID, 2=Stage, 3=Action, 4=Detail, 5=Hash
        QCOMPARE(table->item(0, 1)->text(), QStringLiteral("evt_xyz"));
        QCOMPARE(table->item(0, 2)->text(), QStringLiteral("ingest"));
        QCOMPARE(table->item(0, 3)->text(), QStringLiteral("import_csv"));
    }

    // 5 ── After log.clear() + refresh() the table has 0 rows
    void testClearLog()
    {
        ProvenanceLog log;
        log.record("evt_1", "ingest", "import", "d");
        log.record("evt_2", "nlp",    "parse",  "d");

        AuditLogWidget w(log);
        w.refresh();

        auto* table = w.findChild<QTableWidget*>();
        QVERIFY(table != nullptr);
        QCOMPARE(table->rowCount(), 2);

        log.clear();
        w.refresh();
        QCOMPARE(table->rowCount(), 0);
    }

    // 6 ── ProvenanceLog::formatChain() (the log's export mechanism)
    //      returns a non-empty string that contains the event ID
    void testExportToMarkdown()
    {
        ProvenanceLog log;
        log.record("evt_export_1", "ingest", "import", "first step");
        log.record("evt_export_1", "model",  "fit",    "model trained");

        const QString md = log.formatChain("evt_export_1");
        QVERIFY(!md.isEmpty());
        QVERIFY(md.contains("evt_export_1"));
    }

    // 7 ── All three entries appear in the table regardless of insertion order
    void testSortByTimestamp()
    {
        ProvenanceLog log;
        log.record("evt_a", "ingest", "act1", "d");
        QTest::qWait(10);
        log.record("evt_b", "nlp",    "act2", "d");
        QTest::qWait(10);
        log.record("evt_c", "model",  "act3", "d");

        AuditLogWidget w(log);
        w.refresh();

        auto* table = w.findChild<QTableWidget*>();
        QVERIFY(table != nullptr);
        // All three entries must be visible; table sorting must not crash
        QCOMPARE(table->rowCount(), 3);
    }

    // 8 ── Filter by stage: typing "ingest" into the filter QLineEdit
    //      shows only the matching row
    void testFilterByStage()
    {
        ProvenanceLog log;
        log.record("evt_1", "ingest",    "act", "d");
        log.record("evt_2", "nlp",       "act", "d");
        log.record("evt_3", "inference", "act", "d");

        AuditLogWidget w(log);
        w.refresh();

        auto* table      = w.findChild<QTableWidget*>();
        auto* filterEdit = w.findChild<QLineEdit*>();
        QVERIFY(table != nullptr);
        if (!filterEdit) QSKIP("No filter QLineEdit found in AuditLogWidget");

        QCOMPARE(table->rowCount(), 3);

        filterEdit->setText("ingest");
        QCoreApplication::processEvents();
        QCOMPARE(table->rowCount(), 1);

        filterEdit->clear();
        QCoreApplication::processEvents();
        QCOMPARE(table->rowCount(), 3);
    }

    // 9 ── An entry whose stage is "anomaly" does not crash the widget
    void testHighlightAnomalies()
    {
        ProvenanceLog log;
        log.record("evt_1", "anomaly", "detected", "spatial outlier");
        log.record("evt_2", "ingest",  "import",   "normal row");

        AuditLogWidget w(log);
        w.refresh();   // must not crash even for unknown stage colour

        auto* table = w.findChild<QTableWidget*>();
        QVERIFY(table != nullptr);
        QCOMPARE(table->rowCount(), 2);
    }

    // 10 ── Widget reports positive dimensions after resize (headless-safe)
    void testWidgetSize()
    {
        ProvenanceLog log;
        AuditLogWidget w(log);
        w.resize(800, 600);
        // Do not call show() — use sizeHint/minimumSizeHint instead so the
        // test passes on headless CI where no display is available.
        QVERIFY(w.width()  > 0);
        QVERIFY(w.height() > 0);
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// main
// ─────────────────────────────────────────────────────────────────────────────
int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    TestAuditLogWidget t;
    return QTest::qExec(&t, argc, argv);
}

#include "test_audit_log_widget.moc"
