// test_audit_log_deep4.cpp — Deep audit iteration 21: AuditLogWidget
// filter limitations, stage colours, empty filter restore, clear-with-filter.

#include <QTest>
#include <QApplication>
#include <QTableWidget>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>

#include "ui/AuditLogWidget.h"
#include "audit/ProvenanceLog.h"

class TestAuditLogDeep4 : public QObject {
    Q_OBJECT

    static QLabel* countLabel(const AuditLogWidget& widget)
    {
        for (auto* lbl : widget.findChildren<QLabel*>()) {
            if (lbl->text().contains(QStringLiteral("entr")))
                return lbl;
        }
        return nullptr;
    }

    static QTableWidgetItem* stageItem(QTableWidget* table, const QString& stageText)
    {
        for (int row = 0; row < table->rowCount(); ++row) {
            if (auto* item = table->item(row, 2)) {
                if (item->text() == stageText)
                    return item;
            }
        }
        return nullptr;
    }

private slots:
    void initTestCase()
    {
        qputenv("QT_QPA_PLATFORM", "offscreen");
    }

    void testFilterByDetailNotSupported()
    {
        ProvenanceLog log;
        log.record(QStringLiteral("evt_detail"), QStringLiteral("ingest"),
                   QStringLiteral("load"), QStringLiteral("unique_detail_token_xyz"));

        AuditLogWidget widget(log);
        auto* table = widget.findChild<QTableWidget*>();
        auto* filterEdit = widget.findChild<QLineEdit*>();
        QVERIFY(table != nullptr);
        QVERIFY(filterEdit != nullptr);

        filterEdit->setText(QStringLiteral("unique_detail_token_xyz"));
        QApplication::processEvents();

        // BUG AuditLogWidget.cpp:157-160 — filter ignores detail column.
        if (table->rowCount() != 0) {
            QWARN("BUG AuditLogWidget.cpp:157-160: filter ignores detail column");
        }
        QCOMPARE(table->rowCount(), 0);
    }

    void testUnknownStageUsesDefaultGreyColour()
    {
        ProvenanceLog log;
        log.record(QStringLiteral("evt_unknown"), QStringLiteral("custom_stage"),
                   QStringLiteral("run"), QStringLiteral("detail"));

        AuditLogWidget widget(log);
        auto* table = widget.findChild<QTableWidget*>();
        QVERIFY(table != nullptr);

        auto* stage = stageItem(table, QStringLiteral("custom_stage"));
        QVERIFY(stage != nullptr);
        QCOMPARE(stage->foreground().color(), QColor(QStringLiteral("#a0a8b8")));
    }

    void testKnownStageColoursBeyondIngest()
    {
        ProvenanceLog log;
        log.record(QStringLiteral("evt_nlp"), QStringLiteral("nlp"),
                   QStringLiteral("parse"), QStringLiteral("d"));
        log.record(QStringLiteral("evt_model"), QStringLiteral("model"),
                   QStringLiteral("fit"), QStringLiteral("d"));
        log.record(QStringLiteral("evt_inference"), QStringLiteral("inference"),
                   QStringLiteral("score"), QStringLiteral("d"));
        log.record(QStringLiteral("evt_output"), QStringLiteral("output"),
                   QStringLiteral("export"), QStringLiteral("d"));

        AuditLogWidget widget(log);
        auto* table = widget.findChild<QTableWidget*>();
        QVERIFY(table != nullptr);
        QCOMPARE(table->rowCount(), 4);

        QCOMPARE(stageItem(table, QStringLiteral("nlp"))->foreground().color(),
                 QColor(QStringLiteral("#a78bfa")));
        QCOMPARE(stageItem(table, QStringLiteral("model"))->foreground().color(),
                 QColor(QStringLiteral("#34d399")));
        QCOMPARE(stageItem(table, QStringLiteral("inference"))->foreground().color(),
                 QColor(QStringLiteral("#fbbf24")));
        QCOMPARE(stageItem(table, QStringLiteral("output"))->foreground().color(),
                 QColor(QStringLiteral("#e94560")));
    }

