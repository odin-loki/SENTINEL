// test_appconfig_roundtrip.cpp — Round-trip persistence tests for AppConfig
// using temporary INI files (saveTo / loadFrom). 10 test cases covering
// defaults, numeric precision, clamping, unknown keys, and multiple cycles.

#include <QTest>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QSettings>
#include <QTemporaryFile>
#include <cmath>

#include "core/AppConfig.h"

class TestAppConfigRoundtrip : public QObject
{
    Q_OBJECT

    // Create a temporary INI file on disk; caller must QFile::remove() when done.
    // Uses setAutoRemove(false) so the file persists after the pointer is deleted.
    static QString makeTempIni()
    {
        auto* f = new QTemporaryFile(QStringLiteral("sentinel_cfg_XXXXXX.ini"));
        f->setAutoRemove(false);
        if (!f->open()) { delete f; return {}; }
        const QString path = f->fileName();
        f->close();
        delete f;  // file stays on disk because autoRemove == false
        return path;
    }

private slots:

    // ── 1. Default values are sensible ────────────────────────────────────────
    void testDefaultValues()
    {
        AppConfig cfg;

        QVERIFY2(cfg.defaultLat  >= -90.0  && cfg.defaultLat  <= 90.0,  "defaultLat out of range");
        QVERIFY2(cfg.defaultLon  >= -180.0 && cfg.defaultLon  <= 180.0, "defaultLon out of range");
        QCOMPARE(cfg.theme, QString("dark"));
        QVERIFY2(cfg.mapZoomLevel >= 1.0 && cfg.mapZoomLevel <= 20.0,   "mapZoomLevel out of range");
        QVERIFY2(cfg.alertElevated >= 0.0 && cfg.alertElevated <= 1.0,  "alertElevated out of range");
        QVERIFY2(cfg.maxLeadCount >= 1 && cfg.maxLeadCount <= 10000,    "maxLeadCount out of range");
    }

    // ── 2. saveTo / loadFrom roundtrip preserves all numeric fields ───────────
    void testNumericFieldsRoundtrip()
    {
        const QString path = makeTempIni();
        QVERIFY(!path.isEmpty());

        AppConfig orig;
        orig.defaultLat            = 40.7128;
        orig.defaultLon            = -74.0060;
        orig.defaultRadius         = 8.5;
        orig.seriesEpsKm           = 0.45;
        orig.seriesEpsDays         = 10.0;
        orig.alertElevated         = 0.25;
        orig.alertHigh             = 0.55;
        orig.alertCritical         = 0.80;
        orig.mapZoomLevel          = 12.0;
        orig.maxLeadCount          = 100;
        orig.forecastHorizonDays   = 14;
        orig.gpSigma2              = 2.5;
        orig.gpLengthscale         = 0.8;
        orig.gpNoiseSigma2         = 0.05;
        orig.ensemblePoissonWeight = 0.7;
        orig.ensembleHawkesWeight  = 0.3;
        orig.hawkesHistoryDays     = 180;
        orig.qualityThreshold      = 0.45;
        orig.refreshIntervalSeconds = 1800;
        orig.saveTo(path);

        const AppConfig loaded = AppConfig::loadFrom(path);

        // Use fuzzy compare for doubles (QSettings may lose precision)
        auto near = [](double a, double b){ return std::abs(a - b) < 1e-4; };
        QVERIFY2(near(loaded.defaultLat,            orig.defaultLat),            "defaultLat mismatch");
        QVERIFY2(near(loaded.defaultLon,            orig.defaultLon),            "defaultLon mismatch");
        QVERIFY2(near(loaded.defaultRadius,         orig.defaultRadius),         "defaultRadius mismatch");
        QVERIFY2(near(loaded.seriesEpsKm,           orig.seriesEpsKm),           "seriesEpsKm mismatch");
        QVERIFY2(near(loaded.seriesEpsDays,         orig.seriesEpsDays),         "seriesEpsDays mismatch");
        QVERIFY2(near(loaded.alertElevated,         orig.alertElevated),         "alertElevated mismatch");
        QVERIFY2(near(loaded.alertHigh,             orig.alertHigh),             "alertHigh mismatch");
        QVERIFY2(near(loaded.alertCritical,         orig.alertCritical),         "alertCritical mismatch");
        QVERIFY2(near(loaded.mapZoomLevel,          orig.mapZoomLevel),          "mapZoomLevel mismatch");
        QCOMPARE(loaded.maxLeadCount,           orig.maxLeadCount);
        QCOMPARE(loaded.forecastHorizonDays,    orig.forecastHorizonDays);
        QVERIFY2(near(loaded.gpSigma2,              orig.gpSigma2),              "gpSigma2 mismatch");
        QVERIFY2(near(loaded.gpLengthscale,         orig.gpLengthscale),         "gpLengthscale mismatch");
        QVERIFY2(near(loaded.gpNoiseSigma2,         orig.gpNoiseSigma2),         "gpNoiseSigma2 mismatch");
        QVERIFY2(near(loaded.ensemblePoissonWeight, orig.ensemblePoissonWeight), "ensemblePoissonWeight mismatch");
        QVERIFY2(near(loaded.ensembleHawkesWeight,  orig.ensembleHawkesWeight),  "ensembleHawkesWeight mismatch");
        QCOMPARE(loaded.hawkesHistoryDays,      orig.hawkesHistoryDays);
        QVERIFY2(near(loaded.qualityThreshold,      orig.qualityThreshold),      "qualityThreshold mismatch");
        QCOMPARE(loaded.refreshIntervalSeconds, orig.refreshIntervalSeconds);

        QFile::remove(path);
    }

