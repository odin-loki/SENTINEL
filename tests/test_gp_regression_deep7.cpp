// test_gp_regression_deep7.cpp — Deep audit iteration 22: GPRegression
// Verifies: single-point fit, unfitted predict, lengthscale clamp, variance bounds,
// batch predict parity, hyperparameter invalidation, log-ML data scaling.

#include <QTest>
#include <cmath>

#include "models/GPRegression.h"

class TestGPRegressionDeep7 : public QObject
{
    Q_OBJECT

private slots:
    void testSinglePointFitInterpolatesTarget();
    void testUnfittedPredictReturnsZero();
    void testNegativeLengthscaleClampedToMinimum();
    void testPredictVarianceNonNegative();
    void testBatchPredictMatchesUncertaintyMean();
    void testSetKernelParamsClearsFittedState();
    void testLogMarginalLikelihoodImprovesWithMoreData();
    void testUnfittedLogMarginalLikelihoodIsZero();
};

// ─── Helpers ─────────────────────────────────────────────────────────────────

static QVector<QPair<double, double>> gridX(int n, double yCoord = 0.0)
{
    QVector<QPair<double, double>> X;
    X.reserve(n);
    for (int i = 0; i < n; ++i)
        X.append({static_cast<double>(i), yCoord});
    return X;
}

// ─── Tests ─────────────────────────────────────────────────────────────────

void TestGPRegressionDeep7::testSinglePointFitInterpolatesTarget()
{
    GPRegression gp;
    gp.setKernelParams(1.0, 0.8, 1e-8);
    constexpr double target = 7.25;
    gp.fit({{3.0, -2.0}}, {target});

    QVERIFY(gp.isFitted());
    QCOMPARE(gp.nTrainingPoints(), 1);

    const double mean = gp.predict(3.0, -2.0);
    const auto [mu, var] = gp.predictWithUncertainty(3.0, -2.0);

    QVERIFY2(qAbs(mean - target) < 1e-5,
             qPrintable(QStringLiteral("single-point predict expected %1, got %2")
                            .arg(target).arg(mean)));
    QVERIFY2(qAbs(mu - target) < 1e-5,
             qPrintable(QStringLiteral("single-point uncertainty mean expected %1, got %2")
                            .arg(target).arg(mu)));
    QVERIFY2(var < 1e-4,
             qPrintable(QStringLiteral("near-zero noise variance expected ≈0, got %1").arg(var)));
}

void TestGPRegressionDeep7::testUnfittedPredictReturnsZero()
{
    GPRegression gp;
    QCOMPARE(gp.predict(1.0, 2.0), 0.0);

    const auto [mean, var] = gp.predictWithUncertainty(1.0, 2.0);
    QCOMPARE(mean, 0.0);
    QVERIFY(var > 0.0);
    QVERIFY(!gp.isFitted());
}

void TestGPRegressionDeep7::testNegativeLengthscaleClampedToMinimum()
{
    GPRegression gp;
    gp.setKernelParams(1.0, -10.0, 1e-8);
    gp.fit({{0.0, 0.0}, {1.0, 0.0}, {2.0, 0.0}}, {0.0, 1.0, 0.0});
    QVERIFY(gp.isFitted());

    // Clamped lengthscale must not prevent exact recovery at training inputs.
    const QVector<double> yExpected = {0.0, 1.0, 0.0};
    for (int i = 0; i < 3; ++i) {
        const double mean = gp.predict(static_cast<double>(i), 0.0);
        QVERIFY2(qAbs(mean - yExpected[i]) < 1e-4,
                 qPrintable(QStringLiteral("training point %1 expected %2, got %3")
                                .arg(i).arg(yExpected[i]).arg(mean)));
    }

    const double farPoint = gp.predict(100.0, 100.0);
    QVERIFY2(qAbs(farPoint) < 0.05,
             qPrintable(QStringLiteral("far point mean=%1 should revert toward prior")
                            .arg(farPoint)));
}

void TestGPRegressionDeep7::testPredictVarianceNonNegative()
{
    GPRegression gp;
    gp.setKernelParams(1.5, 0.6, 0.05);
    gp.fit({{0.0, 0.0}, {1.0, 1.0}, {2.0, 0.5}, {3.0, -1.0}}, {1.0, -0.5, 2.0, 0.25});

    for (double x = -0.5; x <= 3.5; x += 0.25) {
        for (double y = -0.5; y <= 2.0; y += 0.25) {
            const auto [mean, var] = gp.predictWithUncertainty(x, y);
            Q_UNUSED(mean)
            QVERIFY2(var >= 0.0,
                     qPrintable(QStringLiteral("variance at (%1,%2) must be non-negative, got %3")
                                    .arg(x).arg(y).arg(var)));
            QVERIFY2(std::isfinite(var),
                     qPrintable(QStringLiteral("variance at (%1,%2) must be finite, got %3")
                                    .arg(x).arg(y).arg(var)));
        }
    }
}

