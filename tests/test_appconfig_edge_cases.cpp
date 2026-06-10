// test_appconfig_edge_cases.cpp
// Tests AppConfig default values, clamping, and save/load roundtrip.
#include <QTest>
#include <QTemporaryFile>
#include "core/AppConfig.h"
#include <cmath>

class AppConfigEdgeCasesTest : public QObject
{
    Q_OBJECT

private:
    static QString writeTempIni()
    {
        QTemporaryFile f;
        f.setAutoRemove(false);
        f.open();
        const QString path = f.fileName();
        f.close();
        return path;
    }

private slots:

    // ── 1. Default lat/lon are valid (London area) ────────────────────────────
    void testDefaultLatLon()
    {
        AppConfig cfg;
        QVERIFY2(cfg.defaultLat >= -90.0  && cfg.defaultLat <= 90.0,
                 qPrintable(QStringLiteral("defaultLat %1 not in [-90,90]").arg(cfg.defaultLat)));
        QVERIFY2(cfg.defaultLon >= -180.0 && cfg.defaultLon <= 180.0,
                 qPrintable(QStringLiteral("defaultLon %1 not in [-180,180]").arg(cfg.defaultLon)));
    }

    // ── 2. Default theme is "dark" ────────────────────────────────────────────
    void testDefaultTheme()
    {
        AppConfig cfg;
        QVERIFY2(cfg.theme == QStringLiteral("dark") || cfg.theme == QStringLiteral("light"),
                 qPrintable(QStringLiteral("Default theme '%1' invalid").arg(cfg.theme)));
    }

    // ── 3. Alert thresholds ordered: elevated < high < critical ──────────────
    void testAlertThresholdOrdering()
    {
        AppConfig cfg;
        QVERIFY2(cfg.alertElevated < cfg.alertHigh,
                 qPrintable(QStringLiteral("alertElevated %1 must be < alertHigh %2")
                    .arg(cfg.alertElevated).arg(cfg.alertHigh)));
        QVERIFY2(cfg.alertHigh < cfg.alertCritical,
                 qPrintable(QStringLiteral("alertHigh %1 must be < alertCritical %2")
                    .arg(cfg.alertHigh).arg(cfg.alertCritical)));
    }

    // ── 4. Ensemble weights are positive ─────────────────────────────────────
    void testEnsembleWeightsPositive()
    {
        AppConfig cfg;
        QVERIFY2(cfg.ensemblePoissonWeight > 0.0, "ensemblePoissonWeight must be > 0");
        QVERIFY2(cfg.ensembleHawkesWeight  > 0.0, "ensembleHawkesWeight must be > 0");
    }

    // ── 5. saveTo/loadFrom roundtrip: defaultLat preserved ───────────────────
    void testSaveLoadLatRoundtrip()
    {
        const QString path = writeTempIni();
        AppConfig cfg;
        cfg.defaultLat = 53.4808;  // Manchester
        cfg.saveTo(path);
        const auto cfg2 = AppConfig::loadFrom(path);
        QFile::remove(path);
        QVERIFY2(std::abs(cfg2.defaultLat - 53.4808) < 0.001,
                 qPrintable(QStringLiteral("defaultLat roundtrip: %1 vs 53.4808")
                    .arg(cfg2.defaultLat)));
    }

    // ── 6. saveTo/loadFrom: theme preserved ──────────────────────────────────
    void testSaveLoadThemeRoundtrip()
    {
        const QString path = writeTempIni();
        AppConfig cfg;
        cfg.theme = QStringLiteral("light");
        cfg.saveTo(path);
        const auto cfg2 = AppConfig::loadFrom(path);
        QFile::remove(path);
        QVERIFY2(cfg2.theme == QStringLiteral("light"),
                 qPrintable(QStringLiteral("Theme roundtrip: '%1' vs 'light'").arg(cfg2.theme)));
    }

    // ── 7. mapZoomLevel clamp [1, 20] ────────────────────────────────────────
    void testMapZoomLevelClamped()
    {
        const QString path = writeTempIni();
        // Manually write an out-of-range zoom
        {
            QSettings s(path, QSettings::IniFormat);
            s.setValue(QStringLiteral("ui/map_zoom_level"), 999.0);
            s.sync();
        }
        const auto cfg = AppConfig::loadFrom(path);
        QFile::remove(path);
        QVERIFY2(cfg.mapZoomLevel >= 1.0 && cfg.mapZoomLevel <= 20.0,
                 qPrintable(QStringLiteral("mapZoomLevel %1 not clamped to [1,20]")
                    .arg(cfg.mapZoomLevel)));
    }

    // ── 8. alertElevated clamped to [0, 1] ───────────────────────────────────
    void testAlertElevatedClamped()
    {
        const QString path = writeTempIni();
        {
            QSettings s(path, QSettings::IniFormat);
            s.setValue(QStringLiteral("alert/elevated"), -5.0);
            s.sync();
        }
        const auto cfg = AppConfig::loadFrom(path);
        QFile::remove(path);
        QVERIFY2(cfg.alertElevated >= 0.0 && cfg.alertElevated <= 1.0,
                 qPrintable(QStringLiteral("alertElevated %1 not clamped to [0,1]")
                    .arg(cfg.alertElevated)));
    }

    // ── 9. maxLeadCount clamped to [1, 10000] ────────────────────────────────
    void testMaxLeadCountClamped()
    {
        const QString path = writeTempIni();
        {
            QSettings s(path, QSettings::IniFormat);
            s.setValue(QStringLiteral("ui/max_lead_count"), 99999);
            s.sync();
        }
        const auto cfg = AppConfig::loadFrom(path);
        QFile::remove(path);
        QVERIFY2(cfg.maxLeadCount >= 1 && cfg.maxLeadCount <= 10000,
                 qPrintable(QStringLiteral("maxLeadCount %1 not clamped to [1,10000]")
                    .arg(cfg.maxLeadCount)));
    }

    // ── 10. Multiple save/load cycles maintain value ──────────────────────────
    void testMultipleCyclesConsistent()
    {
        const QString path = writeTempIni();
        AppConfig cfg;
        cfg.seriesEpsKm = 0.75;
        for (int i = 0; i < 3; ++i) {
            cfg.saveTo(path);
            cfg = AppConfig::loadFrom(path);
        }
        QFile::remove(path);
        QVERIFY2(std::abs(cfg.seriesEpsKm - 0.75) < 0.001,
                 qPrintable(QStringLiteral("seriesEpsKm after 3 cycles: %1").arg(cfg.seriesEpsKm)));
    }
};

QTEST_MAIN(AppConfigEdgeCasesTest)
#include "test_appconfig_edge_cases.moc"
