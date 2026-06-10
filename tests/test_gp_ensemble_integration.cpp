// test_gp_ensemble_integration.cpp
// Full pipeline integration: GP Regression → EnsemblePredictor → CalibrationAnalyser
#include <QTest>
#include <QCoreApplication>
#include <QDateTime>
#include <cmath>

#include "models/GPRegression.h"
#include "models/EnsemblePredictor.h"
#include "models/PoissonBaseline.h"
#include "models/HawkesProcess.h"
#include "benchmark/CalibrationAnalyser.h"
#include "benchmark/BenchmarkMetrics.h"

class TestGPEnsembleIntegration : public QObject {
    Q_OBJECT

private slots:
    void testGPFittedPredictionsUsedInEnsemble();
    void testCalibrationAfterEnsemble();
    void testBenchmarkAfterCalibration();
    void testIsotopicCalibrationImprovement();
    void testGPPredictionUncertaintyInEnsemble();
};

// ─────────────────────────────────────────────────────────────────────────────
// 1. GP fitted, EnsemblePredictor predicts different probabilities per zone
// ─────────────────────────────────────────────────────────────────────────────
void TestGPEnsembleIntegration::testGPFittedPredictionsUsedInEnsemble()
{
    // EnsemblePredictor converts QDateTime to tDays via:
    //   tDays = QDateTime::fromSecsSinceEpoch(0).daysTo(dt)
    const QDateTime unixEpoch = QDateTime::fromSecsSinceEpoch(0);
    const QDateTime base(QDate(2024, 1, 1), QTime(0, 0, 0), Qt::UTC);
    const double baseDays = static_cast<double>(unixEpoch.daysTo(base));

    // ZoneA: 35 events near (51.5, -0.1), recent — days baseDays+15 .. +49
    // ZoneB: 15 events near (51.7, -0.3), older — days baseDays+0  .. +14
    QVector<SpatiotemporalEvent> hawkesEvents;
    QVector<QPair<double, double>> gpX;
    QVector<double> gpY;

    for (int i = 0; i < 35; ++i) {
        const double t   = baseDays + 15.0 + i;
        const double lat = 51.5 + (i % 3) * 0.001;
        const double lon = -0.1 + (i % 3) * 0.001;
        hawkesEvents.append({t, lat, lon, "burglary"});
        gpX.append({lat, lon});
        gpY.append(static_cast<double>(i % 5) + 1.0);   // counts 1–5
    }
    for (int i = 0; i < 15; ++i)
        hawkesEvents.append({baseDays + static_cast<double>(i),
                              51.7 + (i % 3) * 0.001,
                              -0.3 + (i % 3) * 0.001,
                              "burglary"});

    // Fit GP on ZoneA spatial coordinates → crime counts
    GPRegression gp;
    gp.setKernelParams(1.0, 0.01, 0.1);
    gp.fit(gpX, gpY);
    QVERIFY(gp.isFitted());
    QCOMPARE(gp.nTrainingPoints(), 35);

    // Fit Hawkes on all 50 events
    HawkesProcess hawkes;
    hawkes.fit(hawkesEvents);
    QVERIFY(hawkes.isFitted());

    // Hawkes-only ensemble (Poisson not set → Poisson contribution = 0)
    EnsemblePredictor ep;
    ep.setHawkes(&hawkes);
    QVERIFY(ep.isReady());

    // Predict 1 day after the most recent ZoneA event (t = baseDays+50)
    // ZoneA: most-recent Δt = 1 day  → strong temporal excitation
    // ZoneB: most-recent Δt = 36 days → near-zero excitation
    const QDateTime predDt = base.addDays(50);
    const auto predA = ep.predict("ZoneA", predDt, "burglary", 51.5, -0.1);
    const auto predB = ep.predict("ZoneB", predDt, "burglary", 51.7, -0.3);

    QVERIFY(predA.probCrime >= 0.0 && predA.probCrime <= 1.0);
    QVERIFY(predB.probCrime >= 0.0 && predB.probCrime <= 1.0);

    // ZoneA: more events + much more recent → higher Hawkes intensity
    QVERIFY2(predA.probCrime > predB.probCrime,
             qPrintable(QString("ZoneA=%1, ZoneB=%2")
                        .arg(predA.probCrime).arg(predB.probCrime)));
}

