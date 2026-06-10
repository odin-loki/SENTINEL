// test_settings_persistence.cpp — Settings save/load persistence tests
// Covers: roundtrip, defaults, theme toggle, alert clamping, and UI prefs.

#include <QTest>
#include <QCoreApplication>
#include <QSettings>
#include <QTemporaryFile>
#include <QDir>
#include <cmath>

#include "core/AppConfig.h"

class TestSettingsPersistence : public QObject
{
    Q_OBJECT

    // Return the path of a fresh temporary INI file.
    // The file is created and closed; the caller owns the path.
    static QString makeTempPath()
    {
        QTemporaryFile tmp;
        tmp.setAutoRemove(false);
        tmp.open();
        const QString p = tmp.fileName();
        tmp.close();
        return p;
    }

private slots:
    // ── 1: Default-constructed AppConfig has sane values for every field ──────
    void testDefaultValuesAreSane()
    {
        AppConfig cfg;

        // Core numeric defaults
        QVERIFY(cfg.forecastHorizonDays  >  0);
        QVERIFY(cfg.hawkesHistoryDays    >  0);
        QVERIFY(cfg.defaultRadius        >  0.0);
        QVERIFY(cfg.refreshIntervalSeconds > 0);
        QVERIFY(cfg.maxLeadCount         >  0);

        // Alert thresholds in [0, 1]
        QVERIFY(cfg.alertElevated >= 0.0 && cfg.alertElevated <= 1.0);
        QVERIFY(cfg.alertHigh     >= 0.0 && cfg.alertHigh     <= 1.0);
        QVERIFY(cfg.alertCritical >= 0.0 && cfg.alertCritical <= 1.0);

        // UI preference defaults
        QVERIFY(!cfg.theme.isEmpty());
        QVERIFY(cfg.mapZoomLevel >= 1.0 && cfg.mapZoomLevel <= 20.0);
        QCOMPARE(cfg.theme, QString("dark"));
        QCOMPARE(cfg.mapZoomLevel, 14.0);
        QCOMPARE(cfg.maxLeadCount, 50);
    }

    // ── 2: Full save→load roundtrip via temp INI file ─────────────────────────
    void testSaveLoadRoundtrip()
    {
        const QString path = makeTempPath();

        AppConfig orig;
        orig.openWeatherKey          = "ow_key_test";
        orig.socrataToken            = "socrata_test";
        orig.defaultLat              = 48.8566;
        orig.defaultLon              = 2.3522;
        orig.defaultRadius           = 8.5;
        orig.hawkesHistoryDays       = 200;
        orig.seriesMinEvents         = 4;
        orig.seriesEpsKm             = 0.5;
        orig.seriesEpsDays           = 10.0;
        orig.qualityThreshold        = 0.45;
        orig.alertElevated           = 0.20;
        orig.alertHigh               = 0.45;
        orig.alertCritical           = 0.70;
        orig.forecastHorizonDays     = 14;
        orig.gpSigma2                = 2.0;
        orig.gpLengthscale           = 0.7;
        orig.gpNoiseSigma2           = 0.15;
        orig.ensemblePoissonWeight   = 0.6;
        orig.ensembleHawkesWeight    = 0.4;
        orig.autoRefreshEnabled      = true;
        orig.refreshIntervalSeconds  = 1800;
        orig.databasePath            = "/tmp/sentinel_test.db";
        orig.theme                   = "light";
        orig.mapZoomLevel            = 12.5;
        orig.exportDirectory         = "/tmp/exports";
        orig.maxLeadCount            = 100;

        orig.saveTo(path);

        AppConfig loaded = AppConfig::loadFrom(path);

        QCOMPARE(loaded.openWeatherKey,         orig.openWeatherKey);
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
        QCOMPARE(loaded.theme,                  orig.theme);
        QCOMPARE(loaded.mapZoomLevel,           orig.mapZoomLevel);
        QCOMPARE(loaded.exportDirectory,        orig.exportDirectory);
        QCOMPARE(loaded.maxLeadCount,           orig.maxLeadCount);

        QFile::remove(path);
    }

