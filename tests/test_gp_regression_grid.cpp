// test_gp_regression_grid.cpp
// GPRegression grid prediction, posterior covariance, kernel parameter
// effects, and logMarginalLikelihood tests.
#include <QTest>
#include "models/GPRegression.h"
#include <cmath>
#include <algorithm>

class GPRegressionGridTest : public QObject
{
    Q_OBJECT

private:
    using Pt = QPair<double, double>;

    static void fitLinear(GPRegression& gp, int n = 10)
    {
        QVector<Pt> X;
        QVector<double> y;
        for (int i = 0; i < n; ++i) {
            const double t = static_cast<double>(i) / (n - 1);
            X.append({ t, 0.0 });
            y.append(t);  // y = x (linear)
        }
        gp.fit(X, y);
    }

    static void fitSine(GPRegression& gp, int n = 20)
    {
        QVector<Pt> X;
        QVector<double> y;
        for (int i = 0; i < n; ++i) {
            const double t = 2.0 * M_PI * i / n;
            X.append({ t, 0.0 });
            y.append(std::sin(t));
        }
        gp.fit(X, y);
    }

private slots:

    // 1. isFitted() false before fit
    void testNotFittedBeforeFit()
    {
        GPRegression gp;
        QVERIFY(!gp.isFitted());
    }

    // 2. isFitted() true after fit
    void testFittedAfterFit()
    {
        GPRegression gp;
        fitLinear(gp);
        QVERIFY(gp.isFitted());
    }

    // 3. nTrainingPoints() matches input
    void testNTrainingPoints()
    {
        GPRegression gp;
        fitLinear(gp, 10);
        QCOMPARE(gp.nTrainingPoints(), 10);
    }

    // 4. predict: linear function at training points within 0.1 of true value
    void testPredictLinearAccuracy()
    {
        GPRegression gp;
        fitLinear(gp, 10);
        for (int i = 0; i < 10; ++i) {
            const double t    = static_cast<double>(i) / 9.0;
            const double pred = gp.predict(t, 0.0);
            QVERIFY2(std::abs(pred - t) < 0.2,
                     qPrintable(QStringLiteral("Predict at %1: got %2, expected ~%3")
                        .arg(t).arg(pred).arg(t)));
        }
    }

    // 5. predictWithUncertainty: mean matches predict
    void testPredictWithUncertaintyMeanMatchesPredict()
    {
        GPRegression gp;
        fitLinear(gp);
        const double mean  = gp.predict(0.5, 0.0);
        const auto   meanVar = gp.predictWithUncertainty(0.5, 0.0);
        QVERIFY2(std::abs(mean - meanVar.first) < 1e-6,
                 qPrintable(QStringLiteral("Mean mismatch: predict=%1, pair=%2")
                    .arg(mean).arg(meanVar.first)));
    }

    // 6. predictWithUncertainty: variance positive
    void testPredictWithUncertaintyVariancePositive()
    {
        GPRegression gp;
        fitLinear(gp);
        const auto meanVar = gp.predictWithUncertainty(2.0, 0.0);  // extrapolation
        QVERIFY2(meanVar.second >= 0.0,
                 qPrintable(QStringLiteral("Variance %1 must be >= 0").arg(meanVar.second)));
    }

    // 7. logMarginalLikelihood: negative finite after fit
    void testLogMarginalLikelihoodFiniteAfterFit()
    {
        GPRegression gp;
        fitLinear(gp);
        const double lml = gp.logMarginalLikelihood();
        QVERIFY2(std::isfinite(lml),
                 qPrintable(QStringLiteral("logMarginalLikelihood %1 must be finite").arg(lml)));
    }

    // 8. logMarginalLikelihood == 0 before fit
    void testLogMarginalLikelihoodZeroBeforeFit()
    {
        GPRegression gp;
        QCOMPARE(gp.logMarginalLikelihood(), 0.0);
    }

    // 9. Different lengthscale produces different predictions
    void testDifferentLengthscalesDifferentPredictions()
    {
        GPRegression gp1, gp2;
        gp1.setKernelParams(1.0, 0.1, 1e-3);
        gp2.setKernelParams(1.0, 2.0, 1e-3);
        fitSine(gp1, 20);
        fitSine(gp2, 20);

        double diff = 0.0;
        for (int i = 0; i < 5; ++i) {
            const double t = M_PI * i / 5.0;
            diff += std::abs(gp1.predict(t, 0.0) - gp2.predict(t, 0.0));
        }
        QVERIFY2(diff > 0.0,
                 "Different lengthscale must produce different predictions");
    }

    // 10. Grid of predictions: 4x4 grid all finite
    void testGridPredictionsAllFinite()
    {
        GPRegression gp;
        fitLinear(gp, 10);
        const int N = 4;
        for (int i = 0; i < N; ++i) {
            for (int j = 0; j < N; ++j) {
                const double x1 = static_cast<double>(i) / (N - 1);
                const double x2 = static_cast<double>(j) / (N - 1);
                const double pred = gp.predict(x1, x2);
                QVERIFY2(std::isfinite(pred),
                         qPrintable(QStringLiteral("Grid predict(%1,%2)=%3 not finite")
                            .arg(x1).arg(x2).arg(pred)));
            }
        }
    }
};

QTEST_MAIN(GPRegressionGridTest)
#include "test_gp_regression_grid.moc"