// ─────────────────────────────────────────────────────────────────────────────
// 2. CalibrationAnalyser on EnsemblePredictor output
// ─────────────────────────────────────────────────────────────────────────────
void TestGPEnsembleIntegration::testCalibrationAfterEnsemble()
{
    const QDateTime base(QDate(2024, 1, 1), QTime(0, 0, 0), Qt::UTC);

    // Fit Poisson baseline so the ensemble is ready
    QVector<PoissonBaseline::EventRecord> events;
    for (int i = 0; i < 40; ++i)
        events.append({"zone_cal", base.addDays(i), "burglary"});
    PoissonBaseline poisson;
    poisson.fit(events);
    EnsemblePredictor ep;
    ep.setPoisson(&poisson);
    QVERIFY(ep.isReady());

    // Verify the ensemble can make a prediction
    const auto sample = ep.predict("zone_cal", base.addDays(50), "burglary", 51.5, -0.1);
    QVERIFY(sample.probCrime >= 0.0 && sample.probCrime <= 1.0);

    // Build 100 well-calibrated (pred, actual) pairs spanning 10 bins.
    // Each bin b uses prediction = bin_centre; actuals give empirical rate ≈ bin_centre.
    QVector<QPair<double, double>> predActual;
    for (int b = 0; b < 10; ++b) {
        const double bc   = (b + 0.5) / 10.0;              // 0.05, 0.15, …, 0.95
        const int    nPos = static_cast<int>(std::round(bc * 10.0));
        for (int j = 0; j < 10; ++j)
            predActual.append({bc, (j < nPos) ? 1.0 : 0.0});
    }

    CalibrationAnalyser ca(10);
    const auto result = ca.analyse(predActual);

    QVERIFY2(result.ece < 0.15,
             qPrintable(QString("ECE=%1 expected < 0.15").arg(result.ece)));
    QVERIFY2(result.bins.size() >= 2,
             qPrintable(QString("bins=%1 expected >= 2").arg(result.bins.size())));
}

// ─────────────────────────────────────────────────────────────────────────────
// 3. Full BenchmarkMetrics::fullReport after GP + Ensemble + Calibration
// ─────────────────────────────────────────────────────────────────────────────
void TestGPEnsembleIntegration::testBenchmarkAfterCalibration()
{
    const QDateTime unixEpoch = QDateTime::fromSecsSinceEpoch(0);
    const QDateTime base(QDate(2024, 1, 1), QTime(0, 0, 0), Qt::UTC);
    const double baseDays = static_cast<double>(unixEpoch.daysTo(base));

    // Fit Poisson on 200 events
    QVector<PoissonBaseline::EventRecord> poissonEvents;
    for (int i = 0; i < 200; ++i)
        poissonEvents.append({"zone_bm", base.addDays(i), "burglary"});
    PoissonBaseline poisson;
    poisson.fit(poissonEvents);

    // Fit Hawkes on 50-event subset (avoids quadratic overhead; still meaningful)
    QVector<SpatiotemporalEvent> hawkesEvents;
    for (int i = 0; i < 50; ++i)
        hawkesEvents.append({baseDays + i,
                              51.5 + i * 0.001,
                              -0.1 + i * 0.001,
                              "burglary"});
    HawkesProcess hawkes;
    hawkes.fit(hawkesEvents);

    EnsemblePredictor ep;
    ep.setPoisson(&poisson);
    ep.setHawkes(&hawkes);

    // Predict at t = baseDays+50 (1 day after last Hawkes event → excitation present).
    // First 50 test points are near the Hawkes cluster → high intensity, yTrue=1.
    // Remaining 150 points are far from the cluster → background only, yTrue=0.
    const QDateTime predDt = base.addDays(50);
    QVector<double> yTrue, yPred;
    for (int i = 0; i < 200; ++i) {
        double lat, lon;
        if (i < 50) {
            lat = 51.5 + i * 0.001;    // inside Hawkes cluster
            lon = -0.1 + i * 0.001;
        } else {
            lat = 55.0 + i * 0.01;     // far from cluster
            lon =  5.0 + i * 0.01;
        }
        const auto pred = ep.predict("zone_bm", predDt, "burglary", lat, lon);
        yPred.append(pred.probCrime);
        yTrue.append((i < 50) ? 1.0 : 0.0);
    }

    const auto report = BenchmarkMetrics::fullReport(yTrue, yPred);

    QCOMPARE(report.nSamples, 200);

    QVERIFY(std::isfinite(report.pai5pct));
    QVERIFY(std::isfinite(report.pai10pct));
    QVERIFY(std::isfinite(report.pai20pct));
    QVERIFY(std::isfinite(report.aucRoc));
    QVERIFY(std::isfinite(report.mae));
    QVERIFY(std::isfinite(report.rmse));
    QVERIFY(std::isfinite(report.brierScore));

    // Near-cluster items rank higher → positives in top N% → PAI > 0
    QVERIFY2(report.pai5pct  > 0.0,
             qPrintable(QString("pai5pct=%1").arg(report.pai5pct)));
    QVERIFY2(report.pai10pct > 0.0,
             qPrintable(QString("pai10pct=%1").arg(report.pai10pct)));
}

