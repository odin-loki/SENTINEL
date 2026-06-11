// test_events_table_deep4.cpp — Deep audit iteration 19: EventsTableWidget date
// range filter, row selection/detail, eventSelected signal, search edge cases.

#include <QTest>
#include <QApplication>
#include <QStandardItemModel>
#include <QTableView>
#include <QComboBox>
#include <QLineEdit>
#include <QDateEdit>
#include <QPushButton>
#include <QLabel>
#include <QTextEdit>
#include <QSignalSpy>
#include <QItemSelectionModel>
#include <memory>

#include "ui/EventsTableWidget.h"
#include "core/Database.h"
#include "core/AppConfig.h"
#include "core/CrimeEvent.h"

class TestEventsTableDeep4 : public QObject {
    Q_OBJECT

private:
    static AppConfig memCfg()
    {
        AppConfig cfg;
        cfg.databasePath = QStringLiteral(":memory:");
        return cfg;
    }

    static std::shared_ptr<Database> openDb()
    {
        auto db = std::make_shared<Database>(memCfg());
        db->open();
        return db;
    }

    static CrimeEvent makeEvent(const QString& id, const QString& type,
                                const QString& suburb = QStringLiteral("AlphaSuburb"),
                                const QDateTime& occurred = QDateTime::currentDateTimeUtc())
    {
        CrimeEvent ev;
        ev.eventId      = id;
        ev.id           = id;
        ev.crimeType    = type;
        ev.suburb       = suburb;
        ev.locationRaw  = suburb + QStringLiteral(" High Street");
        ev.ingestedAt   = QDateTime::currentDateTimeUtc();
        ev.occurredAt   = occurred;
        ev.timestamp    = occurred;
        ev.qualityScore = 0.75;
        ev.source       = QStringLiteral("deep4_test");
        return ev;
    }

    static QComboBox* crimeTypeCombo(EventsTableWidget& widget)
    {
        for (auto* combo : widget.findChildren<QComboBox*>()) {
            if (combo->count() > 1 && combo->itemText(0).contains(QStringLiteral("All")))
                return combo;
        }
        return nullptr;
    }

    static QPushButton* filterButton(EventsTableWidget& widget)
    {
        for (auto* btn : widget.findChildren<QPushButton*>()) {
            if (btn->text().contains(QStringLiteral("Filter"), Qt::CaseInsensitive))
                return btn;
        }
        return nullptr;
    }

    static QLineEdit* searchEdit(EventsTableWidget& widget)
    {
        for (auto* edit : widget.findChildren<QLineEdit*>()) {
            if (edit->placeholderText().contains(QStringLiteral("Keyword"), Qt::CaseInsensitive))
                return edit;
        }
        return nullptr;
    }

    static QDateEdit* fromDateEdit(EventsTableWidget& widget)
    {
        const auto edits = widget.findChildren<QDateEdit*>();
        return edits.isEmpty() ? nullptr : edits.first();
    }

    static QDateEdit* toDateEdit(EventsTableWidget& widget)
    {
        const auto edits = widget.findChildren<QDateEdit*>();
        return edits.size() >= 2 ? edits.at(1) : nullptr;
    }

    static int tableRowCount(EventsTableWidget& widget)
    {
        auto* model = widget.findChild<QStandardItemModel*>();
        return model ? model->rowCount() : -1;
    }

    static QTextEdit* detailPanel(EventsTableWidget& widget)
    {
        for (auto* edit : widget.findChildren<QTextEdit*>()) {
            if (edit->placeholderText().contains(QStringLiteral("Select a row"),
                                                 Qt::CaseInsensitive))
                return edit;
        }
        return nullptr;
    }

private slots:
    void initTestCase()
    {
        qputenv("QT_QPA_PLATFORM", "offscreen");
    }

    void testDateRangeFilterExcludesOldEvents()
    {
        auto db = openDb();
        const QDateTime recent = QDateTime::currentDateTimeUtc().addDays(-5);
        const QDateTime old    = QDateTime::currentDateTimeUtc().addDays(-90);

        QVERIFY(db->insertEvent(makeEvent(QStringLiteral("ET4-REC"), QStringLiteral("theft"),
                                          QStringLiteral("Recent"), recent)));
        QVERIFY(db->insertEvent(makeEvent(QStringLiteral("ET4-OLD"), QStringLiteral("theft"),
                                          QStringLiteral("Ancient"), old)));

        EventsTableWidget widget(db);
        widget.refresh();
        QApplication::processEvents();
        QVERIFY2(tableRowCount(widget) >= 2,
                 "Both events should load under default 1-year window");

        auto* from = fromDateEdit(widget);
        auto* to   = toDateEdit(widget);
        auto* btn  = filterButton(widget);
        QVERIFY(from != nullptr);
        QVERIFY(to != nullptr);
        QVERIFY(btn != nullptr);

        from->setDate(QDate::currentDate().addDays(-30));
        to->setDate(QDate::currentDate());
        btn->click();
        QApplication::processEvents();

        QCOMPARE(tableRowCount(widget), 1);
        auto* model = widget.findChild<QStandardItemModel*>();
        QCOMPARE(model->item(0, 0)->text(), QStringLiteral("ET4-REC"));
    }