    // ── 3. Theme save / load roundtrip ────────────────────────────────────────
    void testThemeRoundtrip()
    {
        const QString path = makeTempIni();
        QVERIFY(!path.isEmpty());

        AppConfig cfg;
        cfg.theme = "light";
        cfg.saveTo(path);
        QCOMPARE(AppConfig::loadFrom(path).theme, QString("light"));

        cfg.theme = "dark";
        cfg.saveTo(path);
        QCOMPARE(AppConfig::loadFrom(path).theme, QString("dark"));

        QFile::remove(path);
    }

    // ── 4. mapZoomLevel clamped to [1, 20] on load ────────────────────────────
    void testMapZoomLevelClamped()
    {
        const QString path = makeTempIni();
        QVERIFY(!path.isEmpty());

        AppConfig cfg;

        cfg.mapZoomLevel = 25.0;    // above max
        cfg.saveTo(path);
        QVERIFY2(AppConfig::loadFrom(path).mapZoomLevel <= 20.0, "mapZoomLevel should clamp to 20.0");

        cfg.mapZoomLevel = -3.0;    // below min
        cfg.saveTo(path);
        QVERIFY2(AppConfig::loadFrom(path).mapZoomLevel >= 1.0, "mapZoomLevel should clamp to 1.0");

        QFile::remove(path);
    }

    // ── 5. alertElevated clamped to [0, 1] on load ────────────────────────────
    void testAlertElevatedClamped()
    {
        const QString path = makeTempIni();
        QVERIFY(!path.isEmpty());

        AppConfig cfg;

        cfg.alertElevated = 1.5;    // above 1.0
        cfg.saveTo(path);
        QVERIFY2(AppConfig::loadFrom(path).alertElevated <= 1.0, "alertElevated should clamp to 1.0");

        cfg.alertElevated = -0.2;   // below 0.0
        cfg.saveTo(path);
        QVERIFY2(AppConfig::loadFrom(path).alertElevated >= 0.0, "alertElevated should clamp to 0.0");

        QFile::remove(path);
    }