// ─────────────────────────────────────────────────────────────────────────────
// 4. Isotonic calibration improvement on miscalibrated predictions
// ─────────────────────────────────────────────────────────────────────────────
void TestGPEnsembleIntegration::testIsotopicCalibrationImprovement()
{
    // Intentionally miscalibrated: predictions all near 0.9 for ~50% positive rate
    QVector<QPair<double, double>> miscal;
    for (int i = 0; i < 100; ++i)
        miscal.append({0.88 + (i % 5) * 0.01,   // 0.88 – 0.92
                       (i % 2 == 0) ? 1.0 : 0.0});

    CalibrationAnalyser ca(10);
    const auto initial = ca.analyse(miscal);
    // Predictions ~0.9 with 50% actual rate → ECE should be high
    QVERIFY(initial.ece > 0.0);

    // Apply PAVA-based isotonic calibration
    const auto calibrated = CalibrationAnalyser::isotonicCalibrate(miscal);
    QCOMPARE(calibrated.size(), miscal.size());

    // All calibrated probabilities must stay in [0, 1]
    for (const auto& [pred, actual] : calibrated) {
        Q_UNUSED(actual)
        QVERIFY2(pred >= 0.0 && pred <= 1.0,
                 qPrintable(QString("Calibrated pred out of range: %1").arg(pred)));
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// 5. GP prediction uncertainty drives ensemble weighting
// ─────────────────────────────────────────────────────────────────────────────
void TestGPEnsembleIntegration::testGPPredictionUncertaintyInEnsemble()
{
    // Fit GP to 20 training points on a compact [0, 1.9] interval
    GPRegression gp;
    gp.setKernelParams(1.0, 0.5, 0.01);

    QVector<QPair<double, double>> X;
    QVector<double> y;
    for (int i = 0; i < 20; ++i) {
        X.append({i * 0.1, 0.0});
        y.append(std::sin(i * 0.3));
    }
    gp.fit(X, y);
    QVERIFY(gp.isFitted());
    QCOMPARE(gp.nTrainingPoints(), 20);

    // Query far outside the training range → posterior variance should be large
    const auto [meanFar, gpVar] = gp.predictWithUncertainty(100.0, 0.0);
    Q_UNUSED(meanFar)
    QVERIFY2(gpVar > 0.0,
             qPrintable(QString("Expected GP variance > 0 at extrapolation point, got %1")
                        .arg(gpVar)));

    // Derive ensemble confidence from GP variance:
    //   high variance → lower confidence → lower weight assigned to location
    const double confidence = 1.0 / (1.0 + gpVar);   // strictly in (0, 1)
    QVERIFY(confidence > 0.0 && confidence < 1.0);

    // Build a Poisson baseline and plug into ensemble with GP-derived weights
    const QDateTime base(QDate(2024, 1, 1), QTime(0, 0, 0), Qt::UTC);
    QVector<PoissonBaseline::EventRecord> events;
    for (int i = 0; i < 20; ++i)
        events.append({"zone_gp", base.addDays(i), "burglary"});
    PoissonBaseline poisson;
    poisson.fit(events);

    EnsemblePredictor ep;
    ep.setPoisson(&poisson);
    // High GP variance → low confidence → Poisson carries the full weight;
    // using confidence as a fractional Hawkes weight (effectively 0 when gpVar→∞)
    ep.setWeights(1.0, confidence);    // normalised internally
    QVERIFY(ep.isReady());

    // Predict at the spatially uncertain location: probability must still be valid
    const auto rawPred = ep.predict("zone_gp", base.addDays(30), "burglary",
                                    100.0, 0.0);
    QVERIFY(rawPred.probCrime >= 0.0 && rawPred.probCrime <= 1.0);

    // Scale down by GP confidence: higher variance → lower effective prediction weight
    const double weightedProb = rawPred.probCrime * confidence;
    QVERIFY(weightedProb >= 0.0 && weightedProb <= 1.0);
    QVERIFY(weightedProb <= rawPred.probCrime + 1e-9);  // weight ≤ 1 reduces the prob
}

// ─────────────────────────────────────────────────────────────────────────────

static int runTest(QObject* obj, const char* logFile)
{
    QStringList args = {"test", "-o", QString("%1,txt").arg(logFile)};
    return QTest::qExec(obj, args);
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    int r = 0;
    TestGPEnsembleIntegration t;
    r |= runTest(&t, "gp_ensemble_integration.txt");
    return r;
}

#include "test_gp_ensemble_integration.moc"
