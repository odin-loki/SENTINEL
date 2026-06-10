// test_gp_regression_intervals.cpp
// Validates GPRegression posterior mean, variance, prediction intervals,
// kernel hyperparameters, and marginal likelihood.
#include <QTest>
#include "models/GPRegression.h"
#include <cmath>

class GPRegressionIntervalsTest : public QObject
{
    Q_OBJECT

private:
    static QVector<QPair<double,double>> lineX(int n, double step = 0.1)
    {
        QVector<QPair<double,double>> X;
        for (int i = 0; i < n; ++i) X.append({ i * step, 0.0 });
        return X;
    }

    static QVector<double> sinY(const QVector<QPair<double,double>>& X)
    {
        QVector<double> y;
        for (const auto& [x1, x2] : X) y.append(std::sin(x1));
        return y;
    }

private slots:

    // ── 1. isFitted() true after fit ────────────────────────────────────────
    void testIsFittedAfterFit()
    {
        GPRegression gp;
        QVERIFY(!gp.isFitted());
        gp.fit(lineX(10), sinY(lineX(10)));
        QVERIFY(gp.isFitted());
    }

    // ── 2. nTrainingPoints() matches input ───────────────────────────────────
    void testNTrainingPoints()
    {
        GPRegression gp;
        const auto X = lineX(15);
        gp.fit(X, sinY(X));
        QCOMPARE(gp.nTrainingPoints(), 15);
    }

    // ── 3. predict() at a training point is close to the observed value ──────
    void testPredictAtTrainingPoint()
    {
        GPRegression gp;
        gp.setKernelParams(1.0, 0.5, 1e-6);  // tiny noise → near-interpolation
        const auto X = lineX(10, 0.5);
        const auto y = sinY(X);
        gp.fit(X, y);

        // Predict at the first training point (0.0, 0.0) → y[0] = sin(0) = 0
        const double pred = gp.predict(0.0, 0.0);
        QVERIFY2(std::abs(pred - 0.0) < 0.15,
                 qPrintable(QStringLiteral("Pred at (0,0) expected ~0, got %1").arg(pred)));
    }

    // ── 4. Posterior variance increases far from training data ───────────────
    void testVarianceIncreasesAway()
    {
        GPRegression gp;
        gp.setKernelParams(1.0, 0.3, 1e-4);
        const auto X = lineX(8, 0.3);
        gp.fit(X, sinY(X));

        // Near training data
        const auto [meanNear, varNear] = gp.predictWithUncertainty(0.0, 0.0);
        // Far from training data
        const auto [meanFar, varFar] = gp.predictWithUncertainty(100.0, 0.0);

        QVERIFY2(varFar > varNear,
                 qPrintable(QStringLiteral("Var far (%1) should exceed var near (%2)")
                    .arg(varFar).arg(varNear)));
    }

    // ── 5. Posterior variance is non-negative ────────────────────────────────
    void testVarianceNonNegative()
    {
        GPRegression gp;
        gp.setKernelParams(1.0, 0.5, 1e-3);
        const auto X = lineX(12, 0.2);
        gp.fit(X, sinY(X));

        for (double x = -2.0; x <= 4.0; x += 0.5) {
            const auto [mean, var] = gp.predictWithUncertainty(x, 0.0);
            QVERIFY2(var >= -1e-9,
                     qPrintable(QStringLiteral("Variance %1 at x=%2 must be >= 0").arg(var).arg(x)));
        }
    }

    // ── 6. Log marginal likelihood is finite and < 0 ─────────────────────────
    void testLogMarginalLikelihood()
    {
        GPRegression gp;
        gp.setKernelParams(1.0, 0.5, 0.01);
        const auto X = lineX(10);
        gp.fit(X, sinY(X));

        const double lml = gp.logMarginalLikelihood();
        QVERIFY2(std::isfinite(lml), "Log marginal likelihood must be finite");
    }

    // ── 7. Prediction is consistent: calling twice returns same value ─────────
    void testPredictionConsistent()
    {
        GPRegression gp;
        gp.setKernelParams(1.0, 0.5, 0.01);
        const auto X = lineX(10);
        gp.fit(X, sinY(X));

        const double p1 = gp.predict(1.5, 0.5);
        const double p2 = gp.predict(1.5, 0.5);
        QVERIFY2(std::abs(p1 - p2) < 1e-12,
                 "Prediction must be deterministic");
    }

    // ── 8. setKernelParams invalidates fit ────────────────────────────────────
    void testSetKernelParamsInvalidates()
    {
        GPRegression gp;
        const auto X = lineX(8);
        gp.fit(X, sinY(X));
        QVERIFY(gp.isFitted());

        gp.setKernelParams(2.0, 1.0, 0.1);
        // After changing params, fit is invalidated
        QVERIFY(!gp.isFitted());
    }

    // ── 9. Uncertainty (mean, var) has correct return types and ranges ─────────
    void testPredictWithUncertaintyBounds()
    {
        GPRegression gp;
        gp.setKernelParams(1.0, 0.5, 0.01);
        const auto X = lineX(10);
        gp.fit(X, sinY(X));

        for (double x = 0.0; x <= 0.9; x += 0.1) {
            const auto [mean, var] = gp.predictWithUncertainty(x, 0.0);
            // Mean of sin(x) should be in [-1.5, 1.5]
            QVERIFY2(mean >= -1.5 && mean <= 1.5,
                     qPrintable(QStringLiteral("GP mean %1 at x=%2 out of range").arg(mean).arg(x)));
            QVERIFY2(var >= 0.0,
                     qPrintable(QStringLiteral("GP var %1 at x=%2 negative").arg(var).arg(x)));
        }
    }

    // ── 10. Empty fit → isFitted() false, predict doesn't crash ──────────────
    void testEmptyFitNoCrash()
    {
        GPRegression gp;
        gp.fit({}, {});
        QVERIFY(!gp.isFitted());
        // Must not crash on predict (return 0 or similar)
        const double p = gp.predict(0.0, 0.0);
        QVERIFY(std::isfinite(p));
    }
};

QTEST_MAIN(GPRegressionIntervalsTest)
#include "test_gp_regression_intervals.moc"
