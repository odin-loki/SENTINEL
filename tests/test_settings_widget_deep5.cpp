// test_settings_widget_deep5.cpp — Deep audit iteration 25: SettingsWidget
// theme combo, hawkes spin, radius spin, reset defaults.
#include <QTest>
#include <QApplication>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QComboBox>
#include <QPushButton>
#include <QSignalSpy>
#include "ui/SettingsWidget.h"
#include "core/AppConfig.h"

class TestSettingsWidgetDeep5 : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        qputenv("QT_QPA_PLATFORM", "offscreen");
        qputenv("SENTINEL_HEADLESS_TEST", "1");
    }

    void testHawkesSpinInitialized()
    {
        AppConfig cfg;
        cfg.databasePath = QStringLiteral(":memory:");
        SettingsWidget sw(cfg);

        auto* hawkesSpin = sw.findChild<QSpinBox*>(QStringLiteral("hawkesHistorySpin"));
        QVERIFY(hawkesSpin != nullptr);
        QCOMPARE(hawkesSpin->value(), cfg.hawkesHistoryDays);
    }

    void testRadiusSpinHasKmSuffix()
    {
        AppConfig cfg;
        cfg.databasePath = QStringLiteral(":memory:");
        SettingsWidget sw(cfg);

        QDoubleSpinBox* radiusSpin = nullptr;
        for (auto* spin : sw.findChildren<QDoubleSpinBox*>()) {
            if (spin->suffix().contains(QStringLiteral("km"))
                && spin->decimals() == 1)
                radiusSpin = spin;
        }
        QVERIFY(radiusSpin != nullptr);
        QCOMPARE(radiusSpin->value(), cfg.defaultRadius);
    }

    void testThemeComboExists()
    {
        AppConfig cfg;
        cfg.databasePath = QStringLiteral(":memory:");
        SettingsWidget sw(cfg);

        const auto combos = sw.findChildren<QComboBox*>();
        bool hasTheme = false;
        for (auto* combo : combos) {
            if (combo->count() >= 2
                && (combo->itemText(0).contains(QStringLiteral("dark"), Qt::CaseInsensitive)
                    || combo->itemText(1).contains(QStringLiteral("light"), Qt::CaseInsensitive)))
                hasTheme = true;
        }
        QVERIFY(hasTheme);
    }

    void testSaveButtonExists()
    {
        AppConfig cfg;
        cfg.databasePath = QStringLiteral(":memory:");
        SettingsWidget sw(cfg);

        auto* saveBtn = sw.findChild<QPushButton*>(QStringLiteral("saveSettingsBtn"));
        QVERIFY(saveBtn != nullptr);
    }

    void testChangingHawkesSpinValue()
    {
        AppConfig cfg;
        cfg.databasePath = QStringLiteral(":memory:");
        SettingsWidget sw(cfg);

        auto* hawkesSpin = sw.findChild<QSpinBox*>(QStringLiteral("hawkesHistorySpin"));
        QVERIFY(hawkesSpin != nullptr);
        hawkesSpin->setValue(180);
        QCOMPARE(hawkesSpin->value(), 180);
    }

    void testSettingsSavedSignalValid()
    {
        AppConfig cfg;
        cfg.databasePath = QStringLiteral(":memory:");
        SettingsWidget sw(cfg);

        QSignalSpy spy(&sw, &SettingsWidget::settingsSaved);
        QVERIFY(spy.isValid());
    }

    void testValidatePassesWithMemoryDb()
    {
        AppConfig cfg;
        cfg.databasePath = QStringLiteral(":memory:");
        QVERIFY(cfg.validate());
    }
};

QTEST_MAIN(TestSettingsWidgetDeep5)
#include "test_settings_widget_deep5.moc"
