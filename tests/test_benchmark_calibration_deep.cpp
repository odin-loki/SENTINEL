// test_benchmark_calibration_deep.cpp
// Iteration-4 deep audit: BenchmarkMetrics, CalibrationAnalyser, BiasAuditor
#include <QTest>
#include "benchmark/BenchmarkMetrics.h"
#include "benchmark/CalibrationAnalyser.h"
#include "benchmark/BiasAuditor.h"
#include <cmath>
#include <numeric>

class BenchmarkCalibrationDeepTest : public QObject
{
    Q_OBJECT

private:
    using PA = QPair<double, double>;

    static double manualPAI(const QVector<double>& yTrue,
                            const QVector<double>& yPred,
                            double areaFraction)
    {
        const int n = yTrue.size();
        const double totalCrimes = std::accumulate(yTrue.begin(), yTrue.end(), 0.0);
        const double totalArea = static_cast<double>(n);
        if (totalCrimes <= 0.0 || totalArea <= 0.0) return 0.0;

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
        const double areaCaptured = static_cast<double>(nFlagged) / totalArea;
        return hitRate / areaCaptured;
    }

    static double minMaxDisparateImpact(const QVector<GroupStats>& groups)
    {
        double maxRate = -1.0, minRate = 2.0;
        for (const auto& g : groups) {
            if (g.nEvents == 0) return 0.0;
            const double rate = static_cast<double>(g.nFlagged) / g.nEvents;
            maxRate = std::max(maxRate, rate);
            minRate = std::min(minRate, rate);
        }
        if (maxRate < 1e-12) return 0.0;
        return minRate / maxRate;
    }

private slots:

    // ── BenchmarkMetrics ──────────────────────────────────────────────────────