    // ── 6. maxLeadCount clamped to [1, 10000] ─────────────────────────────────
    void testMaxLeadCountClamped()
    {
        const QString path = makeTempIni();
        QVERIFY(!path.isEmpty());

        AppConfig cfg;

        cfg.maxLeadCount = 99999;   // above max
        cfg.saveTo(path);
        QCOMPARE(AppConfig::loadFrom(path).maxLeadCount, 10000);

        cfg.maxLeadCount = 0;       // below min
        cfg.saveTo(path);
        QCOMPARE(AppConfig::loadFrom(path).maxLeadCount, 1);

        QFile::remove(path);
    }

    // ── 7. Unknown INI keys don't crash loadFrom ──────────────────────────────
    void testUnknownIniKeysNoCrash()
    {
        const QString path = makeTempIni();
        QVERIFY(!path.isEmpty());

        {
            QSettings s(path, QSettings::IniFormat);
            s.setValue("map/default_lat",              48.8566);   // Paris
            s.setValue("map/default_lon",              2.3522);
            s.setValue("totally_unknown_section/key1", "nonsense");
            s.setValue("another_bogus/key2",           9999);
            s.setValue("ui/theme",                     "light");
            s.sync();
        }

        const AppConfig loaded = AppConfig::loadFrom(path);
        QVERIFY2(std::abs(loaded.defaultLat - 48.8566) < 1e-6,
                 "Known key defaultLat not loaded correctly");
        QCOMPARE(loaded.theme, QString("light"));

        QFile::remove(path);
    }

    // ── 8. Multiple save/load cycles maintain consistency ─────────────────────
    void testMultipleSaveLoadCycles()
    {
        const QString path = makeTempIni();
        QVERIFY(!path.isEmpty());

        AppConfig cfg;
        cfg.defaultLat   = 48.8566;  // Paris
        cfg.defaultLon   = 2.3522;
        cfg.theme        = "light";
        cfg.maxLeadCount = 200;
        cfg.seriesEpsKm  = 0.6;

        for (int cycle = 0; cycle < 3; ++cycle) {
            cfg.saveTo(path);
            cfg = AppConfig::loadFrom(path);
        }

        QVERIFY2(std::abs(cfg.defaultLat - 48.8566) < 1e-6, "defaultLat drifted");
        QVERIFY2(std::abs(cfg.defaultLon - 2.3522)  < 1e-6, "defaultLon drifted");
        QCOMPARE(cfg.theme,        QString("light"));
        QCOMPARE(cfg.maxLeadCount, 200);
        QVERIFY2(std::abs(cfg.seriesEpsKm - 0.6) < 1e-9,    "seriesEpsKm drifted");

        QFile::remove(path);
    }

    // ── 9. databasePath saves / loads correctly ────────────────────────────────
    void testDatabasePathRoundtrip()
    {
        const QString path = makeTempIni();
        QVERIFY(!path.isEmpty());

        const QString dbPath = QDir::tempPath() + "/sentinel_unit_test.db";

        AppConfig cfg;
        cfg.databasePath = dbPath;
        cfg.saveTo(path);

        const AppConfig loaded = AppConfig::loadFrom(path);
        QCOMPARE(loaded.databasePath, dbPath);

        QFile::remove(path);
    }

    // ── 10. ensemblePoissonWeight + hawkesWeight save / load correctly ─────────
    void testEnsembleWeightsRoundtrip()
    {
        const QString path = makeTempIni();
        QVERIFY(!path.isEmpty());

        AppConfig cfg;
        cfg.ensemblePoissonWeight = 0.35;
        cfg.ensembleHawkesWeight  = 0.65;
        cfg.saveTo(path);

        const AppConfig loaded = AppConfig::loadFrom(path);
        QCOMPARE(loaded.ensemblePoissonWeight, 0.35);
        QCOMPARE(loaded.ensembleHawkesWeight,  0.65);

        QFile::remove(path);
    }
};

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    TestAppConfigRoundtrip tests;
    return QTest::qExec(&tests, argc, argv);
}
#include "test_appconfig_roundtrip.moc"
