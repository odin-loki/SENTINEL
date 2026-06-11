// test_gp_regression_deep5.cpp — Deep audit iteration 16: GPRegression
// predict variance bounds, empty-fit prior behaviour, and kernel hyperparameters.

#include <QTest>
#include <cmath>

#include "models/GPRegression.h"

class TestGPRegressionDeep5 : public QObject
{
    Q_OBJECT

private slots:
    void testPosteriorVarianceNeverExceedsPrior();
    void testTrainingPointVarianceBoundedByNoiseFloor();
    void testEmptyFitReturnsZeroLogMarginalLikelihood();
    void testSetKernelParamsInvalidatesFit();
    void testVarianceRisesBetweenTrainingPoints();
    void testLengthscaleControlsSpatialDecay();
    void testPriorVarianceScalesWithSigma2();
    void testNTtrainingPointsTracksFitSize();
};

// ─── Helpers ─────────────────────────────────────────────────────────────────

static GPRegression fitLineGP(double noise = 1e-4)
{
    GPRegression gp;
    gp.setKernelParams(1.0, 1.0, noise);
    QVector<QPair<double, double>> X = {{0.0, 0.0}, {1.0, 0.0}, {2.0, 0.0}};
    QVector<double> y = {0.0, 1.0, 0.0};
    gp.fit(X, y);
    return gp;
}

// ─── Tests ───────────────────────────────────────────────────────────────────

// Posterior variance σ*² = k(x*,x*) − vᵀv ≤ k(x*,x*) = σ² for any test point.
void TestGPRegressionDeep5::testPosteriorVarianceNeverExceedsPrior()
{
    const double sigma2 = 2.0;
    GPRegression gp = fitLineGP();
    gp.setKernelParams(sigma2, 1.0, 1e-4);
    gp.fit({{0.0, 0.0}, {1.0, 1.0}}, {1.0, -1.0});
    QVERIFY(gp.isFitted());

    for (double x = -2.0; x <= 3.0; x += 0.5) {
        for (double y = -2.0; y <= 3.0; y += 0.5) {
            const auto [mean, var] = gp.predictWithUncertainty(x, y);
            Q_UNUSED(mean)
            QVERIFY2(var <= sigma2 + 1e-9,
                     qPrintable(QString("Variance %1 at (%2,%3) exceeds prior σ²=%4")
                                    .arg(var).arg(x).arg(y).arg(sigma2)));
        }
    }
}

// With additive noise σ_n², posterior mean at a training point is
//   μ* = k(x,x) / (k(x,x) + σ_n²) · y  (not exactly y when σ_n² > 0).
// Posterior variance should sit near the noise floor.
void TestGPRegressionDeep5::testTrainingPointVarianceBoundedByNoiseFloor()
{
    const double noise  = 0.05;
    const double sigma2 = 1.0;
    const double yTrain = 3.0;

    GPRegression gp;
    gp.setKernelParams(sigma2, 1.0, noise);
    gp.fit({{0.0, 0.0}}, {yTrain});

    const double expectedMean = yTrain * sigma2 / (sigma2 + noise);
    const auto [mean, var] = gp.predictWithUncertainty(0.0, 0.0);

    QVERIFY2(qAbs(mean - expectedMean) < 1e-6,
             qPrintable(QString("Mean at training point expected %1, got %2")
                            .arg(expectedMean).arg(mean)));
    QVERIFY2(var >= noise * 0.5,
             qPrintable(QString("Variance %1 should respect noise floor ≈%2")
                            .arg(var).arg(noise)));
    QVERIFY2(var <= noise * 3.0,
             qPrintable(QString("Variance %1 should not greatly exceed noise %2")
                            .arg(var).arg(noise)));
}

// logMarginalLikelihood() is defined only after fit(); unfitted / empty-fit → 0.
void TestGPRegressionDeep5::testEmptyFitReturnsZeroLogMarginalLikelihood()
{
    GPRegression gp;
    QCOMPARE(gp.logMarginalLikelihood(), 0.0);

    gp.setKernelParams(1.0, 1.0, 1e-3);
    gp.fit({}, {});
    QVERIFY(!gp.isFitted());
    QCOMPARE(gp.logMarginalLikelihood(), 0.0);
}

