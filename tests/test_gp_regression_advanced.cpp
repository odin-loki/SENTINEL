// test_gp_regression_advanced.cpp
// Advanced tests for GPRegression: kernel parameter effects, variance patterns,
// posterior consistency, log marginal likelihood, and hyperparameter changes.
#include <QTest>
#include "models/GPRegression.h"
#include <cmath>
#include <algorithm>

class GPRegressionAdvancedTest : public QObject
{
    Q_OBJECT

private:
    using Pt = QPair<double, double>;

    static QVector<Pt> grid2D(double lo, double hi, int n)
    {
        QVector<Pt> pts;
        pts.reserve(n);
        for (int i = 0; i < n; ++i) {
            const double t = lo + (hi - lo) * i / (n - 1);
            pts.append({ t, 0.0 });
        }
        return pts;
    }

    static QVector<double> sinY(const QVector<Pt>& x)
    {
        QVector<double> y;
        y.reserve(x.size());
        for (const auto& p : x) y.append(std::sin(p.first));
        return y;
    }

private slots:

    // ── 1. Prediction at training points is close to observed ────────────────
    void testPredictionAtTrainingPoints()
    {
        GPRegression gp;
        const auto x = grid2D(0.0, 2 * M_PI, 10);
        const auto y = sinY(x);
        gp.fit(x, y);

        for (int i = 0; i < x.size(); ++i) {
            const double pred = gp.predict(x[i].first, x[i].second);
            const double actual = y[i];
            QVERIFY2(std::abs(pred - actual) < 0.5,
                     qPrintable(QStringLiteral("x=%1: pred=%2 actual=%3")
                        .arg(x[i].first).arg(pred).arg(actual)));
        }
    }

    // ── 2. Variance non-negative everywhere ──────────────────────────────────
    void testVarianceNonNegative()
    {
        GPRegression gp;
        const auto x = grid2D(0.0, 5.0, 8);
        gp.fit(x, sinY(x));

        for (double xi = -1.0; xi <= 6.0; xi += 0.5) {
            const auto [mean, var] = gp.predictWithUncertainty(xi, 0.0);
            QVERIFY2(var >= -1e-9,
                     qPrintable(QStringLiteral("Variance %1 at x=%2 should be >= 0")
                        .arg(var).arg(xi)));
        }
    }

    // ── 3. Variance higher far from training data ────────────────────────────
    void testVarianceHigherFarFromData()
    {
        GPRegression gp;
        const auto x = grid2D(0.0, 1.0, 5);
        gp.fit(x, sinY(x));

        const auto [mN, varNear] = gp.predictWithUncertainty(0.5, 0.0);  // inside
        const auto [mF, varFar]  = gp.predictWithUncertainty(10.0, 0.0); // far outside
        QVERIFY2(varFar >= varNear - 1e-6,
                 qPrintable(QStringLiteral("Far var %1 should >= near var %2")
                    .arg(varFar).arg(varNear)));
    }

    // ── 4. log marginal likelihood is finite ─────────────────────────────────
    void testLogMarginalLikelihoodFinite()
    {
        GPRegression gp;
        gp.fit(grid2D(0.0, 5.0, 10), sinY(grid2D(0.0, 5.0, 10)));
        const double lml = gp.logMarginalLikelihood();
        QVERIFY2(std::isfinite(lml),
                 qPrintable(QStringLiteral("LML %1 must be finite").arg(lml)));
    }

    // ── 5. nTrainingPoints() returns correct count ────────────────────────────
    void testNTrainingPoints()
    {
        GPRegression gp;
        gp.fit(grid2D(0.0, 4.0, 15), sinY(grid2D(0.0, 4.0, 15)));
        QCOMPARE(gp.nTrainingPoints(), 15);
    }

    // ── 6. setKernelParams invalidates fit ────────────────────────────────────
    void testSetKernelParamsInvalidatesFit()
    {
        GPRegression gp;
        gp.fit(grid2D(0.0, 4.0, 8), sinY(grid2D(0.0, 4.0, 8)));
        QVERIFY(gp.isFitted());
        gp.setKernelParams(2.0, 1.0, 0.1);
        QVERIFY2(!gp.isFitted(), "setKernelParams should invalidate fit");
    }

    // ── 7. Refitting after setKernelParams works ──────────────────────────────
    void testRefitAfterKernelChange()
    {
        GPRegression gp;
        const auto x = grid2D(0.0, 4.0, 8);
        const auto y = sinY(x);
        gp.fit(x, y);
        gp.setKernelParams(2.0, 0.8, 0.05);
        gp.fit(x, y);
        QVERIFY2(gp.isFitted(), "Should be fitted after refit");
    }

    // ── 8. Prediction is consistent across identical calls ───────────────────
    void testPredictionConsistent()
    {
        GPRegression gp;
        gp.fit(grid2D(0.0, 5.0, 10), sinY(grid2D(0.0, 5.0, 10)));
        const double p1 = gp.predict(2.5, 0.0);
        const double p2 = gp.predict(2.5, 0.0);
        QVERIFY2(std::abs(p1 - p2) < 1e-12, "Identical calls should return same result");
    }

    // ── 9. predictWithUncertainty mean == predict ─────────────────────────────
    void testUncertaintyMeanEqualsPredict()
    {
        GPRegression gp;
        gp.fit(grid2D(0.0, 5.0, 10), sinY(grid2D(0.0, 5.0, 10)));
        const double xTest = 3.0;
        const double predMean = gp.predict(xTest, 0.0);
        const auto [mean, var] = gp.predictWithUncertainty(xTest, 0.0);
        QVERIFY2(std::abs(predMean - mean) < 1e-9,
                 qPrintable(QStringLiteral("predict %1 should == predictWithUncertainty mean %2")
                    .arg(predMean).arg(mean)));
    }

    // ── 10. Empty fit does not crash ──────────────────────────────────────────
    void testEmptyFitNoCrash()
    {
        GPRegression gp;
        gp.fit({}, {});
        QVERIFY(!gp.isFitted());
        QVERIFY(true);  // no crash
    }
};

QTEST_MAIN(GPRegressionAdvancedTest)
#include "test_gp_regression_advanced.moc"