    void testEmptyFilterRestoresAllRows()
    {
        ProvenanceLog log;
        log.record(QStringLiteral("evt_a"), QStringLiteral("ingest"),
                   QStringLiteral("a"), QStringLiteral("d"));
        log.record(QStringLiteral("evt_b"), QStringLiteral("nlp"),
                   QStringLiteral("b"), QStringLiteral("d"));

        AuditLogWidget widget(log);
        auto* table = widget.findChild<QTableWidget*>();
        auto* filterEdit = widget.findChild<QLineEdit*>();
        QVERIFY(table != nullptr);
        QVERIFY(filterEdit != nullptr);

        filterEdit->setText(QStringLiteral("nlp"));
        QApplication::processEvents();
        QCOMPARE(table->rowCount(), 1);

        filterEdit->clear();
        QApplication::processEvents();
        QCOMPARE(table->rowCount(), 2);
    }

    void testClearLogWithActiveFilter()
    {
        ProvenanceLog log;
        log.record(QStringLiteral("evt_keep"), QStringLiteral("ingest"),
                   QStringLiteral("a"), QStringLiteral("d"));
        log.record(QStringLiteral("evt_drop"), QStringLiteral("nlp"),
                   QStringLiteral("b"), QStringLiteral("d"));

        AuditLogWidget widget(log);
        auto* table = widget.findChild<QTableWidget*>();
        auto* filterEdit = widget.findChild<QLineEdit*>();
        auto* clearBtn = widget.findChild<QPushButton*>();
        QVERIFY(table != nullptr);
        QVERIFY(filterEdit != nullptr);
        QVERIFY(clearBtn != nullptr);

        filterEdit->setText(QStringLiteral("nlp"));
        QApplication::processEvents();
        QCOMPARE(table->rowCount(), 1);

        QTest::mouseClick(clearBtn, Qt::LeftButton);
        QApplication::processEvents();

        QCOMPARE(log.count(), 0);
        QCOMPARE(table->rowCount(), 0);
        QVERIFY(countLabel(widget)->text().contains(QStringLiteral("0")));
    }

    void testTimestampColumnIsoFormat()
    {
        ProvenanceLog log;
        log.record(QStringLiteral("evt_ts"), QStringLiteral("ingest"),
                   QStringLiteral("load"), QStringLiteral("detail"));

        AuditLogWidget widget(log);
        auto* table = widget.findChild<QTableWidget*>();
        QVERIFY(table != nullptr);
        QCOMPARE(table->rowCount(), 1);

        auto* tsItem = table->item(0, 0);
        QVERIFY(tsItem != nullptr);
        QVERIFY2(tsItem->text().contains(QStringLiteral("-")),
                 qPrintable(QStringLiteral("Expected ISO-like timestamp, got: %1")
                                .arg(tsItem->text())));
        QVERIFY2(tsItem->text().contains(QStringLiteral(":")),
                 qPrintable(tsItem->text()));
    }

    void testTableHasSixColumnsWithExpectedHeaders()
    {
        ProvenanceLog log;
        log.record(QStringLiteral("evt_hdr"), QStringLiteral("ingest"),
                   QStringLiteral("a"), QStringLiteral("d"));

        AuditLogWidget widget(log);
        auto* table = widget.findChild<QTableWidget*>();
        QVERIFY(table != nullptr);
        QCOMPARE(table->columnCount(), 6);

        const QStringList expected = {
            QStringLiteral("Timestamp"),
            QStringLiteral("Event ID"),
            QStringLiteral("Stage"),
            QStringLiteral("Action"),
            QStringLiteral("Detail"),
            QStringLiteral("Hash"),
        };
        for (int col = 0; col < expected.size(); ++col)
            QCOMPARE(table->horizontalHeaderItem(col)->text(), expected.at(col));
    }

    void testFilterByStageCaseInsensitive()
    {
        ProvenanceLog log;
        log.record(QStringLiteral("evt_stage"), QStringLiteral("MODEL"),
                   QStringLiteral("fit"), QStringLiteral("d"));
        log.record(QStringLiteral("evt_other"), QStringLiteral("ingest"),
                   QStringLiteral("load"), QStringLiteral("d"));

        AuditLogWidget widget(log);
        auto* table = widget.findChild<QTableWidget*>();
        auto* filterEdit = widget.findChild<QLineEdit*>();
        QVERIFY(table != nullptr);
        QVERIFY(filterEdit != nullptr);

        filterEdit->setText(QStringLiteral("model"));
        QApplication::processEvents();

        QCOMPARE(table->rowCount(), 1);
        QCOMPARE(table->item(0, 2)->text(), QStringLiteral("MODEL"));
    }
};

QTEST_MAIN(TestAuditLogDeep4)

#include "test_audit_log_deep4.moc"
