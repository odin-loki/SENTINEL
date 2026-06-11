// test_analytics_widget_deep3.cpp — Deep audit iteration 17: AnalyticsWidget
// refresh(), summary metrics labels, tab/period switching.

#include <QTest>
#include <QApplication>
#include <QLabel>
#include <QComboBox>
#include <QTabWidget>
#include <QUuid>
#include <memory>

#include "ui/AnalyticsWidget.h"
#include "core/Database.h"
#include "core/AppConfig.h"
#include "core/CrimeEvent.h"

class TestAnalyticsWidgetDeep3 : public QObject {
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
        ev.eventId    = QUuid::createUuid().toString();
        ev.id         = ev.eventId;
        ev.crimeType  = (index % 2 == 0) ? QStringLiteral("theft") : QStringLiteral("assault");
        ev.suburb     = QStringLiteral("TestSuburb");
        ev.lat        = 51.5 + index * 0.001;
        ev.lon        = -0.12 + index * 0.001;
        ev.latitude   = ev.lat.value();
        ev.longitude  = ev.lon.value();
        ev.occurredAt = QDateTime::currentDateTimeUtc().addDays(-(index % 5));
        ev.timestamp  = ev.occurredAt.value();
        ev.ingestedAt = QDateTime::currentDateTimeUtc();
        ev.source     = QStringLiteral("deep3_test");
        return ev;
    }

    static QLabel* findSummaryLabel(const AnalyticsWidget& widget)
    {
        for (auto* lbl : widget.findChildren<QLabel*>()) {
            const QString t = lbl->text();
            if (t.contains(QStringLiteral("Total:"))
                || t.contains(QStringLiteral("Period total:")))
                return lbl;
        }
        return nullptr;
    }

private slots:
    void initTestCase()
    {
        qputenv("QT_QPA_PLATFORM", "offscreen");
    }

    void testConstructHasTabsAndPeriodCombo()
    {
        auto cfg = memCfg();
        auto db  = openDb();

        AnalyticsWidget widget(db, cfg);
        widget.resize(900, 600);

        auto* tabs = widget.findChild<QTabWidget*>();
        auto* period = widget.findChild<QComboBox*>();
        QVERIFY(tabs != nullptr);
        QVERIFY(period != nullptr);
        QVERIFY(tabs->count() >= 3);
        QCOMPARE(period->count(), 4);
    }

    void testRefreshEmptyDbSummaryLabel()
    {
        auto cfg = memCfg();
        auto db  = openDb();

        AnalyticsWidget widget(db, cfg);
        widget.refresh();
        QApplication::processEvents();

        auto* summary = findSummaryLabel(widget);
        QVERIFY2(summary != nullptr, "summary label with Total: not found");
        QVERIFY(!summary->text().contains(QStringLiteral("Loading")));
        QVERIFY(summary->text().contains(QStringLiteral("Total:")));
        QVERIFY(summary->text().contains(QStringLiteral("0")));
    }

    void testRefreshPopulatedDbSummaryTotal()
    {
        auto cfg = memCfg();
        auto db  = openDb();
        constexpr int kCount = 12;
        for (int i = 0; i < kCount; ++i)
            QVERIFY(db->insertEvent(makeEvent(i)));

        AnalyticsWidget widget(db, cfg);
        widget.refresh();
        QApplication::processEvents();

        auto* summary = findSummaryLabel(widget);
        QVERIFY(summary != nullptr);
        QVERIFY(summary->text().contains(QString::number(kCount)));
        QVERIFY(summary->text().contains(QStringLiteral("Peak hour:")));
    }

    void testTabSwitchUpdatesSummaryFormat()
    {
        auto cfg = memCfg();
        auto db  = openDb();
        for (int i = 0; i < 5; ++i)
            db->insertEvent(makeEvent(i));

        AnalyticsWidget widget(db, cfg);
        auto* tabs = widget.findChild<QTabWidget*>();
        QVERIFY(tabs != nullptr);

        tabs->setCurrentIndex(1);  // Crime Types
        QApplication::processEvents();

        auto* summary = findSummaryLabel(widget);
        QVERIFY(summary != nullptr);
        QVERIFY(summary->text().contains(QStringLiteral("Most common:")));

        tabs->setCurrentIndex(2);  // Temporal Trend
        QApplication::processEvents();

        summary = findSummaryLabel(widget);
        QVERIFY(summary != nullptr);
        QVERIFY(summary->text().contains(QStringLiteral("Period total:")));
    }

    void testPeriodComboChangeTriggersRefresh()
    {
        auto cfg = memCfg();
        auto db  = openDb();

        CrimeEvent old;
        old.eventId   = QUuid::createUuid().toString();
        old.id        = old.eventId;
        old.crimeType = QStringLiteral("theft");
        old.suburb    = QStringLiteral("OldZone");
        old.occurredAt = QDateTime::currentDateTimeUtc().addDays(-60);
        old.timestamp = old.occurredAt.value();
        old.ingestedAt = QDateTime::currentDateTimeUtc();
        old.source    = QStringLiteral("deep3_test");
        QVERIFY(db->insertEvent(old));

        CrimeEvent recent;
        recent.eventId   = QUuid::createUuid().toString();
        recent.id        = recent.eventId;
        recent.crimeType = QStringLiteral("assault");
        recent.suburb    = QStringLiteral("NewZone");
        recent.occurredAt = QDateTime::currentDateTimeUtc().addDays(-1);
        recent.timestamp = recent.occurredAt.value();
        recent.ingestedAt = QDateTime::currentDateTimeUtc();
        recent.source    = QStringLiteral("deep3_test");
        QVERIFY(db->insertEvent(recent));

        AnalyticsWidget widget(db, cfg);
        auto* period = widget.findChild<QComboBox*>();
        QVERIFY(period != nullptr);

        period->setCurrentIndex(3);  // All time
        QApplication::processEvents();
        widget.refresh();
        QApplication::processEvents();

        auto* summary = findSummaryLabel(widget);
        QVERIFY(summary != nullptr);
        QVERIFY(summary->text().contains(QStringLiteral("2")));
    }

    void testDoubleRefreshStableSummary()
    {
        auto cfg = memCfg();
        auto db  = openDb();
        db->insertEvent(makeEvent(0));

        AnalyticsWidget widget(db, cfg);
        widget.refresh();
        QApplication::processEvents();
        widget.refresh();
        QApplication::processEvents();

        auto* summary = findSummaryLabel(widget);
        QVERIFY(summary != nullptr);
        QVERIFY(!summary->text().contains(QStringLiteral("Loading")));
        QVERIFY(summary->text().contains(QStringLiteral("Total:")));
    }
};

QTEST_MAIN(TestAnalyticsWidgetDeep3)

#include "test_analytics_widget_deep3.moc"
