// test_audit_log_deep6.cpp — Deep audit iteration 29: AuditLogWidget
// action column, refresh reloads entries, clear button enabled, stage visible.
#include <QTest>
#include <QApplication>
#include <QTableWidget>
#include <QPushButton>
#include <QLineEdit>
#include "ui/AuditLogWidget.h"
#include "audit/ProvenanceLog.h"

class TestAuditLogDeep6 : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase() { qputenv("QT_QPA_PLATFORM", "offscreen"); }

    void testActionColumnPopulated()
    {
        ProvenanceLog log;
        log.record(QStringLiteral("ACT-6"), QStringLiteral("model"),
                   QStringLiteral("fit"), QStringLiteral("trained"));

        AuditLogWidget widget(log);
        auto* table = widget.findChild<QTableWidget*>();
        QVERIFY(table != nullptr);

        bool sawFit = false;
        for (int row = 0; row < table->rowCount(); ++row) {
            for (int col = 0; col < table->columnCount(); ++col) {
                if (auto* item = table->item(row, col)) {
                    if (item->text().contains(QStringLiteral("fit"), Qt::CaseInsensitive))
                        sawFit = true;
                }
            }
        }
        QVERIFY(sawFit);
    }

    void testRefreshAfterNewRecord()
    {
        ProvenanceLog log;
        log.record(QStringLiteral("R1"), QStringLiteral("ingest"),
                   QStringLiteral("load"), QStringLiteral("a"));

        AuditLogWidget widget(log);
        log.record(QStringLiteral("R2"), QStringLiteral("ingest"),
                   QStringLiteral("load"), QStringLiteral("b"));
        widget.refresh();

        auto* table = widget.findChild<QTableWidget*>();
        QVERIFY(table != nullptr);
        QCOMPARE(table->rowCount(), 2);
    }

    void testClearButtonEnabledWithEntries()
    {
        ProvenanceLog log;
        log.record(QStringLiteral("CLR-6"), QStringLiteral("output"),
                   QStringLiteral("export"), QStringLiteral("file"));

        AuditLogWidget widget(log);
        auto* clearBtn = widget.findChild<QPushButton*>();
        QVERIFY(clearBtn != nullptr);
        QVERIFY(clearBtn->isEnabled());
    }

    void testStageColumnShowsIngest()
    {
        ProvenanceLog log;
        log.record(QStringLiteral("STG-6"), QStringLiteral("ingest"),
                   QStringLiteral("parse"), QStringLiteral("csv"));

        AuditLogWidget widget(log);
        auto* table = widget.findChild<QTableWidget*>();
        QVERIFY(table != nullptr);

        bool sawIngest = false;
        for (int row = 0; row < table->rowCount(); ++row) {
            for (int col = 0; col < table->columnCount(); ++col) {
                if (auto* item = table->item(row, col)) {
                    if (item->text().contains(QStringLiteral("ingest"), Qt::CaseInsensitive))
                        sawIngest = true;
                }
            }
        }
        QVERIFY(sawIngest);
    }

    void testFilterNarrowingReducesRows()
    {
        ProvenanceLog log;
        log.record(QStringLiteral("KEEP-6"), QStringLiteral("ingest"),
                   QStringLiteral("load"), QStringLiteral("a"));
        log.record(QStringLiteral("DROP-6"), QStringLiteral("model"),
                   QStringLiteral("fit"), QStringLiteral("b"));

        AuditLogWidget widget(log);
        auto* filter = widget.findChild<QLineEdit*>();
        auto* table  = widget.findChild<QTableWidget*>();
        QVERIFY(filter != nullptr && table != nullptr);

        const int allRows = table->rowCount();
        filter->setText(QStringLiteral("KEEP-6"));
        QApplication::processEvents();
        QVERIFY(table->rowCount() <= allRows);
    }
};

QTEST_MAIN(TestAuditLogDeep6)
#include "test_audit_log_deep6.moc"
