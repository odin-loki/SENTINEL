// test_appconfig_deep7.cpp — Deep audit iteration 30: AppConfig
// series params, alert ordering, GP params, maxLeadCount clamp.
#include <QtTest/QtTest>
#include "core/AppConfig.h"

class AppConfigDeep7Test : public QObject
{
    Q_OBJECT

private slots:

    void testSeriesMinEventsClampedLow()
    {
        AppConfig cfg;
        cfg.databasePath    = QStringLiteral(":memory:");
        cfg.seriesMinEvents = 1;
        cfg.validate();
        QCOMPARE(cfg.seriesMinEvents, 2);
    }

    void testAlertThresholdsOrderedAfterValidate()
    {
        AppConfig cfg;
        cfg.databasePath   = QStringLiteral(":memory:");
        cfg.alertElevated  = 0.95;
        cfg.alertHigh      = 0.50;
        cfg.alertCritical  = 0.40;
        cfg.validate();
        QVERIFY(cfg.alertElevated < cfg.alertHigh);
        QVERIFY(cfg.alertHigh < cfg.alertCritical);
    }

    void testGpSigma2ClampedHigh()
    {
        AppConfig cfg;
        cfg.databasePath = QStringLiteral(":memory:");
        cfg.gpSigma2     = 500.0;
        cfg.validate();
        QCOMPARE(cfg.gpSigma2, 100.0);
    }

    void testMaxLeadCountPositiveAfterValidate()
    {
        AppConfig cfg;
        cfg.databasePath = QStringLiteral(":memory:");
        cfg.maxLeadCount = 0;
        cfg.validate();
        QVERIFY(cfg.maxLeadCount >= 1);
    }

    void testSeriesEpsKmWithinRange()
    {
        AppConfig cfg;
        cfg.databasePath = QStringLiteral(":memory:");
        cfg.seriesEpsKm  = -1.0;
        cfg.validate();
        QVERIFY(cfg.seriesEpsKm > 0.0);
    }
};

QTEST_GUILESS_MAIN(AppConfigDeep7Test)
#include "test_appconfig_deep7.moc"
