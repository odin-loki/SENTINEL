// test_audit_log_deep2.cpp — Deep audit iteration 14: AuditLogWidget display updates.

#include <QTest>
#include <QApplication>
#include <QTableWidget>
#include <QLabel>
#include <QLineEdit>

#include "ui/AuditLogWidget.h"
#include "audit/ProvenanceLog.h"

class TestAuditLogDeep2 : public QObject {
    Q_OBJECT

private slots:
    void initTestCase()
    {
        qputenv("QT_QPA_PLATFORM", "offscreen");
    }

    void testConstructShowsExistingEntries()
    {
        ProvenanceLog log;
        log.record(QStringLiteral("evt_init"), QStringLiteral("ingest"),
                   QStringLiteral("import"), QStringLiteral("initial entry"));

        AuditLogWidget widget(log);

        auto* table = widget.findChild<QTableWidget*>();
        QVERIFY(table != nullptr);
        QCOMPARE(table->rowCount(), 1);
        QCOMPARE(table->item(0, 1)->text(), QStringLiteral("evt_init"));
    }

    void testAppendEntryUpdatesDisplay()
    {
        ProvenanceLog log;
        AuditLogWidget widget(log);

        auto* table = widget.findChild<QTableWidget*>();
        QVERIFY(table != nullptr);
        QCOMPARE(table->rowCount(), 0);

        log.record(QStringLiteral("evt_deep2_a"), QStringLiteral("model"),
                   QStringLiteral("fit"), QStringLiteral("poisson trained"));
        log.record(QStringLiteral("evt_deep2_b"), QStringLiteral("inference"),
                   QStringLiteral("score"), QStringLiteral("risk computed"));

        widget.refresh();
        QApplication::processEvents();

        QCOMPARE(table->rowCount(), 2);

        bool foundCountLabel = false;
        for (auto* lbl : widget.findChildren<QLabel*>()) {
            if (lbl->text().contains(QStringLiteral("2"))) {
                foundCountLabel = true;
                break;
            }
        }
        QVERIFY(foundCountLabel);

        bool foundModelStage = false;
        bool foundScoreAction = false;
        for (int row = 0; row < table->rowCount(); ++row) {
            if (table->item(row, 2) && table->item(row, 2)->text() == QStringLiteral("model"))
                foundModelStage = true;
            if (table->item(row, 3) && table->item(row, 3)->text() == QStringLiteral("score"))
                foundScoreAction = true;
        }
        QVERIFY(foundModelStage);
        QVERIFY(foundScoreAction);
    }

    void testFilterUpdatesRowCount()
    {
        ProvenanceLog log;
        log.record(QStringLiteral("evt_x"), QStringLiteral("ingest"), QStringLiteral("a"), QString());
        log.record(QStringLiteral("evt_y"), QStringLiteral("nlp"), QStringLiteral("b"), QString());

        AuditLogWidget widget(log);
        auto* table = widget.findChild<QTableWidget*>();
        auto* filterEdit = widget.findChild<QLineEdit*>();
        QVERIFY(table != nullptr);
        QVERIFY(filterEdit != nullptr);
        QCOMPARE(table->rowCount(), 2);

        filterEdit->setText(QStringLiteral("nlp"));
        QApplication::processEvents();
        QCOMPARE(table->rowCount(), 1);
        QCOMPARE(table->item(0, 2)->text(), QStringLiteral("nlp"));
    }
};

QTEST_MAIN(TestAuditLogDeep2)

#include "test_audit_log_deep2.moc"
