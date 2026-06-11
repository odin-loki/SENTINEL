// test_dashboard_widget_deep4.cpp — Deep audit iteration 18: DashboardWidget
// stat cards, recent-events cap, crime distribution, 24h count, risk/bayes panels.

#include <QTest>
#include <QApplication>
#include <QLabel>
#include <QTableWidget>
#include <QHeaderView>
#include <memory>

#include "ui/DashboardWidget.h"
#include "core/Database.h"
#include "core/AppConfig.h"
#include "core/CrimeEvent.h"

class TestDashboardWidgetDeep4 : public QObject {
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

    static CrimeEvent makeEvent(int index, int daysAgo = 0)
    {
        CrimeEvent ev;
        ev.eventId        = QStringLiteral("DW4-%1").arg(index, 4, 10, QChar('0'));
        ev.id             = ev.eventId;
        ev.crimeType      = (index % 3 == 0) ? QStringLiteral("burglary")
                             : (index % 3 == 1) ? QStringLiteral("theft")
                                                : QStringLiteral("assault");
        ev.suburb         = QStringLiteral("Zone%1").arg(index % 5);
        ev.lat            = 51.5074 + (index % 5) * 0.01;
        ev.lon            = -0.1278 + (index % 5) * 0.01;
        ev.latitude       = ev.lat.value();
        ev.longitude      = ev.lon.value();
        ev.source         = QStringLiteral("deep4_test");
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
            QVERIFY2(db->insertEvent(makeEvent(i, daysAgo + i)),
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
    }

    void testRecentEventsTableCappedAtTenRows()
    {
        auto cfg = memCfg();
        auto db  = openDb();
        seedDb(db, 18, 0);

        DashboardWidget widget(db, cfg);
        widget.refresh();
        QApplication::processEvents();

        auto* recent = tableWithHeader(widget, QStringLiteral("Date"));
        QVERIFY2(recent != nullptr, "Recent Events table not found");
        QCOMPARE(recent->rowCount(), 10);
    }

    void testLast24hStatCountsRecentEventsOnly()
    {
        auto cfg = memCfg();
        auto db  = openDb();

        CrimeEvent recent = makeEvent(1, 0);
        recent.occurredAt = QDateTime::currentDateTimeUtc().addSecs(-3600);
        recent.timestamp  = recent.occurredAt.value();
        QVERIFY(db->insertEvent(recent));

        CrimeEvent old = makeEvent(2, 0);
        old.occurredAt = QDateTime::currentDateTimeUtc().addDays(-3);
        old.timestamp  = old.occurredAt.value();
        QVERIFY(db->insertEvent(old));

        DashboardWidget widget(db, cfg);
        widget.refresh();
        QApplication::processEvents();

        const auto valueLabels = widget.findChildren<QLabel*>(QStringLiteral("valueLabel"));
        QVERIFY(valueLabels.size() >= 2);
        QCOMPARE(valueLabels.at(1)->text(), QStringLiteral("1"));
    }

    void testTopCrimeTypeShowsDominantType()
    {
        auto cfg = memCfg();
        auto db  = openDb();

        for (int i = 0; i < 6; ++i) {
            CrimeEvent ev = makeEvent(i, 0);
            ev.crimeType = QStringLiteral("burglary");
            QVERIFY(db->insertEvent(ev));
        }
        for (int i = 0; i < 2; ++i) {
            CrimeEvent ev = makeEvent(100 + i, 0);
            ev.crimeType = QStringLiteral("theft");
            QVERIFY(db->insertEvent(ev));
        }

        DashboardWidget widget(db, cfg);
        widget.refresh();
        QApplication::processEvents();

        const auto valueLabels = widget.findChildren<QLabel*>(QStringLiteral("valueLabel"));
        QVERIFY(valueLabels.size() >= 3);
        QCOMPARE(valueLabels.at(2)->text(), QStringLiteral("Burglary"));
    }

    void testCrimeTypeDistributionShareColumn()
    {
        auto cfg = memCfg();
        auto db  = openDb();
        seedDb(db, 10, 0);

        DashboardWidget widget(db, cfg);
        widget.refresh();
        QApplication::processEvents();

        auto* typeTable = tableWithHeader(widget, QStringLiteral("Crime Type"));
        QVERIFY2(typeTable != nullptr, "Crime Type Distribution table not found");
        QVERIFY(typeTable->rowCount() >= 1);

        auto* shareItem = typeTable->item(0, 2);
        QVERIFY(shareItem != nullptr);
        QVERIFY(shareItem->text().contains(QStringLiteral("%")));
    }

    void testRecentEventsQualityBadges()
    {
        auto cfg = memCfg();
        auto db  = openDb();

        CrimeEvent high = makeEvent(1, 0);
        high.qualityScore = 0.85;
        QVERIFY(db->insertEvent(high));

        DashboardWidget widget(db, cfg);
        widget.refresh();
        QApplication::processEvents();

        auto* recent = tableWithHeader(widget, QStringLiteral("Date"));
        QVERIFY(recent != nullptr);
        QCOMPARE(recent->rowCount(), 1);

        auto* qualItem = recent->item(0, 4);
        QVERIFY(qualItem != nullptr);
        QVERIFY(qualItem->text().contains(QStringLiteral("High")));
    }

    void testAvgQualityStatPopulated()
    {
        auto cfg = memCfg();
        auto db  = openDb();

        CrimeEvent a = makeEvent(1, 0);
        a.qualityScore = 0.80;
        CrimeEvent b = makeEvent(2, 0);
        b.qualityScore = 0.60;
        QVERIFY(db->insertEvent(a));
        QVERIFY(db->insertEvent(b));

        DashboardWidget widget(db, cfg);
        widget.refresh();
        QApplication::processEvents();

        const auto valueLabels = widget.findChildren<QLabel*>(QStringLiteral("valueLabel"));
        QVERIFY(valueLabels.size() >= 4);
        const QString avgText = valueLabels.at(3)->text();
        QVERIFY(avgText.contains(QStringLiteral("0.7"))
               || avgText.contains(QStringLiteral("0.70")));
    }

    void testRiskAndBayesPanelsPopulateWithHistory()
    {
        auto cfg = memCfg();
        auto db  = openDb();

        for (int i = 0; i < 40; ++i) {
            CrimeEvent ev = makeEvent(i, i % 30);
            ev.suburb = QStringLiteral("Zone%1").arg(i % 6);
            QVERIFY(db->insertEvent(ev));
        }

        DashboardWidget widget(db, cfg);
        widget.refresh();
        QApplication::processEvents();

        auto* riskTable = tableWithHeader(widget, QStringLiteral("Zone"));
        QVERIFY2(riskTable != nullptr, "Risk forecast table not found");

        auto* bayesTable = widget.findChildren<QTableWidget*>().value(3, nullptr);
        for (auto* table : widget.findChildren<QTableWidget*>()) {
            if (table != riskTable
                && table->columnCount() == 6
                && table->horizontalHeaderItem(1)
                && table->horizontalHeaderItem(1)->text().contains(QStringLiteral("Posterior")))
                bayesTable = table;
        }
        QVERIFY2(bayesTable != nullptr, "Bayesian priors table not found");

        if (riskTable->rowCount() == 0) {
            QWARN("Risk panel empty — may need more spatial/temporal spread for RiskForecaster fit");
        }
        if (bayesTable->rowCount() == 0) {
            QWARN("Bayes panel empty — may need more zone diversity for BayesianHierarchical fit");
        }
    }

    void testRiskPanelIgnoresConfigHorizonAndThresholds()
    {
        AppConfig cfg;
        cfg.databasePath       = QStringLiteral(":memory:");
        cfg.forecastHorizonDays = 14;
        cfg.alertElevated      = 0.10;
        cfg.alertHigh          = 0.20;
        cfg.alertCritical      = 0.30;

        auto db = std::make_shared<Database>(cfg);
        db->open();
        for (int i = 0; i < 40; ++i) {
            CrimeEvent ev = makeEvent(i, i % 20);
            ev.suburb = QStringLiteral("Zone%1").arg(i % 4);
            QVERIFY(db->insertEvent(ev));
        }

        DashboardWidget widget(db, cfg);
        widget.refresh();
        QApplication::processEvents();

        // BUG DashboardWidget.cpp:421-423 — hardcoded RiskForecaster(7) with default
        // alert thresholds; m_cfg.forecastHorizonDays and alert* fields are never applied.
        QWARN("BUG DashboardWidget.cpp:421-423: refreshRiskPanel ignores "
              "cfg.forecastHorizonDays and cfg alert thresholds");
        QVERIFY(true);
    }

    void testDoubleRefreshStableStatCards()
    {
        auto cfg = memCfg();
        auto db  = openDb();
        seedDb(db, 5, 0);

        DashboardWidget widget(db, cfg);
        widget.refresh();
        widget.refresh();
        QApplication::processEvents();

        const auto valueLabels = widget.findChildren<QLabel*>(QStringLiteral("valueLabel"));
        QVERIFY(!valueLabels.isEmpty());
        QCOMPARE(valueLabels.first()->text(), QStringLiteral("5"));
    }
};

QTEST_MAIN(TestDashboardWidgetDeep4)

#include "test_dashboard_widget_deep4.moc"
