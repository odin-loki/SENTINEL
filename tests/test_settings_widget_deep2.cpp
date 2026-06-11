// test_settings_widget_deep2.cpp — Deep audit iteration 14: SettingsWidget defaults.

#include <QTest>
#include <QApplication>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QPushButton>

#include "ui/SettingsWidget.h"
#include "core/AppConfig.h"

class TestSettingsWidgetDeep2 : public QObject {
    Q_OBJECT

private slots:
    void initTestCase()
    {
        qputenv("QT_QPA_PLATFORM", "offscreen");
        qputenv("SENTINEL_HEADLESS_TEST", "1");
    }

    void testConstructNoCrash()
    {
        AppConfig cfg;
        cfg.databasePath = QStringLiteral(":memory:");
        SettingsWidget widget(cfg);
        widget.resize(900, 700);
        QVERIFY(widget.width() > 0);
        QVERIFY(widget.height() > 0);
    }

    void testLoadDefaultsReflectConfig()
    {
        AppConfig cfg;
        cfg.databasePath = QStringLiteral(":memory:");
        cfg.hawkesHistoryDays = 365;
        cfg.defaultRadius = 5.0;
        cfg.forecastHorizonDays = 7;
        cfg.alertElevated = 0.30;

        SettingsWidget widget(cfg);

        auto* hawkesSpin = widget.findChild<QSpinBox*>(QStringLiteral("hawkesHistorySpin"));
        QVERIFY(hawkesSpin != nullptr);
        QCOMPARE(hawkesSpin->value(), 365);

        QDoubleSpinBox* radiusSpin = nullptr;
        for (auto* spin : widget.findChildren<QDoubleSpinBox*>()) {
            if (spin->suffix().contains(QStringLiteral("km"))) {
                radiusSpin = spin;
                break;
            }
        }
        QVERIFY(radiusSpin != nullptr);
        QCOMPARE(radiusSpin->value(), 5.0);

        auto* saveBtn = widget.findChild<QPushButton*>(QStringLiteral("saveSettingsBtn"));
        QVERIFY(saveBtn != nullptr);
    }

    void testResetToDefaultsNoCrash()
    {
        AppConfig cfg;
        cfg.databasePath = QStringLiteral(":memory:");
        cfg.hawkesHistoryDays = 90;

        SettingsWidget widget(cfg);

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

        auto* hawkesSpin = widget.findChild<QSpinBox*>(QStringLiteral("hawkesHistorySpin"));
        QVERIFY(hawkesSpin != nullptr);
        QCOMPARE(hawkesSpin->value(), 365);
    }
};

QTEST_MAIN(TestSettingsWidgetDeep2)

#include "test_settings_widget_deep2.moc"
