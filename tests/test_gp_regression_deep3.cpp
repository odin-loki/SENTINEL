#include <QTest>
#include <cmath>
#include "models/GPRegression.h"

class TestGPRegressionDeep3 : public QObject {
    Q_OBJECT

private slots:

    void testSEKernelVarianceAtSamePoint()
    {
        // An unfitted GP returns {0.0, m_sigma2} for predictWithUncertainty.
        // This exposes the prior variance, which equals k(x,x) = σ².
        GPRegression gp;
        gp.setKernelParams(2.5, 1.0, 1e-4);
        // Not fitted: predictWithUncertainty returns prior variance = sigma2
        auto [mean, var] = gp.predictWithUncertainty(0.0, 0.0);
        QVERIFY(std::abs(var - 2.5) < 1e-9);
        QCOMPARE(mean, 0.0);
    }

    void testSEKernelDecreaseMonotonicallyWithDistance()
    {
        // Fit 1 training point at (0,0) with y=1.  With small noise the posterior
        // mean at (d, 0) ≈ exp(-d²/2) · const, which is strictly decreasing for d>0.
        GPRegression gp;
        gp.setKernelParams(1.0, 1.0, 1e-6);
        gp.fit({{0.0, 0.0}}, {1.0});
        QVERIFY(gp.isFitted());

        double prev = gp.predict(0.0, 0.0);
        for (double d : {0.5, 1.0, 1.5, 2.0, 3.0}) {
            const double cur = gp.predict(d, 0.0);
            QVERIFY(cur < prev);
            prev = cur;
        }
    }

    void testPosteriorVarianceNonNegativeEverywhere()
    {
        GPRegression gp;
        gp.setKernelParams(1.0, 1.0, 1e-3);

        QVector<QPair<double,double>> X = {{0.0,0.0},{1.0,0.0},{0.0,1.0},{1.0,1.0},{0.5,0.5}};
        QVector<double>              y = {1.0, 2.0, 1.5, 2.5, 1.8};
        gp.fit(X, y);
        QVERIFY(gp.isFitted());

        for (double x1 : {-1.0, 0.0, 0.5, 1.0, 1.5, 2.0}) {
            for (double x2 : {-1.0, 0.0, 0.5, 1.0, 1.5, 2.0}) {
                auto [mean, var] = gp.predictWithUncertainty(x1, x2);
                QVERIFY(var >= 0.0);
                Q_UNUSED(mean)
            }
        }
    }

    void testPosteriorMeanAtTrainingPointCloseToObserved()
    {
        // With tiny noise the GP interpolates: μ*(x_i) ≈ y_i.
        GPRegression gp;
        gp.setKernelParams(1.0, 1.0, 1e-9);

        QVector<QPair<double,double>> X = {{0.0,0.0},{2.0,0.0},{0.0,2.0}};
        QVector<double>              y = {1.0, 3.0, 2.0};
        gp.fit(X, y);
        QVERIFY(gp.isFitted());

        for (int i = 0; i < X.size(); ++i) {
            const double pred = gp.predict(X[i].first, X[i].second);
            QVERIFY(std::abs(pred - y[i]) < 1e-4);
        }
    }

    void testSingleTrainingPointSingleTestPoint()
    {
        GPRegression gp;
        gp.setKernelParams(1.0, 1.0, 1e-6);
        gp.fit({{0.0, 0.0}}, {5.0});
        QVERIFY(gp.isFitted());
        QCOMPARE(gp.nTrainingPoints(), 1);

        auto [mean, var] = gp.predictWithUncertainty(0.0, 0.0);
        QVERIFY(std::abs(mean - 5.0) < 1e-3);
        QVERIFY(var >= 0.0);
        QVERIFY(var < 1.0);   // posterior variance should be well below prior at training point
    }

    void testCholeskyDoesNotFailOnPositiveDefiniteMatrix()
    {
        // Exercised implicitly by a successful fit: Cholesky must not produce NaN/Inf.
        GPRegression gp;
        gp.setKernelParams(1.0, 1.0, 1e-3);

        QVector<QPair<double,double>> X;
        QVector<double>              y;
        for (int i = 0; i < 5; ++i) {
            X.append({static_cast<double>(i), 0.0});
            y.append(static_cast<double>(i + 1));
        }
        gp.fit(X, y);
        QVERIFY(gp.isFitted());

        // Verify no NaN in predictions
        for (int i = 0; i < X.size(); ++i) {
            const double pred = gp.predict(X[i].first, X[i].second);
            QVERIFY(!std::isnan(pred));
            QVERIFY(!std::isinf(pred));
            auto [m, v] = gp.predictWithUncertainty(X[i].first, X[i].second);
            QVERIFY(!std::isnan(m));
            QVERIFY(!std::isnan(v));
            QVERIFY(v >= 0.0);
        }
    }

    void testPriorVarianceApproachedFarFromTrainingData()
    {
        // Far from all training points, posterior variance → σ² (prior).
        const double sigma2 = 3.0;
        GPRegression gp;
        gp.setKernelParams(sigma2, 1.0, 1e-4);
        gp.fit({{0.0,0.0},{1.0,0.0}}, {1.0, 2.0});
        QVERIFY(gp.isFitted());

        // At (1000, 1000), kernel values to training points ≈ 0 → var ≈ σ²
        auto [mean, var] = gp.predictWithUncertainty(1000.0, 1000.0);
        QVERIFY(std::abs(var - sigma2) < 1e-6);
        Q_UNUSED(mean)
    }
};

QTEST_GUILESS_MAIN(TestGPRegressionDeep3)
#include "test_gp_regression_deep3.moc"