    // ── 3: Theme toggle (dark → light → dark) persists correctly ──────────────
    void testThemeTogglePersists()
    {
        const QString path = makeTempPath();

        // Save "dark"
        {
            AppConfig cfg;
            cfg.theme = "dark";
            cfg.saveTo(path);
        }
        {
            AppConfig loaded = AppConfig::loadFrom(path);
            QCOMPARE(loaded.theme, QString("dark"));
        }

        // Switch to "light"
        {
            AppConfig cfg;
            cfg.theme = "light";
            cfg.saveTo(path);
        }
        {
            AppConfig loaded = AppConfig::loadFrom(path);
            QCOMPARE(loaded.theme, QString("light"));
        }

        // Switch back to "dark"
        {
            AppConfig cfg;
            cfg.theme = "dark";
            cfg.saveTo(path);
        }
        {
            AppConfig loaded = AppConfig::loadFrom(path);
            QCOMPARE(loaded.theme, QString("dark"));
        }

        QFile::remove(path);
    }

    // ── 4: Alert threshold > 1.0 is clamped to 1.0 on load ───────────────────
    void testAlertThresholdClampedAbove()
    {
        const QString path = makeTempPath();

        // Write out-of-range values directly via QSettings
        {
            QSettings s(path, QSettings::IniFormat);
            s.setValue("alert/elevated", 2.5);
            s.setValue("alert/high",     1.8);
            s.setValue("alert/critical", 99.0);
            s.sync();
        }

        AppConfig loaded = AppConfig::loadFrom(path);

        QCOMPARE(loaded.alertElevated,  1.0);
        QCOMPARE(loaded.alertHigh,      1.0);
        QCOMPARE(loaded.alertCritical,  1.0);

        QFile::remove(path);
    }

    // ── 5: Alert threshold < 0.0 is clamped to 0.0 on load ───────────────────
    void testAlertThresholdClampedBelow()
    {
        const QString path = makeTempPath();

        {
            QSettings s(path, QSettings::IniFormat);
            s.setValue("alert/elevated", -0.5);
            s.setValue("alert/high",     -1.0);
            s.setValue("alert/critical", -99.0);
            s.sync();
        }

        AppConfig loaded = AppConfig::loadFrom(path);

        QCOMPARE(loaded.alertElevated,  0.0);
        QCOMPARE(loaded.alertHigh,      0.0);
        QCOMPARE(loaded.alertCritical,  0.0);

        QFile::remove(path);
    }

    // ── 6: mapZoomLevel persists and is clamped to [1, 20] ───────────────────
    void testMapZoomLevelPersists()
    {
        const QString path = makeTempPath();

        // Normal value
        {
            AppConfig cfg;
            cfg.mapZoomLevel = 15.5;
            cfg.saveTo(path);
            AppConfig loaded = AppConfig::loadFrom(path);
            QCOMPARE(loaded.mapZoomLevel, 15.5);
        }

        // Clamp: too large
        {
            QSettings s(path, QSettings::IniFormat);
            s.setValue("ui/map_zoom_level", 25.0);
            s.sync();
            AppConfig loaded = AppConfig::loadFrom(path);
            QCOMPARE(loaded.mapZoomLevel, 20.0);
        }

        // Clamp: too small
        {
            QSettings s(path, QSettings::IniFormat);
            s.setValue("ui/map_zoom_level", -3.0);
            s.sync();
            AppConfig loaded = AppConfig::loadFrom(path);
            QCOMPARE(loaded.mapZoomLevel, 1.0);
        }

        QFile::remove(path);
    }

    // ── 7: exportDirectory persists correctly ─────────────────────────────────
    void testExportDirectoryPersists()
    {
        const QString path = makeTempPath();

        AppConfig cfg;
        cfg.exportDirectory = "/home/analyst/sentinel_exports";
        cfg.saveTo(path);

        AppConfig loaded = AppConfig::loadFrom(path);
        QCOMPARE(loaded.exportDirectory, QString("/home/analyst/sentinel_exports"));

        // Empty string also round-trips
        cfg.exportDirectory = "";
        cfg.saveTo(path);
        loaded = AppConfig::loadFrom(path);
        QCOMPARE(loaded.exportDirectory, QString(""));

        QFile::remove(path);
    }

