// test_calibration.cpp — CalibrationAnalyser unit tests
#include <QTest>
#include <QCoreApplication>
#include "benchmark/CalibrationAnalyser.h"

class TestCalibrationAnalyser : public QObject {
    Q_OBJECT

    // Build perfectly-calibrated data: uniform probabilities, matching actuals
    static QVector<QPair<double,double>> perfectlyCalibrated(int n = 100) {
        QVector<QPair<double,double>> data;
        for (int i = 0; i < n; ++i) {
            const double p = (i + 0.5) / n;
            // Actual = 1 if uniform[0,1) < p (simulated via LCG)
            const double act = (i % 2 == 0) ? 1.0 : 0.0;
            data.append({p, act});
        }
        return data;
    }

    static QVector<QPair<double,double>> constantPredictor(double pred, int n = 100) {
        QVector<QPair<double,double>> data;
        for (int i = 0; i < n; ++i)
            data.append({pred, (i % 2 == 0) ? 1.0 : 0.0});
        return data;
    }

    static QVector<QPair<double,double>> perfectPredictions(int n = 100) {
        QVector<QPair<double,double>> data;
        for (int i = 0; i < n; ++i)
            data.append({(i < n/2) ? 1.0 : 0.0, (i < n/2) ? 1.0 : 0.0});
        return data;
    }

private slots:

    void testEmptyInput() {
        CalibrationAnalyser ca;
        const auto res = ca.analyse({});
        QCOMPARE(res.nSamples, 0);
        QCOMPARE(res.ece, 0.0);
    }

    void testECERange() {
        CalibrationAnalyser ca;
        const auto res = ca.analyse(constantPredictor(0.5));
        QVERIFY(res.ece >= 0.0);
        QVERIFY(res.ece <= 1.0);
    }

    void testECEPerfectPredictions() {
        // Perfect binary predictions → Brier = 0, ECE ≈ 0
        CalibrationAnalyser ca;
        const auto res = ca.analyse(perfectPredictions());
        QVERIFY(res.brierScore < 1e-9);
        QVERIFY(res.ece < 1e-9);
    }

    void testMCENotLessThanECE() {
        CalibrationAnalyser ca;
        const auto res = ca.analyse(constantPredictor(0.3));
        QVERIFY(res.mce >= res.ece);
    }

    void testBrierScoreRange() {
        CalibrationAnalyser ca;
        const auto res = ca.analyse(constantPredictor(0.5));
        QVERIFY(res.brierScore >= 0.0);
        QVERIFY(res.brierScore <= 1.0);
    }

    void testBrierScoreWorstCase() {
        // All predictions wrong (1.0 when actual=0, 0.0 when actual=1)
        QVector<QPair<double,double>> data;
        for (int i = 0; i < 100; ++i)
            data.append({(i % 2 == 0) ? 0.0 : 1.0,   // wrong predictions
                         (i % 2 == 0) ? 1.0 : 0.0});  // actual
        CalibrationAnalyser ca;
        const auto res = ca.analyse(data);
        QVERIFY(std::abs(res.brierScore - 1.0) < 1e-9);
    }

    void testLogLossNonNegative() {
        CalibrationAnalyser ca;
        const auto res = ca.analyse(constantPredictor(0.5));
        QVERIFY(res.logLoss >= 0.0);
    }

    void testSharpnessConstantPredictor() {
        // Constant predictor has zero sharpness (no variance)
        CalibrationAnalyser ca;
        const auto res = ca.analyse(constantPredictor(0.5));
        QVERIFY(res.sharpness < 1e-9);
    }

    void testSharpnessExtremePredictions() {
        // Predictions spanning 0..1 have higher sharpness than a constant
        CalibrationAnalyser ca;
        const auto res1 = ca.analyse(constantPredictor(0.5));
        const auto res2 = ca.analyse(perfectPredictions());
        QVERIFY(res2.sharpness > res1.sharpness);
    }

    void testBinCountMatchesNBins() {
        const int N = 10;
        CalibrationAnalyser ca(N);
        const auto res = ca.analyse(constantPredictor(0.5));
        QCOMPARE(res.nBins, N);
    }

    void testAllBinFractionsSum() {
        CalibrationAnalyser ca(5);
        // Spread data across all bins
        QVector<QPair<double,double>> data;
        for (int i = 0; i < 100; ++i)
            data.append({(i % 5) * 0.2 + 0.1, (i % 2 == 0) ? 1.0 : 0.0});
        const auto res = ca.analyse(data);
        double total = 0.0;
        for (const auto& bin : res.bins) total += bin.fraction;
        QVERIFY(std::abs(total - 1.0) < 1e-9);
    }

    void testStatusExcellent() {
        // Build data that achieves ECE < 0.05
        CalibrationResult res;
        res.ece = 0.03;
        QCOMPARE(res.status(), QStringLiteral("EXCELLENT"));
    }

    void testStatusGood() {
        CalibrationResult res;
        res.ece = 0.07;
        QCOMPARE(res.status(), QStringLiteral("GOOD"));
    }

    void testStatusFair() {
        CalibrationResult res;
        res.ece = 0.15;
        QCOMPARE(res.status(), QStringLiteral("FAIR"));
    }

    void testStatusPoor() {
        CalibrationResult res;
        res.ece = 0.35;
        QCOMPARE(res.status(), QStringLiteral("POOR"));
    }

    void testReliabilityDiagramPoints() {
        CalibrationAnalyser ca(5);
        QVector<QPair<double,double>> data;
        for (int i = 0; i < 50; ++i)
            data.append({i * 0.02, (i % 2 == 0) ? 1.0 : 0.0});
        const auto pts = ca.reliabilityDiagram(data);
        QVERIFY(!pts.isEmpty());
        for (const auto& [x, y] : pts) {
            QVERIFY(x >= 0.0 && x <= 1.0);
            QVERIFY(y >= 0.0 && y <= 1.0);
        }
    }

    void testIsotonicCalibrateNonDecreasing() {
        // Output calibrated probabilities should be non-decreasing with sorted input
        QVector<QPair<double,double>> data;
        // Deliberately miscalibrated: high preds → 0, low preds → 1
        for (int i = 0; i < 20; ++i)
            data.append({i * 0.05, (i > 10) ? 0.0 : 1.0});
        const auto cal = CalibrationAnalyser::isotonicCalibrate(data);
        QCOMPARE(cal.size(), data.size());
        // Sorted by pred, calibrated probs should be non-decreasing
        for (int i = 0; i + 1 < cal.size(); ++i)
            QVERIFY(cal[i].first <= cal[i+1].first + 1e-9);
    }

    void testIsotonicCalibratePreservesSize() {
        QVector<QPair<double,double>> data;
        for (int i = 0; i < 30; ++i)
            data.append({i * 0.033, (i % 3 == 0) ? 1.0 : 0.0});
        const auto cal = CalibrationAnalyser::isotonicCalibrate(data);
        QCOMPARE(cal.size(), data.size());
    }

    void testNSamplesPopulated() {
        CalibrationAnalyser ca;
        const auto data = constantPredictor(0.4, 77);
        const auto res  = ca.analyse(data);
        QCOMPARE(res.nSamples, 77);
    }
};

// ─── main ─────────────────────────────────────────────────────────────────────

static int runTest(QObject* obj, const char* logFile) {
    QStringList args = { "test", "-o", QString("%1,txt").arg(logFile) };
    return QTest::qExec(obj, args);
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    int r = 0;
    TestCalibrationAnalyser t1; r |= runTest(&t1, "calib_analyser.txt");
    return r;
}

#include "test_calibration.moc"
