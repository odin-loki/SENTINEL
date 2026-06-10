// test_gp_regression_full.cpp — Comprehensive integration tests for GPRegression
#include <QTest>
#include <QCoreApplication>
#include <cmath>
#include "models/GPRegression.h"

class TestGPRegressionFull : public QObject {
    Q_OBJECT
private slots:
    void testFitEmpty();
    void testFitSinglePoint();
    void testFitFivePoints();
    void testPredictReturnsMean();
    void testPredictReturnsPositiveVariance();
    void testInterpolationBetweenPoints();
    void testExtrapolationRiskBounds();
    void testHyperparameterSigmaEffect();
    void testHyperparameterLengthscaleEffect();
    void testBulkPredictions();
    void testPredictionAtExactTrainingPoint();
    void testNoisyDataFit();
    void testAllForecastMetrics();
    void testFitLinearTrend();
    void testRiskSurfaceGrid();
};

// 1. fit with 0 points → isFitted() = false, predict returns 0 ────────────────
void TestGPRegressionFull::testFitEmpty()
{
    GPRegression gp;
    QVector<QPair<double,double>> X;
    QVector<double> y;
    gp.fit(X, y);
    QVERIFY(!gp.isFitted());
    QCOMPARE(gp.predict(1.0, 2.0), 0.0);
    const auto [mean, var] = gp.predictWithUncertainty(1.0, 2.0);
    Q_UNUSED(mean)
    QVERIFY(std::isfinite(var));
}

// 2. fit with 1 point → predict near that point returns value near its y ───────
void TestGPRegressionFull::testFitSinglePoint()
{
    GPRegression gp;
    gp.setKernelParams(1.0, 1.0, 1e-6);
    QVector<QPair<double,double>> X = {{2.0, 3.0}};
    QVector<double> y = {7.5};
    gp.fit(X, y);
    // With 1 point the fit may succeed — if so, prediction nearby should be close
    if (gp.isFitted()) {
        const double pred = gp.predict(2.0, 3.0);
        QVERIFY2(std::abs(pred - 7.5) < 1.5,
                 qPrintable(QString("pred=%1 expected≈7.5").arg(pred)));
    } else {
        // isFitted() = false is also acceptable for a single point
        QCOMPARE(gp.predict(2.0, 3.0), 0.0);
    }
}

// 3. fit 5 points, MAE at training inputs < 1.0 ────────────────────────────────
void TestGPRegressionFull::testFitFivePoints()
{
    GPRegression gp;
    gp.setKernelParams(1.0, 1.0, 1e-6);

    QVector<QPair<double,double>> X = {{0,0},{1,0},{2,0},{3,0},{4,0}};
    QVector<double> y = {1.0, 2.0, 0.5, 3.0, 1.5};
    gp.fit(X, y);
    QVERIFY(gp.isFitted());

    double mae = 0.0;
    for (int i = 0; i < X.size(); ++i) {
        const double pred = gp.predict(X[i].first, X[i].second);
        mae += std::abs(pred - y[i]);
    }
    mae /= X.size();
    QVERIFY2(mae < 1.0,
             qPrintable(QString("MAE=%1 exceeds 1.0").arg(mae)));
}

// 4. predict().mean is not NaN or Inf ──────────────────────────────────────────
void TestGPRegressionFull::testPredictReturnsMean()
{
    GPRegression gp;
    gp.setKernelParams(1.0, 1.0, 0.01);

    QVector<QPair<double,double>> X = {{0,0},{1,1},{2,0}};
    QVector<double> y = {0.0, 1.0, 0.5};
    gp.fit(X, y);
    QVERIFY(gp.isFitted());

    const auto [mean, var] = gp.predictWithUncertainty(0.5, 0.5);
    QVERIFY2(std::isfinite(mean),
             qPrintable(QString("mean=%1 is not finite").arg(mean)));
    Q_UNUSED(var)
}

// 5. variance >= 0 (not negative) ──────────────────────────────────────────────
void TestGPRegressionFull::testPredictReturnsPositiveVariance()
{
    GPRegression gp;
    gp.setKernelParams(1.0, 1.0, 0.01);

    QVector<QPair<double,double>> X = {{0,0},{1,0},{2,0},{3,0}};
    QVector<double> y = {1.0, 0.5, 2.0, 1.5};
    gp.fit(X, y);
    QVERIFY(gp.isFitted());

    for (double x = -2.0; x <= 5.0; x += 0.5) {
        const auto [mean, var] = gp.predictWithUncertainty(x, 0.0);
        Q_UNUSED(mean)
        QVERIFY2(var >= 0.0,
                 qPrintable(QString("var=%1 < 0 at x=%2").arg(var).arg(x)));
    }
}