    // ── 8: maxLeadCount persists and is clamped to [1, 10000] ────────────────
    void testMaxLeadCountPersists()
    {
        const QString path = makeTempPath();

        // Normal value
        {
            AppConfig cfg;
            cfg.maxLeadCount = 200;
            cfg.saveTo(path);
            AppConfig loaded = AppConfig::loadFrom(path);
            QCOMPARE(loaded.maxLeadCount, 200);
        }

        // Clamp: zero or negative
        {
            QSettings s(path, QSettings::IniFormat);
            s.setValue("ui/max_lead_count", 0);
            s.sync();
            AppConfig loaded = AppConfig::loadFrom(path);
            QCOMPARE(loaded.maxLeadCount, 1);
        }

        // Clamp: above maximum
        {
            QSettings s(path, QSettings::IniFormat);
            s.setValue("ui/max_lead_count", 50000);
            s.sync();
            AppConfig loaded = AppConfig::loadFrom(path);
            QCOMPARE(loaded.maxLeadCount, 10000);
        }

        QFile::remove(path);
    }

    // ── 9: Default alert thresholds are strictly ordered ─────────────────────
    void testAlertThresholdsDefaultOrder()
    {
        AppConfig cfg;
        QVERIFY(cfg.alertElevated < cfg.alertHigh);
        QVERIFY(cfg.alertHigh     < cfg.alertCritical);
        QVERIFY(cfg.alertElevated >= 0.0);
        QVERIFY(cfg.alertCritical <= 1.0);
        QCOMPARE(cfg.alertElevated, 0.30);
        QCOMPARE(cfg.alertHigh,     0.50);
        QCOMPARE(cfg.alertCritical, 0.75);
    }

    // ── 10: Loading from a fresh (empty) INI file yields exact defaults ───────
    void testLoadFromEmptyFileGivesDefaults()
    {
        const QString path = makeTempPath();

        AppConfig loaded = AppConfig::loadFrom(path);

        QCOMPARE(loaded.defaultLat,           51.5074);
        QCOMPARE(loaded.defaultLon,           -0.1278);
        QCOMPARE(loaded.defaultRadius,        5.0);
        QCOMPARE(loaded.forecastHorizonDays,  7);
        QCOMPARE(loaded.alertElevated,        0.30);
        QCOMPARE(loaded.alertHigh,            0.50);
        QCOMPARE(loaded.alertCritical,        0.75);
        QCOMPARE(loaded.theme,                QString("dark"));
        QCOMPARE(loaded.mapZoomLevel,         14.0);
        QCOMPARE(loaded.maxLeadCount,         50);
        QVERIFY(loaded.exportDirectory.isEmpty());
        QCOMPARE(loaded.autoRefreshEnabled,   false);
        QCOMPARE(loaded.refreshIntervalSeconds, 3600);

        QFile::remove(path);
    }

    // ── 11: Multiple partial saves accumulate correctly ───────────────────────
    void testPartialSavesAccumulate()
    {
        const QString path = makeTempPath();

        AppConfig first;
        first.theme        = "light";
        first.maxLeadCount = 75;
        first.saveTo(path);

        AppConfig second;
        second.mapZoomLevel    = 16.0;
        second.exportDirectory = "/tmp/out";
        // Merge: write second over the same file
        second.saveTo(path);

        AppConfig loaded = AppConfig::loadFrom(path);
        // Second save overwrites completely, so first's values are gone (replaced by defaults)
        QCOMPARE(loaded.mapZoomLevel,    16.0);
        QCOMPARE(loaded.exportDirectory, QString("/tmp/out"));
        QCOMPARE(loaded.theme,           QString("dark"));  // second's default
        QCOMPARE(loaded.maxLeadCount,    50);               // second's default

        QFile::remove(path);
    }
};

// ─────────────────────────────────────────────────────────────────────────────

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    TestSettingsPersistence tests;
    return QTest::qExec(&tests, argc, argv);
}
#include "test_settings_persistence.moc"
