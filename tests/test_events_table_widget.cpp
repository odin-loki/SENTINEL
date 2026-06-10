// test_events_table_widget.cpp
// Headless unit tests for EventsTableWidget.
// Run with: test_events_table_widget.exe -platform offscreen
#include <QTest>
#include <QApplication>
#include <QSignalSpy>
#include <QStandardItemModel>
#include <QTableView>
#include <QComboBox>
#include <QLineEdit>
#include <QDateEdit>
#include <QPushButton>
#include <QDialog>
#include <QTimer>
#include <QCoreApplication>
#include <memory>

#include "ui/EventsTableWidget.h"
#include "core/Database.h"
#include "core/AppConfig.h"
#include "core/CrimeEvent.h"

// ─── helpers ─────────────────────────────────────────────────────────────────

static std::shared_ptr<Database> makeMemoryDb()
{
    AppConfig cfg;
    cfg.databasePath = ":memory:";
    auto db = std::make_shared<Database>(cfg);
    db->open();
    return db;
}

static CrimeEvent makeEvent(const QString& id,
                             const QString& crimeType,
                             const QDate&   date    = QDate::currentDate(),
                             double         quality = 0.8)
{
    CrimeEvent ev;
    ev.eventId      = id;
    ev.id           = id;
    ev.source       = "test";
    ev.crimeType    = crimeType;
    ev.ingestedAt   = QDateTime::currentDateTimeUtc();
    ev.occurredAt   = QDateTime(date, QTime(12, 0, 0), Qt::LocalTime);
    ev.timestamp    = *ev.occurredAt;
    ev.qualityScore = quality;
    ev.suburb       = "TestSuburb";
    return ev;
}

// ─── test class ──────────────────────────────────────────────────────────────

class TestEventsTableWidget : public QObject {
    Q_OBJECT

private slots:

    // 1. Widget constructs without crash
    void testWidgetCreation()
    {
        auto db = makeMemoryDb();
        EventsTableWidget w(db);
        QVERIFY(true);
    }

    // 2. refresh() doesn't crash on empty DB
    void testRefreshEmptyDb()
    {
        auto db = makeMemoryDb();
        EventsTableWidget w(db);
        w.refresh();
        QVERIFY(true);
    }

    // 3. Model has correct column count (7 columns)
    void testModelColumnCount()
    {
        auto db = makeMemoryDb();
        EventsTableWidget w(db);
        auto* model = w.findChild<QStandardItemModel*>();
        QVERIFY(model != nullptr);
        QCOMPARE(model->columnCount(), 7);
    }

    // 4. After inserting events and calling refresh(), row count matches
    void testRowCountAfterInsert()
    {
        auto db = makeMemoryDb();
        db->insertEvent(makeEvent("e1", "theft"));
        db->insertEvent(makeEvent("e2", "assault"));
        db->insertEvent(makeEvent("e3", "burglary"));
        EventsTableWidget w(db);
        w.refresh();

        auto* model = w.findChild<QStandardItemModel*>();
        QVERIFY(model != nullptr);
        QCOMPARE(model->rowCount(), 3);
    }

    // 5. Filter by crime type reduces displayed count
    void testFilterByCrimeType()
    {
        auto db = makeMemoryDb();
        db->insertEvent(makeEvent("e1", "theft"));
        db->insertEvent(makeEvent("e2", "theft"));
        db->insertEvent(makeEvent("e3", "assault"));
        EventsTableWidget w(db);
        w.refresh();

        auto* model = w.findChild<QStandardItemModel*>();
        auto* combo = w.findChild<QComboBox*>();
        QVERIFY(model != nullptr && combo != nullptr);
        QCOMPARE(model->rowCount(), 3);

        const int idx = combo->findData("theft");
        if (idx < 0) QSKIP("'theft' not found in crime-type combo");
        combo->setCurrentIndex(idx);
        w.refresh();

        QCOMPARE(model->rowCount(), 2);
    }

