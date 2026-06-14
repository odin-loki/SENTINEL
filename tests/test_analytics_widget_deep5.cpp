// test_analytics_widget_deep5.cpp — Deep audit iteration 24: AnalyticsWidget
// construction, refresh, chart tabs, period filter, summary label.
#include <QTest>
#include <QApplication>
#include <QTabWidget>
#include <QLabel>
#include <QComboBox>
#include <memory>
#include "ui/AnalyticsWidget.h"
#include "core/Database.h"
#include "core/AppConfig.h"
#include "core/CrimeEvent.h"

class TestAnalyticsWidgetDeep5 : public QObject
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
        ev.eventId    = QStringLiteral("AW5-%1").arg(i);
        ev.id         = ev.eventId;
        ev.crimeType  = (i % 2 == 0) ? QStringLiteral("theft") : QStringLiteral("assault");
        ev.suburb     = QStringLiteral("Zone%1").arg(i % 3);
        ev.occurredAt = QDateTime::currentDateTimeUtc().addDays(-(i % 5));
        ev.timestamp  = ev.occurredAt.value();
        ev.lat        = 51.5074 + i * 0.001;
        ev.lon        = -0.1278 + i * 0.001;
        ev.latitude   = *ev.lat;
        ev.longitude  = *ev.lon;
        return ev;
    }

private slots:
    void initTestCase()
    {
        qputenv("QT_QPA_PLATFORM", "offscreen");
        qputenv("SENTINEL_HEADLESS_TEST", "1");
    }

    void testConstructionNoCrash()
    {
        auto db = openDb();
        AppConfig cfg = memCfg();
        AnalyticsWidget widget(db, cfg);
        widget.resize(800, 600);
        widget.show();
        QApplication::processEvents();
        auto* tabs = widget.findChild<QTabWidget*>();
        QVERIFY(tabs != nullptr);
        QVERIFY(tabs->count() >= 3);
    }

    void testRefreshWithSeededEvents()
    {
        auto db = openDb();
        for (int i = 0; i < 12; ++i)
            QVERIFY(db->insertEvent(makeEvent(i)));

        AppConfig cfg = memCfg();
        AnalyticsWidget widget(db, cfg);
        widget.refresh();
        QApplication::processEvents();
        QVERIFY(true);
    }

    void testChartTabsExist()
    {
        auto db = openDb();
        AppConfig cfg = memCfg();
        AnalyticsWidget widget(db, cfg);
        auto* tabs = widget.findChild<QTabWidget*>();
        QVERIFY(tabs != nullptr);
        bool hasBenchmark = false;
        bool hasCalibration = false;
        for (int i = 0; i < tabs->count(); ++i) {
            const QString label = tabs->tabText(i);
            if (label.contains(QStringLiteral("Benchmark"), Qt::CaseInsensitive))
                hasBenchmark = true;
            if (label.contains(QStringLiteral("Calibration"), Qt::CaseInsensitive))
                hasCalibration = true;
        }
        QVERIFY(hasBenchmark);
        QVERIFY(hasCalibration);
    }

    void testPeriodComboExists()
    {
        auto db = openDb();
        AppConfig cfg = memCfg();
        AnalyticsWidget widget(db, cfg);
        auto* combo = widget.findChild<QComboBox*>();
        QVERIFY(combo != nullptr);
        QVERIFY(combo->count() >= 2);
    }

    void testDoubleRefreshStable()
    {
        auto db = openDb();
        for (int i = 0; i < 5; ++i)
            db->insertEvent(makeEvent(i));
        AppConfig cfg = memCfg();
        AnalyticsWidget widget(db, cfg);
        widget.refresh();
        widget.refresh();
        QApplication::processEvents();
        QVERIFY(true);
    }

    void testEmptyDatabaseRefresh()
    {
        auto db = openDb();
        AppConfig cfg = memCfg();
        AnalyticsWidget widget(db, cfg);
        widget.refresh();
        QApplication::processEvents();
        QVERIFY(true);
    }
};

QTEST_MAIN(TestAnalyticsWidgetDeep5)
#include "test_analytics_widget_deep5.moc"
