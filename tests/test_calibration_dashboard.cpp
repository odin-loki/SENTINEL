// test_calibration_dashboard.cpp — CalibrationDashboardWidget headless smoke test
#include <QTest>
#include <QTimeZone>
#include <QApplication>
#include <QPushButton>
#include <QtCharts/QChartView>
#include <QLabel>
#include <memory>
#include "ui/CalibrationDashboardWidget.h"
#include "core/Database.h"
#include "core/AppConfig.h"
#include "core/CrimeEvent.h"

class TestCalibrationDashboard : public QObject
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

    static CrimeEvent monthEvent(const QString& id, int month, int day = 15)
    {
        CrimeEvent ev;
        ev.eventId    = id;
        ev.id         = id;
        ev.crimeType  = QStringLiteral("burglary");
        ev.suburb     = QStringLiteral("ZONE-A");
        ev.occurredAt = QDateTime(QDate(2024, month, day), QTime(12, 0), QTimeZone::utc());
        ev.timestamp  = ev.occurredAt.value();
        ev.lat        = 51.5;
        ev.lon        = -0.1;
        return ev;
    }

private slots:
    void initTestCase()
    {
        qputenv("QT_QPA_PLATFORM", "offscreen");
        qputenv("SENTINEL_HEADLESS_TEST", "1");
    }

    void testConstructsWithChartView()
    {
        auto db = openDb();
        AppConfig cfg = memCfg();
        CalibrationDashboardWidget widget(db, cfg);
        QVERIFY(widget.findChild<QChartView*>() != nullptr);
        QVERIFY(widget.findChild<QPushButton*>() != nullptr);
        QVERIFY(widget.findChild<QLabel*>() != nullptr);
    }

    void testRunAnalysisWithMonthHoldoutData()
    {
        auto db = openDb();
        AppConfig cfg = memCfg();

        for (int i = 0; i < 25; ++i)
            db->insertEvent(monthEvent(QStringLiteral("TR-%1").arg(i), 2, 1 + (i % 28)));
        for (int i = 0; i < 12; ++i)
            db->insertEvent(monthEvent(QStringLiteral("TE-%1").arg(i), 6, 1 + (i % 28)));

        CalibrationDashboardWidget widget(db, cfg);
        widget.refresh();

        const auto labels = widget.findChildren<QLabel*>();
        bool hasSummary = false;
        for (QLabel* lbl : labels) {
            if (lbl->text().contains(QStringLiteral("ECE")))
                hasSummary = true;
        }
        QVERIFY(hasSummary);

        auto* chartView = widget.findChild<QChartView*>();
        QVERIFY(chartView != nullptr);
        QVERIFY(chartView->chart() != nullptr);
        QVERIFY(chartView->chart()->series().size() >= 1);
    }
};

QTEST_MAIN(TestCalibrationDashboard)
#include "test_calibration_dashboard.moc"
