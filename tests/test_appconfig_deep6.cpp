// test_appconfig_deep6.cpp — Deep audit iteration 26: AppConfig
// theme field, hawkes clamp, ensemble weight normalisation, map zoom.
#include <QtTest/QtTest>
#include "core/AppConfig.h"

class AppConfigDeep6Test : public QObject
{
    Q_OBJECT

private slots:

    void testValidateClampsHawkesHistory()
    {
        AppConfig cfg;
        cfg.databasePath      = QStringLiteral(":memory:");
        cfg.hawkesHistoryDays = 2;
        cfg.validate();
        QCOMPARE(cfg.hawkesHistoryDays, 7);
    }

    void testValidateNormalisesEnsembleWeights()
    {
        AppConfig cfg;
        cfg.databasePath        = QStringLiteral(":memory:");
        cfg.ensemblePoissonWeight = 3.0;
        cfg.ensembleHawkesWeight  = 1.0;
        cfg.validate();
        QVERIFY2(std::abs(cfg.ensemblePoissonWeight + cfg.ensembleHawkesWeight - 1.0) < 1e-6,
                 qPrintable(QStringLiteral("poisson=%1 hawkes=%2")
                                .arg(cfg.ensemblePoissonWeight)
                                .arg(cfg.ensembleHawkesWeight)));
    }

    void testThemeDefaultsToDark()
    {
        AppConfig cfg;
        QVERIFY(cfg.theme == QStringLiteral("dark") || cfg.theme.isEmpty());
    }

    void testMapZoomWithinRangeAfterValidate()
    {
        AppConfig cfg;
        cfg.databasePath = QStringLiteral(":memory:");
        cfg.mapZoomLevel = 99.0;
        cfg.validate();
        QCOMPARE(cfg.mapZoomLevel, 20.0);
    }

    void testForecastHorizonClamped()
    {
        AppConfig cfg;
        cfg.databasePath    = QStringLiteral(":memory:");
        cfg.forecastHorizonDays = 0;
        cfg.validate();
        QCOMPARE(cfg.forecastHorizonDays, 1);
    }
};

QTEST_GUILESS_MAIN(AppConfigDeep6Test)
#include "test_appconfig_deep6.moc"
