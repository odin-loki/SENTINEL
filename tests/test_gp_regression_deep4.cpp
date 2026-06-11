// test_gp_regression_deep4.cpp
// Deep audit of GPRegression: SE kernel properties, Cholesky-based prediction,
// interpolation accuracy, non-negative variance, and unfitted (prior) behaviour.

#include <QTest>
#include <cmath>

#include "models/GPRegression.h"

class TestGPRegressionDeep4 : public QObject
{
    Q_OBJECT

private slots:
    void testSEKernelDiagonalEqualsSigma2();
    void testSEKernelFarPointsApproachZero();
    void testFitPredictInterpolatesTrainingPoint();
    void testPredictVarianceNonNegativeEverywhere();
    void testEmptyTrainingDataReturnsPrior();
};

// ─── Helpers ─────────────────────────────────────────────────────────────────

// Fit GP on a small noiseless-ish training set and return the fitted object.
static GPRegression makeSmallGP()
{
    GPRegression gp;
    gp.setKernelParams(1.0, 1.0, 1e-6);
    QVector<QPair<double,double>> X = {{0.0,0.0},{1.0,0.0},{0.0,1.0}};
    QVector<double>               y = {1.0, 2.0, 1.5};
    gp.fit(X, y);
    return gp;
}

// ─── Tests ───────────────────────────────────────────────────────────────────

// The SE kernel k(x,x) = σ² * exp(0) = σ².
// For an unfitted GP the predictive variance at any point equals the prior
// variance k(x*,x*) = σ², so predictWithUncertainty(·,·).second == sigma2.
void TestGPRegressionDeep4::testSEKernelDiagonalEqualsSigma2()
{
    GPRegression gp;
    const double sigma2 = 2.75;
    gp.setKernelParams(sigma2, 1.0, 1e-3);
    // No fit → prior
    auto [mean, var] = gp.predictWithUncertainty(3.0, 7.0);
    QCOMPARE(mean, 0.0);
    QVERIFY2(qAbs(var - sigma2) < 1e-9,
             qPrintable(QString("Prior variance expected %1 (sigma2), got %2")
                        .arg(sigma2).arg(var)));

    // Also at origin
    auto [m2, v2] = gp.predictWithUncertainty(0.0, 0.0);
    QVERIFY2(qAbs(v2 - sigma2) < 1e-9,
             qPrintable(QString("Prior variance at origin expected %1, got %2")
                        .arg(sigma2).arg(v2)));
}

// For far-apart points the SE kernel approaches 0.
// Consequence: after fitting a single training point at the origin, the GP
// posterior at a very distant test point should have mean ≈ 0 and
// variance ≈ σ² (i.e. the kernel term k_star ≈ 0 contributes nothing).
void TestGPRegressionDeep4::testSEKernelFarPointsApproachZero()
{
    GPRegression gp;
    const double sigma2 = 1.0;
    gp.setKernelParams(sigma2, 1.0, 1e-6);
    gp.fit({{0.0, 0.0}}, {10.0});   // training point at origin with large y

    // Predict 1 000 000 units away: k_star = exp(-1e12 / 2) ≈ 0
    auto [mean, var] = gp.predictWithUncertainty(1e6, 0.0);

    QVERIFY2(qAbs(mean) < 1e-4,
             qPrintable(QString("Far-point mean should be ≈ 0, got %1").arg(mean)));
    QVERIFY2(qAbs(var - sigma2) < 0.01,
             qPrintable(QString("Far-point variance should be ≈ sigma2 (%1), got %2")
                        .arg(sigma2).arg(var)));
}

// At a training point with near-zero noise, the posterior mean must reproduce
// the training target (interpolation) and the posterior variance must be ≈ 0.
void TestGPRegressionDeep4::testFitPredictInterpolatesTrainingPoint()
{
    GPRegression gp;
    gp.setKernelParams(1.0, 1.0, 1e-6);

    const double x1  = 2.0, x2 = 3.0;
    const double yTrain = 7.5;
    gp.fit({{x1, x2}}, {yTrain});

    // predict() — mean only
    const double meanScalar = gp.predict(x1, x2);
    QVERIFY2(qAbs(meanScalar - yTrain) < 0.01,
             qPrintable(QString("predict() at training point: expected ≈%1, got %2")
                        .arg(yTrain).arg(meanScalar)));

    // predictWithUncertainty() — mean + variance
    auto [mean, var] = gp.predictWithUncertainty(x1, x2);
    QVERIFY2(qAbs(mean - yTrain) < 0.01,
             qPrintable(QString("predictWithUncertainty() mean at training point: "
                                "expected ≈%1, got %2").arg(yTrain).arg(mean)));
    QVERIFY2(var < 0.01,
             qPrintable(QString("Variance at training point must be ≈0 "
                                "(noiseless), got %1").arg(var)));
}

// The posterior variance must be non-negative at every prediction site.
// (The code clamps with std::max(0.0, …) but we verify it holds in practice.)
void TestGPRegressionDeep4::testPredictVarianceNonNegativeEverywhere()
{
    GPRegression gp = makeSmallGP();
    QVERIFY(gp.isFitted());

    // Sweep a dense 2-D grid including points inside and outside the training hull
    for (double x = -3.0; x <= 3.0; x += 0.25) {
        for (double y = -3.0; y <= 3.0; y += 0.25) {
            auto [mean, var] = gp.predictWithUncertainty(x, y);
            QVERIFY2(var >= 0.0,
                     qPrintable(QString("Negative variance %1 at (%2, %3)")
                                .arg(var).arg(x).arg(y)));
        }
    }
}

// Before any call to fit(), or after fitting on an empty dataset,
// predict() must return 0 (prior mean) and predictWithUncertainty() must
// return {0, sigma2} (prior variance = k(x*,x*) = sigma2).
void TestGPRegressionDeep4::testEmptyTrainingDataReturnsPrior()
{
    const double sigma2 = 3.5;

    // Case A: no fit called at all
    {
        GPRegression gp;
        gp.setKernelParams(sigma2, 1.0, 1e-3);
        QVERIFY(!gp.isFitted());
        QCOMPARE(gp.predict(1.0, 2.0), 0.0);
        auto [m, v] = gp.predictWithUncertainty(1.0, 2.0);
        QCOMPARE(m, 0.0);
        QVERIFY2(qAbs(v - sigma2) < 1e-9,
                 qPrintable(QString("Prior variance expected %1, got %2")
                            .arg(sigma2).arg(v)));
    }

    // Case B: fit() called with empty data
    {
        GPRegression gp;
        gp.setKernelParams(sigma2, 1.0, 1e-3);
        gp.fit({}, {});
        QVERIFY(!gp.isFitted());
        QCOMPARE(gp.predict(5.0, 5.0), 0.0);
        auto [m, v] = gp.predictWithUncertainty(5.0, 5.0);
        QCOMPARE(m, 0.0);
        QVERIFY2(qAbs(v - sigma2) < 1e-9,
                 qPrintable(QString("Prior variance after empty fit expected %1, got %2")
                            .arg(sigma2).arg(v)));
    }
}

QTEST_GUILESS_MAIN(TestGPRegressionDeep4)
#include "test_gp_regression_deep4.moc"