// 6. predict at midpoint between two training points → intermediate value ───────
void TestGPRegressionFull::testInterpolationBetweenPoints()
{
    GPRegression gp;
    gp.setKernelParams(1.0, 2.0, 1e-6);

    QVector<QPair<double,double>> X = {{0,0},{2,0}};
    QVector<double> y = {0.0, 4.0};
    gp.fit(X, y);
    QVERIFY(gp.isFitted());

    const double pred = gp.predict(1.0, 0.0);
    // Midpoint should lie between the two values
    QVERIFY2(pred > 0.0 && pred < 4.0,
             qPrintable(QString("midpoint pred=%1 not in (0,4)").arg(pred)));
}

// 7. predict far outside training range returns some value (no crash) ───────────
void TestGPRegressionFull::testExtrapolationRiskBounds()
{
    GPRegression gp;
    gp.setKernelParams(1.0, 1.0, 0.01);

    QVector<QPair<double,double>> X = {{0,0},{1,0},{2,0}};
    QVector<double> y = {1.0, 2.0, 1.0};
    gp.fit(X, y);
    QVERIFY(gp.isFitted());

    // Far extrapolation should not crash and return a finite number
    const double pred = gp.predict(1000.0, 1000.0);
    QVERIFY2(std::isfinite(pred),
             qPrintable(QString("extrapolation pred=%1 is not finite").arg(pred)));
}

// 8. larger sigma → higher posterior variance at test point ────────────────────
void TestGPRegressionFull::testHyperparameterSigmaEffect()
{
    QVector<QPair<double,double>> X = {{0,0},{1,0},{2,0}};
    QVector<double> y = {1.0, 2.0, 1.5};

    GPRegression gpLow, gpHigh;
    gpLow.setKernelParams(0.1, 1.0, 1e-4);
    gpHigh.setKernelParams(10.0, 1.0, 1e-4);
    gpLow.fit(X, y);
    gpHigh.fit(X, y);

    const auto [mL, vL] = gpLow.predictWithUncertainty(5.0, 0.0);
    const auto [mH, vH] = gpHigh.predictWithUncertainty(5.0, 0.0);
    Q_UNUSED(mL) Q_UNUSED(mH)

    // Larger sigma2 = larger prior → larger posterior variance far from data
    QVERIFY2(vH >= vL,
             qPrintable(QString("vLow=%1 vHigh=%2, expected vHigh>=vLow").arg(vL).arg(vH)));
}

// 9. larger lengthscale → wider influence, lower variance at nearby unseen ──────
void TestGPRegressionFull::testHyperparameterLengthscaleEffect()
{
    QVector<QPair<double,double>> X = {{0,0},{4,0}};
    QVector<double> y = {1.0, 1.0};

    GPRegression gpSmall, gpLarge;
    gpSmall.setKernelParams(1.0, 0.1, 1e-6);
    gpLarge.setKernelParams(1.0, 10.0, 1e-6);
    gpSmall.fit(X, y);
    gpLarge.fit(X, y);

    // At x=2 (between training points), large l → training data strongly informs → low variance
    const auto [mS, vS] = gpSmall.predictWithUncertainty(2.0, 0.0);
    const auto [mL, vL] = gpLarge.predictWithUncertainty(2.0, 0.0);
    Q_UNUSED(mS) Q_UNUSED(mL)

    QVERIFY2(vL < vS,
             qPrintable(QString("vSmall=%1 vLarge=%2, expected vLarge<vSmall").arg(vS).arg(vL)));
}

// 10. fit 20 points, predict at 50 query points — no crashes ────────────────────
void TestGPRegressionFull::testBulkPredictions()
{
    GPRegression gp;
    gp.setKernelParams(1.0, 1.0, 0.01);

    QVector<QPair<double,double>> X;
    QVector<double> y;
    for (int i = 0; i < 20; ++i) {
        X.append({static_cast<double>(i) * 0.5, 0.0});
        y.append(std::sin(i * 0.5));
    }
    gp.fit(X, y);
    QVERIFY(gp.isFitted());

    // Predict at 50 query points
    int finiteCount = 0;
    for (int i = 0; i < 50; ++i) {
        const double qx = i * 0.2 - 2.0;
        const double pred = gp.predict(qx, 0.0);
        if (std::isfinite(pred)) ++finiteCount;
    }
    QCOMPARE(finiteCount, 50);
}

// 11. predict at exact training point → variance ≈ noise variance ───────────────
void TestGPRegressionFull::testPredictionAtExactTrainingPoint()
{
    const double noiseSigma2 = 0.01;
    GPRegression gp;
    gp.setKernelParams(1.0, 1.0, noiseSigma2);

    QVector<QPair<double,double>> X = {{0,0},{1,0},{2,0},{3,0},{4,0}};
    QVector<double> y = {0.0, 1.0, 0.5, 0.8, 0.3};
    gp.fit(X, y);
    QVERIFY(gp.isFitted());

    // At an exact training point the variance should be small (near noise level)
    const auto [mean, var] = gp.predictWithUncertainty(2.0, 0.0);
    Q_UNUSED(mean)
    // variance should be much less than the prior variance (1.0) and non-negative
    QVERIFY2(var >= 0.0, qPrintable(QString("var=%1 is negative").arg(var)));
    QVERIFY2(var < 0.5,
             qPrintable(QString("var=%1 at training point is too large").arg(var)));
}