    // 6. Date filter works — narrowing the to-date hides events outside range
    void testDateFilter()
    {
        auto db = makeMemoryDb();
        const QDate today = QDate::currentDate();
        db->insertEvent(makeEvent("e1", "theft",   today));
        db->insertEvent(makeEvent("e2", "assault", today));
        db->insertEvent(makeEvent("e3", "burglary",today));
        EventsTableWidget w(db);
        w.refresh();

        auto* model = w.findChild<QStandardItemModel*>();
        QVERIFY(model != nullptr);
        QCOMPARE(model->rowCount(), 3);

        // Move toDate to yesterday; today's events should be excluded
        auto dateEdits = w.findChildren<QDateEdit*>();
        QVERIFY(dateEdits.size() >= 2);
        // dateEdits[1] is m_toDate (second QDateEdit created in setupUI)
        dateEdits[1]->setDate(today.addDays(-1));
        w.refresh();

        QCOMPARE(model->rowCount(), 0);
    }

    // 7. qualityBadge returns non-empty string for score 0.8 (tested via model column 6)
    void testQualityBadge()
    {
        auto db = makeMemoryDb();
        db->insertEvent(makeEvent("e1", "theft", QDate::currentDate(), 0.8));
        EventsTableWidget w(db);
        w.refresh();

        auto* model = w.findChild<QStandardItemModel*>();
        QVERIFY(model != nullptr);
        QVERIFY(model->rowCount() >= 1);
        // Column 6 is "Quality"; qualityBadge() is called to populate it
        const QString badge = model->item(0, 6)->text();
        QVERIFY(!badge.isEmpty());
    }

    // 8. onExportCsv doesn't crash — dismiss the file dialog programmatically
    void testExportCsvNoCrash()
    {
        auto db = makeMemoryDb();
        db->insertEvent(makeEvent("e1", "theft"));
        EventsTableWidget w(db);
        w.refresh();

        auto buttons = w.findChildren<QPushButton*>();
        QPushButton* exportBtn = nullptr;
        for (auto* btn : buttons) {
            if (btn->text().contains("Export", Qt::CaseInsensitive)) {
                exportBtn = btn;
                break;
            }
        }
        if (!exportBtn) QSKIP("Export button not found");

        // Dismiss any dialog (e.g. QFileDialog or QMessageBox) that pops up
        QTimer::singleShot(100, []() {
            const auto widgets = QApplication::topLevelWidgets();
            for (QWidget* w : widgets) {
                if (auto* d = qobject_cast<QDialog*>(w)) {
                    d->reject();
                    return;
                }
            }
        });

        QTest::mouseClick(exportBtn, Qt::LeftButton);
        QCoreApplication::processEvents(QEventLoop::AllEvents, 300);
        QVERIFY(true);
    }

    // 9. Search text filter reduces displayed count
    void testSearchFilterReducesCount()
    {
        auto db = makeMemoryDb();
        db->insertEvent(makeEvent("alpha_001", "theft"));
        db->insertEvent(makeEvent("bravo_002", "assault"));
        db->insertEvent(makeEvent("charlie_003", "burglary"));
        EventsTableWidget w(db);
        w.refresh();

        auto* model  = w.findChild<QStandardItemModel*>();
        auto* search = w.findChild<QLineEdit*>();
        QVERIFY(model != nullptr && search != nullptr);
        QCOMPARE(model->rowCount(), 3);

        search->setText("alpha_001");
        w.refresh();

        QCOMPARE(model->rowCount(), 1);
    }

    // 10. eventSelected signal is emitted when a row is selected
    void testEventSelectedSignal()
    {
        auto db = makeMemoryDb();
        db->insertEvent(makeEvent("e1", "theft"));
        EventsTableWidget w(db);
        w.refresh();

        auto* model = w.findChild<QStandardItemModel*>();
        auto* table = w.findChild<QTableView*>();
        QVERIFY(model != nullptr && table != nullptr);
        QVERIFY(model->rowCount() >= 1);

        QSignalSpy spy(&w, &EventsTableWidget::eventSelected);
        table->selectionModel()->setCurrentIndex(
            model->index(0, 0),
            QItemSelectionModel::SelectCurrent);
        QCoreApplication::processEvents();

        QCOMPARE(spy.count(), 1);
    }
};

// ─── main ────────────────────────────────────────────────────────────────────

int main(int argc, char** argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    TestEventsTableWidget t;
    return QTest::qExec(&t, argc, argv);
}

#include "test_events_table_widget.moc"
