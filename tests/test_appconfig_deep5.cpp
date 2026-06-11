// test_appconfig_deep5.cpp — Iteration 19 deep audit: validate() gaps,
// loadFrom() vs validate() parity, and JSON persistence edge cases.
#include <QTest>
#include <QFile>
#include <QSettings>
#include <QTemporaryFile>
#include <cmath>

#include "core/AppConfig.h"

class TestAppConfigDeep5 : public QObject
{
    Q_OBJECT

private:
    static QString tempIniPath()
    {
        QTemporaryFile f(QStringLiteral("sentinel_cfg_deep5_XXXXXX.ini"));
        f.setAutoRemove(false);
        if (!f.open())
            return {};
        const QString path = f.fileName();
        f.close();
        return path;
    }

    static QString tempJsonPath()
    {
        QTemporaryFile f(QStringLiteral("sentinel_cfg_deep5_XXXXXX.json"));
        f.setAutoRemove(false);
        if (!f.open())
            return {};
        const QString path = f.fileName();
        f.close();
        return path;
    }

private slots:
    void testValidateNoUpperBoundOnSeriesEpsKm();
    void testValidateNoUpperBoundOnSeriesEpsDays();
    void testLoadFromDoesNotEnforceAlertOrdering();
    void testLoadFromDoesNotNormalizeEnsembleWeights();
    void testFromJsonPreservesOutOfRangeValuesUntilValidate();
    void testValidateClampsRefreshIntervalAndForecastHorizon();
    void testJsonFileRoundTripPreservesSeriesParams();
    void testValidateFixesZeroSumEnsembleWeights();
};

void TestAppConfigDeep5::testValidateNoUpperBoundOnSeriesEpsKm()
{
    AppConfig cfg;
    cfg.databasePath = QStringLiteral(":memory:");
    cfg.seriesEpsKm    = 5000.0;

    const bool ok = cfg.validate();
    if (ok && cfg.seriesEpsKm > 100.0) {
        QWARN("BUG AppConfig.cpp:135-138 — validate() resets non-positive seriesEpsKm "
              "but imposes no upper bound; extreme spatial epsilon may break clustering");
    }
    QVERIFY(cfg.seriesEpsKm > 100.0);
}

void TestAppConfigDeep5::testValidateNoUpperBoundOnSeriesEpsDays()
{
    AppConfig cfg;
    cfg.databasePath  = QStringLiteral(":memory:");
    cfg.seriesEpsDays = 99999.0;

    const bool ok = cfg.validate();
    if (ok && cfg.seriesEpsDays > 365.0) {
        QWARN("BUG AppConfig.cpp:139-142 — validate() resets non-positive seriesEpsDays "
              "but imposes no upper bound; multi-decade epsilon may break series detection");
    }
    QVERIFY(cfg.seriesEpsDays > 365.0);
}

void TestAppConfigDeep5::testLoadFromDoesNotEnforceAlertOrdering()
{
    const QString path = tempIniPath();
    QVERIFY2(!path.isEmpty(), "Could not create temp INI file");

    {
        QSettings s(path, QSettings::IniFormat);
        s.setValue(QStringLiteral("alert/elevated"), 0.80);
        s.setValue(QStringLiteral("alert/high"),     0.40);
        s.setValue(QStringLiteral("alert/critical"), 0.90);
        s.sync();
    }

    const AppConfig cfg = AppConfig::loadFrom(path);
    if (cfg.alertElevated >= cfg.alertHigh) {
        QWARN("BUG AppConfig.h:119-122 — loadFrom clamps alert thresholds to [0,1] "
              "but does not enforce elevated < high < critical ordering (validate() does)");
    }
    QCOMPARE(cfg.alertElevated, 0.80);
    QCOMPARE(cfg.alertHigh,     0.40);

    QFile::remove(path);
}

