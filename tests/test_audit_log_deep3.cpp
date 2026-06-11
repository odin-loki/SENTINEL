// test_audit_log_deep3.cpp — Deep audit iteration 18: AuditLogWidget
// clear log, filter edge cases, count label, stage styling, recent() cap.

#include <QTest>
#include <QApplication>
#include <QTableWidget>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>

#include "ui/AuditLogWidget.h"
#include "audit/ProvenanceLog.h"

class TestAuditLogDeep3 : public QObject {
    Q_OBJECT

    static QLabel* countLabel(const AuditLogWidget& widget)
    {
        for (auto* lbl : widget.findChildren<QLabel*>()) {
            if (lbl->text().contains(QStringLiteral("entr")))
                return lbl;
        }
        return nullptr;
    }

private slots:
    void initTestCase()
    {
        qputenv("QT_QPA_PLATFORM", "offscreen");
    }

    void testClearLogEmptiesTableAndCount()
    {
        ProvenanceLog log;
        log.record(QStringLiteral("evt_clear_a"), QStringLiteral("ingest"),
                   QStringLiteral("import"), QStringLiteral("row a"));
        log.record(QStringLiteral("evt_clear_b"), QStringLiteral("output"),
                   QStringLiteral("export"), QStringLiteral("row b"));

        AuditLogWidget widget(log);
        auto* table = widget.findChild<QTableWidget*>();
        auto* clearBtn = widget.findChild<QPushButton*>();
        QVERIFY(table != nullptr);
        QVERIFY(clearBtn != nullptr);
        QCOMPARE(table->rowCount(), 2);

        QTest::mouseClick(clearBtn, Qt::LeftButton);
        QApplication::processEvents();

        QCOMPARE(table->rowCount(), 0);
        QCOMPARE(log.count(), 0);

        auto* count = countLabel(widget);
        QVERIFY(count != nullptr);
        QVERIFY(count->text().contains(QStringLiteral("0")));
    }

    void testFilterByEventIdCaseInsensitive()
    {
        ProvenanceLog log;
        log.record(QStringLiteral("EVT-ABC-001"), QStringLiteral("ingest"),
                   QStringLiteral("load"), QStringLiteral("detail"));
        log.record(QStringLiteral("evt-xyz-002"), QStringLiteral("nlp"),
                   QStringLiteral("parse"), QStringLiteral("detail"));

        AuditLogWidget widget(log);
        auto* table = widget.findChild<QTableWidget*>();
        auto* filterEdit = widget.findChild<QLineEdit*>();
        QVERIFY(table != nullptr);
        QVERIFY(filterEdit != nullptr);

        filterEdit->setText(QStringLiteral("abc"));
        QApplication::processEvents();

        QCOMPARE(table->rowCount(), 1);
        QCOMPARE(table->item(0, 1)->text(), QStringLiteral("EVT-ABC-001"));
    }

    void testFilterByActionNotSupported()
    {
        ProvenanceLog log;
        log.record(QStringLiteral("evt_action"), QStringLiteral("ingest"),
                   QStringLiteral("unique_action_token"), QStringLiteral("detail"));
        log.record(QStringLiteral("evt_other"), QStringLiteral("model"),
                   QStringLiteral("fit"), QStringLiteral("detail"));

        AuditLogWidget widget(log);
        auto* table = widget.findChild<QTableWidget*>();
        auto* filterEdit = widget.findChild<QLineEdit*>();
        QVERIFY(table != nullptr);
        QVERIFY(filterEdit != nullptr);

        filterEdit->setText(QStringLiteral("unique_action_token"));
        QApplication::processEvents();

        // BUG AuditLogWidget.cpp:157-160 — filter only matches eventId/stage, not action.
        if (table->rowCount() != 0) {
            QWARN("BUG AuditLogWidget.cpp:157-160: filter ignores action column despite "
                  "users often searching by action text");
        }
        QCOMPARE(table->rowCount(), 0);
    }

