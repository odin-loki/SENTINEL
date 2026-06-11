// test_gp_regression_deep6.cpp — Deep audit iteration 19:
// log marginal likelihood, kernel clamping, noise interpolation, symmetry,
// and mean/variance consistency across the GP posterior.

#include <QTest>
#include <cmath>

#include "models/GPRegression.h"

class TestGPRegressionDeep6 : public QObject
{
    Q_OBJECT

private slots:
    void testLogMarginalLikelihoodFiniteAndNegative();
    void testZeroNoiseInterpolatesTrainingTargets();
    void testKernelParamsClampedToPositiveMinimum();
    void testLowNoiseImprovesMarginalLikelihoodOnSmoothData();
    void testSymmetricDataYieldsSymmetricPredictions();
    void testPredictMatchesUncertaintyMean();
    void testWideLengthscaleImprovesLMLOnSmoothData();
    void testFarExtrapolationMeanRevertsToPrior();
};

// ─── Helpers ─────────────────────────────────────────────────────────────────

static QVector<QPair<double, double>> lineX(int n, double yCoord = 0.0)
{
    QVector<QPair<double, double>> X;
    X.reserve(n);
    for (int i = 0; i < n; ++i)
        X.append({static_cast<double>(i), yCoord});
    return X;
}

// ─── Tests ─────────────────────────────────────────────────────────────────

void TestGPRegressionDeep6::testLogMarginalLikelihoodFiniteAndNegative()
{
    GPRegression gp;
    gp.setKernelParams(1.0, 1.0, 1e-4);
    gp.fit({{0.0, 0.0}, {1.0, 0.0}, {2.0, 0.0}}, {0.0, 1.0, 0.0});
    QVERIFY(gp.isFitted());

    const double lml = gp.logMarginalLikelihood();
    QVERIFY2(std::isfinite(lml),
             qPrintable(QStringLiteral("Log ML must be finite, got %1").arg(lml)));
    QVERIFY2(lml < 0.0,
             qPrintable(QStringLiteral("Log ML should be negative for noisy data, got %1")
                            .arg(lml)));
}

void TestGPRegressionDeep6::testZeroNoiseInterpolatesTrainingTargets()
{
    GPRegression gp;
    gp.setKernelParams(1.0, 0.5, 0.0);
    const QVector<QPair<double, double>> X = {{0.0, 0.0}, {1.0, 1.0}, {2.0, 0.0}};
    const QVector<double> y = {2.0, -1.0, 3.5};
    gp.fit(X, y);

    for (int i = 0; i < X.size(); ++i) {
        const double mean = gp.predict(X[i].first, X[i].second);
        const auto [mu, var] = gp.predictWithUncertainty(X[i].first, X[i].second);
        QVERIFY2(qAbs(mean - y[i]) < 1e-5,
                 qPrintable(QStringLiteral("predict() at training point %1: expected %2, got %3")
                                .arg(i).arg(y[i]).arg(mean)));
        QVERIFY2(qAbs(mu - y[i]) < 1e-5,
                 qPrintable(QStringLiteral("predictWithUncertainty mean at %1: expected %2, got %3")
                                .arg(i).arg(y[i]).arg(mu)));
        QVERIFY2(var < 1e-4,
                 qPrintable(QStringLiteral("Zero-noise variance at training point %1 expected ≈0, got %2")
                                .arg(i).arg(var)));
    }
}

void TestGPRegressionDeep6::testKernelParamsClampedToPositiveMinimum()
{
    GPRegression gp;
    gp.setKernelParams(-5.0, 0.0, -1.0);
    gp.fit({{0.0, 0.0}}, {1.0});
    QVERIFY(gp.isFitted());

    const auto [mean, var] = gp.predictWithUncertainty(1.0, 1.0);
    Q_UNUSED(mean)
    // Clamped σ² = 1e-12, noise clamped to 0
    QVERIFY2(var <= 1e-11,
             qPrintable(QStringLiteral("Clamped σ² prior variance expected ≤1e-11, got %1")
                            .arg(var)));
}

