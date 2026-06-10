// test_gp_regression_deep2.cpp — comprehensive GPRegression unit tests
#include <QTest>
#include <QCoreApplication>
#include <cmath>
#include <numbers>

#include "models/GPRegression.h"

// ─────────────────────────────────────────────────────────────────────────────
// TestGPRegressionDeep2
// ─────────────────────────────────────────────────────────────────────────────

class TestGPRegressionDeep2 : public QObject
{
    Q_OBJECT

private slots:

    // 1. Fit on 10 sinusoidal points, predict at a new point — mean is finite ──
    void testFitPredict()
    {
        GPRegression gp;
        gp.setKernelParams(1.0, 1.0, 1e-4);

        QVector<QPair<double,double>> X;
        QVector<double> y;
        for (int i = 0; i < 10; ++i) {
            const double t = i * (2.0 * std::numbers::pi_v<double> / 10.0);
            X.append({t, 0.0});
            y.append(std::sin(t));
        }
        gp.fit(X, y);
        QVERIFY(gp.isFitted());

        // Predict at a point not in the training set
        const double testT = std::numbers::pi_v<double> / 3.0;
        const double pred  = gp.predict(testT, 0.0);

        QVERIFY2(std::isfinite(pred),
                 qPrintable(QString("predict(pi/3, 0)=%1 is not finite").arg(pred)));

        // The prediction should be plausible (sin(pi/3) ≈ 0.866)
        QVERIFY2(std::abs(pred) <= 2.0,
                 qPrintable(QString("predict(pi/3, 0)=%1 is unreasonably large").arg(pred)));
    }

    // 2. Smooth interpolation — predictions at intermediate points track the function
    void testSmoothInterpolation()
    {
        GPRegression gp;
        gp.setKernelParams(1.0, 1.5, 1e-5);

        // Train on a simple linear function f(x) = x/5.0
        QVector<QPair<double,double>> X;
        QVector<double> y;
        for (int i = 0; i <= 10; ++i) {
            X.append({static_cast<double>(i), 0.0});
            y.append(i / 5.0);
        }
        gp.fit(X, y);
        QVERIFY(gp.isFitted());

        // Midpoint between x=4 and x=6 is x=5: f(5)=1.0
        const double pred5 = gp.predict(5.0, 0.0);
        // Predictions should stay "close" to the linear trend in interpolation region
        QVERIFY2(std::abs(pred5 - 1.0) < 0.5,
                 qPrintable(QString("predict(5,0)=%1 expected≈1.0").arg(pred5)));

        // pred at x=3 should be between pred at x=0 and pred at x=10
        // (monotone interpolation)
        const double predLow  = gp.predict(0.0, 0.0);
        const double predHigh = gp.predict(10.0, 0.0);
        const double predMid  = gp.predict(5.0, 0.0);
        QVERIFY2(predMid >= predLow - 0.5 && predMid <= predHigh + 0.5,
                 qPrintable(QString("predMid=%1 not between predLow=%2 and predHigh=%3")
                            .arg(predMid).arg(predLow).arg(predHigh)));
    }

    // 3. Variance decreases at training points ────────────────────────────────
    void testVarianceDecreasesAtTrainingPoints()
    {
        GPRegression gp;
        gp.setKernelParams(1.0, 1.0, 1e-6);

        QVector<QPair<double,double>> X = {{0.0, 0.0}, {2.0, 0.0}, {4.0, 0.0}};
        QVector<double> y = {1.0, 2.0, 1.5};
        gp.fit(X, y);

        // Variance at training points (low noise → should be very small)
        const auto [mAt0, vAt0] = gp.predictWithUncertainty(0.0, 0.0);
        const auto [mAt2, vAt2] = gp.predictWithUncertainty(2.0, 0.0);
        const auto [mAt4, vAt4] = gp.predictWithUncertainty(4.0, 0.0);

        // Variance far from training data
        const auto [mFar, vFar] = gp.predictWithUncertainty(100.0, 0.0);

        Q_UNUSED(mAt0) Q_UNUSED(mAt2) Q_UNUSED(mAt4) Q_UNUSED(mFar)

        QVERIFY2(vAt0 < vFar,
                 qPrintable(QString("vAt0=%1 should be < vFar=%2").arg(vAt0).arg(vFar)));
        QVERIFY2(vAt2 < vFar,
                 qPrintable(QString("vAt2=%1 should be < vFar=%2").arg(vAt2).arg(vFar)));
        QVERIFY2(vAt4 < vFar,
                 qPrintable(QString("vAt4=%1 should be < vFar=%2").arg(vAt4).arg(vFar)));
    }