// Changing hyperparameters must invalidate the cached Cholesky factor.
void TestGPRegressionDeep5::testSetKernelParamsInvalidatesFit()
{
    GPRegression gp = fitLineGP();
    QVERIFY(gp.isFitted());
    QCOMPARE(gp.nTrainingPoints(), 3);

    gp.setKernelParams(2.0, 0.5, 1e-3);
    QVERIFY(!gp.isFitted());
    QCOMPARE(gp.predict(1.0, 0.0), 0.0);

    const auto [mean, var] = gp.predictWithUncertainty(1.0, 0.0);
    QCOMPARE(mean, 0.0);
    QVERIFY2(qAbs(var - 2.0) < 1e-9,
             qPrintable(QString("Prior variance after param change expected 2.0, got %1")
                            .arg(var)));
}

// Between two training points the GP is less certain than at the points themselves
// (posterior variance is elevated in the interior of the convex hull).
void TestGPRegressionDeep5::testVarianceRisesBetweenTrainingPoints()
{
    GPRegression gp;
    gp.setKernelParams(1.0, 1.0, 1e-6);
    gp.fit({{0.0, 0.0}, {2.0, 0.0}}, {1.0, 1.0});

    const auto [_, varTrain] = gp.predictWithUncertainty(0.0, 0.0);
    const auto [__, varMid]   = gp.predictWithUncertainty(1.0, 0.0);

    QVERIFY2(varMid > varTrain,
             qPrintable(QString("Midpoint variance (%1) should exceed training-point "
                                "variance (%2)").arg(varMid).arg(varTrain)));
}

// Smaller lengthscale → faster spatial decay of the posterior mean away from data.
void TestGPRegressionDeep5::testLengthscaleControlsSpatialDecay()
{
    GPRegression gpShort;
    gpShort.setKernelParams(1.0, 0.3, 1e-6);
    gpShort.fit({{0.0, 0.0}}, {5.0});

    GPRegression gpLong;
    gpLong.setKernelParams(1.0, 2.0, 1e-6);
    gpLong.fit({{0.0, 0.0}}, {5.0});

    const double meanShort = gpShort.predict(1.0, 0.0);
    const double meanLong  = gpLong.predict(1.0, 0.0);

    QVERIFY2(meanShort < meanLong,
             qPrintable(QString("Short lengthscale mean (%1) should decay faster "
                                "than long (%2) at unit distance")
                            .arg(meanShort).arg(meanLong)));
    QVERIFY2(meanShort > 0.0 && meanLong > 0.0,
             "Both means should remain positive one unit from the training point");
}

// Prior predictive variance k(x*,x*) = σ² scales linearly with the signal variance.
void TestGPRegressionDeep5::testPriorVarianceScalesWithSigma2()
{
    for (double sigma2 : {0.5, 1.0, 4.0}) {
        GPRegression gp;
        gp.setKernelParams(sigma2, 1.0, 1e-3);
        const auto [mean, var] = gp.predictWithUncertainty(1.5, -2.0);
        QCOMPARE(mean, 0.0);
        QVERIFY2(qAbs(var - sigma2) < 1e-9,
                 qPrintable(QString("Prior variance for σ²=%1 expected %1, got %2")
                                .arg(sigma2).arg(var)));
    }
}

void TestGPRegressionDeep5::testNTtrainingPointsTracksFitSize()
{
    GPRegression gp;
    QCOMPARE(gp.nTrainingPoints(), 0);

    gp.fit({{0.0, 0.0}}, {1.0});
    QCOMPARE(gp.nTrainingPoints(), 1);

    QVector<QPair<double, double>> X;
    QVector<double> y;
    for (int i = 0; i < 7; ++i) {
        X.append({static_cast<double>(i), 0.0});
        y.append(static_cast<double>(i));
    }
    gp.fit(X, y);
    QCOMPARE(gp.nTrainingPoints(), 7);
}

QTEST_GUILESS_MAIN(TestGPRegressionDeep5)
#include "test_gp_regression_deep5.moc"
