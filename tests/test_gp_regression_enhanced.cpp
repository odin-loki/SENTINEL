// ─────────────────────────────────────────────────────────────────────────────
// TestGPRegressionEnhanced — enhanced GP regression tests
//
// Complements test_gp_regression.cpp (which covers basic construction,
// interpolation, noise/lengthscale variance, one-point edge case, etc.).
// These tests focus on statistical coverage, smoothness, extrapolation
// uncertainty, hyperparameter scaling, trend capture, and 2-point robustness.
// ─────────────────────────────────────────────────────────────────────────────
#include <QTest>
#include <QCoreApplication>
#include <cmath>
#include "models/GPRegression.h"

class TestGPRegressionEnhanced : public QObject {
    Q_OBJECT
private slots:
    void testPredictionIntervalCoverage();
    void testSmoothnessWithLengthscale();
    void testNoiseFit();
    void testExtrapolation();
    void testHyperparameterEffect();
    void testSequentialDataFit();
    void testMinimumPoints();
};

// ─── 1. Prediction interval coverage ─────────────────────────────────────────
// GP trained on noisy linear data.  Posterior ±2σ intervals should contain the
// noise-free true value at most tested interpolation points (≥ 6 of 8 = 75%).

void TestGPRegressionEnhanced::testPredictionIntervalCoverage()
{
    GPRegression gp;
    gp.setKernelParams(2.0, 2.0, 0.05);

    // 11 training points on y ≈ x with alternating ±0.1 perturbation
    QVector<QPair<double,double>> X;
    QVector<double> y;
    for (int i = 0; i <= 10; ++i) {
        const double xi = static_cast<double>(i);
        X.append({xi, 0.0});
        y.append(xi + (i % 2 == 0 ? 0.1 : -0.1));
    }
    gp.fit(X, y);
    QVERIFY(gp.isFitted());

    // 8 interior test points; true value (noise-free) = x
    int covered = 0;
    const int nTest = 8;
    for (int i = 0; i < nTest; ++i) {
        const double x = 0.7 + i * 1.1;          // 0.7, 1.8, 2.9, ..., 8.4
        const double trueVal = x;                  // noise-free ground truth
        const auto [mean, var] = gp.predictWithUncertainty(x, 0.0);
        const double twoSigma = 2.0 * std::sqrt(std::max(0.0, var));
        if (std::abs(trueVal - mean) <= twoSigma)
            ++covered;
    }

    // Conservative threshold: expect at least 75% coverage
    QVERIFY2(covered >= 6,
             qPrintable(QString("credible interval coverage = %1/%2 (expected >= 6)")
                        .arg(covered).arg(nTest)));
}

// ─── 2. Smoothness with lengthscale ──────────────────────────────────────────
// Larger lengthscale → smoother predictions.  Measured as total variation of
// predictions over a set of query points; large-l GP must vary less.

void TestGPRegressionEnhanced::testSmoothnessWithLengthscale()
{
    // Alternating 1/0 data deliberately chosen to amplify smoothness difference
    QVector<QPair<double,double>> X;
    QVector<double> y;
    for (int i = 0; i <= 10; ++i) {
        X.append({static_cast<double>(i), 0.0});
        y.append(i % 2 == 0 ? 1.0 : 0.0);
    }

    GPRegression gpSmall, gpLarge;
    gpSmall.setKernelParams(1.0, 0.3, 0.01);
    gpLarge.setKernelParams(1.0, 5.0, 0.01);
    gpSmall.fit(X, y);
    gpLarge.fit(X, y);

    // Total variation over 20 half-unit steps in [0, 10]
    double prevS = gpSmall.predict(0.0, 0.0);
    double prevL = gpLarge.predict(0.0, 0.0);
    double tvSmall = 0.0, tvLarge = 0.0;
    for (int i = 1; i <= 20; ++i) {
        const double x = i * 0.5;
        const double pS = gpSmall.predict(x, 0.0);
        const double pL = gpLarge.predict(x, 0.0);
        tvSmall += std::abs(pS - prevS);
        tvLarge += std::abs(pL - prevL);
        prevS = pS;
        prevL = pL;
    }

    QVERIFY2(tvLarge < tvSmall,
             qPrintable(QString("tvSmall=%1  tvLarge=%2 — large-l must be smoother")
                        .arg(tvSmall).arg(tvLarge)));
}

// ─── 3. Noise fit ─────────────────────────────────────────────────────────────
// High noise parameter → wider posterior uncertainty at midpoints between
// training points (complements test11 which tests at training points).

void TestGPRegressionEnhanced::testNoiseFit()
{
    QVector<QPair<double,double>> X = {{0,0},{1,0},{2,0},{3,0},{4,0}};
    QVector<double> y = {0.5, 1.0, 0.8, 1.2, 0.9};

    GPRegression gpLow, gpHigh;
    gpLow.setKernelParams(1.0, 1.0, 1e-6);
    gpHigh.setKernelParams(1.0, 1.0, 2.0);

    gpLow.fit(X, y);
    gpHigh.fit(X, y);

    // Test uncertainty at the midpoint between two training points
    const auto [mLow,  vLow]  = gpLow.predictWithUncertainty(0.5, 0.0);
    const auto [mHigh, vHigh] = gpHigh.predictWithUncertainty(0.5, 0.0);
    Q_UNUSED(mLow) Q_UNUSED(mHigh)

    QVERIFY2(vHigh > vLow,
             qPrintable(QString("vLow=%1  vHigh=%2 — high noise must widen midpoint interval")
                        .arg(vLow).arg(vHigh)));
}

