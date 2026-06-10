// test_app_config.cpp — Validation tests for AppConfig struct.
// Covers: defaults, round-trip persistence, threshold ordering,
// hyperparameter ranges, and QSettings interaction.

#include <QTest>
#include <QCoreApplication>
#include <QSettings>
#include <cmath>

#include "core/AppConfig.h"

class TestAppConfig : public QObject
{
    Q_OBJECT

    // Wipe the registry/INI entries used by AppConfig so tests don't interfere.
    static void clearSettings()
    {
        QSettings s("SENTINEL", "Sentinel");
        s.clear();
        s.sync();
    }

private slots:
    void testDefaultValuesValid();
    void testLoadSaveRoundtrip();
    void testAlertThresholdsOrdered();
    void testGPHyperparametersPositive();
    void testEnsembleWeightsRange();
    void testEnsembleWeightsSum();
    void testForecastHorizonPositive();
    void testApiTimeoutPositive();
    void testAutoRefreshIntervalPositive();
    void testSaveCreatesFile();
    void testLoadFromFreshSettings();
    void testModifiedValuePersistsAfterReload();
};

// ── 1: Default-constructed config has sensible positive values ─────────────

void TestAppConfig::testDefaultValuesValid()
{
    AppConfig cfg;

    QVERIFY(cfg.forecastHorizonDays      > 0);
    QVERIFY(cfg.hawkesHistoryDays        > 0);
    QVERIFY(cfg.defaultRadius            > 0.0);
    QVERIFY(cfg.refreshIntervalSeconds   > 0);
    QVERIFY(cfg.seriesMinEvents          > 0);
    QVERIFY(cfg.gpSigma2                 > 0.0);
    QVERIFY(cfg.gpLengthscale            > 0.0);
    QVERIFY(cfg.gpNoiseSigma2            > 0.0);
    QVERIFY(cfg.qualityThreshold         >= 0.0);
    QVERIFY(cfg.qualityThreshold         <= 1.0);
}

// ── 2: save() / load() preserves every field ──────────────────────────────

void TestAppConfig::testLoadSaveRoundtrip()
{
    clearSettings();

    AppConfig orig;
    orig.openWeatherKey         = "weather_abc";
    orig.socrataDomain          = "data.test.gov";
    orig.socrataToken           = "token_xyz";
    orig.defaultLat             = 40.7128;
    orig.defaultLon             = -74.0060;
    orig.defaultRadius          = 10.5;
    orig.hawkesHistoryDays      = 180;
    orig.seriesMinEvents        = 5;
    orig.seriesEpsKm            = 0.6;
    orig.seriesEpsDays          = 21.0;
    orig.qualityThreshold       = 0.4;
    orig.alertElevated          = 0.25;
    orig.alertHigh              = 0.55;
    orig.alertCritical          = 0.80;
    orig.forecastHorizonDays    = 14;
    orig.gpSigma2               = 2.5;
    orig.gpLengthscale          = 0.8;
    orig.gpNoiseSigma2          = 0.2;
    orig.ensemblePoissonWeight  = 0.6;
    orig.ensembleHawkesWeight   = 0.4;
    orig.autoRefreshEnabled     = true;
    orig.refreshIntervalSeconds = 1800;
    orig.databasePath           = "/tmp/test_sentinel.db";

    orig.save();

    AppConfig loaded = AppConfig::load();

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
    QCOMPARE(loaded.alertElevated,          orig.alertElevated);
    QCOMPARE(loaded.alertHigh,              orig.alertHigh);
    QCOMPARE(loaded.alertCritical,          orig.alertCritical);
    QCOMPARE(loaded.forecastHorizonDays,    orig.forecastHorizonDays);
    QCOMPARE(loaded.gpSigma2,               orig.gpSigma2);
    QCOMPARE(loaded.gpLengthscale,          orig.gpLengthscale);
    QCOMPARE(loaded.gpNoiseSigma2,          orig.gpNoiseSigma2);
    QCOMPARE(loaded.ensemblePoissonWeight,  orig.ensemblePoissonWeight);
    QCOMPARE(loaded.ensembleHawkesWeight,   orig.ensembleHawkesWeight);
    QCOMPARE(loaded.autoRefreshEnabled,     orig.autoRefreshEnabled);
    QCOMPARE(loaded.refreshIntervalSeconds, orig.refreshIntervalSeconds);
    QCOMPARE(loaded.databasePath,           orig.databasePath);

    clearSettings();
}

// ── 3: Alert thresholds are strictly ordered ──────────────────────────────

void TestAppConfig::testAlertThresholdsOrdered()
{
    AppConfig cfg;
    QVERIFY(cfg.alertElevated < cfg.alertHigh);
    QVERIFY(cfg.alertHigh    < cfg.alertCritical);
    QVERIFY(cfg.alertElevated >= 0.0);
    QVERIFY(cfg.alertCritical <= 1.0);
}

// ── 4: GP hyperparameters are all strictly positive ───────────────────────

