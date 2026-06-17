// test_settings_widget_deep4.cpp — Deep audit iteration 21: SettingsWidget
// auto-save gaps, GP/ensemble/API keys, map zoom, reset-to-defaults signal.

#include <QTest>
#include <QApplication>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QComboBox>
#include <QCheckBox>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalSpy>

#include "ui/SettingsWidget.h"
#include "core/AppConfig.h"

class TestSettingsWidgetDeep4 : public QObject {
    Q_OBJECT

    static QDoubleSpinBox* spinByTooltip(SettingsWidget& widget, const QString& fragment)
    {
        for (auto* spin : widget.findChildren<QDoubleSpinBox*>()) {
            if (spin->toolTip().contains(fragment, Qt::CaseInsensitive))
                return spin;
        }
        return nullptr;
    }

    static QLineEdit* lineEditByPlaceholder(SettingsWidget& widget, const QString& fragment)
    {
        for (auto* edit : widget.findChildren<QLineEdit*>()) {
            if (edit->placeholderText().contains(fragment, Qt::CaseInsensitive))
                return edit;
        }
        return nullptr;
    }

private slots:
    void initTestCase()
    {
        qputenv("QT_QPA_PLATFORM", "offscreen");
        qputenv("SENTINEL_HEADLESS_TEST", "1");
    }

    void testAutoSaveMapZoomUpdatesConfig()
    {
        AppConfig cfg;
        cfg.databasePath = QStringLiteral(":memory:");
        cfg.mapZoomLevel = 14.0;

        SettingsWidget widget(cfg);
        auto* mapZoom = spinByTooltip(widget, QStringLiteral("map zoom"));
        QVERIFY(mapZoom != nullptr);

        mapZoom->setValue(9.5);
        QApplication::processEvents();

        QCOMPARE(cfg.mapZoomLevel, 9.5);
    }

    void testAutoSaveRefreshIntervalUpdatesConfig()
    {
        AppConfig cfg;
        cfg.databasePath           = QStringLiteral(":memory:");
        cfg.autoRefreshEnabled     = true;
        cfg.refreshIntervalSeconds = 600;

        SettingsWidget widget(cfg);
        QSpinBox* intervalSpin = nullptr;
        for (auto* spin : widget.findChildren<QSpinBox*>()) {
            if (spin->suffix().contains(QStringLiteral("sec"))) {
                intervalSpin = spin;
                break;
            }
        }
        QVERIFY(intervalSpin != nullptr);

        intervalSpin->setValue(120);
        QApplication::processEvents();

        QCOMPARE(cfg.refreshIntervalSeconds, 120);
    }

    void testDefaultLocationAutoSavedOnEdit()
    {
        AppConfig cfg;
        cfg.databasePath  = QStringLiteral(":memory:");
        cfg.defaultLat    = 51.5074;
        cfg.defaultLon    = -0.1278;
        cfg.defaultRadius = 5.0;

        SettingsWidget widget(cfg);
        QDoubleSpinBox* latSpin = nullptr;
        QDoubleSpinBox* lonSpin = nullptr;
        QDoubleSpinBox* radiusSpin = nullptr;
        for (auto* spin : widget.findChildren<QDoubleSpinBox*>()) {
            if (spin->suffix().contains(QStringLiteral("km")) && spin->decimals() == 1) {
                radiusSpin = spin;
            } else if (spin->suffix() == QStringLiteral("°")) {
                if (spin->maximum() <= 90.0)
                    latSpin = spin;
                else
                    lonSpin = spin;
            }
        }
        QVERIFY(latSpin != nullptr);
        QVERIFY(lonSpin != nullptr);
        QVERIFY(radiusSpin != nullptr);

        latSpin->setValue(48.8566);
        lonSpin->setValue(2.3522);
        radiusSpin->setValue(12.0);
        QApplication::processEvents();

        QCOMPARE(cfg.defaultLat, 48.8566);
        QCOMPARE(cfg.defaultLon, 2.3522);
        QCOMPARE(cfg.defaultRadius, 12.0);
    }

