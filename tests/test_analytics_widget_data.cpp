// test_analytics_widget_data.cpp — AnalyticsWidget data-model tests
// Tests construction, refresh(), and data-driven behaviour of AnalyticsWidget
// without asserting on rendered pixel colours.
// Runs headless via -platform offscreen.

#include <QTest>
#include <QApplication>
#include <QTabWidget>
#include <QComboBox>
#include <QLabel>
#include <QPushButton>
#include <QUuid>
#include <memory>

#include "core/AppConfig.h"
#include "core/Database.h"
#include "core/CrimeEvent.h"
#include "ui/AnalyticsWidget.h"

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

static std::shared_ptr<Database> makeDB()
{
    AppConfig cfg;
    cfg.databasePath = QStringLiteral(":memory:");
    auto db = std::make_shared<Database>(cfg);
    db->open();
    return db;
}

static CrimeEvent makeEvent(double lat, double lon,
                            const QString& type = QStringLiteral("burglary"))
{
    CrimeEvent e;
    e.eventId      = QUuid::createUuid().toString();
    e.id           = e.eventId;
    e.crimeType    = type;
    e.suburb       = QStringLiteral("TestSuburb");
    e.lat          = lat;
    e.lon          = lon;
    e.latitude     = lat;
    e.longitude    = lon;
    e.occurredAt   = QDateTime::currentDateTimeUtc();
    e.ingestedAt   = QDateTime::currentDateTimeUtc();
    e.source       = QStringLiteral("test");
    e.qualityScore = 0.9;
    return e;
}

// ─────────────────────────────────────────────────────────────────────────────

class TestAnalyticsWidgetData : public QObject {
    Q_OBJECT

private slots:

    // 1. AnalyticsWidget constructs with an empty in-memory database
    void testConstruction()
    {
        AppConfig cfg;
        cfg.databasePath = QStringLiteral(":memory:");
        auto db = makeDB();
        AnalyticsWidget w(db, cfg);
        QVERIFY(true);
    }

    // 2. Widget reports positive dimensions after construction
    void testWidgetHasPositiveSize()
    {
        AppConfig cfg;
        cfg.databasePath = QStringLiteral(":memory:");
        auto db = makeDB();
        AnalyticsWidget w(db, cfg);
        w.resize(900, 600);
        QVERIFY(w.width()  > 0);
        QVERIFY(w.height() > 0);
    }

    // 3. refresh() on an empty database does not crash
    void testRefreshEmptyDatabase()
    {
        AppConfig cfg;
        cfg.databasePath = QStringLiteral(":memory:");
        auto db = makeDB();
        AnalyticsWidget w(db, cfg);
        w.resize(900, 600);
        w.refresh();
        QApplication::processEvents();
        QVERIFY(true);
    }

    // 4. refresh() with a populated database does not crash
    void testRefreshPopulatedDatabase()
    {
        AppConfig cfg;
        cfg.databasePath = QStringLiteral(":memory:");
        auto db = makeDB();

        const QStringList types{ "burglary", "robbery", "assault", "theft", "vandalism" };
        for (int i = 0; i < 50; ++i)
            db->insertEvent(makeEvent(51.4 + (i % 10) * 0.005,
                                     -0.2  + (i % 10) * 0.004,
                                     types[i % types.size()]));

        AnalyticsWidget w(db, cfg);
        w.resize(900, 600);
        w.refresh();
        QApplication::processEvents();
        QVERIFY(true);
    }

    // 5. Multiple successive refresh() calls do not crash
    void testRefreshMultipleTimes()
    {
        AppConfig cfg;
        cfg.databasePath = QStringLiteral(":memory:");
        auto db = makeDB();
        for (int i = 0; i < 20; ++i)
            db->insertEvent(makeEvent(51.5 + i * 0.001, -0.1 + i * 0.001));

        AnalyticsWidget w(db, cfg);
        w.resize(900, 600);
        for (int i = 0; i < 5; ++i) {
            w.refresh();
            QApplication::processEvents();
        }
        QVERIFY(true);
    }

    // 6. Widget contains a QTabWidget child (tabs are always set up)
    void testWidgetHasTabWidget()
    {
        AppConfig cfg;
        cfg.databasePath = QStringLiteral(":memory:");
        auto db = makeDB();
        AnalyticsWidget w(db, cfg);
        w.resize(900, 600);
        QTabWidget* tabs = w.findChild<QTabWidget*>();
        QVERIFY(tabs != nullptr);
    }

    // 7. QTabWidget has at least one tab
    void testTabWidgetHasTabs()
    {
        AppConfig cfg;
        cfg.databasePath = QStringLiteral(":memory:");
        auto db = makeDB();
        AnalyticsWidget w(db, cfg);
        w.resize(900, 600);
        QTabWidget* tabs = w.findChild<QTabWidget*>();
        QVERIFY(tabs != nullptr);
        QVERIFY(tabs->count() > 0);
    }

    // 8. Widget contains a QComboBox (period selector)
    void testWidgetHasComboBox()
    {
        AppConfig cfg;
        cfg.databasePath = QStringLiteral(":memory:");
        auto db = makeDB();
        AnalyticsWidget w(db, cfg);
        w.resize(900, 600);
        QComboBox* combo = w.findChild<QComboBox*>();
        QVERIFY(combo != nullptr);
    }