    void testSearchMatchesAddressNormalisedNotLocationRaw()
    {
        // BUG: search checks locationRaw but not addressNormalised / locationDescription.
        auto db = openDb();
        CrimeEvent ev;
        ev.eventId           = QStringLiteral("ET4-ADDR");
        ev.id                = ev.eventId;
        ev.crimeType         = QStringLiteral("burglary");
        ev.suburb            = QStringLiteral("NormSuburb");
        ev.addressNormalised = QStringLiteral("UniqueNormAddress42");
        ev.ingestedAt        = QDateTime::currentDateTimeUtc();
        ev.occurredAt        = QDateTime::currentDateTimeUtc();
        ev.timestamp         = ev.occurredAt.value();
        ev.source            = QStringLiteral("deep4_test");
        QVERIFY(db->insertEvent(ev));

        EventsTableWidget widget(db);
        widget.refresh();
        QApplication::processEvents();

        searchEdit(widget)->setText(QStringLiteral("UniqueNormAddress42"));
        filterButton(widget)->click();
        QApplication::processEvents();

        QVERIFY2(tableRowCount(widget) >= 1,
                 "Search should match addressNormalised-derived locationDescription");
    }

    void testSearchMatchesNarrativeText()
    {
        auto db = openDb();
        CrimeEvent ev = makeEvent(QStringLiteral("ET4-NAR"), QStringLiteral("theft"));
        ev.narrative = QStringLiteral("UniqueNarrativeTokenDeep4");
        QVERIFY(db->insertEvent(ev));

        EventsTableWidget widget(db);
        widget.refresh();
        QApplication::processEvents();

        searchEdit(widget)->setText(QStringLiteral("UniqueNarrativeTokenDeep4"));
        filterButton(widget)->click();
        QApplication::processEvents();

        QCOMPARE(tableRowCount(widget), 1);
        QCOMPARE(widget.findChild<QStandardItemModel*>()->item(0, 0)->text(),
                 QStringLiteral("ET4-NAR"));
    }

    void testRowSelectionPopulatesDetailPanel()
    {
        auto db = openDb();
        QVERIFY(db->insertEvent(makeEvent(QStringLiteral("ET4-DET"), QStringLiteral("assault"),
                                          QStringLiteral("DetailTown"))));

        EventsTableWidget widget(db);
        widget.refresh();
        QApplication::processEvents();

        auto* table = widget.findChild<QTableView*>();
        auto* detail = detailPanel(widget);
        QVERIFY(table != nullptr);
        QVERIFY(detail != nullptr);
        QVERIFY(detail->toPlainText().trimmed().isEmpty());

        const QModelIndex idx = table->model()->index(0, 0);
        table->selectionModel()->setCurrentIndex(idx, QItemSelectionModel::ClearAndSelect
                                                           | QItemSelectionModel::Rows);
        QApplication::processEvents();

        QVERIFY(detail->toHtml().contains(QStringLiteral("ET4-DET")));
        QVERIFY(detail->toHtml().contains(QStringLiteral("ASSAULT")));
    }

    void testEventSelectedSignalEmitted()
    {
        auto db = openDb();
        QVERIFY(db->insertEvent(makeEvent(QStringLiteral("ET4-SIG"), QStringLiteral("theft"))));

        EventsTableWidget widget(db);
        QSignalSpy spy(&widget, &EventsTableWidget::eventSelected);
        widget.refresh();
        QApplication::processEvents();

        auto* table = widget.findChild<QTableView*>();
        const QModelIndex idx = table->model()->index(0, 0);
        table->selectionModel()->setCurrentIndex(idx, QItemSelectionModel::ClearAndSelect
                                                           | QItemSelectionModel::Rows);
        QApplication::processEvents();

        QCOMPARE(spy.count(), 1);
    }

    void testReturnKeyOnSearchTriggersFilter()
    {
        auto db = openDb();
        QVERIFY(db->insertEvent(makeEvent(QStringLiteral("ET4-KEY"), QStringLiteral("theft"),
                                          QStringLiteral("KeySuburb"))));
        QVERIFY(db->insertEvent(makeEvent(QStringLiteral("ET4-OTH"), QStringLiteral("assault"),
                                          QStringLiteral("OtherSuburb"))));

        EventsTableWidget widget(db);
        widget.refresh();
        QApplication::processEvents();

        searchEdit(widget)->setText(QStringLiteral("KeySuburb"));
        QTest::keyClick(searchEdit(widget), Qt::Key_Return);
        QApplication::processEvents();

        QCOMPARE(tableRowCount(widget), 1);
    }

    void testQualityBadgeReflectsScore()
    {
        auto db = openDb();
        CrimeEvent high = makeEvent(QStringLiteral("ET4-HI"), QStringLiteral("theft"));
        high.qualityScore = 0.85;
        QVERIFY(db->insertEvent(high));

        EventsTableWidget widget(db);
        widget.refresh();
        QApplication::processEvents();

        auto* model = widget.findChild<QStandardItemModel*>();
        QVERIFY(model != nullptr);
        const QString badge = model->item(0, 6)->text();
        QVERIFY(badge.contains(QStringLiteral("High")));
    }

    void testDoubleRefreshStableRowCount()
    {
        auto db = openDb();
        for (int i = 0; i < 3; ++i)
            QVERIFY(db->insertEvent(makeEvent(QStringLiteral("ET4-%1").arg(i),
                                               QStringLiteral("theft"))));

        EventsTableWidget widget(db);
        widget.refresh();
        QApplication::processEvents();
        widget.refresh();
        QApplication::processEvents();

        QCOMPARE(tableRowCount(widget), 3);
    }
};

QTEST_MAIN(TestEventsTableDeep4)

#include "test_events_table_deep4.moc"