void TestAppConfigDeep5::testLoadFromDoesNotNormalizeEnsembleWeights()
{
    const QString path = tempIniPath();
    QVERIFY2(!path.isEmpty(), "Could not create temp INI file");

    {
        QSettings s(path, QSettings::IniFormat);
        s.setValue(QStringLiteral("ensemble/poisson_weight"), 0.9);
        s.setValue(QStringLiteral("ensemble/hawkes_weight"),  0.9);
        s.sync();
    }

    const AppConfig cfg = AppConfig::loadFrom(path);
    const double sum = cfg.ensemblePoissonWeight + cfg.ensembleHawkesWeight;
    if (std::abs(sum - 1.0) > 1e-6) {
        QWARN("BUG AppConfig.h:129-130 — loadFrom reads ensemble weights verbatim; "
              "non-unit sums persist until validate() is called");
    }
    QVERIFY(std::abs(sum - 1.8) < 1e-9);

    QFile::remove(path);
}

void TestAppConfigDeep5::testFromJsonPreservesOutOfRangeValuesUntilValidate()
{
    QJsonObject obj;
    obj[QStringLiteral("database_path")]           = QStringLiteral(":memory:");
    obj[QStringLiteral("default_lat")]             = 200.0;
    obj[QStringLiteral("hawkes_history_days")]     = 2;
    obj[QStringLiteral("ensemble_poisson_weight")] = 2.5;
    obj[QStringLiteral("ensemble_hawkes_weight")]  = 2.5;

    AppConfig cfg = AppConfig::fromJson(obj);
    QVERIFY(cfg.defaultLat > 90.0);
    QVERIFY(cfg.ensemblePoissonWeight > 1.0);

    const bool ok = cfg.validate();
    QVERIFY(!ok);
    QCOMPARE(cfg.defaultLat, 90.0);
    QCOMPARE(cfg.hawkesHistoryDays, 7);
    QVERIFY(std::abs(cfg.ensemblePoissonWeight - 0.5) < 1e-9);
    QVERIFY(std::abs(cfg.ensembleHawkesWeight  - 0.5) < 1e-9);
}

void TestAppConfigDeep5::testValidateClampsRefreshIntervalAndForecastHorizon()
{
    AppConfig cfg;
    cfg.databasePath           = QStringLiteral(":memory:");
    cfg.refreshIntervalSeconds = 1;
    cfg.forecastHorizonDays    = 0;

    const bool ok = cfg.validate();
    QVERIFY(!ok);
    QCOMPARE(cfg.refreshIntervalSeconds, 10);
    QCOMPARE(cfg.forecastHorizonDays,    1);
}

void TestAppConfigDeep5::testJsonFileRoundTripPreservesSeriesParams()
{
    const QString path = tempJsonPath();
    QVERIFY2(!path.isEmpty(), "Could not create temp JSON file");

    AppConfig orig;
    orig.databasePath   = QStringLiteral(":memory:");
    orig.seriesEpsKm    = 0.45;
    orig.seriesEpsDays  = 21.0;
    orig.seriesMinEvents = 5;
    orig.theme          = QStringLiteral("light");

    QVERIFY(orig.saveToFile(path));

    AppConfig loaded;
    QVERIFY(loaded.loadFromFile(path));
    QCOMPARE(loaded.seriesEpsKm,    orig.seriesEpsKm);
    QCOMPARE(loaded.seriesEpsDays,  orig.seriesEpsDays);
    QCOMPARE(loaded.seriesMinEvents, orig.seriesMinEvents);
    QCOMPARE(loaded.theme,           orig.theme);

    QFile::remove(path);
}

void TestAppConfigDeep5::testValidateFixesZeroSumEnsembleWeights()
{
    AppConfig cfg;
    cfg.databasePath          = QStringLiteral(":memory:");
    cfg.ensemblePoissonWeight = 0.0;
    cfg.ensembleHawkesWeight  = 0.0;

    const bool ok = cfg.validate();
    QVERIFY(!ok);
    QCOMPARE(cfg.ensemblePoissonWeight, 0.5);
    QCOMPARE(cfg.ensembleHawkesWeight,  0.5);
}

QTEST_GUILESS_MAIN(TestAppConfigDeep5)
#include "test_appconfig_deep5.moc"
