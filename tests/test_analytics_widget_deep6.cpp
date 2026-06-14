// test_analytics_widget_deep6.cpp — Deep audit iteration 27: AnalyticsWidget
// tab count, period combo options, refresh stability, summary label.
#include <QTest>
#include <QApplication>
#include <QTabWidget>
#include <QComboBox>
#include <QLabel>
#include <memory>
#include "ui/AnalyticsWidget.h"
#include "core/Database.h"
#include "core/AppConfig.h"
#include "core/CrimeEvent.h"

class TestAnalyticsWidgetDeep6 : public QObject
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
        ev.eventId    = QStringLiteral("AW6-%1").arg(i);
        ev.id         = ev.eventId;
        ev.crimeType  = QStringLiteral("assault");
        ev.suburb     = QStringLiteral("Z%1").arg(i);
        ev.occurredAt = QDateTime::currentDateTimeUtc().addDays(-i);
        ev.timestamp  = ev.occurredAt.value();
        ev.lat        = 51.5;
        ev.lon        = -0.1;
        ev.latitude   = 51.5;
        ev.longitude  = -0.1;
        return ev;
    }

private slots:
    void initTestCase()
    {
        qputenv("QT_QPA_PLATFORM", "offscreen");
        qputenv("SENTINEL_HEADLESS_TEST", "1");
    }

    void testHasMultipleTabs()
    {
        auto db = openDb();
        AppConfig cfg = memCfg();
        AnalyticsWidget widget(db, cfg);
        auto* tabs = widget.findChild<QTabWidget*>();
        QVERIFY(tabs != nullptr);
        QVERIFY(tabs->count() >= 2);
    }

    void testPeriodComboHasOptions()
    {
        auto db = openDb();
        AppConfig cfg = memCfg();
        AnalyticsWidget widget(db, cfg);
        QComboBox* period = nullptr;
        for (auto* combo : widget.findChildren<QComboBox*>()) {
            if (combo->count() >= 2)
                period = combo;
        }
        QVERIFY(period != nullptr);
    }

    void testRefreshWithData()
    {
        auto db = openDb();
        for (int i = 0; i < 4; ++i)
            db->insertEvent(makeEvent(i));

        AppConfig cfg = memCfg();
        AnalyticsWidget widget(db, cfg);
        widget.refresh();
        QApplication::processEvents();
        widget.refresh();
        QApplication::processEvents();
        QVERIFY(widget.findChild<QTabWidget*>() != nullptr);
    }

    void testSummaryLabelExists()
    {
        auto db = openDb();
        AppConfig cfg = memCfg();
        AnalyticsWidget widget(db, cfg);
        widget.refresh();
        QApplication::processEvents();

        bool hasSummary = false;
        for (auto* lbl : widget.findChildren<QLabel*>()) {
            if (!lbl->text().isEmpty())
                hasSummary = true;
        }
        QVERIFY(hasSummary);
    }
};

QTEST_MAIN(TestAnalyticsWidgetDeep6)
#include "test_analytics_widget_deep6.moc"