void TestAppConfig::testGPHyperparametersPositive()
{
    AppConfig cfg;
    QVERIFY(cfg.gpSigma2      > 0.0);
    QVERIFY(cfg.gpLengthscale > 0.0);
    QVERIFY(cfg.gpNoiseSigma2 > 0.0);
}

// ── 5: Ensemble weights are in [0, 1] ─────────────────────────────────────

void TestAppConfig::testEnsembleWeightsRange()
{
    AppConfig cfg;
    QVERIFY(cfg.ensemblePoissonWeight >= 0.0);
    QVERIFY(cfg.ensemblePoissonWeight <= 1.0);
    QVERIFY(cfg.ensembleHawkesWeight  >= 0.0);
    QVERIFY(cfg.ensembleHawkesWeight  <= 1.0);
}

// ── 6: Default ensemble weights sum to 1.0 ────────────────────────────────

void TestAppConfig::testEnsembleWeightsSum()
{
    AppConfig cfg;
    const double total = cfg.ensemblePoissonWeight + cfg.ensembleHawkesWeight;
    QVERIFY(std::abs(total - 1.0) < 1e-9);
}

// ── 7: Forecast horizon is at least one day ───────────────────────────────

void TestAppConfig::testForecastHorizonPositive()
{
    AppConfig cfg;
    QVERIFY(cfg.forecastHorizonDays >= 1);
}

// ── 8: Operational timing parameters are positive ─────────────────────────
// (AppConfig has no apiTimeoutMs; nearest equivalents are the pipeline
//  history and minimum-events parameters.)

void TestAppConfig::testApiTimeoutPositive()
{
    AppConfig cfg;
    QVERIFY(cfg.hawkesHistoryDays > 0);
    QVERIFY(cfg.seriesMinEvents   > 0);
    QVERIFY(cfg.seriesEpsKm       > 0.0);
    QVERIFY(cfg.seriesEpsDays     > 0.0);
}

// ── 9: Auto-refresh interval is positive ──────────────────────────────────

void TestAppConfig::testAutoRefreshIntervalPositive()
{
    AppConfig cfg;
    QVERIFY(cfg.refreshIntervalSeconds > 0);
}

// ── 10: save() writes values that QSettings can read back directly ─────────

void TestAppConfig::testSaveCreatesFile()
{
    clearSettings();

    AppConfig cfg;
    cfg.defaultLat  = 48.8566;
    cfg.defaultLon  = 2.3522;
    cfg.gpSigma2    = 3.14;
    cfg.save();

    QSettings s("SENTINEL", "Sentinel");
    QCOMPARE(s.value("map/default_lat").toDouble(), 48.8566);
    QCOMPARE(s.value("map/default_lon").toDouble(), 2.3522);
    QCOMPARE(s.value("gp/sigma2").toDouble(),       3.14);

    clearSettings();
}

// ── 11: Loading from cleared settings yields exact defaults ───────────────

void TestAppConfig::testLoadFromFreshSettings()
{
    clearSettings();

    AppConfig loaded = AppConfig::load();

    QCOMPARE(loaded.defaultLat,          51.5074);
    QCOMPARE(loaded.defaultLon,          -0.1278);
    QCOMPARE(loaded.defaultRadius,       5.0);
    QCOMPARE(loaded.forecastHorizonDays, 7);
    QCOMPARE(loaded.alertElevated,       0.30);
    QCOMPARE(loaded.alertHigh,           0.50);
    QCOMPARE(loaded.alertCritical,       0.75);
    QCOMPARE(loaded.gpSigma2,            1.0);
    QCOMPARE(loaded.gpLengthscale,       0.5);
    QCOMPARE(loaded.gpNoiseSigma2,       0.1);
    QCOMPARE(loaded.ensemblePoissonWeight, 0.5);
    QCOMPARE(loaded.ensembleHawkesWeight,  0.5);
    QCOMPARE(loaded.refreshIntervalSeconds, 3600);

    clearSettings();
}

// ── 12: Modified value persists through a save→reload cycle ───────────────

void TestAppConfig::testModifiedValuePersistsAfterReload()
{
    clearSettings();

    AppConfig cfg;
    cfg.forecastHorizonDays   = 30;
    cfg.gpSigma2              = 5.0;
    cfg.alertCritical         = 0.90;
    cfg.ensemblePoissonWeight = 0.7;
    cfg.ensembleHawkesWeight  = 0.3;
    cfg.save();

    AppConfig reloaded = AppConfig::load();

    QCOMPARE(reloaded.forecastHorizonDays,   30);
    QCOMPARE(reloaded.gpSigma2,              5.0);
    QCOMPARE(reloaded.alertCritical,         0.90);
    QCOMPARE(reloaded.ensemblePoissonWeight, 0.7);
    QCOMPARE(reloaded.ensembleHawkesWeight,  0.3);

    clearSettings();
}

// ─────────────────────────────────────────────────────────────────────────────

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    TestAppConfig tests;
    return QTest::qExec(&tests, argc, argv);
}
#include "test_app_config.moc"
