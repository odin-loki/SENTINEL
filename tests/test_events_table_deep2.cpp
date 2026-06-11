// test_events_table_deep2.cpp — Deep audit iteration 13: EventsTableWidget setEvents.

#include <QTest>
#include <QApplication>
#include <QStandardItemModel>
#include <QTableView>
#include <memory>

#include "ui/EventsTableWidget.h"
#include "core/Database.h"
#include "core/AppConfig.h"
#include "core/CrimeEvent.h"

class TestEventsTableDeep2 : public QObject {
    Q_OBJECT

private slots:
    void initTestCase()
    {
        qputenv("QT_QPA_PLATFORM", "offscreen");
    }

    static CrimeEvent makeEvent(const QString& id, const QString& type)
    {
        CrimeEvent ev;
        ev.eventId      = id;
        ev.id           = id;
        ev.crimeType    = type;
        ev.suburb       = QStringLiteral("TestSuburb");
        ev.ingestedAt   = QDateTime::currentDateTimeUtc();
        ev.occurredAt   = QDateTime::currentDateTimeUtc();
        ev.timestamp    = ev.occurredAt.value();
        ev.qualityScore = 0.75;
        return ev;
    }

    void testConstruct()
    {
        AppConfig cfg;
        cfg.databasePath = QStringLiteral(":memory:");
        auto db = std::make_shared<Database>(cfg);
        db->open();

        EventsTableWidget widget(db);
        widget.resize(800, 500);
        QVERIFY(widget.width() > 0);
    }

    void testSetEventsEmpty()
    {
        AppConfig cfg;
        cfg.databasePath = QStringLiteral(":memory:");
        auto db = std::make_shared<Database>(cfg);
        db->open();

        EventsTableWidget widget(db);
        widget.setEvents({});

        auto* table = widget.findChild<QTableView*>();
        QVERIFY(table != nullptr);
        QVERIFY(table->model() != nullptr);
        QCOMPARE(table->model()->rowCount(), 0);
    }

    void testSetEventsRowCountMatches()
    {
        AppConfig cfg;
        cfg.databasePath = QStringLiteral(":memory:");
        auto db = std::make_shared<Database>(cfg);
        db->open();

        QVector<CrimeEvent> events;
        events.append(makeEvent(QStringLiteral("e1"), QStringLiteral("theft")));
        events.append(makeEvent(QStringLiteral("e2"), QStringLiteral("assault")));
        events.append(makeEvent(QStringLiteral("e3"), QStringLiteral("burglary")));
        events.append(makeEvent(QStringLiteral("e4"), QStringLiteral("robbery")));

        EventsTableWidget widget(db);
        widget.setEvents(events);

        auto* model = widget.findChild<QStandardItemModel*>();
        QVERIFY(model != nullptr);
        QCOMPARE(model->rowCount(), events.size());
        QCOMPARE(model->columnCount(), 7);
    }
};

QTEST_MAIN(TestEventsTableDeep2)

#include "test_events_table_deep2.moc"