    void testPAIFormula()
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
                 qPrintable(QStringLiteral("PAI formula: expected %1, got %2")
                                .arg(expected).arg(actual)));
    }

    void testPEIFormula()
    {
        const int N = 100, NC = 15;
        QVector<double> yTrue(N, 0.0), yPred(N, 0.0);
        for (int i = 0; i < NC; ++i) {
            yTrue[i] = 1.0;
            yPred[i] = 0.95 - i * 0.01;
        }
        for (int i = NC; i < N; ++i)
            yPred[i] = static_cast<double>(i) / N * 0.2;

        const double pai10  = BenchmarkMetrics::pai(yTrue, yPred, 0.10);
        const double pai100 = BenchmarkMetrics::pai(yTrue, yPred, 1.00);
        const double pei    = BenchmarkMetrics::pei(yTrue, yPred, 0.10);

        // Implementation: PEI = PAI(f) / PAI_max(f); PAI(100%) is always 1.0
        QVERIFY2(std::abs(pai100 - 1.0) < 1e-9,
                 qPrintable(QStringLiteral("PAI(100%%) expected 1.0, got %1").arg(pai100)));

        const double totalCrimes = NC;
        const int nFlagged = std::max(1, static_cast<int>(std::round(N * 0.10)));
        const double perfectCrimes = std::min(static_cast<double>(nFlagged), totalCrimes);
        const double paiMax = (perfectCrimes / totalCrimes) / 0.10;
        const double expectedPei = pai10 / paiMax;

        QVERIFY2(std::abs(pei - expectedPei) < 1e-9,
                 qPrintable(QStringLiteral("PEI expected %1, got %2")
                                .arg(expectedPei).arg(pei)));
    }

    void testAUCROCRandomClassifier()
    {
        const int N = 200;
        QVector<double> yTrue(N, 0.0), yPred(N, 0.0);
        for (int i = 0; i < N / 2; ++i) yTrue[i] = 1.0;
        // Pseudo-random uniform scores independent of label order
        for (int i = 0; i < N; ++i)
            yPred[i] = static_cast<double>((i * 48271 + 17) % 997) / 997.0;

        const double auc = BenchmarkMetrics::aucRoc(yTrue, yPred);
        QVERIFY2(std::abs(auc - 0.5) < 0.15,
                 qPrintable(QStringLiteral("Random-ish classifier AUC expected ~0.5, got %1")
                                .arg(auc)));
    }

    void testAUCROCPerfectClassifier()
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
                 qPrintable(QStringLiteral("Perfect classifier AUC expected ~1.0, got %1")
                                .arg(auc)));
    }

    void testBrierScoreRange()
    {
        QVector<double> yTrue = { 0.0, 1.0, 0.0, 1.0, 0.5 };
        QVector<double> yPred = { 0.2, 0.8, 0.9, 0.1, 0.5 };
        const double bs = BenchmarkMetrics::brierScore(yTrue, yPred);
        QVERIFY2(bs >= 0.0 && bs <= 1.0,
                 qPrintable(QStringLiteral("Brier score %1 must be in [0,1]").arg(bs)));
    }

    void testBrierScoreForPerfect()
    {
        QVector<double> yTrue = { 1.0, 0.0, 1.0, 0.0 };
        const double bs = BenchmarkMetrics::brierScore(yTrue, yTrue);
        QVERIFY2(std::abs(bs) < 1e-9,
                 qPrintable(QStringLiteral("Perfect Brier expected 0, got %1").arg(bs)));
    }

    void testBrierScoreForRandom()
    {
        const int N = 200;
        QVector<double> yTrue(N, 0.0);
        for (int i = 0; i < N / 2; ++i) yTrue[i] = 1.0;
        const QVector<double> yPred(N, 0.5);

        const double bs = BenchmarkMetrics::brierScore(yTrue, yPred);
        QVERIFY2(std::abs(bs - 0.25) < 0.01,
                 qPrintable(QStringLiteral("Random Brier expected ~0.25, got %1").arg(bs)));
    }

    void testMAEFormula()
    {
        QVector<double> yTrue = { 1.0, 0.0, 0.5, 1.0, 0.0 };
        QVector<double> yPred = { 0.8, 0.2, 0.5, 0.6, 0.4 };
        const double expected = (0.2 + 0.2 + 0.0 + 0.4 + 0.4) / 5.0;
        const double actual   = BenchmarkMetrics::mae(yTrue, yPred);
        QVERIFY2(std::abs(actual - expected) < 1e-9,
                 qPrintable(QStringLiteral("MAE expected %1, got %2")
                                .arg(expected).arg(actual)));
    }

    void testRMSEFormula()
    {
        QVector<double> yTrue = { 1.0, 0.0, 1.0, 0.0, 1.0, 0.0 };
        QVector<double> yPred = { 0.7, 0.3, 0.8, 0.2, 0.9, 0.1 };

        const double mae  = BenchmarkMetrics::mae(yTrue, yPred);
        const double rmse = BenchmarkMetrics::rmse(yTrue, yPred);
        QVERIFY2(rmse >= mae - 1e-9,
                 qPrintable(QStringLiteral("RMSE (%1) must be >= MAE (%2)")
                                .arg(rmse).arg(mae)));
    }

    void testFullReportNonEmpty()
    {
        QVector<double> yTrue = { 1.0, 0.0, 1.0, 0.0 };
        QVector<double> yPred = { 0.9, 0.1, 0.8, 0.2 };
        const auto report = BenchmarkMetrics::fullReport(yTrue, yPred);
        QVERIFY2(!report.reportText().isEmpty(),
                 "fullReport().reportText() must be non-empty");
    }

    void testDivisionByZeroGuards()
    {
        const QVector<double> empty;
        const QVector<double> noCrimes(10, 0.0);
        const QVector<double> preds(10, 0.5);

        QVERIFY(std::isfinite(BenchmarkMetrics::pai(noCrimes, preds, 0.10)));
        QVERIFY(std::isfinite(BenchmarkMetrics::pei(noCrimes, preds, 0.10)));
        QVERIFY(std::isfinite(BenchmarkMetrics::ser(noCrimes, preds)));
        QVERIFY(std::isfinite(BenchmarkMetrics::aucRoc(noCrimes, preds)));
        QVERIFY(std::isfinite(BenchmarkMetrics::mae(empty, empty)));
        QVERIFY(std::isfinite(BenchmarkMetrics::fullReport(noCrimes, preds).pai10pct));
    }

    // ── CalibrationAnalyser ───────────────────────────────────────────────────

    void testECERange()
    {
        CalibrationAnalyser ca(10);
        QVector<PA> data;
        for (int i = 0; i < 100; ++i)
            data.append({ static_cast<double>(i) / 100.0, static_cast<double>(i % 2) });

        const auto res = ca.analyse(data);
        QVERIFY2(res.ece >= 0.0 && res.ece <= 1.0,
                 qPrintable(QStringLiteral("ECE %1 must be in [0,1]").arg(res.ece)));
    }

    void testECEPerfect()
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

    void testECEWorstCase()
    {
        CalibrationAnalyser ca(10);
        QVector<PA> data;
        for (int i = 0; i < 100; ++i)
            data.append({ 1.0, 0.0 });

        const auto res = ca.analyse(data);
        QVERIFY2(res.ece > 0.9,
                 qPrintable(QStringLiteral("Worst-case ECE expected ~1, got %1")
                                .arg(res.ece)));
    }

    void testMCELargerThanECE()
    {
        CalibrationAnalyser ca(10);
        QVector<PA> data;
        for (int i = 0; i < 100; ++i)
            data.append({ 0.9, static_cast<double>(i % 2) });

        const auto res = ca.analyse(data);
        QVERIFY2(res.mce >= res.ece - 1e-9,
                 qPrintable(QStringLiteral("MCE (%1) must be >= ECE (%2)")
                                .arg(res.mce).arg(res.ece)));
    }

    void testCalibrationBinBoundaries()
    {
        CalibrationAnalyser ca(10);
        QVector<PA> data;
        for (int b = 0; b < 10; ++b) {
            const double pred = (b + 0.5) / 10.0;
            for (int j = 0; j < 5; ++j)
                data.append({ pred, static_cast<double>(j % 2) });
        }

        const auto res = ca.analyse(data);
        QCOMPARE(res.bins.size(), 10);

        for (int b = 0; b < 10; ++b) {
            const double lo = static_cast<double>(b) / 10.0;
            const double hi = static_cast<double>(b + 1) / 10.0;
            QCOMPARE(res.bins[b].count, 5);
            QVERIFY2(res.bins[b].midpoint >= lo && res.bins[b].midpoint <= hi,
                     qPrintable(QStringLiteral("Bin %1 midpoint %2 outside [%3,%4]")
                                    .arg(b).arg(res.bins[b].midpoint).arg(lo).arg(hi)));
        }
    }

    void testCalibrationPlotMonotone()
    {
        CalibrationAnalyser ca(10);
        QVector<PA> data;
        for (int i = 0; i < 100; ++i)
            data.append({ i * 0.01, static_cast<double>(i % 2) });

        const auto plot = ca.reliabilityDiagram(data);
        QVERIFY(!plot.isEmpty());
        for (int i = 0; i + 1 < plot.size(); ++i)
            QVERIFY2(plot[i].first <= plot[i + 1].first + 1e-9,
                     qPrintable(QStringLiteral("Plot not monotone at %1: %2 > %3")
                                    .arg(i).arg(plot[i].first).arg(plot[i + 1].first)));
    }

    // ── BiasAuditor ─────────────────────────────────────────────────────────────

    void testDisparateImpactRange()
    {
        GroupStats a;
        a.nEvents  = 100;
        a.nFlagged = 60;
        GroupStats b;
        b.nEvents  = 100;
        b.nFlagged = 40;

        const double ratio = minMaxDisparateImpact({ a, b });
        QVERIFY2(ratio >= 0.0 && ratio <= 1.0,
                 qPrintable(QStringLiteral("Disparate impact ratio %1 must be in [0,1]")
                                .arg(ratio)));
    }

    void testDisparateImpactPerfect()
    {
        BiasAuditor auditor;
        GroupStats a;
        a.nEvents  = 50;
        a.nFlagged = 25;
        GroupStats b;
        b.nEvents  = 50;
        b.nFlagged = 25;

        const double maxMin = auditor.maxDisparateImpact({ a, b });
        QVERIFY2(std::abs(maxMin - 1.0) < 1e-9,
                 qPrintable(QStringLiteral("Equal rates max/min expected 1.0, got %1")
                                .arg(maxMin)));
        QVERIFY2(std::abs(minMaxDisparateImpact({ a, b }) - 1.0) < 1e-9,
                 "Equal rates min/max expected 1.0");
    }

    void testEqualizedOddsNoGap()
    {
        BiasAuditor auditor;
        GroupStats a;
        a.nEvents = 20;
        a.nTP     = 8;
        a.nActualPos = 10;
        GroupStats b;
        b.nEvents = 20;
        b.nTP     = 8;
        b.nActualPos = 10;

        QVERIFY(std::abs(auditor.equalizedOddsDiff({ a, b })) < 1e-9);
    }

    void testFeedbackLoopRiskPositive()
    {
        QMap<QString, QVector<QPair<double, double>>> trendData;
        QVector<QPair<double, double>> trend;
        for (int t = 0; t < 8; ++t)
            trend.append({ 0.05 + t * 0.08, 0.50 });
        trendData["AmplifiedGroup"] = trend;

        const auto flagged = BiasAuditor::feedbackLoopCheck(trendData, 0.01);
        QVERIFY2(!flagged.isEmpty(),
                 "Amplified prediction trend with flat actuals should flag feedback-loop risk");
        QVERIFY(flagged.contains("AmplifiedGroup"));
    }

    void testEmptyGroupGuard()
    {
        BiasAuditor auditor;

        GroupStats empty;
        empty.nEvents = 0;
        empty.nFlagged = 0;
        GroupStats valid;
        valid.nEvents = 10;
        valid.nFlagged = 5;
        valid.nActualPos = 4;
        valid.nTP = 2;

        QVERIFY(std::isfinite(auditor.maxDisparateImpact({ empty, valid })));
        QVERIFY(std::isfinite(auditor.equalizedOddsDiff({ empty, valid })));

        QVector<QString> groups;
        QVector<double> preds;
        for (int i = 0; i < 10; ++i) {
            groups.append("OnlyGroup");
            preds.append(0.7);
        }
        QVERIFY(BiasAuditor::disparateImpact(groups, preds).isEmpty());
    }
};

QTEST_MAIN(BenchmarkCalibrationDeepTest)
#include "test_benchmark_calibration_deep.moc"