void TestGPRegressionDeep6::testLowNoiseImprovesMarginalLikelihoodOnSmoothData()
{
    const QVector<QPair<double, double>> X = lineX(5);
    const QVector<double> y = {0.0, 0.25, 0.5, 0.75, 1.0};

    GPRegression gpLow;
    gpLow.setKernelParams(1.0, 1.0, 1e-6);
    gpLow.fit(X, y);

    GPRegression gpHigh;
    gpHigh.setKernelParams(1.0, 1.0, 1.0);
    gpHigh.fit(X, y);

    QVERIFY2(gpLow.logMarginalLikelihood() > gpHigh.logMarginalLikelihood(),
             qPrintable(QStringLiteral("Low noise LML (%1) should exceed high noise LML (%2) "
                                        "on smooth linear data")
                            .arg(gpLow.logMarginalLikelihood())
                            .arg(gpHigh.logMarginalLikelihood())));
}

void TestGPRegressionDeep6::testSymmetricDataYieldsSymmetricPredictions()
{
    GPRegression gp;
    gp.setKernelParams(1.0, 1.0, 1e-6);
    gp.fit({{-1.0, 0.0}, {0.0, 0.0}, {1.0, 0.0}}, {1.0, 2.0, 1.0});

    const double left  = gp.predict(-0.5, 0.0);
    const double right = gp.predict( 0.5, 0.0);

    QVERIFY2(qAbs(left - right) < 1e-6,
             qPrintable(QStringLiteral("Symmetric data: predict(-0.5)=%1 should ≈ predict(0.5)=%2")
                            .arg(left).arg(right)));
}

void TestGPRegressionDeep6::testPredictMatchesUncertaintyMean()
{
    GPRegression gp;
    gp.setKernelParams(1.5, 0.8, 1e-4);
    gp.fit({{0.0, 0.0}, {1.0, 2.0}, {2.0, 1.0}}, {1.0, -0.5, 2.0});

    for (double x = -1.0; x <= 3.0; x += 0.5) {
        for (double y = -1.0; y <= 3.0; y += 0.5) {
            const double scalar = gp.predict(x, y);
            const auto [mean, var] = gp.predictWithUncertainty(x, y);
            Q_UNUSED(var)
            QVERIFY2(qAbs(scalar - mean) < 1e-9,
                     qPrintable(QStringLiteral("predict(%1,%2)=%3 != uncertainty mean %4")
                                    .arg(x).arg(y).arg(scalar).arg(mean)));
        }
    }
}

void TestGPRegressionDeep6::testWideLengthscaleImprovesLMLOnSmoothData()
{
    const QVector<QPair<double, double>> X = lineX(4);
    const QVector<double> y = {0.0, 0.25, 0.5, 0.75};

    GPRegression gpWide;
    gpWide.setKernelParams(1.0, 5.0, 1e-4);
    gpWide.fit(X, y);

    GPRegression gpTight;
    gpTight.setKernelParams(1.0, 0.3, 1e-4);
    gpTight.fit(X, y);

    QVERIFY2(gpWide.logMarginalLikelihood() > gpTight.logMarginalLikelihood(),
             qPrintable(QStringLiteral("Wide lengthscale LML (%1) should exceed tight (%2) "
                                        "on smooth linear data")
                            .arg(gpWide.logMarginalLikelihood())
                            .arg(gpTight.logMarginalLikelihood())));
}

void TestGPRegressionDeep6::testFarExtrapolationMeanRevertsToPrior()
{
    GPRegression gp;
    gp.setKernelParams(1.0, 1.0, 1e-6);
    gp.fit({{0.0, 0.0}}, {42.0});

    const double farMean = gp.predict(500.0, 500.0);
    const auto [mean, var] = gp.predictWithUncertainty(500.0, 500.0);

    QVERIFY2(qAbs(farMean) < 0.01,
             qPrintable(QStringLiteral("Far extrapolation mean should revert to 0, got %1")
                            .arg(farMean)));
    QVERIFY2(qAbs(mean) < 0.01,
             qPrintable(QStringLiteral("Far extrapolation uncertainty mean should be ≈0, got %1")
                            .arg(mean)));
    QVERIFY2(var > 0.9,
             qPrintable(QStringLiteral("Far extrapolation variance should approach prior σ², got %1")
                            .arg(var)));
}

QTEST_GUILESS_MAIN(TestGPRegressionDeep6)
#include "test_gp_regression_deep6.moc"