    // 4. Lengthscale effect — short vs long lengthscale affects interpolation variance
    void testHyperparameterLengthscaleEffect()
    {
        // Training points at x=0 and x=4; query at x=2 (midpoint)
        QVector<QPair<double,double>> X = {{0.0, 0.0}, {4.0, 0.0}};
        QVector<double> y = {1.0, 1.0};

        // Short lengthscale: training points at 0 and 4 barely influence x=2
        GPRegression gpShort;
        gpShort.setKernelParams(1.0, 0.1, 1e-5);
        gpShort.fit(X, y);

        // Long lengthscale: training points strongly inform x=2
        GPRegression gpLong;
        gpLong.setKernelParams(1.0, 10.0, 1e-5);
        gpLong.fit(X, y);

        const auto [mS, vS] = gpShort.predictWithUncertainty(2.0, 0.0);
        const auto [mL, vL] = gpLong.predictWithUncertainty(2.0, 0.0);
        Q_UNUSED(mS) Q_UNUSED(mL)

        // Long lengthscale → more information at midpoint → lower variance
        QVERIFY2(vL < vS,
                 qPrintable(QString("vShort=%1 vLong=%2: expected vLong < vShort")
                            .arg(vS).arg(vL)));
    }

    // 5. Log marginal likelihood — finite after fitting and meaningful ─────────
    void testNegativeLogLikelihood()
    {
        QVector<QPair<double,double>> X;
        QVector<double> y;
        for (int i = 0; i < 10; ++i) {
            X.append({static_cast<double>(i), 0.0});
            y.append(std::sin(i * 0.5));
        }

        // Fit with well-tuned params
        GPRegression gpGood;
        gpGood.setKernelParams(1.0, 2.0, 0.01);
        gpGood.fit(X, y);
        const double lmlGood = gpGood.logMarginalLikelihood();

        // Fit with very poor params (extremely short lengthscale)
        GPRegression gpBad;
        gpBad.setKernelParams(1.0, 0.001, 100.0);
        gpBad.fit(X, y);
        const double lmlBad = gpBad.logMarginalLikelihood();

        QVERIFY2(std::isfinite(lmlGood),
                 qPrintable(QString("lmlGood=%1 not finite").arg(lmlGood)));
        QVERIFY2(std::isfinite(lmlBad),
                 qPrintable(QString("lmlBad=%1 not finite").arg(lmlBad)));

        // Well-tuned params should yield higher (less negative) log marginal likelihood
        QVERIFY2(lmlGood >= lmlBad,
                 qPrintable(QString("lmlGood=%1 < lmlBad=%2: expected better-tuned params to score higher")
                            .arg(lmlGood).arg(lmlBad)));
    }

    // 6. Empty fit — no crash, isFitted() = false, predict() returns 0 ────────
    void testEmptyFitNoCrash()
    {
        GPRegression gp;
        gp.fit({}, {});

        QVERIFY(!gp.isFitted());
        QCOMPARE(gp.nTrainingPoints(), 0);

        // predict() returns 0 when not fitted
        QCOMPARE(gp.predict(0.0, 0.0), 0.0);
        QCOMPARE(gp.predict(5.0, 3.0), 0.0);

        // predictWithUncertainty() returns prior variance (m_sigma2 = 1.0)
        const auto [m, v] = gp.predictWithUncertainty(1.0, 1.0);
        Q_UNUSED(m)
        QVERIFY2(v > 0.0, qPrintable(QString("v=%1 should be > 0 for prior").arg(v)));

        // logMarginalLikelihood() returns 0 when not fitted
        QCOMPARE(gp.logMarginalLikelihood(), 0.0);
    }

    // 7. Single training point — fit+predict ──────────────────────────────────
    void testSinglePoint()
    {
        GPRegression gp;
        gp.setKernelParams(1.0, 1.0, 1e-5);

        QVector<QPair<double,double>> X = {{3.0, 2.0}};
        QVector<double> y = {7.5};
        gp.fit(X, y);

        QVERIFY(gp.isFitted());
        QCOMPARE(gp.nTrainingPoints(), 1);

        // Prediction at the training point should be close to 7.5
        const double pred = gp.predict(3.0, 2.0);
        QVERIFY2(std::abs(pred - 7.5) < 1.0,
                 qPrintable(QString("predict(3,2)=%1 expected≈7.5").arg(pred)));

        // Variance at training point should be very small (low noise)
        const auto [m, v] = gp.predictWithUncertainty(3.0, 2.0);
        Q_UNUSED(m)
        QVERIFY2(v >= 0.0, qPrintable(QString("variance=%1").arg(v)));

        // Variance far away should be larger (approaching prior)
        const auto [mFar, vFar] = gp.predictWithUncertainty(1000.0, 0.0);
        Q_UNUSED(mFar)
        QVERIFY2(vFar > v, qPrintable(
            QString("vFar=%1 not > vAt=%2").arg(vFar).arg(v)));
    }