// 12. noisy data — GP does not overfit (residual > 0) ───────────────────────────
void TestGPRegressionFull::testNoisyDataFit()
{
    GPRegression gp;
    // Use significant noise so GP does not interpolate exactly
    gp.setKernelParams(1.0, 1.0, 0.5);

    QVector<QPair<double,double>> X;
    QVector<double> y;
    // y = sin(x) + deterministic "noise"
    for (int i = 0; i < 10; ++i) {
        const double xi = i * 0.5;
        X.append({xi, 0.0});
        // Alternating +/- offset simulates noise
        y.append(std::sin(xi) + (i % 2 == 0 ? 0.3 : -0.3));
    }
    gp.fit(X, y);
    QVERIFY(gp.isFitted());

    // With noise regularisation the GP should NOT interpolate exactly
    double maxResidual = 0.0;
    for (int i = 0; i < X.size(); ++i) {
        const double pred = gp.predict(X[i].first, X[i].second);
        maxResidual = std::max(maxResidual, std::abs(pred - y[i]));
    }
    // At least one residual must be non-zero (not overfitting)
    QVERIFY2(maxResidual > 1e-6,
             qPrintable(QString("maxResidual=%1, GP appears to overfit").arg(maxResidual)));
}

// 13. verify returned struct from predictWithUncertainty has all expected fields ─
void TestGPRegressionFull::testAllForecastMetrics()
{
    GPRegression gp;
    gp.setKernelParams(1.0, 1.0, 0.01);

    QVector<QPair<double,double>> X = {{0,0},{1,0},{2,0}};
    QVector<double> y = {1.0, 2.0, 1.5};
    gp.fit(X, y);
    QVERIFY(gp.isFitted());

    const auto result = gp.predictWithUncertainty(1.0, 0.0);
    const double mean = result.first;
    const double var  = result.second;

    QVERIFY2(std::isfinite(mean), "mean is not finite");
    QVERIFY2(std::isfinite(var),  "variance is not finite");
    QVERIFY2(var >= 0.0,          "variance is negative");

    // logMarginalLikelihood should be finite after fit
    const double lml = gp.logMarginalLikelihood();
    QVERIFY2(std::isfinite(lml),
             qPrintable(QString("logMarginalLikelihood=%1").arg(lml)));

    // nTrainingPoints correct
    QCOMPARE(gp.nTrainingPoints(), 3);
}

// 14. data from y=x line → predictions approximately follow y=x ─────────────────
void TestGPRegressionFull::testFitLinearTrend()
{
    GPRegression gp;
    gp.setKernelParams(4.0, 2.0, 1e-6);

    QVector<QPair<double,double>> X;
    QVector<double> y;
    for (int i = 0; i <= 8; ++i) {
        X.append({static_cast<double>(i), 0.0});
        y.append(static_cast<double>(i));
    }
    gp.fit(X, y);
    QVERIFY(gp.isFitted());

    // Predictions at training points should closely match y=x
    double mae = 0.0;
    for (int i = 0; i <= 8; ++i) {
        const double pred = gp.predict(static_cast<double>(i), 0.0);
        mae += std::abs(pred - i);
    }
    mae /= 9.0;
    QVERIFY2(mae < 1.0,
             qPrintable(QString("Linear trend MAE=%1 > 1.0").arg(mae)));
}

// 15. riskGrid-equivalent: grid of predictWithUncertainty calls in correct layout ─
void TestGPRegressionFull::testRiskSurfaceGrid()
{
    GPRegression gp;
    gp.setKernelParams(1.0, 1.0, 0.1);

    QVector<QPair<double,double>> X = {{0,0},{1,1},{2,0},{0,2},{1,0}};
    QVector<double> y = {1.0, 2.0, 1.5, 0.5, 1.2};
    gp.fit(X, y);
    QVERIFY(gp.isFitted());

    // Build a 5×5 grid of predictions (simulating a risk surface)
    const int gridSize = 5;
    QVector<QVector<double>> grid(gridSize, QVector<double>(gridSize, 0.0));

    for (int row = 0; row < gridSize; ++row) {
        for (int col = 0; col < gridSize; ++col) {
            const double lat = row * 0.5;
            const double lon = col * 0.5;
            grid[row][col] = gp.predict(lat, lon);
            QVERIFY2(std::isfinite(grid[row][col]),
                     qPrintable(QString("grid[%1][%2]=%3 not finite")
                                .arg(row).arg(col).arg(grid[row][col])));
        }
    }

    QCOMPARE(grid.size(), gridSize);
    QCOMPARE(grid[0].size(), gridSize);
}

// ─────────────────────────────────────────────────────────────────────────────

static int runTest(QObject* obj, const char* logFile)
{
    QStringList args = { "test", "-o", QString("%1,txt").arg(logFile) };
    return QTest::qExec(obj, args);
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    int r = 0;
    { TestGPRegressionFull t; r |= runTest(&t, "gp_regression_full.txt"); }
    return r;
}
#include "test_gp_regression_full.moc"
