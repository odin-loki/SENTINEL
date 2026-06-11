#include <QTest>
#include <QCoreApplication>
#include <QJsonObject>
#include <QTemporaryFile>
#include <cmath>

#include "core/AppConfig.h"

class TestAppConfigDeep2 : public QObject
{
    Q_OBJECT

private slots:
    void testValidateValidConfig();
    void testValidateNegativeGpSigma2();
    void testValidateInvertedAlertThresholds();
    void testValidateAllThresholdsEqual();
    void testJsonRoundTrip();
    void testFromJsonUnknownFieldsNoCrash();
    void testDefaultConfigIsValid();
    void testValidateFixesNegativeRadius();
    void testValidateAlertElevatedEqualsHigh();
};

void TestAppConfigDeep2::testValidateValidConfig()
{
    AppConfig cfg;
    cfg.alertElevated    = 0.30;
    cfg.alertHigh        = 0.50;
    cfg.alertCritical    = 0.75;
    cfg.gpSigma2         = 1.0;
    cfg.gpLengthscale    = 0.5;
    cfg.gpNoiseSigma2    = 0.1;
    cfg.defaultLat       = 51.5074;
    cfg.defaultLon       = -0.1278;
    cfg.defaultRadius    = 5.0;
    cfg.databasePath     = QStringLiteral(":memory:");
    QVERIFY(cfg.validate());
}

void TestAppConfigDeep2::testValidateNegativeGpSigma2()
{
    AppConfig cfg;
    cfg.databasePath = QStringLiteral(":memory:");
    cfg.gpSigma2     = -1.0;
    const bool result = cfg.validate();
    QVERIFY(!result);
    QVERIFY(cfg.gpSigma2 > 0.0);
}

void TestAppConfigDeep2::testValidateInvertedAlertThresholds()
{
    AppConfig cfg;
    cfg.databasePath  = QStringLiteral(":memory:");
    cfg.alertElevated = 0.75;
    cfg.alertHigh     = 0.50;
    cfg.alertCritical = 0.30;
    const bool result = cfg.validate();
    QVERIFY(!result);
    QVERIFY(cfg.alertElevated < cfg.alertHigh);
    QVERIFY(cfg.alertHigh     < cfg.alertCritical);
}

void TestAppConfigDeep2::testValidateAllThresholdsEqual()
{
    AppConfig cfg;
    cfg.databasePath  = QStringLiteral(":memory:");
    cfg.alertElevated = 0.95;
    cfg.alertHigh     = 0.95;
    cfg.alertCritical = 0.95;
    const bool result = cfg.validate();
    QVERIFY(!result);
    QVERIFY(cfg.alertElevated < cfg.alertHigh);
    QVERIFY(cfg.alertHigh     < cfg.alertCritical);
}

