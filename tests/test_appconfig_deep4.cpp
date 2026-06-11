// test_appconfig_deep4.cpp — Iteration 15 deep audit: AppConfig validate() clamping,
// ensemble weight bounds, and spatial grid size limits.
#include <QTest>
#include <QFile>
#include <QSettings>
#include <QTemporaryFile>
#include <cmath>

#include "core/AppConfig.h"

class TestAppConfigDeep4 : public QObject
{
    Q_OBJECT

private:
    static QString tempIniPath()
    {
        QTemporaryFile f(QStringLiteral("sentinel_cfg_deep4_XXXXXX.ini"));
        f.setAutoRemove(false);
        if (!f.open())
            return {};
        const QString path = f.fileName();
        f.close();
        return path;
    }

private slots:
    void testValidateClampsLatLonOutOfRange();
    void testValidateClampsGpHyperparameters();
    void testValidateClampsGridSizesBelowMinimum();
    void testValidateClampsGridSizesAboveMaximum();
    void testValidateClampsEnsembleWeightsToUnitInterval();
    void testValidateNormalizesEnsembleWeights();
    void testLoadFromClampsGridSizesInSettings();
    void testValidateClampsNegativeRadiusAndEmptyDbPath();
};

void TestAppConfigDeep4::testValidateClampsLatLonOutOfRange()
{
    AppConfig cfg;
    cfg.databasePath = QStringLiteral(":memory:");
    cfg.defaultLat   = 120.0;
    cfg.defaultLon   = -250.0;

    const bool ok = cfg.validate();
    QVERIFY(!ok);
    QCOMPARE(cfg.defaultLat, 90.0);
    QCOMPARE(cfg.defaultLon, -180.0);
}

void TestAppConfigDeep4::testValidateClampsGpHyperparameters()
{
    AppConfig cfg;
    cfg.databasePath  = QStringLiteral(":memory:");
    cfg.gpSigma2      = -5.0;
    cfg.gpLengthscale = 0.0;
    cfg.gpNoiseSigma2 = 99.0;
    cfg.rossmoF       = 10.0;
    cfg.rossmoG       = -1.0;

    const bool ok = cfg.validate();
    QVERIFY(!ok);
    QVERIFY(cfg.gpSigma2      >= 0.001 && cfg.gpSigma2      <= 100.0);
    QVERIFY(cfg.gpLengthscale >= 0.001 && cfg.gpLengthscale <= 10.0);
    QVERIFY(cfg.gpNoiseSigma2 >= 0.001 && cfg.gpNoiseSigma2 <= 10.0);
    QVERIFY(cfg.rossmoF       >= 0.001 && cfg.rossmoF       <= 3.0);
    QVERIFY(cfg.rossmoG       >= 0.001 && cfg.rossmoG       <= 3.0);
}

void TestAppConfigDeep4::testValidateClampsGridSizesBelowMinimum()
{
    AppConfig cfg;
    cfg.databasePath   = QStringLiteral(":memory:");
    cfg.poissonGridSize = 3;
    cfg.kdeGridSize     = 0;

    const bool ok = cfg.validate();
    QVERIFY(!ok);
    QCOMPARE(cfg.poissonGridSize, 10);
    QCOMPARE(cfg.kdeGridSize,     10);
}

void TestAppConfigDeep4::testValidateClampsGridSizesAboveMaximum()
{
    AppConfig cfg;
    cfg.databasePath   = QStringLiteral(":memory:");
    cfg.poissonGridSize = 999;
    cfg.kdeGridSize     = 750;

    const bool ok = cfg.validate();
    QVERIFY(!ok);
    QCOMPARE(cfg.poissonGridSize, 500);
    QCOMPARE(cfg.kdeGridSize,     500);
}

void TestAppConfigDeep4::testValidateClampsEnsembleWeightsToUnitInterval()
{
    AppConfig cfg;
    cfg.databasePath          = QStringLiteral(":memory:");
    cfg.ensemblePoissonWeight = -0.25;
    cfg.ensembleHawkesWeight  = 1.75;

    const bool ok = cfg.validate();
    QVERIFY(!ok);
    QCOMPARE(cfg.ensemblePoissonWeight, 0.0);
    QCOMPARE(cfg.ensembleHawkesWeight,  1.0);
}

void TestAppConfigDeep4::testValidateNormalizesEnsembleWeights()
{
    AppConfig cfg;
    cfg.databasePath          = QStringLiteral(":memory:");
    cfg.ensemblePoissonWeight = 0.8;
    cfg.ensembleHawkesWeight  = 0.8;

    const bool ok = cfg.validate();
    QVERIFY(!ok);
    QVERIFY(std::abs(cfg.ensemblePoissonWeight - 0.5) < 1e-9);
    QVERIFY(std::abs(cfg.ensembleHawkesWeight  - 0.5) < 1e-9);
    QVERIFY(std::abs(cfg.ensemblePoissonWeight + cfg.ensembleHawkesWeight - 1.0) < 1e-9);
}

void TestAppConfigDeep4::testLoadFromClampsGridSizesInSettings()
{
    const QString path = tempIniPath();
    QVERIFY2(!path.isEmpty(), "Could not create temp INI file");

    {
        QSettings s(path, QSettings::IniFormat);
        s.setValue(QStringLiteral("model/poisson_grid_size"), 5);
        s.setValue(QStringLiteral("model/kde_grid_size"), 900);
        s.sync();
    }

    const AppConfig cfg = AppConfig::loadFrom(path);
    QCOMPARE(cfg.poissonGridSize, 10);
    QCOMPARE(cfg.kdeGridSize,     500);

    QFile::remove(path);
}

void TestAppConfigDeep4::testValidateClampsNegativeRadiusAndEmptyDbPath()
{
    AppConfig cfg;
    cfg.databasePath = QStringLiteral("   ");
    cfg.defaultRadius = -3.0;
    cfg.refreshIntervalSeconds = 2;
    cfg.maxLeadCount = 0;
    cfg.forecastHorizonDays = 99;

    const bool ok = cfg.validate();
    QVERIFY(!ok);
    QCOMPARE(cfg.databasePath, QStringLiteral(":memory:"));
    QCOMPARE(cfg.defaultRadius, 5.0);
    QCOMPARE(cfg.refreshIntervalSeconds, 10);
    QCOMPARE(cfg.maxLeadCount, 1);
    QCOMPARE(cfg.forecastHorizonDays, 30);
}

QTEST_GUILESS_MAIN(TestAppConfigDeep4)
#include "test_appconfig_deep4.moc"
