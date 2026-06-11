// test_appconfig_benchmark_deep.cpp — Iteration-7 deep audit tests for
// AppConfig, BenchmarkMetrics, and CalibrationAnalyser.

#include <QTest>
#include <QCoreApplication>
#include <QFile>
#include <QTemporaryFile>
#include <QJsonObject>
#include <QTimeZone>
#include <cmath>
#include <numeric>

#include "core/AppConfig.h"
#include "benchmark/BenchmarkMetrics.h"
#include "benchmark/CalibrationAnalyser.h"

class AppConfigBenchmarkDeepTest : public QObject
{
    Q_OBJECT

private:
    using PA = QPair<double, double>;

    static QString makeTempJsonPath()
    {
        QTemporaryFile f(QStringLiteral("sentinel_cfg_XXXXXX.json"));
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

    static double manualPAI(const QVector<double>& yTrue,
                            const QVector<double>& yPred,
                            double areaFraction)
    {
        const int n = yTrue.size();
        const double totalCrimes = std::accumulate(yTrue.begin(), yTrue.end(), 0.0);
        if (totalCrimes <= 0.0 || n == 0) return 0.0;

        QVector<int> idx(n);
        std::iota(idx.begin(), idx.end(), 0);
        std::sort(idx.begin(), idx.end(), [&](int a, int b) {
            return yPred[a] > yPred[b];
        });

        const int nFlagged = std::max(1, static_cast<int>(std::round(n * areaFraction)));
        double captured = 0.0;
        for (int i = 0; i < nFlagged && i < n; ++i)
            captured += yTrue[idx[i]];

        const double hitRate = captured / totalCrimes;
        return hitRate / areaFraction;
    }

private slots:

    // ── AppConfig ─────────────────────────────────────────────────────────────

    void testAppConfigDefaultsValid()
    {
        AppConfig cfg;
        QVERIFY2(cfg.validate(), "Default config should pass validate() unchanged");
        QCOMPARE(cfg.hawkesHistoryDays, 365);
        QVERIFY(!cfg.databasePath.trimmed().isEmpty());
        Q_UNUSED(QTimeZone::utc());
    }

    void testAppConfigValidateClamps()
    {
        AppConfig cfg;
        cfg.hawkesHistoryDays = -1;
        QVERIFY2(!cfg.validate(), "Out-of-range hawkesHistoryDays should report clamping");
        QCOMPARE(cfg.hawkesHistoryDays, 7);
    }

    void testAppConfigJsonRoundtrip()
    {
        AppConfig orig;
        orig.openWeatherKey         = QStringLiteral("wx_key");
        orig.socrataDomain          = QStringLiteral("data.test.gov");
        orig.socrataToken           = QStringLiteral("token");
        orig.defaultLat             = 40.7128;
        orig.defaultLon             = -74.0060;
        orig.defaultRadius          = 8.5;
        orig.hawkesHistoryDays      = 200;
        orig.seriesMinEvents        = 4;
        orig.seriesEpsKm            = 0.5;
        orig.seriesEpsDays          = 10.0;
        orig.qualityThreshold       = 0.45;
        orig.autoRefreshEnabled     = true;
        orig.refreshIntervalSeconds = 1800;
        orig.alertElevated          = 0.20;
        orig.alertHigh              = 0.45;
        orig.alertCritical          = 0.70;
        orig.forecastHorizonDays    = 14;
        orig.gpSigma2               = 2.0;
        orig.gpLengthscale          = 0.7;
        orig.gpNoiseSigma2          = 0.15;
        orig.rossmoF                = 1.5;
        orig.rossmoG                = 1.1;
        orig.ensemblePoissonWeight  = 0.6;
        orig.ensembleHawkesWeight   = 0.4;
        orig.databasePath           = QStringLiteral("/tmp/sentinel.json.db");
        orig.theme                  = QStringLiteral("light");
        orig.mapZoomLevel           = 12.5;
        orig.exportDirectory        = QStringLiteral("/tmp/exports");
        orig.maxLeadCount           = 100;
        orig.poissonGridSize        = 80;
        orig.kdeGridSize            = 120;

        const AppConfig loaded = AppConfig::fromJson(orig.toJson());
        assertConfigEqual(orig, loaded);
    }

    void testAppConfigMissingJsonKeys()
    {
        const AppConfig defaults;
        const AppConfig loaded = AppConfig::fromJson(QJsonObject());
        assertConfigEqual(defaults, loaded);
    }

    void testAppConfigSaveLoad()
    {
        const QString path = makeTempJsonPath();
        QVERIFY2(!path.isEmpty(), "Could not create temp JSON file");

        AppConfig orig;
        orig.databasePath      = QStringLiteral("/tmp/sentinel_file.db");
        orig.hawkesHistoryDays = 180;
        orig.theme             = QStringLiteral("light");
        orig.poissonGridSize   = 64;
        orig.kdeGridSize       = 96;
        QVERIFY(orig.saveToFile(path));

        AppConfig loaded;
        QVERIFY(loaded.loadFromFile(path));
        assertConfigEqual(orig, loaded);

        QFile::remove(path);
    }

    // ── BenchmarkMetrics ──────────────────────────────────────────────────────

    void testPAIDefinition()
    {
        const int N = 100, NC = 10;
        QVector<double> yTrue(N, 0.0), yPred(N, 0.0);
        for (int i = 0; i < NC; ++i) {
            yTrue[i] = 1.0;
            yPred[i] = 1.0 - i * 0.01;
        }
        for (int i = NC; i < N; ++i)
            yPred[i] = 0.01;

        const double expected = manualPAI(yTrue, yPred, 0.10);
        const double actual   = BenchmarkMetrics::pai(yTrue, yPred, 0.10);
        QVERIFY2(std::abs(actual - expected) < 1e-9,
                 qPrintable(QStringLiteral("PAI expected %1, got %2")
                                .arg(expected).arg(actual)));
    }

    void testAUCROCPerfect()
    {
        const int N = 100, NC = 30;
        QVector<double> yTrue(N, 0.0), yPred(N, 0.0);
        for (int i = 0; i < NC; ++i) {
            yTrue[i] = 1.0;
            yPred[i] = 1.0;
        }
        for (int i = NC; i < N; ++i)
            yPred[i] = 0.0;

        const double auc = BenchmarkMetrics::aucRoc(yTrue, yPred);
        QVERIFY2(std::abs(auc - 1.0) < 0.05,
                 qPrintable(QStringLiteral("Perfect AUC-ROC expected ~1.0, got %1").arg(auc)));
    }

    void testAUCROCRandom()
    {
        const int N = 200;
        QVector<double> yTrue(N, 0.0), yPred(N, 0.0);
        for (int i = 0; i < N / 2; ++i) yTrue[i] = 1.0;
        for (int i = 0; i < N; ++i)
            yPred[i] = static_cast<double>((i * 48271 + 17) % 997) / 997.0;

        const double auc = BenchmarkMetrics::aucRoc(yTrue, yPred);
        QVERIFY2(std::abs(auc - 0.5) < 0.15,
                 qPrintable(QStringLiteral("Random AUC-ROC expected ~0.5, got %1").arg(auc)));
    }

    void testLogLossZeroGuard()
    {
        const QVector<double> yTrue = { 1.0, 0.0, 1.0, 0.0 };
        const QVector<double> yPred = { 0.0, 1.0, 1.0, 0.0 };

        const double ll = BenchmarkMetrics::logLoss(yTrue, yPred);
        QVERIFY2(std::isfinite(ll), "Log loss must be finite for p=0 or p=1");
        QVERIFY2(ll >= 0.0, "Log loss must be non-negative");
    }

    void testBrierScorePerfect()
    {
        const QVector<double> yTrue = { 1.0 };
        const QVector<double> yPred = { 1.0 };
        const double bs = BenchmarkMetrics::brierScore(yTrue, yPred);
        QVERIFY2(std::abs(bs) < 1e-12,
                 qPrintable(QStringLiteral("Perfect Brier expected 0, got %1").arg(bs)));
    }

    void testRMSEDefinition()
    {
        const QVector<double> yTrue = { 1.0, 0.0, 1.0, 0.0 };
        const QVector<double> yPred = { 0.7, 0.3, 0.8, 0.2 };
        const double expected = std::sqrt((0.09 + 0.09 + 0.04 + 0.04) / 4.0);
        const double actual   = BenchmarkMetrics::rmse(yTrue, yPred);
        QVERIFY2(std::abs(actual - expected) < 1e-9,
                 qPrintable(QStringLiteral("RMSE expected %1, got %2")
                                .arg(expected).arg(actual)));
    }

    void testMAEDefinition()
    {
        const QVector<double> yTrue = { 1.0, 0.0, 0.5, 1.0, 0.0 };
        const QVector<double> yPred = { 0.8, 0.2, 0.5, 0.6, 0.4 };
        const double expected = (0.2 + 0.2 + 0.0 + 0.4 + 0.4) / 5.0;
        const double actual   = BenchmarkMetrics::mae(yTrue, yPred);
        QVERIFY2(std::abs(actual - expected) < 1e-9,
                 qPrintable(QStringLiteral("MAE expected %1, got %2")
                                .arg(expected).arg(actual)));
    }

    void testAUCPRPerfect()
    {
        const int N = 100, NC = 25;
        QVector<double> yTrue(N, 0.0), yPred(N, 0.0);
        for (int i = 0; i < NC; ++i) {
            yTrue[i] = 1.0;
            yPred[i] = 1.0 - i * 0.001;
        }
        for (int i = NC; i < N; ++i)
            yPred[i] = 0.0;

        const double aucPr = BenchmarkMetrics::aucPr(yTrue, yPred);
        QVERIFY2(aucPr > 0.95,
                 qPrintable(QStringLiteral("Perfect AUC-PR expected ~1.0, got %1").arg(aucPr)));
    }

    // ── CalibrationAnalyser ───────────────────────────────────────────────────

    void testECEPerfectCalibration()
    {
        CalibrationAnalyser ca(10);
        QVector<PA> data;
        for (int i = 0; i < 90; ++i) data.append({ 0.1, 0.0 });
        for (int i = 0; i < 10; ++i) data.append({ 0.1, 1.0 });

        const auto res = ca.analyse(data);
        QVERIFY2(res.ece < 0.05,
                 qPrintable(QStringLiteral("Perfect calibration ECE expected ~0, got %1")
                                .arg(res.ece)));
    }

    void testECEOverconfident()
    {
        CalibrationAnalyser ca(10);
        QVector<PA> data;
        for (int i = 0; i < 100; ++i)
            data.append({ 0.9, static_cast<double>(i % 2) });

        const auto res = ca.analyse(data);
        QVERIFY2(std::abs(res.ece - 0.4) < 0.05,
                 qPrintable(QStringLiteral("Overconfident ECE expected ~0.4, got %1")
                                .arg(res.ece)));
    }

    void testMCEMaximumBin()
    {
        CalibrationAnalyser ca(10);
        QVector<PA> data;
        for (int i = 0; i < 50; ++i) data.append({ 0.1, 0.0 });
        for (int i = 0; i < 50; ++i) data.append({ 0.9, 1.0 });

        const auto res = ca.analyse(data);
        double maxBinError = 0.0;
        for (const auto& bin : res.bins) {
            if (bin.count > 0)
                maxBinError = std::max(maxBinError, bin.error);
        }
        QVERIFY2(std::abs(res.mce - maxBinError) < 1e-9,
                 qPrintable(QStringLiteral("MCE %1 should equal max bin error %2")
                                .arg(res.mce).arg(maxBinError)));
    }

    void testCalibrationNoNaN()
    {
        CalibrationAnalyser ca(10);
        const auto empty = ca.analyse({});
        QVERIFY2(std::isfinite(empty.ece), "Empty ECE must be finite");
        QVERIFY2(std::isfinite(empty.mce), "Empty MCE must be finite");
        QVERIFY2(std::isfinite(empty.ace), "Empty ACE must be finite");
        QVERIFY2(std::isfinite(empty.brierScore), "Empty Brier must be finite");
        QVERIFY2(std::isfinite(empty.logLoss), "Empty logLoss must be finite");
        QCOMPARE(empty.ece, 0.0);
        QCOMPARE(empty.mce, 0.0);

        const auto single = ca.analyse({ PA{0.75, 1.0} });
        QVERIFY2(std::isfinite(single.ece), "Single-sample ECE must be finite");
        QVERIFY2(std::isfinite(single.mce), "Single-sample MCE must be finite");
        QVERIFY2(std::isfinite(single.ace), "Single-sample ACE must be finite");
    }

    void testBrierScoreFromAnalyser()
    {
        CalibrationAnalyser ca(10);
        QVector<PA> data = {
            { 0.2, 0.0 }, { 0.8, 1.0 }, { 0.5, 0.0 }, { 0.9, 1.0 }
        };

        QVector<double> yTrue, yPred;
        for (const auto& [p, a] : data) {
            yPred.append(p);
            yTrue.append(a);
        }

        const auto res = ca.analyse(data);
        const double independent = BenchmarkMetrics::brierScore(yTrue, yPred);
        QVERIFY2(std::abs(res.brierScore - independent) < 1e-12,
                 qPrintable(QStringLiteral("Analyser Brier %1 != independent %2")
                                .arg(res.brierScore).arg(independent)));
    }
};

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    AppConfigBenchmarkDeepTest tests;
    return QTest::qExec(&tests, argc, argv);
}
#include "test_appconfig_benchmark_deep.moc"