    // 8. Multi-dimensional input (lat, lon) — predictions are finite ───────────
    void testMultiDimensional()
    {
        GPRegression gp;
        // Lengthscale tuned for lat/lon degree scale
        gp.setKernelParams(1.0, 0.05, 0.01);

        // Grid of training points (lat, lon) with crime count as y
        QVector<QPair<double,double>> X;
        QVector<double> y;
        for (int i = 0; i < 4; ++i) {
            for (int j = 0; j < 4; ++j) {
                X.append({51.4 + i * 0.05, -0.2 + j * 0.05});
                y.append(static_cast<double>((i + j) % 5));
            }
        }
        gp.fit(X, y);
        QVERIFY(gp.isFitted());
        QCOMPARE(gp.nTrainingPoints(), 16);

        // Predict at several grid points and off-grid points
        const QList<QPair<double,double>> testPts = {
            {51.425, -0.125},    // interior, off-grid
            {51.40,  -0.20},     // training point
            {51.60,   0.10},     // outside training region
        };
        for (const auto& [lat, lon] : testPts) {
            const double pred = gp.predict(lat, lon);
            QVERIFY2(std::isfinite(pred),
                     qPrintable(QString("predict(%1,%2)=%3 is not finite")
                                .arg(lat).arg(lon).arg(pred)));

            const auto [m, v] = gp.predictWithUncertainty(lat, lon);
            Q_UNUSED(m)
            QVERIFY2(std::isfinite(v) && v >= 0.0,
                     qPrintable(QString("variance=%1 at (%2,%3)").arg(v).arg(lat).arg(lon)));
        }
    }

    // 9. All predicted means are finite ───────────────────────────────────────
    void testPredictionMeanFinite()
    {
        GPRegression gp;
        gp.setKernelParams(2.0, 1.5, 0.05);

        // Deterministic pseudo-random training data
        QVector<QPair<double,double>> X;
        QVector<double> y;
        for (int i = 0; i < 20; ++i) {
            X.append({(i * 7 % 11) * 0.3, (i * 3 % 7) * 0.4});
            y.append(std::sin(i * 0.3) * 2.0);
        }
        gp.fit(X, y);
        QVERIFY(gp.isFitted());

        // Predict at a range of points including far extrapolation
        for (int i = -5; i <= 15; ++i) {
            for (int j = -3; j <= 10; ++j) {
                const double pred = gp.predict(i * 0.5, j * 0.5);
                QVERIFY2(std::isfinite(pred),
                         qPrintable(QString("predict(%1,%2)=%3 not finite")
                                    .arg(i * 0.5).arg(j * 0.5).arg(pred)));
            }
        }
    }

    // 10. All predicted variances are positive (>= 0) ─────────────────────────
    void testPredictionVariancePositive()
    {
        GPRegression gp;
        gp.setKernelParams(1.0, 1.0, 0.05);

        QVector<QPair<double,double>> X;
        QVector<double> y;
        for (int i = 0; i < 8; ++i) {
            X.append({static_cast<double>(i), static_cast<double>(i % 3)});
            y.append(i * 0.25);
        }
        gp.fit(X, y);
        QVERIFY(gp.isFitted());

        // Check variance at many test points, including training points and far points
        const QList<QPair<double,double>> testPts = {
            {0.0, 0.0}, {1.0, 1.0}, {3.5, 0.5}, {7.0, 2.0},   // near training
            {-10.0, -10.0}, {50.0, 50.0}, {0.0, 100.0},         // far extrapolation
            {2.5, 1.5}, {4.0, 0.0}, {6.0, 2.0},                  // interpolation
        };

        for (const auto& [x1, x2] : testPts) {
            const auto [m, v] = gp.predictWithUncertainty(x1, x2);
            Q_UNUSED(m)
            QVERIFY2(v >= 0.0,
                     qPrintable(QString("variance=%1 at (%2,%3) is negative")
                                .arg(v).arg(x1).arg(x2)));
            QVERIFY2(std::isfinite(v),
                     qPrintable(QString("variance=%1 at (%2,%3) is not finite")
                                .arg(v).arg(x1).arg(x2)));
        }
    }
};

QTEST_MAIN(TestGPRegressionDeep2)
#include "test_gp_regression_deep2.moc"
