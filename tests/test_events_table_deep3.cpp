// test_events_table_deep3.cpp — Deep audit iteration 16: EventsTableWidget
// construct, refresh on empty/populated DB, crime-type and search filters.

#include <QTest>
#include <QApplication>
#include <QStandardItemModel>
#include <QTableView>
#include <QComboBox>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <memory>

#include "ui/EventsTableWidget.h"
#include "core/Database.h"
#include "core/AppConfig.h"
#include "core/CrimeEvent.h"

class TestEventsTableDeep3 : public QObject {
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
        return ev;
    }

    static void seedMixedEvents(const std::shared_ptr<Database>& db)
    {
        QVERIFY2(db->insertEvent(makeEvent(QStringLiteral("ET3-001"),
                                           QStringLiteral("theft"),
                                           QStringLiteral("Northside"))),
                 qPrintable(db->lastError()));
        QVERIFY2(db->insertEvent(makeEvent(QStringLiteral("ET3-002"),
                                           QStringLiteral("assault"),
                                           QStringLiteral("Southside"))),
                 qPrintable(db->lastError()));
        QVERIFY2(db->insertEvent(makeEvent(QStringLiteral("ET3-003"),
                                           QStringLiteral("burglary"),
                                           QStringLiteral("Eastside"))),
                 qPrintable(db->lastError()));
        QVERIFY2(db->insertEvent(makeEvent(QStringLiteral("ET3-004"),
                                           QStringLiteral("theft"),
                                           QStringLiteral("Westside"))),
                 qPrintable(db->lastError()));
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

    static int tableRowCount(EventsTableWidget& widget)
    {
        auto* model = widget.findChild<QStandardItemModel*>();
        return model ? model->rowCount() : -1;
    }

private slots:
    void initTestCase()
    {
        qputenv("QT_QPA_PLATFORM", "offscreen");
    }

    void testConstruct()
    {
        auto db = openDb();
        EventsTableWidget widget(db);
        widget.resize(900, 600);
        widget.show();
        QApplication::processEvents();

        QVERIFY(widget.width() > 0);
        QVERIFY(widget.findChild<QTableView*>() != nullptr);
        QVERIFY(crimeTypeCombo(widget) != nullptr);
        QVERIFY(filterButton(widget) != nullptr);
        QVERIFY(searchEdit(widget) != nullptr);
    }

    void testRefreshEmptyDb()
    {
        auto db = openDb();
        EventsTableWidget widget(db);

        widget.refresh();
        QApplication::processEvents();

        QCOMPARE(tableRowCount(widget), 0);

        bool foundZero = false;
        for (auto* lbl : widget.findChildren<QLabel*>()) {
            if (lbl->text().contains(QStringLiteral("0 event"))) {
                foundZero = true;
                break;
            }
        }
        QVERIFY2(foundZero, "Count label should show 0 events on empty DB");
    }

    void testRefreshPopulatedDb()
    {
        auto db = openDb();
        seedMixedEvents(db);

        EventsTableWidget widget(db);
        widget.refresh();
        QApplication::processEvents();

        QCOMPARE(tableRowCount(widget), 4);
    }

    void testFilterByCrimeType()
    {
        auto db = openDb();
        seedMixedEvents(db);

        EventsTableWidget widget(db);
        widget.refresh();
        QApplication::processEvents();
        QCOMPARE(tableRowCount(widget), 4);

        auto* combo = crimeTypeCombo(widget);
        auto* btn   = filterButton(widget);
        QVERIFY(combo != nullptr);
        QVERIFY(btn != nullptr);

        int theftIndex = -1;
        for (int i = 0; i < combo->count(); ++i) {
            if (combo->itemData(i).toString() == QStringLiteral("theft")) {
                theftIndex = i;
                break;
            }
        }
        QVERIFY2(theftIndex >= 0, "Combo should contain theft filter option");
        combo->setCurrentIndex(theftIndex);
        btn->click();
        QApplication::processEvents();

        QCOMPARE(tableRowCount(widget), 2);

        auto* model = widget.findChild<QStandardItemModel*>();
        QVERIFY(model != nullptr);
        for (int row = 0; row < model->rowCount(); ++row) {
            const QString type = model->item(row, 2)->text();
            QCOMPARE(type, QStringLiteral("theft"));
        }
    }

    void testFilterBySearchKeyword()
    {
        auto db = openDb();
        seedMixedEvents(db);

        EventsTableWidget widget(db);
        widget.refresh();
        QApplication::processEvents();

        auto* search = searchEdit(widget);
        auto* btn    = filterButton(widget);
        QVERIFY(search != nullptr);
        QVERIFY(btn != nullptr);

        search->setText(QStringLiteral("Northside"));
        btn->click();
        QApplication::processEvents();

        QCOMPARE(tableRowCount(widget), 1);

        auto* model = widget.findChild<QStandardItemModel*>();
        QVERIFY(model != nullptr);
        QCOMPARE(model->item(0, 0)->text(), QStringLiteral("ET3-001"));
    }

    void testRefreshOverwritesSetEvents()
    {
        auto db = openDb();
        seedMixedEvents(db);

        EventsTableWidget widget(db);
        widget.setEvents({makeEvent(QStringLiteral("INLINE-ONLY"), QStringLiteral("robbery"))});
        QCOMPARE(tableRowCount(widget), 1);

        widget.refresh();
        QApplication::processEvents();

        // refresh() reloads from DB via onFilterChanged(), replacing setEvents data
        QCOMPARE(tableRowCount(widget), 4);
    }
};

QTEST_MAIN(TestEventsTableDeep3)

#include "test_events_table_deep3.moc"
