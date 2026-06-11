// test_dashboard_widget_deep3.cpp — Deep audit iteration 15: DashboardWidget
// construct, refresh on empty/populated DB, offscreen headless rendering.

#include <QTest>
#include <QApplication>
#include <QLabel>
#include <QTableWidget>
#include <memory>

#include "ui/DashboardWidget.h"
#include "core/Database.h"
#include "core/AppConfig.h"
#include "core/CrimeEvent.h"

class TestDashboardWidgetDeep3 : public QObject {
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
        auto cfg = memCfg();
        auto db  = std::make_shared<Database>(cfg);
        db->open();
        return db;
    }

    static CrimeEvent makeEvent(int index)
    {
        CrimeEvent ev;
        ev.eventId      = QStringLiteral("DW3-%1").arg(index, 3, 10, QChar('0'));
        ev.id           = ev.eventId;
        ev.crimeType    = (index % 2 == 0) ? QStringLiteral("burglary")
                                           : QStringLiteral("theft");
        ev.suburb       = QStringLiteral("Zone%1").arg(index % 3);
        ev.lat          = 51.5074 + index * 0.001;
        ev.lon          = -0.1278 + index * 0.001;
        ev.latitude     = ev.lat.value();
        ev.longitude    = ev.lon.value();
        ev.source       = QStringLiteral("deep3_test");
        ev.occurredAt   = QDateTime::currentDateTimeUtc().addDays(-index);
        ev.timestamp    = ev.occurredAt.value();
        ev.ingestedAt   = QDateTime::currentDateTimeUtc();
        ev.qualityScore = 0.7 + (index % 3) * 0.1;
        return ev;
    }

    static void seedDb(const std::shared_ptr<Database>& db, int count)
    {
        for (int i = 0; i < count; ++i)
            QVERIFY2(db->insertEvent(makeEvent(i)),
                     qPrintable(db->lastError()));
    }

private slots:
    void initTestCase()
    {
        qputenv("QT_QPA_PLATFORM", "offscreen");
    }

    void testConstruct()
    {
        auto cfg = memCfg();
        auto db  = openDb();

        DashboardWidget widget(db, cfg);
        widget.resize(1024, 768);
        QVERIFY(widget.width() > 0);
        QVERIFY(widget.height() > 0);
    }

    void testRefreshEmptyDb()
    {
        auto cfg = memCfg();
        auto db  = openDb();

        DashboardWidget widget(db, cfg);
        widget.refresh();
        QApplication::processEvents();

        QCOMPARE(db->getTotalEventCount(), 0);

        auto* totalLbl = widget.findChild<QLabel*>(QStringLiteral("valueLabel"));
        QVERIFY(totalLbl != nullptr);
        QCOMPARE(totalLbl->text(), QStringLiteral("0"));

        auto tables = widget.findChildren<QTableWidget*>();
        QVERIFY(!tables.isEmpty());
        for (auto* table : tables)
            QCOMPARE(table->rowCount(), 0);
    }

    void testRefreshPopulatedDb()
    {
        auto cfg = memCfg();
        auto db  = openDb();
        constexpr int kCount = 15;
        seedDb(db, kCount);

        DashboardWidget widget(db, cfg);
        widget.refresh();
        QApplication::processEvents();

        QCOMPARE(db->getTotalEventCount(), kCount);

        auto labels = widget.findChildren<QLabel*>(QStringLiteral("valueLabel"));
        QVERIFY(labels.size() >= 1);
        QCOMPARE(labels.first()->text(), QString::number(kCount));

        auto tables = widget.findChildren<QTableWidget*>();
        bool hasRows = false;
        for (auto* table : tables) {
            if (table->rowCount() > 0) {
                hasRows = true;
                break;
            }
        }
        QVERIFY2(hasRows, "At least one table should have rows after populated refresh");
    }

    void testStatCardsExist()
    {
        auto cfg = memCfg();
        auto db  = openDb();

        DashboardWidget widget(db, cfg);
        widget.show();
        QApplication::processEvents();

        const auto valueLabels = widget.findChildren<QLabel*>(QStringLiteral("valueLabel"));
        QVERIFY2(valueLabels.size() >= 4,
                 qPrintable(QStringLiteral("Expected >= 4 stat cards, got %1")
                                .arg(valueLabels.size())));
    }

    void testDoubleRefreshDoesNotCrash()
    {
        auto cfg = memCfg();
        auto db  = openDb();
        seedDb(db, 8);

        DashboardWidget widget(db, cfg);
        widget.refresh();
        widget.refresh();
        widget.refresh();
        QApplication::processEvents();
        QVERIFY(true);
    }

    void testSetEventCountOverridesTotal()
    {
        auto cfg = memCfg();
        auto db  = openDb();
        seedDb(db, 3);

        DashboardWidget widget(db, cfg);
        widget.setEventCount(99);

        auto labels = widget.findChildren<QLabel*>(QStringLiteral("valueLabel"));
        QVERIFY(!labels.isEmpty());
        QCOMPARE(labels.first()->text(), QStringLiteral("99"));
    }
};

QTEST_MAIN(TestDashboardWidgetDeep3)

#include "test_dashboard_widget_deep3.moc"