// ─── 4. Extrapolation ─────────────────────────────────────────────────────────
// Predictions outside the training range should have higher posterior variance
// than interpolated predictions inside the training range.

void TestGPRegressionEnhanced::testExtrapolation()
{
    GPRegression gp;
    gp.setKernelParams(1.0, 1.0, 1e-4);

    // Training: x ∈ [0, 5]
    QVector<QPair<double,double>> X;
    QVector<double> y;
    for (int i = 0; i <= 5; ++i) {
        X.append({static_cast<double>(i), 0.0});
        y.append(i * 0.5);
    }
    gp.fit(X, y);
    QVERIFY(gp.isFitted());

    // Interpolation point well inside the training range
    const auto [mIn, vIn] = gp.predictWithUncertainty(2.5, 0.0);
    // Extrapolation point well outside
    const auto [mOut, vOut] = gp.predictWithUncertainty(15.0, 0.0);
    Q_UNUSED(mIn) Q_UNUSED(mOut)

    QVERIFY2(vOut > vIn,
             qPrintable(QString("vInterpolation=%1  vExtrapolation=%2 — extrapolation must be wider")
                        .arg(vIn).arg(vOut)));
}

// ─── 5. Hyperparameter effect ─────────────────────────────────────────────────
// A higher signal variance σ² increases the prior variance, which manifests as
// wider posterior intervals at far-extrapolation points.

void TestGPRegressionEnhanced::testHyperparameterEffect()
{
    QVector<QPair<double,double>> X = {{0,0},{1,0},{2,0}};
    QVector<double> y = {1.0, 2.0, 1.5};

    GPRegression gpLow, gpHigh;
    gpLow.setKernelParams(0.1, 1.0, 0.1);
    gpHigh.setKernelParams(10.0, 1.0, 0.1);

    gpLow.fit(X, y);
    gpHigh.fit(X, y);

    // At a far extrapolation point the kernel is ~0, so posterior variance ≈ σ²
    const auto [mLow, vLow]   = gpLow.predictWithUncertainty(20.0, 0.0);
    const auto [mHigh, vHigh] = gpHigh.predictWithUncertainty(20.0, 0.0);
    Q_UNUSED(mLow) Q_UNUSED(mHigh)

    QVERIFY2(vHigh > vLow,
             qPrintable(QString("vSigma2Low=%1  vSigma2High=%2 — larger σ² must give wider prior")
                        .arg(vLow).arg(vHigh)));
}

// ─── 6. Sequential data fit (trend capture) ───────────────────────────────────
// Model trained on a linearly increasing sequence y=0.5x should capture the
// trend: predict(4) ≈ 2.0 and predict(6) > predict(2).

void TestGPRegressionEnhanced::testSequentialDataFit()
{
    GPRegression gp;
    gp.setKernelParams(4.0, 2.0, 0.01);

    QVector<QPair<double,double>> X;
    QVector<double> y;
    for (int i = 0; i <= 8; ++i) {
        X.append({static_cast<double>(i), 0.0});
        y.append(i * 0.5);
    }
    gp.fit(X, y);
    QVERIFY(gp.isFitted());

    // Prediction at midpoint should be close to 0.5 × 4 = 2.0
    const double predMid = gp.predict(4.0, 0.0);
    QVERIFY2(std::abs(predMid - 2.0) < 0.5,
             qPrintable(QString("predict(4,0)=%1 expected≈2.0").arg(predMid)));

    // Trend direction: later x → higher y
    const double pred6 = gp.predict(6.0, 0.0);
    const double pred2 = gp.predict(2.0, 0.0);
    QVERIFY2(pred6 > pred2,
             qPrintable(QString("pred(6)=%1  pred(2)=%2 — trend should be increasing")
                        .arg(pred6).arg(pred2)));
}

// ─── 7. Minimum points (2 training points) ────────────────────────────────────
// A 2-point GP must not crash; predictions should be finite with non-negative
// variance, and uncertainty at a far extrapolation point must exceed the
// uncertainty at the midpoint (complements test09 which covers 1 point).

void TestGPRegressionEnhanced::testMinimumPoints()
{
    GPRegression gp;
    gp.setKernelParams(1.0, 1.0, 0.01);

    QVector<QPair<double,double>> X = {{0.0, 0.0}, {1.0, 0.0}};
    QVector<double> y = {0.0, 1.0};
    gp.fit(X, y);
    QVERIFY(gp.isFitted());
    QCOMPARE(gp.nTrainingPoints(), 2);

    // Basic sanity on mean prediction
    const double pred = gp.predict(0.5, 0.0);
    QVERIFY2(std::isfinite(pred),
             qPrintable(QString("predict(0.5,0)=%1 should be finite").arg(pred)));

    // Variance at midpoint
    const auto [mMid, vMid] = gp.predictWithUncertainty(0.5, 0.0);
    Q_UNUSED(mMid)
    QVERIFY2(std::isfinite(vMid) && vMid >= 0.0,
             qPrintable(QString("vMid=%1 should be finite and non-negative").arg(vMid)));

    // Far extrapolation must have greater uncertainty than midpoint
    const auto [mFar, vFar] = gp.predictWithUncertainty(100.0, 0.0);
    Q_UNUSED(mFar)
    QVERIFY2(vFar > vMid,
             qPrintable(QString("vMid=%1  vFar=%2 — far should have more uncertainty")
                        .arg(vMid).arg(vFar)));
}

// ─────────────────────────────────────────────────────────────────────────────

QTEST_MAIN(TestGPRegressionEnhanced)
#include "test_gp_regression_enhanced.moc"
