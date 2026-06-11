// test_appconfig_deep3.cpp — Iteration 12 deep audit: AppConfig JSON I/O,
// validate() threshold ordering, and factory-default sanity.
#include <QTest>
#include <QFile>
#include <QTemporaryFile>
#include <QJsonDocument>
#include <cmath>

#include "core/AppConfig.h"

class TestAppConfigDeep3 : public QObject
{
    Q_OBJECT

private:
    static QString tempJsonPath()
    {
        QTemporaryFile f(QStringLiteral("sentinel_cfg_deep3_XXXXXX.json"));
        f.setAutoRemove(false);
        if (!f.open())
            return {};
        const QString path = f.fileName();
        f.close();
        return path;
    }

    static void assertConfigEqual(const AppConfig& a, const AppConfig& b)
    {
        QCOMPARE(a.openWeatherKey,         b.openWeatherKey);
        QCOMPARE(a.socrataDomain,          b.socrataDomain);
        QCOMPARE(a.socrataToken,           b.socrataToken);
        QCOMPARE(a.defaultLat,             b.defaultLat);
        QCOMPARE(a.defaultLon,             b.defaultLon);
        QCOMPARE(a.defaultRadius,          b.defaultRadius);
        QCOMPARE(a.hawkesHistoryDays,      b.hawkesHistoryDays);
        QCOMPARE(a.seriesMinEvents,        b.seriesMinEvents);
        QCOMPARE(a.seriesEpsKm,            b.seriesEpsKm);
        QCOMPARE(a.seriesEpsDays,          b.seriesEpsDays);
        QCOMPARE(a.qualityThreshold,       b.qualityThreshold);
        QCOMPARE(a.autoRefreshEnabled,     b.autoRefreshEnabled);
        QCOMPARE(a.refreshIntervalSeconds, b.refreshIntervalSeconds);
        QCOMPARE(a.alertElevated,          b.alertElevated);
        QCOMPARE(a.alertHigh,              b.alertHigh);
        QCOMPARE(a.alertCritical,          b.alertCritical);
        QCOMPARE(a.forecastHorizonDays,    b.forecastHorizonDays);
        QCOMPARE(a.gpSigma2,               b.gpSigma2);
        QCOMPARE(a.gpLengthscale,          b.gpLengthscale);
        QCOMPARE(a.gpNoiseSigma2,          b.gpNoiseSigma2);
        QCOMPARE(a.rossmoF,                b.rossmoF);
        QCOMPARE(a.rossmoG,                b.rossmoG);
        QCOMPARE(a.ensemblePoissonWeight,  b.ensemblePoissonWeight);
        QCOMPARE(a.ensembleHawkesWeight,   b.ensembleHawkesWeight);
        QCOMPARE(a.databasePath,           b.databasePath);
        QCOMPARE(a.theme,                  b.theme);
        QCOMPARE(a.mapZoomLevel,           b.mapZoomLevel);
        QCOMPARE(a.exportDirectory,        b.exportDirectory);
        QCOMPARE(a.maxLeadCount,           b.maxLeadCount);
        QCOMPARE(a.poissonGridSize,        b.poissonGridSize);
        QCOMPARE(a.kdeGridSize,            b.kdeGridSize);
    }

private slots:
    void testJsonFileRoundTrip();
    void testJsonObjectRoundTripPreservesAllFields();
    void testValidateAutoCorrectsInvertedThresholds();
    void testValidateAutoCorrectsEqualThresholds();
    void testValidateResetsImpossibleHighThresholdChain();
    void testDefaultValuesAreSensible();
    void testDefaultConfigPassesValidate();
    void testLoadFromFileRejectsInvalidJson();
};

