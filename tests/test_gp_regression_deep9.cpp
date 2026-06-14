// test_gp_regression_deep9.cpp — Deep audit iteration 28: GPRegression
// nTrainingPoints, logML after fit, interpolation smoothness, unfitted predict.
#include <QTest>
#include <cmath>
#include "models/GPRegression.h"

class TestGPRegressionDeep9 : public QObject
{
    Q_OBJECT

private slots:

    void testNTrainingPointsAfterFit()
    {
        GPRegression gp;
        gp.fit({{0.0, 0.0}, {1.0, 0.0}, {0.0, 1.0}}, {0.0, 1.0, 0.5});
        QCOMPARE(gp.nTrainingPoints(), 3);
        QVERIFY(gp.isFitted());
    }

    void testLogMarginalLikelihoodFiniteAfterFit()
    {
        GPRegression gp;
        gp.setKernelParams(1.0, 0.8, 1e-4);
        gp.fit({{0.0, 0.0}, {1.0, 1.0}, {2.0, 0.0}}, {1.0, 0.0, -1.0});
        const double lml = gp.logMarginalLikelihood();
        QVERIFY(std::isfinite(lml));
    }

    void testPredictInterpolatesBetweenPoints()
    {
        GPRegression gp;
        gp.setKernelParams(1.0, 1.0, 1e-6);
        gp.fit({{0.0, 0.0}, {2.0, 0.0}}, {0.0, 2.0});
        const double mid = gp.predict(1.0, 0.0);
        QVERIFY2(mid > 0.0 && mid < 2.0,
                 qPrintable(QStringLiteral("mid=%1").arg(mid)));
    }

    void testUnfittedPredictReturnsZero()
    {
        GPRegression gp;
        QCOMPARE(gp.predict(1.0, 2.0), 0.0);
    }

    void testSetKernelParamsInvalidatesNeedRefit()
    {
        GPRegression gp;
        gp.fit({{0.0, 0.0}}, {1.0});
        gp.setKernelParams(2.0, 0.5, 1e-3);
        QVERIFY(!gp.isFitted());
    }
};

QTEST_GUILESS_MAIN(TestGPRegressionDeep9)
#include "test_gp_regression_deep9.moc"
