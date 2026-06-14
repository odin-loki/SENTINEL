// test_events_table_deep7.cpp — Deep audit iteration 30: EventsTableWidget
// crime type filter, refresh button, row count after insert, model non-null.
#include <QTest>
#include <QApplication>
#include <QComboBox>
#include <QPushButton>
#include <QDateEdit>
#include <QStandardItemModel>
#include <memory>
#include "ui/EventsTableWidget.h"
#include "core/Database.h"
#include "core/AppConfig.h"
#include "core/CrimeEvent.h"

class TestEventsTableDeep7 : public QObject
{
    Q_OBJECT

    static std::shared_ptr<Database> openDb()
    {
        auto db = std::make_shared<Database>([] {
            AppConfig cfg;
            cfg.databasePath = QStringLiteral(":memory:");
            return cfg;
        }());
        db->open();
        return db;
    }

    static CrimeEvent makeEvent(const QString& id, const QString& type)
    {
        CrimeEvent ev;
        ev.eventId      = id;
        ev.id           = id;
        ev.crimeType    = type;
        ev.suburb       = QStringLiteral("Deep7");
        ev.ingestedAt   = QDateTime::currentDateTimeUtc();
        ev.occurredAt   = QDateTime(QDate(2024, 7, 10), QTime(9, 0), QTimeZone::utc());
        ev.timestamp    = ev.occurredAt.value();
        ev.qualityScore = 0.8;
        ev.source       = QStringLiteral("deep7_test");
        return ev;
    }

private slots:
    void initTestCase()
    {
        qputenv("QT_QPA_PLATFORM", "offscreen");
        qputenv("SENTINEL_HEADLESS_TEST", "1");
    }

    void testCrimeTypeComboExists()
    {
        auto db = openDb();
        EventsTableWidget widget(db);

        const auto combos = widget.findChildren<QComboBox*>();
        QVERIFY(!combos.isEmpty());
    }

    void testFilterButtonPresent()
    {
        auto db = openDb();
        EventsTableWidget widget(db);

        bool hasFilter = false;
        for (auto* btn : widget.findChildren<QPushButton*>()) {
            if (btn->text().contains(QStringLiteral("Filter"), Qt::CaseInsensitive))
                hasFilter = true;
        }
        QVERIFY(hasFilter);
    }

    void testRowsAfterInsert()
    {
        auto db = openDb();
        QVERIFY(db->insertEvent(makeEvent(QStringLiteral("ET7-1"), QStringLiteral("burglary"))));
        QVERIFY(db->insertEvent(makeEvent(QStringLiteral("ET7-2"), QStringLiteral("theft"))));

        EventsTableWidget widget(db);
        auto* fromEdit = widget.findChildren<QDateEdit*>().value(0, nullptr);
        auto* toEdit   = widget.findChildren<QDateEdit*>().value(1, nullptr);
        if (fromEdit && toEdit) {
            fromEdit->setDate(QDate(2024, 1, 1));
            toEdit->setDate(QDate(2024, 12, 31));
        }

        widget.refresh();
        QApplication::processEvents();

        auto* model = widget.findChild<QStandardItemModel*>();
        QVERIFY(model != nullptr);
        QVERIFY(model->rowCount() >= 2);
    }

    void testFilterByCrimeTypeReducesSet()
    {
        auto db = openDb();
        QVERIFY(db->insertEvent(makeEvent(QStringLiteral("F-B"), QStringLiteral("burglary"))));
        QVERIFY(db->insertEvent(makeEvent(QStringLiteral("F-T"), QStringLiteral("theft"))));

        const auto all = db->queryEvents(QStringLiteral("burglary"),
                                         QDate(2020, 1, 1), QDate(2030, 1, 1), {}, 100);
        QCOMPARE(all.size(), 1);
        QCOMPARE(all.first().crimeType, QStringLiteral("burglary"));
    }

    void testEmptyDatabaseRefreshSafe()
    {
        auto db = openDb();
        EventsTableWidget widget(db);
        widget.refresh();
        QApplication::processEvents();
        QVERIFY(true);
    }
};

QTEST_MAIN(TestEventsTableDeep7)
#include "test_events_table_deep7.moc"