void TestAppConfigDeep3::testJsonFileRoundTrip()
{
    const QString path = tempJsonPath();
    QVERIFY2(!path.isEmpty(), "Could not create temp JSON file");

    AppConfig orig;
    orig.openWeatherKey         = QStringLiteral("owm_test_key");
    orig.socrataDomain          = QStringLiteral("data.testcity.gov");
    orig.socrataToken           = QStringLiteral("soc_token_99");
    orig.defaultLat             = 40.7128;
    orig.defaultLon             = -74.0060;
    orig.defaultRadius          = 12.5;
    orig.hawkesHistoryDays      = 90;
    orig.seriesMinEvents        = 5;
    orig.seriesEpsKm            = 0.45;
    orig.seriesEpsDays          = 21.0;
    orig.qualityThreshold       = 0.35;
    orig.autoRefreshEnabled     = true;
    orig.refreshIntervalSeconds = 900;
    orig.alertElevated          = 0.20;
    orig.alertHigh              = 0.45;
    orig.alertCritical          = 0.70;
    orig.forecastHorizonDays    = 14;
    orig.gpSigma2               = 2.0;
    orig.gpLengthscale          = 1.2;
    orig.gpNoiseSigma2          = 0.25;
    orig.rossmoF                = 1.4;
    orig.rossmoG                = 1.6;
    orig.ensemblePoissonWeight  = 0.7;
    orig.ensembleHawkesWeight   = 0.3;
    orig.databasePath           = QStringLiteral("/data/sentinel_test.db");
    orig.theme                  = QStringLiteral("light");
    orig.mapZoomLevel           = 11.0;
    orig.exportDirectory        = QStringLiteral("/exports/sentinel");
    orig.maxLeadCount           = 25;
    orig.poissonGridSize        = 100;
    orig.kdeGridSize            = 120;

    QVERIFY2(orig.saveToFile(path), "saveToFile should succeed");

    AppConfig loaded;
    QVERIFY2(loaded.loadFromFile(path), "loadFromFile should succeed");
    assertConfigEqual(orig, loaded);

    QFile::remove(path);
}

void TestAppConfigDeep3::testJsonObjectRoundTripPreservesAllFields()
{
    AppConfig orig;
    orig.socrataDomain       = QStringLiteral("data.cityofchicago.org");
    orig.alertElevated       = 0.30;
    orig.alertHigh           = 0.50;
    orig.alertCritical       = 0.75;
    orig.databasePath        = QStringLiteral(":memory:");
    orig.poissonGridSize     = 50;
    orig.kdeGridSize         = 50;

    const QJsonObject json = orig.toJson();
    const AppConfig restored = AppConfig::fromJson(json);

    // Every serialised key must round-trip
    QCOMPARE(restored.toJson().keys().size(), json.keys().size());
    assertConfigEqual(orig, restored);
}

void TestAppConfigDeep3::testValidateAutoCorrectsInvertedThresholds()
{
    AppConfig cfg;
    cfg.databasePath  = QStringLiteral(":memory:");
    cfg.alertElevated = 0.80;
    cfg.alertHigh     = 0.40;
    cfg.alertCritical = 0.20;

    const bool ok = cfg.validate();
    QVERIFY(!ok);
    QVERIFY(cfg.alertElevated < cfg.alertHigh);
    QVERIFY(cfg.alertHigh     < cfg.alertCritical);
    QVERIFY(cfg.alertElevated >= 0.0 && cfg.alertElevated <= 1.0);
    QVERIFY(cfg.alertHigh     >= 0.0 && cfg.alertHigh     <= 1.0);
    QVERIFY(cfg.alertCritical >= 0.0 && cfg.alertCritical <= 1.0);
}

void TestAppConfigDeep3::testValidateAutoCorrectsEqualThresholds()
{
    AppConfig cfg;
    cfg.databasePath  = QStringLiteral(":memory:");
    cfg.alertElevated = 0.60;
    cfg.alertHigh     = 0.60;
    cfg.alertCritical = 0.60;

    const bool ok = cfg.validate();
    QVERIFY(!ok);
    QVERIFY(cfg.alertElevated < cfg.alertHigh);
    QVERIFY(cfg.alertHigh     < cfg.alertCritical);
}

