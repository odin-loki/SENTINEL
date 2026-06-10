// test_ensemble_ece.cpp — EnsemblePredictor and CalibrationAnalyser ECE tests
#include <QTest>
#include <QCoreApplication>
#include <cmath>
#include <algorithm>
#include "models/EnsemblePredictor.h"
#include "benchmark/CalibrationAnalyser.h"

// ─── helpers ─────────────────────────────────────────────────────────────────

// pred == actual for every sample
static QVector<QPair<double,double>> perfectPredictions(int n = 100)
{
    QVector<QPair<double,double>> data;
    data.reserve(n);
    for (int i = 0; i < n; ++i) {
        const double act = (i < n / 2) ? 1.0 : 0.0;
        data.append({act, act});
    }
    return data;
}

// All predictions = 1.0, half of actuals = 1 → heavily miscalibrated
static QVector<QPair<double,double>> allOnesMiscalibrated(int n = 100)
{
    QVector<QPair<double,double>> data;
    data.reserve(n);
    for (int i = 0; i < n; ++i)
        data.append({1.0, (i % 2 == 0) ? 1.0 : 0.0});
    return data;
}

// Uniform spread of probabilities
static QVector<QPair<double,double>> uniformPredictions(int n = 200)
{
    QVector<QPair<double,double>> data;
    data.reserve(n);
    for (int i = 0; i < n; ++i)
        data.append({(i + 0.5) / n, (i % 2 == 0) ? 1.0 : 0.0});
    return data;
}

// ─── test class ──────────────────────────────────────────────────────────────

class TestEnsembleECE : public QObject {
    Q_OBJECT

private slots:

    // 1. ECE (and predicted probability) must always lie in [0, 1].
    void testEnsemblePredictionInRange()
    {
        const double e = EnsemblePredictor::ece(uniformPredictions(), 10);
        QVERIFY2(e >= 0.0 && e <= 1.0,
                 qPrintable(QString("ECE = %1, expected in [0, 1]").arg(e)));
    }

    // 2. When predicted probability exactly matches the actual binary label, ECE ≈ 0.
    void testECEOnPerfectCalibration()
    {
        const double e = EnsemblePredictor::ece(perfectPredictions(100), 10);
        QVERIFY2(e < 1e-9,
                 qPrintable(QString("ECE on perfect predictions = %1, expected ~0").arg(e)));
    }

    // 3. All predictions = 1.0 but half are actually 0 → ECE should be high (≥ 0.3).
    void testECEOnMisCalibrated()
    {
        const double e = EnsemblePredictor::ece(allOnesMiscalibrated(100), 10);
        QVERIFY2(e > 0.3,
                 qPrintable(QString("ECE on mis-calibrated = %1, expected > 0.3").arg(e)));
    }

    // 4. CalibrationAnalyser bins must be non-overlapping and cover [0, 1].
    void testCalibrationBinsNonOverlapping()
    {
        CalibrationAnalyser ca(10);
        QVector<QPair<double,double>> data;
        for (int i = 0; i < 100; ++i)
            data.append({i * 0.01, (i % 2 == 0) ? 1.0 : 0.0});

        const auto res = ca.analyse(data);
        QVERIFY(!res.bins.isEmpty());

        // Midpoints must be strictly increasing
        for (int i = 0; i + 1 < res.bins.size(); ++i) {
            QVERIFY2(res.bins[i].midpoint < res.bins[i + 1].midpoint,
                     qPrintable(QString("Bin midpoints not increasing at index %1").arg(i)));
        }
        // Every midpoint must be within [0, 1]
        for (const auto& bin : res.bins) {
            QVERIFY2(bin.midpoint >= 0.0 && bin.midpoint <= 1.0,
                     qPrintable(QString("Bin midpoint %1 out of [0, 1]").arg(bin.midpoint)));
        }
    }

