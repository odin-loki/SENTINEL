// test_dashboard_widget_deep2.cpp — Deep audit iteration 12: DashboardWidget display updates.

#include <QTest>
#include <QApplication>
#include <QLabel>
#include <memory>

#include "ui/DashboardWidget.h"
#include "core/Database.h"
#include "core/AppConfig.h"

class TestDashboardWidgetDeep2 : public QObject {
    Q_OBJECT

private slots:
    void initTestCase()
    {
        qputenv("QT_QPA_PLATFORM", "offscreen");
    }

    void testConstructs()
    {
        AppConfig cfg;
        cfg.databasePath = QStringLiteral(":memory:");
        auto db = std::make_shared<Database>(cfg);
        db->open();

        DashboardWidget widget(db, cfg);
        QVERIFY(true);
    }

    void testSetEventCountUpdatesDisplay()
    {
        AppConfig cfg;
        cfg.databasePath = QStringLiteral(":memory:");
        auto db = std::make_shared<Database>(cfg);
        db->open();

        DashboardWidget widget(db, cfg);
        widget.setEventCount(42);

        auto* valueLabel = widget.findChild<QLabel*>(QStringLiteral("valueLabel"));
        QVERIFY(valueLabel != nullptr);
        QCOMPARE(valueLabel->text(), QStringLiteral("42"));
    }

    void testEmptyStateDoesNotCrash()
    {
        AppConfig cfg;
        cfg.databasePath = QStringLiteral(":memory:");
        auto db = std::make_shared<Database>(cfg);
        db->open();

        DashboardWidget widget(db, cfg);
        widget.refresh();
        widget.refresh();
        QVERIFY(true);
    }
};

QTEST_MAIN(TestDashboardWidgetDeep2)

#include "test_dashboard_widget_deep2.moc"