void TestAppConfigDeep3::testValidateResetsImpossibleHighThresholdChain()
{
    AppConfig cfg;
    cfg.databasePath  = QStringLiteral(":memory:");
    cfg.alertElevated = 0.95;
    cfg.alertHigh     = 0.50;
    cfg.alertCritical = 0.30;

    const bool ok = cfg.validate();
    QVERIFY(!ok);
    // Chain fixup pushes values to 1.0 then resets to safe defaults
    QCOMPARE(cfg.alertElevated, 0.30);
    QCOMPARE(cfg.alertHigh,     0.50);
    QCOMPARE(cfg.alertCritical, 0.75);
}

void TestAppConfigDeep3::testDefaultValuesAreSensible()
{
    const AppConfig cfg;

    // Geographic defaults (London)
    QVERIFY(cfg.defaultLat  >= -90.0  && cfg.defaultLat  <= 90.0);
    QVERIFY(cfg.defaultLon  >= -180.0 && cfg.defaultLon  <= 180.0);
    QVERIFY(cfg.defaultRadius > 0.0);

    // Pipeline / model defaults
    QVERIFY(cfg.hawkesHistoryDays >= 7);
    QVERIFY(cfg.seriesMinEvents   >= 2);
    QVERIFY(cfg.seriesEpsKm       > 0.0);
    QVERIFY(cfg.seriesEpsDays     > 0.0);
    QVERIFY(cfg.qualityThreshold  >= 0.0 && cfg.qualityThreshold <= 1.0);

    // Alert thresholds strictly ordered
    QVERIFY(cfg.alertElevated < cfg.alertHigh);
    QVERIFY(cfg.alertHigh     < cfg.alertCritical);

    // GP / Rossmo hyperparameters positive and in range
    QVERIFY(cfg.gpSigma2      > 0.0);
    QVERIFY(cfg.gpLengthscale > 0.0);
    QVERIFY(cfg.gpNoiseSigma2 > 0.0);
    QVERIFY(cfg.rossmoF       > 0.0);
    QVERIFY(cfg.rossmoG       > 0.0);

    // Ensemble weights in [0, 1]
    QVERIFY(cfg.ensemblePoissonWeight >= 0.0 && cfg.ensemblePoissonWeight <= 1.0);
    QVERIFY(cfg.ensembleHawkesWeight  >= 0.0 && cfg.ensembleHawkesWeight  <= 1.0);

    // Grid sizes within documented bounds
    QVERIFY(cfg.poissonGridSize >= 10 && cfg.poissonGridSize <= 500);
    QVERIFY(cfg.kdeGridSize     >= 10 && cfg.kdeGridSize     <= 500);

    // UI / storage defaults
    QVERIFY(cfg.theme == QStringLiteral("dark") || cfg.theme == QStringLiteral("light"));
    QVERIFY(cfg.mapZoomLevel >= 1.0 && cfg.mapZoomLevel <= 20.0);
    QVERIFY(cfg.maxLeadCount >= 1);
    QVERIFY(!cfg.databasePath.isEmpty());
}

void TestAppConfigDeep3::testDefaultConfigPassesValidate()
{
    AppConfig cfg;
    cfg.databasePath = QStringLiteral(":memory:");
    QVERIFY(cfg.validate());
}

void TestAppConfigDeep3::testLoadFromFileRejectsInvalidJson()
{
    const QString path = tempJsonPath();
    QVERIFY2(!path.isEmpty(), "Could not create temp JSON file");

    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("[not an object]");
    f.close();

    AppConfig cfg;
    QVERIFY(!cfg.loadFromFile(path));

    QFile::remove(path);
}

QTEST_GUILESS_MAIN(TestAppConfigDeep3)
#include "test_appconfig_deep3.moc"
