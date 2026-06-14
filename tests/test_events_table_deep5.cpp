// test_events_table_deep5.cpp — Deep audit iteration 23: EventsTableWidget
// row counts, crime-type filter, date sorting, quality badge, clear/reset.

#include <QTest>
#include <QApplication>
#include <QStandardItemModel>
#include <QTableView>
#include <QHeaderView>
#include <QComboBox>
#include <QPushButton>
#include <QDateEdit>
#include <QTime>
#include <memory>

#include "ui/EventsTableWidget.h"
#include "core/Database.h"
#include "core/AppConfig.h"
#include "core/CrimeEvent.h"

class TestEventsTableDeep5 : public QObject {
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
                                const QDateTime& occurred = QDateTime::currentDateTimeUtc(),
                                double quality = 0.75)
    {
        CrimeEvent ev;
        ev.eventId      = id;
        ev.id           = id;
        ev.crimeType    = type;
        ev.suburb       = QStringLiteral("Deep5Suburb");
        ev.locationRaw  = QStringLiteral("Deep5 High Street");
        ev.ingestedAt   = QDateTime::currentDateTimeUtc();
        ev.occurredAt   = occurred;
        ev.timestamp    = occurred;
        ev.qualityScore = quality;
        ev.source       = QStringLiteral("deep5_test");
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

    static int tableRowCount(EventsTableWidget& widget)
    {
        auto* model = widget.findChild<QStandardItemModel*>();
        return model ? model->rowCount() : -1;
    }

    static QStandardItemModel* tableModel(EventsTableWidget& widget)
    {
        return widget.findChild<QStandardItemModel*>();
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

private slots:
    void initTestCase()
    {
        qputenv("QT_QPA_PLATFORM", "offscreen");
        qputenv("SENTINEL_HEADLESS_TEST", "1");
    }

    void testModelRowCountMatchesInsertedEvents()
    {
        auto db = openDb();
        for (int i = 0; i < 6; ++i) {
            QVERIFY(db->insertEvent(makeEvent(QStringLiteral("ET5-%1").arg(i),
                                               QStringLiteral("theft"))));
        }

        EventsTableWidget widget(db);
        widget.refresh();
        QApplication::processEvents();

        QCOMPARE(tableRowCount(widget), 6);
        auto* model = tableModel(widget);
        QVERIFY(model != nullptr);
        QCOMPARE(model->columnCount(), 7);
    }

    void testCrimeTypeFilterReducesRows()
    {
        auto db = openDb();
        QVERIFY(db->insertEvent(makeEvent(QStringLiteral("ET5-T1"), QStringLiteral("theft"))));
        QVERIFY(db->insertEvent(makeEvent(QStringLiteral("ET5-T2"), QStringLiteral("theft"))));
        QVERIFY(db->insertEvent(makeEvent(QStringLiteral("ET5-A1"), QStringLiteral("assault"))));
        QVERIFY(db->insertEvent(makeEvent(QStringLiteral("ET5-B1"), QStringLiteral("burglary"))));

        EventsTableWidget widget(db);
        widget.refresh();
        QApplication::processEvents();
        QCOMPARE(tableRowCount(widget), 4);

        auto* combo = crimeTypeCombo(widget);
        auto* btn   = filterButton(widget);
        QVERIFY(combo != nullptr);
        QVERIFY(btn != nullptr);

        const int theftIdx = combo->findData(QStringLiteral("theft"));
        QVERIFY(theftIdx >= 0);
        combo->setCurrentIndex(theftIdx);
        btn->click();
        QApplication::processEvents();

        QCOMPARE(tableRowCount(widget), 2);
    }

    void testSortingByDateWorks()
    {
        auto db = openDb();
        const QDateTime early(QDate(2024, 1, 10), QTime(10, 0), Qt::UTC);
        const QDateTime mid(QDate(2024, 3, 15), QTime(12, 0), Qt::UTC);
        const QDateTime late(QDate(2024, 6, 20), QTime(14, 0), Qt::UTC);

        QVERIFY(db->insertEvent(makeEvent(QStringLiteral("ET5-EARLY"), QStringLiteral("theft"), mid)));
        QVERIFY(db->insertEvent(makeEvent(QStringLiteral("ET5-MID"), QStringLiteral("theft"), late)));
        QVERIFY(db->insertEvent(makeEvent(QStringLiteral("ET5-LATE"), QStringLiteral("theft"), early)));

        EventsTableWidget widget(db);
        auto* from = fromDateEdit(widget);
        auto* to   = toDateEdit(widget);
        QVERIFY(from != nullptr);
        QVERIFY(to != nullptr);
        from->setDate(QDate(2024, 1, 1));
        to->setDate(QDate(2024, 12, 31));
        widget.refresh();
        QApplication::processEvents();

        auto* table = widget.findChild<QTableView*>();
        QVERIFY(table != nullptr);
        auto* model = tableModel(widget);
        QVERIFY(model != nullptr);
        model->sort(1, Qt::AscendingOrder);
        QApplication::processEvents();

        QCOMPARE(model->rowCount(), 3);

        const QString firstDate = model->item(0, 1)->text();
        const QString lastDate  = model->item(2, 1)->text();
        QVERIFY(firstDate <= lastDate);
        QVERIFY(firstDate.startsWith(QStringLiteral("2024-01")));
        QVERIFY(lastDate.startsWith(QStringLiteral("2024-06")));
    }

    void testQualityBadgeColumnPresent()
    {
        auto db = openDb();
        CrimeEvent high = makeEvent(QStringLiteral("ET5-HI"), QStringLiteral("theft"));
        high.qualityScore = 0.82;
        CrimeEvent low = makeEvent(QStringLiteral("ET5-LO"), QStringLiteral("assault"));
        low.qualityScore = 0.25;
        QVERIFY(db->insertEvent(high));
        QVERIFY(db->insertEvent(low));

        EventsTableWidget widget(db);
        widget.refresh();
        QApplication::processEvents();

        auto* model = tableModel(widget);
        QVERIFY(model != nullptr);
        QCOMPARE(model->headerData(6, Qt::Horizontal).toString(), QStringLiteral("Quality"));

        QString hiBadge;
        QString loBadge;
        for (int row = 0; row < model->rowCount(); ++row) {
            const QString id = model->item(row, 0)->text();
            const QString badge = model->item(row, 6)->text();
            if (id == QStringLiteral("ET5-HI"))
                hiBadge = badge;
            if (id == QStringLiteral("ET5-LO"))
                loBadge = badge;
        }
        QVERIFY(!hiBadge.isEmpty());
        QVERIFY(!loBadge.isEmpty());
        QVERIFY(hiBadge.contains(QStringLiteral("High")));
        QVERIFY(loBadge.contains(QStringLiteral("Low")));
    }

    void testClearResetRestoresEmptyState()
    {
        auto db = openDb();
        QVERIFY(db->insertEvent(makeEvent(QStringLiteral("ET5-CLR1"), QStringLiteral("theft"))));
        QVERIFY(db->insertEvent(makeEvent(QStringLiteral("ET5-CLR2"), QStringLiteral("assault"))));

        EventsTableWidget widget(db);
        widget.refresh();
        QApplication::processEvents();
        QCOMPARE(tableRowCount(widget), 2);

        widget.setEvents({});
        QApplication::processEvents();

        QCOMPARE(tableRowCount(widget), 0);
        auto* model = tableModel(widget);
        QVERIFY(model != nullptr);
        QVERIFY(model->item(0, 0) == nullptr);
    }

    void testRefreshReloadsFromDatabaseAfterSetEvents()
    {
        auto db = openDb();
        QVERIFY(db->insertEvent(makeEvent(QStringLiteral("ET5-DB1"), QStringLiteral("robbery"))));
        QVERIFY(db->insertEvent(makeEvent(QStringLiteral("ET5-DB2"), QStringLiteral("robbery"))));

        EventsTableWidget widget(db);
        widget.setEvents({ makeEvent(QStringLiteral("ET5-LOCAL"), QStringLiteral("theft")) });
        QApplication::processEvents();
        QCOMPARE(tableRowCount(widget), 1);

        widget.refresh();
        QApplication::processEvents();
        QCOMPARE(tableRowCount(widget), 2);
    }

    void testCountLabelUpdatesWithFilteredRows()
    {
        auto db = openDb();
        QVERIFY(db->insertEvent(makeEvent(QStringLiteral("ET5-C1"), QStringLiteral("theft"))));
        QVERIFY(db->insertEvent(makeEvent(QStringLiteral("ET5-C2"), QStringLiteral("assault"))));

        EventsTableWidget widget(db);
        widget.refresh();
        QApplication::processEvents();

        auto* combo = crimeTypeCombo(widget);
        auto* btn   = filterButton(widget);
        combo->setCurrentIndex(combo->findData(QStringLiteral("theft")));
        btn->click();
        QApplication::processEvents();

        const auto labels = widget.findChildren<QLabel*>();
        bool foundCount = false;
        for (auto* lbl : labels) {
            if (lbl->text().contains(QStringLiteral("1 event"))) {
                foundCount = true;
                break;
            }
        }
        QVERIFY(foundCount);
    }
};

QTEST_MAIN(TestEventsTableDeep5)

#include "test_events_table_deep5.moc"
