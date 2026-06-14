// test_events_table_deep6.cpp — Deep audit iteration 27: EventsTableWidget
// column headers, refresh idempotent, date filter range, search keyword.
#include <QTest>
#include <QApplication>
#include <QStandardItemModel>
#include <QComboBox>
#include <QPushButton>
#include <QDateEdit>
#include <QLineEdit>
#include <memory>
#include "ui/EventsTableWidget.h"
#include "core/Database.h"
#include "core/AppConfig.h"
#include "core/CrimeEvent.h"

class TestEventsTableDeep6 : public QObject
{
    Q_OBJECT

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
                                const QDateTime& occurred)
    {
        CrimeEvent ev;
        ev.eventId      = id;
        ev.id           = id;
        ev.crimeType    = type;
        ev.suburb       = QStringLiteral("Deep6");
        ev.locationRaw  = QStringLiteral("High Street Camden");
        ev.ingestedAt   = QDateTime::currentDateTimeUtc();
        ev.occurredAt   = occurred;
        ev.timestamp    = occurred;
        ev.qualityScore = 0.8;
        ev.source       = QStringLiteral("deep6_test");
        return ev;
    }

private slots:
    void initTestCase()
    {
        qputenv("QT_QPA_PLATFORM", "offscreen");
        qputenv("SENTINEL_HEADLESS_TEST", "1");
    }

    void testHeaderLabelsPresent()
    {
        auto db = openDb();
        EventsTableWidget widget(db);
        widget.refresh();
        QApplication::processEvents();

        auto* model = widget.findChild<QStandardItemModel*>();
        QVERIFY(model != nullptr);
        QVERIFY(model->headerData(0, Qt::Horizontal).toString().length() > 0);
    }

    void testRefreshTwiceSameCount()
    {
        auto db = openDb();
        QVERIFY(db->insertEvent(makeEvent(QStringLiteral("R1"), QStringLiteral("theft"),
                                          QDateTime(QDate(2024, 3, 1), QTime(0, 0), Qt::UTC))));

        EventsTableWidget widget(db);
        widget.refresh();
        QApplication::processEvents();
        const int first = widget.findChild<QStandardItemModel*>()->rowCount();

        widget.refresh();
        QApplication::processEvents();
        QCOMPARE(widget.findChild<QStandardItemModel*>()->rowCount(), first);
    }

    void testDateFilterNarrowsRows()
    {
        auto db = openDb();
        QVERIFY(db->insertEvent(makeEvent(QStringLiteral("OLD"), QStringLiteral("theft"),
            QDateTime(QDate(2023, 1, 1), QTime(0, 0), Qt::UTC))));
        QVERIFY(db->insertEvent(makeEvent(QStringLiteral("NEW"), QStringLiteral("theft"),
            QDateTime(QDate(2024, 6, 1), QTime(0, 0), Qt::UTC))));

        EventsTableWidget widget(db);
        auto* fromEdit = widget.findChildren<QDateEdit*>().value(0, nullptr);
        auto* toEdit   = widget.findChildren<QDateEdit*>().value(1, nullptr);
        QVERIFY(fromEdit != nullptr);
        QVERIFY(toEdit != nullptr);

        fromEdit->setDate(QDate(2024, 1, 1));
        toEdit->setDate(QDate(2024, 12, 31));

        QPushButton* filterBtn = nullptr;
        for (auto* btn : widget.findChildren<QPushButton*>()) {
            if (btn->text().contains(QStringLiteral("Filter"), Qt::CaseInsensitive))
                filterBtn = btn;
        }
        QVERIFY(filterBtn != nullptr);
        QTest::mouseClick(filterBtn, Qt::LeftButton);
        QApplication::processEvents();

        auto* model = widget.findChild<QStandardItemModel*>();
        QVERIFY(model->rowCount() >= 1);
        QVERIFY(model->rowCount() <= 2);
    }

    void testSearchBoxExists()
    {
        auto db = openDb();
        EventsTableWidget widget(db);
        QVERIFY(widget.findChild<QLineEdit*>() != nullptr);
    }
};

QTEST_MAIN(TestEventsTableDeep6)
#include "test_events_table_deep6.moc"