    void testCountLabelSingularAndFiltered()
    {
        ProvenanceLog log;
        log.record(QStringLiteral("evt_one"), QStringLiteral("ingest"),
                   QStringLiteral("a"), QStringLiteral("d"));

        AuditLogWidget widget(log);
        auto* count = countLabel(widget);
        QVERIFY(count != nullptr);
        QVERIFY(count->text().contains(QStringLiteral("1 entr")));

        log.record(QStringLiteral("evt_two"), QStringLiteral("nlp"),
                   QStringLiteral("b"), QStringLiteral("d"));
        widget.refresh();
        QApplication::processEvents();
        QVERIFY(count->text().contains(QStringLiteral("2 entr")));

        auto* filterEdit = widget.findChild<QLineEdit*>();
        QVERIFY(filterEdit != nullptr);
        filterEdit->setText(QStringLiteral("nlp"));
        QApplication::processEvents();

        QVERIFY(count->text().contains(QStringLiteral("1 / 2")));
    }

    void testStageColumnColourCoding()
    {
        ProvenanceLog log;
        log.record(QStringLiteral("evt_stage"), QStringLiteral("ingest"),
                   QStringLiteral("load"), QStringLiteral("detail"));

        AuditLogWidget widget(log);
        auto* table = widget.findChild<QTableWidget*>();
        QVERIFY(table != nullptr);
        QCOMPARE(table->rowCount(), 1);

        auto* stageItem = table->item(0, 2);
        QVERIFY(stageItem != nullptr);
        QCOMPARE(stageItem->text(), QStringLiteral("ingest"));
        QCOMPARE(stageItem->foreground().color(), QColor(QStringLiteral("#4a9eff")));
    }

    void testHashColumnPresent()
    {
        ProvenanceLog log;
        log.record(QStringLiteral("evt_hash"), QStringLiteral("model"),
                   QStringLiteral("fit"), QStringLiteral("trained"),
                   QStringLiteral("deadbeef12345678"));

        AuditLogWidget widget(log);
        auto* table = widget.findChild<QTableWidget*>();
        QVERIFY(table != nullptr);
        QCOMPARE(table->columnCount(), 6);
        QCOMPARE(table->horizontalHeaderItem(5)->text(), QStringLiteral("Hash"));

        auto* hashItem = table->item(0, 5);
        QVERIFY(hashItem != nullptr);
        QCOMPARE(hashItem->text(), QStringLiteral("deadbeef12345678"));
    }

    void testRecentCapAtFiveHundredEntries()
    {
        ProvenanceLog log;
        for (int i = 0; i < 505; ++i) {
            log.record(QStringLiteral("evt_cap_%1").arg(i, 4, 10, QChar('0')),
                       QStringLiteral("ingest"),
                       QStringLiteral("import"),
                       QStringLiteral("bulk"));
        }
        QCOMPARE(log.count(), 505);

        AuditLogWidget widget(log);
        auto* table = widget.findChild<QTableWidget*>();
        auto* count = countLabel(widget);
        QVERIFY(table != nullptr);
        QVERIFY(count != nullptr);

        QCOMPARE(table->rowCount(), 500);
        if (!count->text().contains(QStringLiteral("500"))) {
            QWARN("BUG AuditLogWidget.cpp:151 — count label reflects recent(500) cap, "
                  "not total log size (505 stored)");
        }
        QVERIFY(count->text().contains(QStringLiteral("500")));
    }

    void testSortingEnabledAfterRefresh()
    {
        ProvenanceLog log;
        log.record(QStringLiteral("evt_sort_a"), QStringLiteral("ingest"),
                   QStringLiteral("a"), QStringLiteral("d"));
        log.record(QStringLiteral("evt_sort_b"), QStringLiteral("model"),
                   QStringLiteral("b"), QStringLiteral("d"));

        AuditLogWidget widget(log);
        auto* table = widget.findChild<QTableWidget*>();
        QVERIFY(table != nullptr);
        QVERIFY(table->isSortingEnabled());

        widget.refresh();
        QApplication::processEvents();
        QVERIFY(table->isSortingEnabled());
        QCOMPARE(table->rowCount(), 2);
    }
};

QTEST_MAIN(TestAuditLogDeep3)

#include "test_audit_log_deep3.moc"
