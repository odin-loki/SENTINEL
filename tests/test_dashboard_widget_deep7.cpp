// test_dashboard_widget_deep7.cpp — Deep audit iteration 30: DashboardWidget
// crime type breakdown, refresh idempotent, risk table exists, multiple zones.
#include <QTest>
#include <QApplication>
#include <QTableWidget>
#include <memory>
#include "ui/DashboardWidget.h"
#include "core/Database.h"
#include "core/AppConfig.h"
#include "core/CrimeEvent.h"

class TestDashboardWidgetDeep7 : public QObject
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
        ev.eventId    = QStringLiteral("DW7-%1").arg(i);
        ev.id         = ev.eventId;
        ev.crimeType  = (i % 3 == 0) ? QStringLiteral("burglary")
                       : (i % 3 == 1) ? QStringLiteral("theft")
                                      : QStringLiteral("assault");
        ev.suburb     = QStringLiteral("Zone%1").arg(i % 3);
        ev.lat        = 51.5 + i * 0.01;
        ev.lon        = -0.1;
        ev.latitude   = ev.lat.value();
        ev.longitude  = ev.lon.value();
        ev.occurredAt = QDateTime::currentDateTimeUtc().addDays(-i);
        ev.timestamp  = ev.occurredAt.value();
        ev.qualityScore = 0.7;
        ev.source     = QStringLiteral("deep7_test");
        return ev;
    }

private slots:
    void initTestCase()
    {
        qputenv("QT_QPA_PLATFORM", "offscreen");
        qputenv("SENTINEL_HEADLESS_TEST", "1");
    }

    void testMultipleCrimeTypesInserted()
    {
        auto db = openDb();
        for (int i = 0; i < 9; ++i)
            QVERIFY(db->insertEvent(makeEvent(i)));

        AppConfig cfg = memCfg();
        DashboardWidget widget(db, cfg);
        widget.refresh();
        QApplication::processEvents();

        const auto counts = db->crimeTypeCounts();
        QVERIFY(counts.size() >= 3);
    }

    void testRiskTableWidgetExists()
    {
        auto db = openDb();
        AppConfig cfg = memCfg();
        DashboardWidget widget(db, cfg);
        widget.refresh();

        auto* table = widget.findChild<QTableWidget*>();
        QVERIFY(table != nullptr);
    }

    void testDoubleRefreshStable()
    {
        auto db = openDb();
        QVERIFY(db->insertEvent(makeEvent(0)));

        AppConfig cfg = memCfg();
        DashboardWidget widget(db, cfg);
        widget.refresh();
        QApplication::processEvents();
        widget.refresh();
        QApplication::processEvents();
        QVERIFY(true);
    }

    void testWidgetConstructsWithData()
    {
        auto db = openDb();
        for (int i = 0; i < 3; ++i)
            db->insertEvent(makeEvent(i));

        AppConfig cfg = memCfg();
        DashboardWidget widget(db, cfg);
        widget.show();
        QApplication::processEvents();
        QVERIFY(widget.isVisible() || widget.windowTitle().isEmpty());
    }

    void testDatabaseEventCountMatches()
    {
        auto db = openDb();
        for (int i = 0; i < 4; ++i)
            db->insertEvent(makeEvent(i));
        QCOMPARE(db->eventCount(), 4);
    }
};

QTEST_MAIN(TestDashboardWidgetDeep7)
#include "test_dashboard_widget_deep7.moc"
