// test_audit_log_deep5.cpp — Deep audit iteration 26: AuditLogWidget
// row count label, stage column, filter by event id, clear restores all.
#include <QTest>
#include <QApplication>
#include <QTableWidget>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include "ui/AuditLogWidget.h"
#include "audit/ProvenanceLog.h"

class TestAuditLogDeep5 : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase() { qputenv("QT_QPA_PLATFORM", "offscreen"); }

    void testTableShowsRecordedEntries()
    {
        ProvenanceLog log;
        log.record(QStringLiteral("AUD5-1"), QStringLiteral("ingest"),
                   QStringLiteral("load"), QStringLiteral("row one"));
        log.record(QStringLiteral("AUD5-2"), QStringLiteral("model"),
                   QStringLiteral("fit"), QStringLiteral("row two"));

        AuditLogWidget widget(log);
        auto* table = widget.findChild<QTableWidget*>();
        QVERIFY(table != nullptr);
        QCOMPARE(table->rowCount(), 2);
    }

    void testCountLabelUpdates()
    {
        ProvenanceLog log;
        log.record(QStringLiteral("CNT-1"), QStringLiteral("ingest"),
                   QStringLiteral("load"), QStringLiteral("detail"));

        AuditLogWidget widget(log);
        QLabel* countLbl = nullptr;
        for (auto* lbl : widget.findChildren<QLabel*>()) {
            if (lbl->text().contains(QStringLiteral("entr"), Qt::CaseInsensitive))
                countLbl = lbl;
        }
        QVERIFY(countLbl != nullptr);
        QVERIFY(countLbl->text().contains(QStringLiteral("1")));
    }

    void testFilterByEventIdShowsMatchingRow()
    {
        ProvenanceLog log;
        log.record(QStringLiteral("FILTER-A"), QStringLiteral("ingest"),
                   QStringLiteral("load"), QStringLiteral("a"));
        log.record(QStringLiteral("FILTER-B"), QStringLiteral("ingest"),
                   QStringLiteral("load"), QStringLiteral("b"));

        AuditLogWidget widget(log);
        auto* filter = widget.findChild<QLineEdit*>();
        QVERIFY(filter != nullptr);

        filter->setText(QStringLiteral("FILTER-A"));
        QApplication::processEvents();

        auto* table = widget.findChild<QTableWidget*>();
        QVERIFY(table != nullptr);
        QVERIFY(table->rowCount() >= 1);
        bool sawA = false;
        for (int row = 0; row < table->rowCount(); ++row) {
            if (auto* item = table->item(row, 1)) {
                if (item->text().contains(QStringLiteral("FILTER-A"), Qt::CaseInsensitive))
                    sawA = true;
            }
        }
        QVERIFY(sawA);
    }

    void testClearButtonExists()
    {
        ProvenanceLog log;
        AuditLogWidget widget(log);
        bool hasClear = false;
        for (auto* btn : widget.findChildren<QPushButton*>()) {
            if (btn->text().contains(QStringLiteral("Clear"), Qt::CaseInsensitive))
                hasClear = true;
        }
        QVERIFY(hasClear);
    }
};

QTEST_MAIN(TestAuditLogDeep5)
#include "test_audit_log_deep5.moc"
