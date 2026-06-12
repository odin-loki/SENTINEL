// test_analytics_widget_deep4.cpp — Deep audit iteration 20: AnalyticsWidget
// extended tabs (Benchmark/Calibration/Heat Map/Map), run actions, period filters.

#include <QTest>
#include <QApplication>
#include <QLabel>
#include <QComboBox>
#include <QTabWidget>
#include <QTableWidget>
#include <QPushButton>
#include <QUuid>
#include <memory>

#include "ui/AnalyticsWidget.h"
#include "ui/MapWidget.h"
#include "ui/TemporalHeatmapWidget.h"
#include "core/Database.h"
#include "core/AppConfig.h"
#include "core/CrimeEvent.h"

class TestAnalyticsWidgetDeep4 : public QObject {
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

    static CrimeEvent makeEvent(int index, int daysAgo = 0, bool optionalLatLon = true)
    {
        CrimeEvent ev;
        ev.eventId    = QUuid::createUuid().toString();
        ev.id         = ev.eventId;
        ev.crimeType  = (index % 2 == 0) ? QStringLiteral("theft") : QStringLiteral("assault");
        ev.suburb     = QStringLiteral("Zone%1").arg(index % 4);
        ev.source     = QStringLiteral("deep4_test");
        ev.occurredAt = QDateTime::currentDateTimeUtc().addDays(-daysAgo);
        ev.timestamp  = ev.occurredAt.value();
        ev.ingestedAt = QDateTime::currentDateTimeUtc();

        const double lat = 51.5074 + (index % 6) * 0.003;
        const double lon = -0.1278 + (index % 6) * 0.003;
        ev.latitude  = lat;
        ev.longitude = lon;
        if (optionalLatLon) {
            ev.lat = lat;
            ev.lon = lon;
        }
        return ev;
    }

    static void seedEvents(const std::shared_ptr<Database>& db, int count,
                           int daysAgo = 0, bool optionalLatLon = true)
    {
        for (int i = 0; i < count; ++i)
            QVERIFY2(db->insertEvent(makeEvent(i, daysAgo + (i % 3), optionalLatLon)),
                     qPrintable(db->lastError()));
    }

    static QLabel* findSummaryLabel(const AnalyticsWidget& widget)
    {
        for (auto* lbl : widget.findChildren<QLabel*>()) {
            const QString t = lbl->text();
            if (t.contains(QStringLiteral("Total:"))
                || t.contains(QStringLiteral("Period total:"))
                || t.contains(QStringLiteral("Benchmark:")))
                return lbl;
        }
        return nullptr;
    }

    static QPushButton* findButtonByText(const AnalyticsWidget& widget, const QString& text)
    {
        for (auto* btn : widget.findChildren<QPushButton*>()) {
            if (btn->text().contains(text, Qt::CaseInsensitive))
                return btn;
        }
        return nullptr;
    }

private slots:
    void initTestCase()
    {
        qputenv("QT_QPA_PLATFORM", "offscreen");
    }

    void testConstructHasSevenTabs()
    {
        auto cfg = memCfg();
        auto db  = openDb();

        AnalyticsWidget widget(db, cfg);
        auto* tabs = widget.findChild<QTabWidget*>();
        QVERIFY(tabs != nullptr);
        QCOMPARE(tabs->count(), 7);
        QCOMPARE(tabs->tabText(3), QStringLiteral("Benchmark"));
        QCOMPARE(tabs->tabText(4), QStringLiteral("Calibration"));
        QCOMPARE(tabs->tabText(5), QStringLiteral("Heat Map"));
        QCOMPARE(tabs->tabText(6), QStringLiteral("Map View"));
    }

    void testBenchmarkInsufficientDataMessage()
    {
        auto cfg = memCfg();
        auto db  = openDb();
        seedEvents(db, 3);

        AnalyticsWidget widget(db, cfg);
        auto* btn = findButtonByText(widget, QStringLiteral("Run Benchmark"));
        QVERIFY(btn != nullptr);

        btn->click();
        QApplication::processEvents();

        auto* summary = findSummaryLabel(widget);
        QVERIFY(summary != nullptr);
        QVERIFY(summary->text().contains(QStringLiteral("insufficient data")));
        QVERIFY(btn->isEnabled());
    }

    void testCalibrationInsufficientDataMessage()
    {
        auto cfg = memCfg();
        auto db  = openDb();
        seedEvents(db, 10);

        AnalyticsWidget widget(db, cfg);
        auto* tabs = widget.findChild<QTabWidget*>();
        tabs->setCurrentIndex(4);
        QApplication::processEvents();

        auto* btn = findButtonByText(widget, QStringLiteral("Run Calibration"));
        QVERIFY(btn != nullptr);

        btn->click();
        QApplication::processEvents();

        const auto summaryLabels = widget.findChildren<QLabel*>();
        bool foundInsufficient = false;
        for (auto* lbl : summaryLabels) {
            if (lbl->text().contains(QStringLiteral("insufficient data"),
                                     Qt::CaseInsensitive)) {
                foundInsufficient = true;
                break;
            }
        }
        QVERIFY2(foundInsufficient,
                 "calibration with <20 events should show insufficient-data message");
        QVERIFY(btn->isEnabled());
    }

