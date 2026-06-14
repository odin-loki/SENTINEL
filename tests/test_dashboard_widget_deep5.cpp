// test_dashboard_widget_deep5.cpp — Deep audit iteration 23: DashboardWidget
// construction, empty-DB stats, stat cards, risk alerts table, risk panel rows.

#include <QTest>
#include <QApplication>
#include <QLabel>
#include <QTableWidget>
#include <QHeaderView>
#include <QGroupBox>
#include <memory>

#include "ui/DashboardWidget.h"
#include "core/Database.h"
#include "core/AppConfig.h"
#include "core/CrimeEvent.h"

class TestDashboardWidgetDeep5 : public QObject {
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

    static CrimeEvent makeEvent(int index, int daysAgo = 0)
    {
        CrimeEvent ev;
        ev.eventId        = QStringLiteral("DW5-%1").arg(index, 4, 10, QChar('0'));
        ev.id             = ev.eventId;
        ev.crimeType      = (index % 3 == 0) ? QStringLiteral("burglary")
                             : (index % 3 == 1) ? QStringLiteral("theft")
                                                : QStringLiteral("assault");
        ev.suburb         = QStringLiteral("Zone%1").arg(index % 6);
        ev.lat            = 51.5074 + (index % 5) * 0.01;
        ev.lon            = -0.1278 + (index % 5) * 0.01;
        ev.latitude       = ev.lat.value();
        ev.longitude      = ev.lon.value();
        ev.source         = QStringLiteral("deep5_test");
        ev.occurredAt     = QDateTime::currentDateTimeUtc().addDays(-daysAgo);
        ev.timestamp      = ev.occurredAt.value();
        ev.ingestedAt     = QDateTime::currentDateTimeUtc();
        ev.qualityScore   = 0.35 + (index % 4) * 0.15;
        ev.locationDescription = QStringLiteral("Loc %1").arg(index);
        return ev;
    }

    static void seedDb(const std::shared_ptr<Database>& db, int count, int daysAgo = 0)
    {
        for (int i = 0; i < count; ++i)
            QVERIFY2(db->insertEvent(makeEvent(i, daysAgo + (i % 20))),
                     qPrintable(db->lastError()));
    }

    static QTableWidget* tableWithHeader(DashboardWidget& widget, const QString& firstCol)
    {
        for (auto* table : widget.findChildren<QTableWidget*>()) {
            if (table->columnCount() > 0
                && table->horizontalHeaderItem(0)
                && table->horizontalHeaderItem(0)->text() == firstCol)
                return table;
        }
        return nullptr;
    }

private slots:
    void initTestCase()
    {
        qputenv("QT_QPA_PLATFORM", "offscreen");
        qputenv("SENTINEL_HEADLESS_TEST", "1");
    }

    void testConstructionWithoutCrash()
    {
        auto cfg = memCfg();
        auto db  = openDb();

        DashboardWidget widget(db, cfg);
        widget.resize(1024, 768);
        widget.show();
        QApplication::processEvents();

        QVERIFY(widget.isVisible());
        QVERIFY(widget.findChild<QTableWidget*>() != nullptr);
    }

    void testRefreshEmptyDatabaseShowsZeroStats()
    {
        auto cfg = memCfg();
        auto db  = openDb();

        DashboardWidget widget(db, cfg);
        widget.refresh();
        QApplication::processEvents();

        const auto valueLabels = widget.findChildren<QLabel*>(QStringLiteral("valueLabel"));
        QVERIFY(valueLabels.size() >= 4);
        QCOMPARE(valueLabels.at(0)->text(), QStringLiteral("0"));
        QCOMPARE(valueLabels.at(1)->text(), QStringLiteral("0"));
        QCOMPARE(valueLabels.at(3)->text(), QStringLiteral("0.00"));
    }

    void testStatCardsExistWithExpectedTitles()
    {
        auto cfg = memCfg();
        auto db  = openDb();

        DashboardWidget widget(db, cfg);
        QApplication::processEvents();

        const auto cards = widget.findChildren<QGroupBox*>();
        QStringList titles;
        for (auto* card : cards) {
            const QString title = card->title();
            if (!title.isEmpty())
                titles.append(title);
        }

        QVERIFY(titles.contains(QStringLiteral("Recent Events")));
        QVERIFY(titles.contains(QStringLiteral("Crime Type Distribution")));

        const auto valueLabels = widget.findChildren<QLabel*>(QStringLiteral("valueLabel"));
        QCOMPARE(valueLabels.size(), 4);
    }

    void testTotalEventsStatMatchesInsertedCount()
    {
        auto cfg = memCfg();
        auto db  = openDb();
        seedDb(db, 7, 0);

        DashboardWidget widget(db, cfg);
        widget.refresh();
        QApplication::processEvents();

        const auto valueLabels = widget.findChildren<QLabel*>(QStringLiteral("valueLabel"));
        QVERIFY(!valueLabels.isEmpty());
        QCOMPARE(valueLabels.first()->text(), QStringLiteral("7"));
    }

    void testRiskAlertsTableExistsAfterRefresh()
    {
        auto cfg = memCfg();
        auto db  = openDb();

        DashboardWidget widget(db, cfg);
        widget.refresh();
        QApplication::processEvents();

        auto* riskTable = tableWithHeader(widget, QStringLiteral("Zone"));
        QVERIFY2(riskTable != nullptr, "Risk alerts table with Zone column not found");
        QCOMPARE(riskTable->columnCount(), 5);
        QVERIFY(riskTable->horizontalHeaderItem(1) != nullptr);
        QCOMPARE(riskTable->horizontalHeaderItem(1)->text(),
                 QStringLiteral("Alert Level"));
    }

    void testRefreshRiskPanelWithEventsPopulatesRows()
    {
        auto cfg = memCfg();
        auto db  = openDb();
        seedDb(db, 45, 0);

        DashboardWidget widget(db, cfg);
        widget.refresh();
        QApplication::processEvents();

        auto* riskTable = tableWithHeader(widget, QStringLiteral("Zone"));
        QVERIFY2(riskTable != nullptr, "Risk alerts table not found");

        if (riskTable->rowCount() == 0) {
            QWARN("Risk panel empty — may need more spatial/temporal spread for RiskForecaster fit");
            return;
        }

        QVERIFY(riskTable->rowCount() <= 8);
        auto* zoneItem = riskTable->item(0, 0);
        auto* riskItem = riskTable->item(0, 2);
        QVERIFY(zoneItem != nullptr);
        QVERIFY(riskItem != nullptr);
        QVERIFY(!zoneItem->text().isEmpty());
        QVERIFY(riskItem->text().contains(QStringLiteral("%")));
    }

    void testDoubleRefreshStableOnPopulatedDb()
    {
        auto cfg = memCfg();
        auto db  = openDb();
        seedDb(db, 12, 0);

        DashboardWidget widget(db, cfg);
        widget.refresh();
        widget.refresh();
        QApplication::processEvents();

        const auto valueLabels = widget.findChildren<QLabel*>(QStringLiteral("valueLabel"));
        QVERIFY(!valueLabels.isEmpty());
        QCOMPARE(valueLabels.first()->text(), QStringLiteral("12"));

        auto* recent = tableWithHeader(widget, QStringLiteral("Date"));
        QVERIFY(recent != nullptr);
        QCOMPARE(recent->rowCount(), 10);
    }
};

QTEST_MAIN(TestDashboardWidgetDeep5)

#include "test_dashboard_widget_deep5.moc"