void TestGPRegressionDeep7::testBatchPredictMatchesUncertaintyMean()
{
    GPRegression gp;
    gp.setKernelParams(1.0, 1.2, 1e-3);
    gp.fit({{0.0, 0.0}, {1.0, 0.0}, {0.0, 1.0}, {1.0, 1.0}}, {1.0, 0.5, -0.25, 2.0});

    const QVector<QPair<double, double>> queries = {
        {0.25, 0.25}, {0.75, 0.25}, {0.5, 0.5}, {2.0, 2.0}, {-1.0, -1.0}
    };

    for (const auto& q : queries) {
        const double scalar = gp.predict(q.first, q.second);
        const auto [mean, var] = gp.predictWithUncertainty(q.first, q.second);
        Q_UNUSED(var)
        QVERIFY2(qAbs(scalar - mean) < 1e-9,
                 qPrintable(QStringLiteral("predict(%1,%2)=%3 != uncertainty mean %4")
                                .arg(q.first).arg(q.second).arg(scalar).arg(mean)));
    }
}

void TestGPRegressionDeep7::testSetKernelParamsClearsFittedState()
{
    GPRegression gp;
    gp.setKernelParams(1.0, 1.0, 1e-4);
    gp.fit({{0.0, 0.0}, {1.0, 0.0}}, {1.0, 2.0});
    QVERIFY(gp.isFitted());
    QVERIFY(qAbs(gp.predict(0.5, 0.0)) > 0.01);

    gp.setKernelParams(2.0, 0.5, 1e-2);
    QVERIFY(!gp.isFitted());
    QCOMPARE(gp.predict(0.5, 0.0), 0.0);

    gp.fit({{0.0, 0.0}, {1.0, 0.0}}, {1.0, 2.0});
    QVERIFY(gp.isFitted());
    QVERIFY(qAbs(gp.predict(0.5, 0.0)) > 0.01);
}

void TestGPRegressionDeep7::testLogMarginalLikelihoodImprovesWithMoreData()
{
    GPRegression gpSmall;
    GPRegression gpLarge;
    gpSmall.setKernelParams(1.0, 2.0, 1e-6);
    gpLarge.setKernelParams(1.0, 2.0, 1e-6);

    const QVector<QPair<double, double>> Xsmall = gridX(4);
    const QVector<double> ysmall = {0.0, 0.25, 0.5, 0.75};

    const QVector<QPair<double, double>> Xlarge = gridX(12);
    QVector<double> ylarge;
    ylarge.reserve(12);
    for (int i = 0; i < 12; ++i)
        ylarge.append(static_cast<double>(i) / 11.0);

    gpSmall.fit(Xsmall, ysmall);
    gpLarge.fit(Xlarge, ylarge);

    QVERIFY2(gpLarge.logMarginalLikelihood() > gpSmall.logMarginalLikelihood(),
             qPrintable(QStringLiteral("more training data LML (%1) should exceed sparse LML (%2)")
                            .arg(gpLarge.logMarginalLikelihood())
                            .arg(gpSmall.logMarginalLikelihood())));
}

void TestGPRegressionDeep7::testUnfittedLogMarginalLikelihoodIsZero()
{
    GPRegression gp;
    QCOMPARE(gp.logMarginalLikelihood(), 0.0);

    gp.setKernelParams(1.0, 1.0, 1e-4);
    gp.fit({{0.0, 0.0}}, {1.0});
    QVERIFY(gp.isFitted());
    QVERIFY(gp.logMarginalLikelihood() < 0.0);

    gp.setKernelParams(1.0, 2.0, 1e-4);
    QVERIFY(!gp.isFitted());
    QCOMPARE(gp.logMarginalLikelihood(), 0.0);

    gp.fit({}, {});
    QVERIFY(!gp.isFitted());
    QCOMPARE(gp.logMarginalLikelihood(), 0.0);
}

QTEST_GUILESS_MAIN(TestGPRegressionDeep7)
#include "test_gp_regression_deep7.moc"
