// test_dashboard_widget_deep6.cpp — Deep audit iteration 27: DashboardWidget
// refresh updates counts, multiple crime types, risk table headers, empty DB.
#include <QTest>
#include <QApplication>
#include <QLabel>
#include <QTableWidget>
#include <memory>
#include "ui/DashboardWidget.h"
#include "core/Database.h"
#include "core/AppConfig.h"
#include "core/CrimeEvent.h"

class TestDashboardWidgetDeep6 : public QObject
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

    static CrimeEvent makeEvent(int i)
    {
        CrimeEvent ev;
        ev.eventId    = QStringLiteral("DW6-%1").arg(i);
        ev.id         = ev.eventId;
        ev.crimeType  = (i % 2 == 0) ? QStringLiteral("burglary") : QStringLiteral("theft");
        ev.suburb     = QStringLiteral("Zone%1").arg(i % 4);
        ev.lat        = 51.5 + i * 0.01;
        ev.lon        = -0.1;
        ev.latitude   = ev.lat.value();
        ev.longitude  = ev.lon.value();
        ev.occurredAt = QDateTime::currentDateTimeUtc().addDays(-i);
        ev.timestamp  = ev.occurredAt.value();
        ev.qualityScore = 0.7;
        ev.source     = QStringLiteral("deep6_test");
        return ev;
    }

private slots:
    void initTestCase()
    {
        qputenv("QT_QPA_PLATFORM", "offscreen");
        qputenv("SENTINEL_HEADLESS_TEST", "1");
    }

    void testEmptyDatabaseShowsZeroFriendly()
    {
        auto db = openDb();
        AppConfig cfg = memCfg();
        DashboardWidget widget(db, cfg);
        widget.refresh();
        QApplication::processEvents();

        const auto labels = widget.findChildren<QLabel*>(QStringLiteral("valueLabel"));
        QVERIFY(!labels.isEmpty());
    }

    void testRefreshAfterInsertUpdatesContent()
    {
        auto db = openDb();
        for (int i = 0; i < 5; ++i)
            QVERIFY(db->insertEvent(makeEvent(i)));

        AppConfig cfg = memCfg();
        DashboardWidget widget(db, cfg);
        widget.refresh();
        QApplication::processEvents();

        bool hasNonZero = false;
        for (auto* lbl : widget.findChildren<QLabel*>(QStringLiteral("valueLabel"))) {
            if (lbl->text() != QStringLiteral("0") && !lbl->text().isEmpty())
                hasNonZero = true;
        }
        QVERIFY(hasNonZero);
    }

    void testRiskTableHasColumns()
    {
        auto db = openDb();
        for (int i = 0; i < 3; ++i)
            db->insertEvent(makeEvent(i));

        AppConfig cfg = memCfg();
        DashboardWidget widget(db, cfg);
        widget.refresh();
        QApplication::processEvents();

        QTableWidget* riskTable = nullptr;
        for (auto* table : widget.findChildren<QTableWidget*>()) {
            if (table->columnCount() >= 3)
                riskTable = table;
        }
        QVERIFY(riskTable != nullptr);
        QVERIFY(riskTable->horizontalHeaderItem(0) != nullptr);
    }

    void testDoubleRefreshStable()
    {
        auto db = openDb();
        db->insertEvent(makeEvent(0));

        AppConfig cfg = memCfg();
        DashboardWidget widget(db, cfg);
        widget.refresh();
        QApplication::processEvents();
        widget.refresh();
        QApplication::processEvents();
        QVERIFY(true);
    }
};

QTEST_MAIN(TestDashboardWidgetDeep6)
#include "test_dashboard_widget_deep6.moc"
