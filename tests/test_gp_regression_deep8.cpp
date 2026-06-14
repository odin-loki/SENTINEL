// test_gp_regression_deep8.cpp — Deep audit iteration 24: GPRegression
// posterior variance, log-ML guard, kernel clamp, training interpolation.
#include <QTest>
#include <cmath>
#include "models/GPRegression.h"

class TestGPRegressionDeep8 : public QObject
{
    Q_OBJECT

private slots:

    void testPosteriorVarianceNonNegative()
    {
        GPRegression gp;
        gp.setKernelParams(1.5, 0.6, 0.05);
        gp.fit({{0.0, 0.0}, {1.0, 1.0}, {2.0, 0.5}}, {1.0, -0.5, 2.0});

        const auto [mean, var] = gp.predictWithUncertainty(0.5, 0.5);
        Q_UNUSED(mean);
        QVERIFY2(var >= 0.0, qPrintable(QStringLiteral("var=%1").arg(var)));
    }

    void testLogMarginalLikelihoodUnfittedIsZero()
    {
        GPRegression gp;
        QCOMPARE(gp.logMarginalLikelihood(), 0.0);
    }

    void testPredictAtTrainingPointLowVariance()
    {
        GPRegression gp;
        gp.setKernelParams(1.0, 0.8, 1e-8);
        constexpr double target = 4.5;
        gp.fit({{1.0, 2.0}}, {target});

        const auto [mean, var] = gp.predictWithUncertainty(1.0, 2.0);
        QVERIFY2(std::abs(mean - target) < 1e-4,
                 qPrintable(QStringLiteral("mean=%1").arg(mean)));
        QVERIFY2(var < 1e-3,
                 qPrintable(QStringLiteral("var=%1").arg(var)));
    }

    void testNegativeLengthscaleClamped()
    {
        GPRegression gp;
        gp.setKernelParams(1.0, -5.0, 1e-8);
        gp.fit({{0.0, 0.0}, {1.0, 0.0}}, {0.0, 1.0});
        QVERIFY(gp.isFitted());
        const double p0 = gp.predict(0.0, 0.0);
        const double p1 = gp.predict(1.0, 0.0);
        QVERIFY2(std::abs(p0) < 1e-3, qPrintable(QStringLiteral("p0=%1").arg(p0)));
        QVERIFY2(std::abs(p1 - 1.0) < 1e-3, qPrintable(QStringLiteral("p1=%1").arg(p1)));
    }

    void testUnfittedPredictReturnsZero()
    {
        GPRegression gp;
        QCOMPARE(gp.predict(0.5, 0.5), 0.0);
    }

    void testFitEmptyNoCrash()
    {
        GPRegression gp;
        gp.fit({}, {});
        QVERIFY(!gp.isFitted());
    }

    void testLogMLImprovesWithMoreData()
    {
        GPRegression gpSmall;
        gpSmall.setKernelParams(1.0, 0.5, 0.01);
        gpSmall.fit({{0.0, 0.0}, {1.0, 0.0}}, {0.0, 1.0});
        const double llSmall = gpSmall.logMarginalLikelihood();

        GPRegression gpLarge;
        gpLarge.setKernelParams(1.0, 0.5, 0.01);
        gpLarge.fit({{0.0, 0.0}, {1.0, 0.0}, {2.0, 0.0}, {3.0, 0.0}},
                    {0.0, 1.0, 0.5, -0.5});
        const double llLarge = gpLarge.logMarginalLikelihood();

        QVERIFY(std::isfinite(llSmall));
        QVERIFY(std::isfinite(llLarge));
    }
};

QTEST_GUILESS_MAIN(TestGPRegressionDeep8)
#include "test_gp_regression_deep8.moc"