void TestAppConfigDeep2::testJsonRoundTrip()
{
    AppConfig orig;
    orig.openWeatherKey         = QStringLiteral("key_abc");
    orig.socrataDomain          = QStringLiteral("data.example.gov");
    orig.socrataToken           = QStringLiteral("tok123");
    orig.defaultLat             = 48.8566;
    orig.defaultLon             = 2.3522;
    orig.defaultRadius          = 8.0;
    orig.hawkesHistoryDays      = 180;
    orig.seriesMinEvents        = 4;
    orig.seriesEpsKm            = 0.5;
    orig.seriesEpsDays          = 7.0;
    orig.qualityThreshold       = 0.4;
    orig.autoRefreshEnabled     = true;
    orig.refreshIntervalSeconds = 1800;
    orig.alertElevated          = 0.25;
    orig.alertHigh              = 0.55;
    orig.alertCritical          = 0.80;
    orig.forecastHorizonDays    = 14;
    orig.gpSigma2               = 2.5;
    orig.gpLengthscale          = 0.8;
    orig.gpNoiseSigma2          = 0.2;
    orig.rossmoF                = 1.5;
    orig.rossmoG                = 1.8;
    orig.ensemblePoissonWeight  = 0.6;
    orig.ensembleHawkesWeight   = 0.4;
    orig.databasePath           = QStringLiteral("/tmp/test.db");
    orig.theme                  = QStringLiteral("light");
    orig.mapZoomLevel           = 12.0;
    orig.exportDirectory        = QStringLiteral("/tmp/exports");
    orig.maxLeadCount           = 100;
    orig.poissonGridSize        = 75;
    orig.kdeGridSize            = 80;

    const QJsonObject json   = orig.toJson();
    const AppConfig loaded   = AppConfig::fromJson(json);

    QCOMPARE(loaded.openWeatherKey,         orig.openWeatherKey);
    QCOMPARE(loaded.socrataDomain,          orig.socrataDomain);
    QCOMPARE(loaded.socrataToken,           orig.socrataToken);
    QCOMPARE(loaded.defaultLat,             orig.defaultLat);
    QCOMPARE(loaded.defaultLon,             orig.defaultLon);
    QCOMPARE(loaded.defaultRadius,          orig.defaultRadius);
    QCOMPARE(loaded.hawkesHistoryDays,      orig.hawkesHistoryDays);
    QCOMPARE(loaded.seriesMinEvents,        orig.seriesMinEvents);
    QCOMPARE(loaded.seriesEpsKm,            orig.seriesEpsKm);
    QCOMPARE(loaded.seriesEpsDays,          orig.seriesEpsDays);
    QCOMPARE(loaded.qualityThreshold,       orig.qualityThreshold);
    QCOMPARE(loaded.autoRefreshEnabled,     orig.autoRefreshEnabled);
    QCOMPARE(loaded.refreshIntervalSeconds, orig.refreshIntervalSeconds);
    QCOMPARE(loaded.alertElevated,          orig.alertElevated);
    QCOMPARE(loaded.alertHigh,              orig.alertHigh);
    QCOMPARE(loaded.alertCritical,          orig.alertCritical);
    QCOMPARE(loaded.forecastHorizonDays,    orig.forecastHorizonDays);
    QCOMPARE(loaded.gpSigma2,              orig.gpSigma2);
    QCOMPARE(loaded.gpLengthscale,         orig.gpLengthscale);
    QCOMPARE(loaded.gpNoiseSigma2,         orig.gpNoiseSigma2);
    QCOMPARE(loaded.rossmoF,               orig.rossmoF);
    QCOMPARE(loaded.rossmoG,               orig.rossmoG);
    QCOMPARE(loaded.ensemblePoissonWeight, orig.ensemblePoissonWeight);
    QCOMPARE(loaded.ensembleHawkesWeight,  orig.ensembleHawkesWeight);
    QCOMPARE(loaded.databasePath,          orig.databasePath);
    QCOMPARE(loaded.theme,                 orig.theme);
    QCOMPARE(loaded.mapZoomLevel,          orig.mapZoomLevel);
    QCOMPARE(loaded.exportDirectory,       orig.exportDirectory);
    QCOMPARE(loaded.maxLeadCount,          orig.maxLeadCount);
    QCOMPARE(loaded.poissonGridSize,       orig.poissonGridSize);
    QCOMPARE(loaded.kdeGridSize,           orig.kdeGridSize);
}

void TestAppConfigDeep2::testFromJsonUnknownFieldsNoCrash()
{
    QJsonObject obj;
    obj[QStringLiteral("unknown_field_xyz")]     = QStringLiteral("some_value");
    obj[QStringLiteral("another_unknown_field")] = 42;
    obj[QStringLiteral("default_lat")]           = 51.5;
    obj[QStringLiteral("default_lon")]           = -0.12;

    AppConfig cfg = AppConfig::fromJson(obj);
    QCOMPARE(cfg.defaultLat, 51.5);
    QCOMPARE(cfg.defaultLon, -0.12);
}

void TestAppConfigDeep2::testDefaultConfigIsValid()
{
    AppConfig cfg;
    cfg.databasePath = QStringLiteral(":memory:");
    QVERIFY(cfg.validate());
}

void TestAppConfigDeep2::testValidateFixesNegativeRadius()
{
    AppConfig cfg;
    cfg.databasePath  = QStringLiteral(":memory:");
    cfg.defaultRadius = -5.0;
    cfg.seriesEpsKm   = -1.0;
    cfg.seriesEpsDays = 0.0;
    const bool result = cfg.validate();
    QVERIFY(!result);
    QVERIFY(cfg.defaultRadius > 0.0);
    QVERIFY(cfg.seriesEpsKm   > 0.0);
    QVERIFY(cfg.seriesEpsDays > 0.0);
}

void TestAppConfigDeep2::testValidateAlertElevatedEqualsHigh()
{
    AppConfig cfg;
    cfg.databasePath  = QStringLiteral(":memory:");
    cfg.alertElevated = 0.50;
    cfg.alertHigh     = 0.50;
    cfg.alertCritical = 0.75;
    const bool result = cfg.validate();
    QVERIFY(!result);
    QVERIFY(cfg.alertElevated < cfg.alertHigh);
    QVERIFY(cfg.alertHigh     < cfg.alertCritical);
}

QTEST_GUILESS_MAIN(TestAppConfigDeep2)
#include "test_appconfig_deep2.moc"