    void testRunBenchmarkPopulatesMetricsTable()
    {
        auto cfg = memCfg();
        auto db  = openDb();
        seedEvents(db, 40);

        AnalyticsWidget widget(db, cfg);
        auto* tabs = widget.findChild<QTabWidget*>();
        tabs->setCurrentIndex(3);
        QApplication::processEvents();

        auto* table = widget.findChild<QTableWidget*>();
        auto* btn   = findButtonByText(widget, QStringLiteral("Run Benchmark"));
        QVERIFY(table != nullptr);
        QVERIFY(btn != nullptr);

        btn->click();
        QApplication::processEvents();

        auto* valueCell = table->item(5, 1);  // AUC-ROC row
        auto* statusCell = table->item(5, 3);
        QVERIFY(valueCell != nullptr);
        QVERIFY(statusCell != nullptr);
        QVERIFY(valueCell->text() != QStringLiteral("—"));
        QVERIFY(statusCell->text() != QStringLiteral("—"));
        QVERIFY(btn->isEnabled());
    }

    void testHeatmapTabRespectsPeriodFilter()
    {
        auto cfg = memCfg();
        auto db  = openDb();

        CrimeEvent recent = makeEvent(0, 1);
        recent.occurredAt = QDateTime::currentDateTimeUtc().addDays(-2);
        recent.timestamp  = recent.occurredAt.value();
        QVERIFY(db->insertEvent(recent));

        CrimeEvent old = makeEvent(1, 1);
        old.occurredAt = QDateTime::currentDateTimeUtc().addDays(-45);
        old.timestamp  = old.occurredAt.value();
        QVERIFY(db->insertEvent(old));

        AnalyticsWidget widget(db, cfg);
        auto* tabs   = widget.findChild<QTabWidget*>();
        auto* period = widget.findChild<QComboBox*>();
        QVERIFY(tabs != nullptr);
        QVERIFY(period != nullptr);

        period->setCurrentIndex(0);  // Last 7 days
        tabs->setCurrentIndex(5);
        QApplication::processEvents();

        auto* heatmap = widget.findChild<TemporalHeatmapWidget*>();
        QVERIFY(heatmap != nullptr);

        int totalRecent = 0;
        for (int v : heatmap->hourlyData())
            totalRecent += v;
        QCOMPARE(totalRecent, 1);

        period->setCurrentIndex(3);  // All time
        QApplication::processEvents();

        int totalAll = 0;
        for (int v : heatmap->hourlyData())
            totalAll += v;
        QCOMPARE(totalAll, 2);
    }

    void testMapTabRefreshWithGeoEvents()
    {
        auto cfg = memCfg();
        auto db  = openDb();
        seedEvents(db, 12, 0, true);

        AnalyticsWidget widget(db, cfg);
        auto* tabs = widget.findChild<QTabWidget*>();
        tabs->setCurrentIndex(6);
        QApplication::processEvents();

        auto* map = widget.findChild<MapWidget*>();
        QVERIFY2(map != nullptr, "Map View tab should embed MapWidget");
        QVERIFY(!map->grab().isNull());
    }

    void testRefreshMapViewKdeIgnoresLatitudeLongitudeOnly()
    {
        // BUG: refreshMapView builds KDE input from ev.lat/ev.lon optional fields
        // only; events with latitude/longitude but unset optional coords are omitted.
        auto cfg = memCfg();
        auto db  = openDb();
        seedEvents(db, 8, 0, false);

        AnalyticsWidget widget(db, cfg);
        auto* tabs = widget.findChild<QTabWidget*>();
        tabs->setCurrentIndex(6);
        QApplication::processEvents();

        auto* map = widget.findChild<MapWidget*>();
        QVERIFY(map != nullptr);

        const QPixmap latitudeOnlyGrab = map->grab();

        auto cfg2 = memCfg();
        auto db2  = openDb();
        seedEvents(db2, 8, 0, true);

        AnalyticsWidget widget2(db2, cfg2);
        widget2.findChild<QTabWidget*>()->setCurrentIndex(6);
        QApplication::processEvents();

        const QPixmap optionalGrab = widget2.findChild<MapWidget*>()->grab();

        QVERIFY2(latitudeOnlyGrab.toImage() == optionalGrab.toImage(),
                 "KDE hotspots should render when latitude/longitude are populated");
    }

    void testRunCalibrationPopulatesMetricsWhenEnoughEvents()
    {
        auto cfg = memCfg();
        auto db  = openDb();
        seedEvents(db, 30);

        AnalyticsWidget widget(db, cfg);
        widget.findChild<QTabWidget*>()->setCurrentIndex(4);
        QApplication::processEvents();

        auto* btn = findButtonByText(widget, QStringLiteral("Run Calibration"));
        QVERIFY(btn != nullptr);
        btn->click();
        QApplication::processEvents();

        bool foundEce = false;
        for (auto* lbl : widget.findChildren<QLabel*>()) {
            if (lbl->text().contains(QStringLiteral("ECE:"))) {
                foundEce = true;
                break;
            }
        }
        QVERIFY(foundEce);
        QVERIFY(btn->isEnabled());
    }
};

QTEST_MAIN(TestAnalyticsWidgetDeep4)

#include "test_analytics_widget_deep4.moc"
