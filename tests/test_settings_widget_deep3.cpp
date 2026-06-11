// test_settings_widget_deep3.cpp — Deep audit iteration 18: SettingsWidget
// auto-save coverage, manual save/reset, validation, signal emission.

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

class TestSettingsWidgetDeep3 : public QObject {
    Q_OBJECT

    static QDoubleSpinBox* alertSpin(SettingsWidget& widget, const QString& tooltipFragment)
    {
        for (auto* spin : widget.findChildren<QDoubleSpinBox*>()) {
            if (spin->toolTip().contains(tooltipFragment, Qt::CaseInsensitive))
                return spin;
        }
        return nullptr;
    }

    static QLineEdit* databasePathEdit(SettingsWidget& widget)
    {
        for (auto* edit : widget.findChildren<QLineEdit*>()) {
            if (edit->placeholderText().contains(QStringLiteral("sentinel.db"), Qt::CaseInsensitive))
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

    void testAutoSaveThemeUpdatesConfig()
    {
        AppConfig cfg;
        cfg.databasePath = QStringLiteral(":memory:");
        cfg.theme        = QStringLiteral("dark");

        SettingsWidget widget(cfg);
        auto* themeCombo = widget.findChild<QComboBox*>();
        QVERIFY(themeCombo != nullptr);

        themeCombo->setCurrentIndex(1);
        QApplication::processEvents();

        QCOMPARE(cfg.theme, QStringLiteral("light"));
    }

    void testAutoSaveAlertThresholdUpdatesConfig()
    {
        AppConfig cfg;
        cfg.databasePath  = QStringLiteral(":memory:");
        cfg.alertElevated = 0.30;

        SettingsWidget widget(cfg);
        auto* elevated = alertSpin(widget, QStringLiteral("Elevated alert"));
        QVERIFY(elevated != nullptr);

        elevated->setValue(0.42);
        QApplication::processEvents();

        QCOMPARE(cfg.alertElevated, 0.42);
    }

    void testModelParamsNotAutoSavedUntilManualSave()
    {
        AppConfig cfg;
        cfg.databasePath      = QStringLiteral(":memory:");
        cfg.hawkesHistoryDays = 365;

        SettingsWidget widget(cfg);
        auto* hawkesSpin = widget.findChild<QSpinBox*>(QStringLiteral("hawkesHistorySpin"));
        QVERIFY(hawkesSpin != nullptr);

        hawkesSpin->setValue(120);
        QApplication::processEvents();

        QCOMPARE(cfg.hawkesHistoryDays, 365);
    }

    void testManualSaveAppliesModelParamsAndEmitsSignal()
    {
        AppConfig cfg;
        cfg.databasePath      = QStringLiteral(":memory:");
        cfg.hawkesHistoryDays = 365;
        cfg.forecastHorizonDays = 7;

        SettingsWidget widget(cfg);
        auto* hawkesSpin = widget.findChild<QSpinBox*>(QStringLiteral("hawkesHistorySpin"));
        auto* horizonSpin = widget.findChild<QSpinBox*>();
        QVERIFY(hawkesSpin != nullptr);

        for (auto* spin : widget.findChildren<QSpinBox*>()) {
            if (spin->suffix().contains(QStringLiteral("days"))
                && spin != hawkesSpin
                && spin->maximum() == 30) {
                horizonSpin = spin;
                break;
            }
        }
        QVERIFY(horizonSpin != nullptr);

        hawkesSpin->setValue(200);
        horizonSpin->setValue(14);

        QSignalSpy spy(&widget, &SettingsWidget::settingsSaved);
        auto* saveBtn = widget.findChild<QPushButton*>(QStringLiteral("saveSettingsBtn"));
        QVERIFY(saveBtn != nullptr);
        QTest::mouseClick(saveBtn, Qt::LeftButton);
        QApplication::processEvents();

        QCOMPARE(cfg.hawkesHistoryDays, 200);
        QCOMPARE(cfg.forecastHorizonDays, 14);
        QCOMPARE(spy.count(), 1);
    }

    void testSaveRejectsEmptyDatabasePath()
    {
        AppConfig cfg;
        cfg.databasePath      = QStringLiteral(":memory:");
        cfg.hawkesHistoryDays = 365;

        SettingsWidget widget(cfg);
        auto* dbEdit = databasePathEdit(widget);
        QVERIFY(dbEdit != nullptr);
        dbEdit->clear();

        auto* hawkesSpin = widget.findChild<QSpinBox*>(QStringLiteral("hawkesHistorySpin"));
        QVERIFY(hawkesSpin != nullptr);
        hawkesSpin->setValue(90);

        QSignalSpy spy(&widget, &SettingsWidget::settingsSaved);
        auto* saveBtn = widget.findChild<QPushButton*>(QStringLiteral("saveSettingsBtn"));
        QTest::mouseClick(saveBtn, Qt::LeftButton);
        QApplication::processEvents();

        QCOMPARE(spy.count(), 0);
        QCOMPARE(cfg.hawkesHistoryDays, 365);
    }

    void testAutoRefreshToggleDisablesIntervalSpin()
    {
        AppConfig cfg;
        cfg.databasePath         = QStringLiteral(":memory:");
        cfg.autoRefreshEnabled   = true;
        cfg.refreshIntervalSeconds = 600;

        SettingsWidget widget(cfg);

        QCheckBox* autoCheck = nullptr;
        QSpinBox* intervalSpin = nullptr;
        for (auto* check : widget.findChildren<QCheckBox*>()) {
            if (check->text().contains(QStringLiteral("automatic"), Qt::CaseInsensitive)) {
                autoCheck = check;
                break;
            }
        }
        for (auto* spin : widget.findChildren<QSpinBox*>()) {
            if (spin->suffix().contains(QStringLiteral("sec"))) {
                intervalSpin = spin;
                break;
            }
        }
        QVERIFY(autoCheck != nullptr);
        QVERIFY(intervalSpin != nullptr);
        QVERIFY(intervalSpin->isEnabled());

        autoCheck->setChecked(false);
        QApplication::processEvents();
        QVERIFY(!intervalSpin->isEnabled());

        autoCheck->setChecked(true);
        QApplication::processEvents();
        QVERIFY(intervalSpin->isEnabled());
    }

    void testResetToSavedReloadsModifiedFields()
    {
        AppConfig cfg;
        cfg.databasePath      = QStringLiteral(":memory:");
        cfg.hawkesHistoryDays = 365;
        cfg.maxLeadCount      = 50;

        SettingsWidget widget(cfg);
        auto* hawkesSpin = widget.findChild<QSpinBox*>(QStringLiteral("hawkesHistorySpin"));
        QVERIFY(hawkesSpin != nullptr);
        hawkesSpin->setValue(42);

        QPushButton* resetBtn = nullptr;
        for (auto* btn : widget.findChildren<QPushButton*>()) {
            if (btn->text().contains(QStringLiteral("Saved"))) {
                resetBtn = btn;
                break;
            }
        }
        QVERIFY(resetBtn != nullptr);
        QTest::mouseClick(resetBtn, Qt::LeftButton);
        QApplication::processEvents();

        QCOMPARE(hawkesSpin->value(), 365);
    }

    void testMaxLeadCountAutoSaveUpdatesConfig()
    {
        AppConfig cfg;
        cfg.databasePath = QStringLiteral(":memory:");
        cfg.maxLeadCount = 50;

        SettingsWidget widget(cfg);
        QSpinBox* maxLeads = nullptr;
        for (auto* spin : widget.findChildren<QSpinBox*>()) {
            if (spin->toolTip().contains(QStringLiteral("leads panel"), Qt::CaseInsensitive)) {
                maxLeads = spin;
                break;
            }
        }
        QVERIFY(maxLeads != nullptr);

        maxLeads->setValue(120);
        QApplication::processEvents();

        QCOMPARE(cfg.maxLeadCount, 120);
    }
};

QTEST_MAIN(TestSettingsWidgetDeep3)

#include "test_settings_widget_deep3.moc"