    // 9. refresh() with a large batch of mixed crime types does not crash
    void testRefreshLargeDataset()
    {
        AppConfig cfg;
        cfg.databasePath = QStringLiteral(":memory:");
        auto db = makeDB();

        const QStringList types{ "burglary", "robbery", "assault", "theft",
                                 "vandalism", "drug_offence", "vehicle_crime", "antisocial" };
        for (int i = 0; i < 200; ++i)
            db->insertEvent(makeEvent(51.3 + (i % 30) * 0.003,
                                     -0.3  + (i % 30) * 0.003,
                                     types[i % types.size()]));

        AnalyticsWidget w(db, cfg);
        w.resize(1200, 800);
        w.refresh();
        QApplication::processEvents();
        QVERIFY(true);
    }

    // 10. QTabWidget has exactly 7 tabs (Hour, Types, Trend, Benchmark, Calibration, Heatmap, Map)
    void testTabWidgetHasSevenTabs()
    {
        AppConfig cfg;
        cfg.databasePath = QStringLiteral(":memory:");
        auto db = makeDB();
        AnalyticsWidget w(db, cfg);
        w.resize(900, 600);
        QTabWidget* tabs = w.findChild<QTabWidget*>();
        QVERIFY(tabs != nullptr);
        QCOMPARE(tabs->count(), 7);
    }

    // 11. Calibration tab (index 4) exists with correct title
    void testCalibrationTabExists()
    {
        AppConfig cfg;
        cfg.databasePath = QStringLiteral(":memory:");
        auto db = makeDB();
        AnalyticsWidget w(db, cfg);
        w.resize(900, 600);
        QTabWidget* tabs = w.findChild<QTabWidget*>();
        QVERIFY(tabs != nullptr);
        QVERIFY(tabs->count() >= 5);
        // Tab 4 should be the Calibration tab
        QCOMPARE(tabs->tabText(4), QStringLiteral("Calibration"));
    }

    // 12. Switching to the calibration tab does not crash
    void testCalibrationTabSwitch()
    {
        AppConfig cfg;
        cfg.databasePath = QStringLiteral(":memory:");
        auto db = makeDB();
        for (int i = 0; i < 30; ++i)
            db->insertEvent(makeEvent(51.5 + i * 0.001, -0.1 + i * 0.001));
        AnalyticsWidget w(db, cfg);
        w.resize(900, 600);
        QTabWidget* tabs = w.findChild<QTabWidget*>();
        QVERIFY(tabs != nullptr);
        if (tabs->count() >= 5) {
            tabs->setCurrentIndex(4);
            QApplication::processEvents();
        }
        QVERIFY(true);
    }

    // 13. Calibration run button exists in the calibration tab
    void testCalibrationRunButtonExists()
    {
        AppConfig cfg;
        cfg.databasePath = QStringLiteral(":memory:");
        auto db = makeDB();
        AnalyticsWidget w(db, cfg);
        w.resize(900, 600);
        // Calibration dashboard uses "Run Holdout Analysis" (CalibrationDashboardWidget)
        const QList<QPushButton*> buttons = w.findChildren<QPushButton*>();
        bool found = false;
        for (const auto* btn : buttons) {
            if (btn->text().contains(QStringLiteral("Holdout"), Qt::CaseInsensitive)
                || btn->text().contains(QStringLiteral("Calibration"), Qt::CaseInsensitive)) {
                found = true;
                break;
            }
        }
        QVERIFY(found);
    }

    // 14. Calibration analysis on populated data does not crash
    void testCalibrationRunWithData()
    {
        AppConfig cfg;
        cfg.databasePath = QStringLiteral(":memory:");
        auto db = makeDB();

        // Insert 100 events with varied zones and types
        const QStringList types{ "burglary", "robbery", "assault", "theft", "vandalism" };
        for (int i = 0; i < 100; ++i) {
            auto ev = makeEvent(51.4 + (i % 20) * 0.003, -0.2 + (i % 20) * 0.003, types[i % types.size()]);
            ev.suburb = QString("Zone%1").arg(i % 5);
            db->insertEvent(ev);
        }

        AnalyticsWidget w(db, cfg);
        w.resize(900, 600);

        // Find and click the calibration run button
        const QList<QPushButton*> buttons = w.findChildren<QPushButton*>();
        for (auto* btn : buttons) {
            if (btn->text().contains(QStringLiteral("Holdout"), Qt::CaseInsensitive)
                || btn->text().contains(QStringLiteral("Calibration"), Qt::CaseInsensitive)) {
                btn->click();
                QApplication::processEvents();
                break;
            }
        }
        QVERIFY(true);
    }

    // 15. Construction with non-default AppConfig values does not crash
    void testConstructionNonDefaultConfig()
    {
        AppConfig cfg;
        cfg.databasePath        = QStringLiteral(":memory:");
        cfg.defaultLat          = 37.7749;
        cfg.defaultLon          = -122.4194;
        cfg.defaultRadius       = 10.0;
        cfg.forecastHorizonDays = 14;
        cfg.theme               = QStringLiteral("light");

        auto db = makeDB();
        for (int i = 0; i < 30; ++i)
            db->insertEvent(makeEvent(37.77 + i * 0.001, -122.42 + i * 0.001, "theft"));

        AnalyticsWidget w(db, cfg);
        w.resize(900, 600);
        w.refresh();
        QApplication::processEvents();
        QVERIFY(true);
    }
};

// ─────────────────────────────────────────────────────────────────────────────

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    app.setAttribute(Qt::AA_Use96Dpi);
    TestAnalyticsWidgetData tc;
    return QTest::qExec(&tc, argc, argv);
}

#include "test_analytics_widget_data.moc"