    void testApiKeysNotAutoSavedUntilManualSave()
    {
        AppConfig cfg;
        cfg.databasePath   = QStringLiteral(":memory:");
        cfg.openWeatherKey = QStringLiteral("old_key");
        cfg.socrataToken   = QStringLiteral("old_token");

        SettingsWidget widget(cfg);
        auto* weatherEdit = lineEditByPlaceholder(widget, QStringLiteral("OpenWeather"));
        auto* socrataEdit = lineEditByPlaceholder(widget, QStringLiteral("Socrata"));
        QVERIFY(weatherEdit != nullptr);
        QVERIFY(socrataEdit != nullptr);

        weatherEdit->setText(QStringLiteral("new_weather_key"));
        socrataEdit->setText(QStringLiteral("new_socrata_token"));
        QApplication::processEvents();

        // BUG SettingsWidget.cpp — API key fields lack auto-save connections.
        QCOMPARE(cfg.openWeatherKey, QStringLiteral("old_key"));
        QCOMPARE(cfg.socrataToken, QStringLiteral("old_token"));
    }

    void testGpAndEnsembleNotAutoSavedUntilManualSave()
    {
        AppConfig cfg;
        cfg.databasePath         = QStringLiteral(":memory:");
        cfg.gpSigma2             = 1.0;
        cfg.ensemblePoissonWeight = 0.5;

        SettingsWidget widget(cfg);
        auto* gpSigma = spinByTooltip(widget, QStringLiteral("signal variance"));
        auto* poissonWeight = spinByTooltip(widget, QStringLiteral("Poisson baseline"));
        QVERIFY(gpSigma != nullptr);
        QVERIFY(poissonWeight != nullptr);

        gpSigma->setValue(2.5);
        poissonWeight->setValue(0.75);
        QApplication::processEvents();

        QCOMPARE(cfg.gpSigma2, 1.0);
        QCOMPARE(cfg.ensemblePoissonWeight, 0.5);
    }

    void testResetToDefaultsEmitsSettingsSaved()
    {
        AppConfig cfg;
        cfg.databasePath      = QStringLiteral(":memory:");
        cfg.hawkesHistoryDays = 90;
        cfg.maxLeadCount      = 200;

        SettingsWidget widget(cfg);
        QSignalSpy spy(&widget, &SettingsWidget::settingsSaved);

        QPushButton* defaultsBtn = nullptr;
        for (auto* btn : widget.findChildren<QPushButton*>()) {
            if (btn->text().contains(QStringLiteral("Defaults"))) {
                defaultsBtn = btn;
                break;
            }
        }
        QVERIFY(defaultsBtn != nullptr);
        QTest::mouseClick(defaultsBtn, Qt::LeftButton);
        QApplication::processEvents();

        QCOMPARE(spy.count(), 1);
        QCOMPARE(cfg.hawkesHistoryDays, 365);
        QCOMPARE(cfg.maxLeadCount, 50);
    }

    void testAllThreeAlertThresholdsAutoSave()
    {
        AppConfig cfg;
        cfg.databasePath   = QStringLiteral(":memory:");
        cfg.alertElevated  = 0.30;
        cfg.alertHigh      = 0.50;
        cfg.alertCritical  = 0.75;

        SettingsWidget widget(cfg);
        auto* elevated = spinByTooltip(widget, QStringLiteral("Elevated alert"));
        auto* high     = spinByTooltip(widget, QStringLiteral("High alert"));
        auto* critical = spinByTooltip(widget, QStringLiteral("Critical alert"));
        QVERIFY(elevated != nullptr);
        QVERIFY(high != nullptr);
        QVERIFY(critical != nullptr);

        elevated->setValue(0.35);
        high->setValue(0.55);
        critical->setValue(0.80);
        QApplication::processEvents();

        QCOMPARE(cfg.alertElevated, 0.35);
        QCOMPARE(cfg.alertHigh, 0.55);
        QCOMPARE(cfg.alertCritical, 0.80);
    }

    void testManualSaveAppliesDefaultLocationAfterEdit()
    {
        AppConfig cfg;
        cfg.databasePath = QStringLiteral(":memory:");
        cfg.defaultLat   = 51.5074;

        SettingsWidget widget(cfg);
        QDoubleSpinBox* latSpin = nullptr;
        for (auto* spin : widget.findChildren<QDoubleSpinBox*>()) {
            if (spin->maximum() <= 90.0 && spin->suffix() == QStringLiteral("°")) {
                latSpin = spin;
                break;
            }
        }
        QVERIFY(latSpin != nullptr);
        latSpin->setValue(53.4808);

        auto* saveBtn = widget.findChild<QPushButton*>(QStringLiteral("saveSettingsBtn"));
        QVERIFY(saveBtn != nullptr);
        QTest::mouseClick(saveBtn, Qt::LeftButton);
        QApplication::processEvents();

        QCOMPARE(cfg.defaultLat, 53.4808);
    }
};

QTEST_MAIN(TestSettingsWidgetDeep4)

#include "test_settings_widget_deep4.moc"