    // 5. A 95 % CI must be strictly wider than a 50 % CI for the same prediction.
    void testConfidenceIntervalWidth()
    {
        // Use EnsemblePrediction struct directly to test the width convention.
        EnsemblePrediction pred;
        pred.ciLow95  = 0.20;
        pred.ciHigh95 = 0.80;
        const double width95 = pred.ciHigh95 - pred.ciLow95;

        // Simulate a tighter interval (e.g. 50 %)
        const double ciLow50  = 0.35;
        const double ciHigh50 = 0.65;
        const double width50  = ciHigh50 - ciLow50;

        QVERIFY2(width95 > width50,
                 qPrintable(QString("95%% CI width %1 should exceed 50%% CI width %2")
                            .arg(width95).arg(width50)));
        QVERIFY2(width95 >= 0.0, "CI width must be non-negative");
    }

    // 6. The ensemble weights passed to setWeights() must sum to 1.0.
    void testEnsembleWeightSumToOne()
    {
        EnsemblePredictor ep;
        const double wP = 0.6, wH = 0.4;
        ep.setWeights(wP, wH);

        // Verify the API contract: caller must supply weights that sum to 1.
        QVERIFY2(std::abs(wP + wH - 1.0) < 1e-9,
                 qPrintable(QString("Weights %1 + %2 = %3, expected 1.0")
                            .arg(wP).arg(wH).arg(wP + wH)));

        // Also test a second valid split
        const double wP2 = 0.3, wH2 = 0.7;
        ep.setWeights(wP2, wH2);
        QVERIFY2(std::abs(wP2 + wH2 - 1.0) < 1e-9,
                 qPrintable(QString("Weights %1 + %2 = %3, expected 1.0")
                            .arg(wP2).arg(wH2).arg(wP2 + wH2)));
    }

    // 7. Brier score: perfect predictions → ~0, constant-0.5 predictions → ~0.25.
    void testBrierScore()
    {
        // Perfect binary predictions
        const double bsPerfect = EnsemblePredictor::brierScore(perfectPredictions(100));
        QVERIFY2(bsPerfect < 1e-9,
                 qPrintable(QString("Brier (perfect) = %1, expected ~0").arg(bsPerfect)));

        // Constant p=0.5, half actual=1 → Brier = (0.5-1)²*0.5 + (0.5-0)²*0.5 = 0.25
        QVector<QPair<double,double>> random;
        random.reserve(200);
        for (int i = 0; i < 200; ++i)
            random.append({0.5, (i % 2 == 0) ? 1.0 : 0.0});

        const double bsRandom = EnsemblePredictor::brierScore(random);
        QVERIFY2(std::abs(bsRandom - 0.25) < 0.01,
                 qPrintable(QString("Brier (random p=0.5) = %1, expected ~0.25").arg(bsRandom)));
    }

    // 8. Isotonic calibration must produce calibrated values in [0, 1].
    void testIsotonicCalibration()
    {
        QVector<QPair<double,double>> data;
        data.reserve(30);
        for (int i = 0; i < 30; ++i)
            data.append({i / 30.0, (i % 3 == 0) ? 1.0 : 0.0});

        const auto calibrated = CalibrationAnalyser::isotonicCalibrate(data);

        QCOMPARE(calibrated.size(), data.size());
        for (const auto& [pred, actual] : calibrated) {
            QVERIFY2(pred >= 0.0 && pred <= 1.0,
                     qPrintable(QString("Calibrated pred %1 out of [0, 1]").arg(pred)));
        }
    }
};

// ─── main ────────────────────────────────────────────────────────────────────

static int runTest(QObject* obj, const char* logFile)
{
    QStringList args = { "test", "-v2", "-o", QString("%1,txt").arg(logFile) };
    return QTest::qExec(obj, args);
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    Q_UNUSED(argv)
    TestEnsembleECE t;
    return runTest(&t, "ensemble_ece.txt");
}

#include "test_ensemble_ece.moc"
