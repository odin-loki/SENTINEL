#include <QTest>
#include <QCoreApplication>
#include <cmath>
#include "models/GPRegression.h"

// ─────────────────────────────────────────────────────────────────────────────
// TestGPRegression — 12 tests covering correctness, edge cases, and behaviour
// ─────────────────────────────────────────────────────────────────────────────
class TestGPRegression : public QObject {
    Q_OBJECT
private slots:
    void test01_defaultConstruction();
    void test02_notFittedBeforeFit();
    void test03_constantFunctionInterpolation();
    void test04_linearFunctionInterpolation();
    void test05_uncertaintyIncreasesAwayFromData();
    void test06_logMarginalLikelihoodFiniteAfterFit();
    void test07_nTrainingPointsCorrect();
    void test08_predictWithUncertaintyVarianceNonNegative();
    void test09_edgeCaseOnePoint();
    void test10_edgeCaseBoundedPredictions();
    void test11_higherNoiseSigmaHigherPosteriorVariance();
    void test12_largerLengthscaleLowerVarianceNearby();
};

// 1. Default construction ─────────────────────────────────────────────────────
void TestGPRegression::test01_defaultConstruction()
{
    GPRegression gp;
    QVERIFY(!gp.isFitted());
    QCOMPARE(gp.nTrainingPoints(), 0);
}

// 2. isFitted() returns false before fit ──────────────────────────────────────
void TestGPRegression::test02_notFittedBeforeFit()
{
    GPRegression gp;
    QVERIFY(!gp.isFitted());
    QCOMPARE(gp.predict(0.0, 0.0), 0.0);
    auto [m, v] = gp.predictWithUncertainty(0.0, 0.0);
    Q_UNUSED(m)
    QVERIFY(v > 0.0);   // unfitted: returns prior variance
}

// 3. Constant function y=1 — predict near 1.0 at nearby points ───────────────
void TestGPRegression::test03_constantFunctionInterpolation()
{
    GPRegression gp;
    gp.setKernelParams(1.0, 1.0, 1e-6);

    QVector<QPair<double,double>> X = {{0,0},{1,0},{2,0},{3,0},{4,0}};
    QVector<double> y = {1.0, 1.0, 1.0, 1.0, 1.0};
    gp.fit(X, y);
    QVERIFY(gp.isFitted());

    // Predictions at and near training inputs should be close to 1.0
    for (int i = 0; i <= 4; ++i) {
        const double pred = gp.predict(static_cast<double>(i), 0.0);
        QVERIFY2(std::abs(pred - 1.0) < 0.2,
                 qPrintable(QString("predict(%1,0)=%2").arg(i).arg(pred)));
    }
}

// 4. Linear function — interpolation check ────────────────────────────────────
void TestGPRegression::test04_linearFunctionInterpolation()
{
    GPRegression gp;
    gp.setKernelParams(4.0, 2.0, 1e-6);

    QVector<QPair<double,double>> X;
    QVector<double> y;
    for (int i = 0; i <= 10; ++i) {
        X.append({static_cast<double>(i), 0.0});
        y.append(static_cast<double>(i) * 0.5);   // f(x) = 0.5x
    }
    gp.fit(X, y);
    QVERIFY(gp.isFitted());

    const double pred = gp.predict(5.0, 0.0);
    QVERIFY2(std::abs(pred - 2.5) < 0.5,
             qPrintable(QString("predict(5,0)=%1 expected≈2.5").arg(pred)));
}

// 5. Uncertainty increases away from training data ────────────────────────────
void TestGPRegression::test05_uncertaintyIncreasesAwayFromData()
{
    GPRegression gp;
    gp.setKernelParams(1.0, 0.5, 1e-6);

    QVector<QPair<double,double>> X = {{0,0},{1,0},{2,0}};
    QVector<double> y = {0.0, 1.0, 0.0};
    gp.fit(X, y);

    const auto [mNear, vNear] = gp.predictWithUncertainty(1.0, 0.0);  // training point
    const auto [mFar,  vFar]  = gp.predictWithUncertainty(100.0, 0.0); // far extrapolation
    Q_UNUSED(mNear) Q_UNUSED(mFar)

    QVERIFY2(vFar > vNear,
             qPrintable(QString("vNear=%1  vFar=%2").arg(vNear).arg(vFar)));
}

// 6. logMarginalLikelihood() returns finite value after fit ───────────────────
void TestGPRegression::test06_logMarginalLikelihoodFiniteAfterFit()
{
    GPRegression gp;
    gp.setKernelParams(1.0, 1.0, 0.1);

    QVector<QPair<double,double>> X = {{0,0},{1,0},{2,0}};
    QVector<double> y = {1.0, 2.0, 1.5};
    gp.fit(X, y);

    const double lml = gp.logMarginalLikelihood();
    QVERIFY2(std::isfinite(lml),
             qPrintable(QString("logML=%1").arg(lml)));
}

// 7. nTrainingPoints() returns correct count ───────────────────────────────────
void TestGPRegression::test07_nTrainingPointsCorrect()
{
    GPRegression gp;
    QVector<QPair<double,double>> X = {{0,0},{1,0},{2,0},{3,0}};
    QVector<double> y = {1.0, 2.0, 3.0, 4.0};
    gp.fit(X, y);
    QCOMPARE(gp.nTrainingPoints(), 4);
}

// 8. predictWithUncertainty — returns two values, variance ≥ 0 ────────────────
void TestGPRegression::test08_predictWithUncertaintyVarianceNonNegative()
{
    GPRegression gp;
    gp.setKernelParams(1.0, 1.0, 0.01);

    QVector<QPair<double,double>> X = {{0,0},{1,0},{2,0},{3,0}};
    QVector<double> y = {0.0, 1.0, 0.5, 0.8};
    gp.fit(X, y);

    for (double x = -3.0; x <= 6.0; x += 0.5) {
        const auto [mean, var] = gp.predictWithUncertainty(x, 0.0);
        Q_UNUSED(mean)
        QVERIFY2(var >= 0.0,
                 qPrintable(QString("var=%1 at x=%2").arg(var).arg(x)));
    }
}

// 9. Edge case: fit to 1 point ─────────────────────────────────────────────────
void TestGPRegression::test09_edgeCaseOnePoint()
{
    GPRegression gp;
    gp.setKernelParams(1.0, 1.0, 0.01);

    QVector<QPair<double,double>> X = {{0.0, 0.0}};
    QVector<double> y = {3.14};
    gp.fit(X, y);
    QVERIFY(gp.isFitted());
    QCOMPARE(gp.nTrainingPoints(), 1);

    const double pred = gp.predict(0.0, 0.0);
    QVERIFY2(std::abs(pred - 3.14) < 0.5,
             qPrintable(QString("pred=%1 expected≈3.14").arg(pred)));
}

// 10. Edge case: 100 random points — predictions bounded within [-5, 5] ────────
void TestGPRegression::test10_edgeCaseBoundedPredictions()
{
    GPRegression gp;
    gp.setKernelParams(1.0, 1.0, 0.1);

    QVector<QPair<double,double>> X;
    QVector<double> y;
    // Deterministic pseudo-random values in [-0.5, 0.5]
    for (int i = 0; i < 10; ++i) {
        for (int j = 0; j < 10; ++j) {
            X.append({i * 0.1, j * 0.1});
            y.append(((i * 7 + j * 3) % 10) / 10.0 - 0.45);
        }
    }
    gp.fit(X, y);
    QVERIFY(gp.isFitted());
    QCOMPARE(gp.nTrainingPoints(), 100);

    for (int i = 0; i < 10; ++i) {
        const double pred = gp.predict(i * 0.15, i * 0.15);
        QVERIFY2(std::abs(pred) < 5.0,
                 qPrintable(QString("pred=%1 at step %2").arg(pred).arg(i)));
    }
}

// 11. Higher σ_n² → higher posterior variance at training point ───────────────
void TestGPRegression::test11_higherNoiseSigmaHigherPosteriorVariance()
{
    GPRegression gpLow, gpHigh;
    gpLow.setKernelParams(1.0, 1.0, 1e-6);
    gpHigh.setKernelParams(1.0, 1.0, 1.0);

    QVector<QPair<double,double>> X = {{0,0},{1,0},{2,0}};
    QVector<double> y = {0.5, 1.0, 0.7};

    gpLow.fit(X, y);
    gpHigh.fit(X, y);

    const auto [mLow,  vLow]  = gpLow.predictWithUncertainty(1.0, 0.0);
    const auto [mHigh, vHigh] = gpHigh.predictWithUncertainty(1.0, 0.0);
    Q_UNUSED(mLow) Q_UNUSED(mHigh)

    QVERIFY2(vHigh > vLow,
             qPrintable(QString("vLow=%1  vHigh=%2").arg(vLow).arg(vHigh)));
}

// 12. Larger lengthscale → smoother predictions (lower variance at nearby unseen point)
void TestGPRegression::test12_largerLengthscaleLowerVarianceNearby()
{
    GPRegression gpSmall, gpLarge;
    // With small l, distant training points contribute little → high variance at x=1
    gpSmall.setKernelParams(1.0, 0.1, 1e-4);
    // With large l, training points at x=0,2,4 strongly inform x=1 → low variance
    gpLarge.setKernelParams(1.0, 10.0, 1e-4);

    QVector<QPair<double,double>> X = {{0,0},{2,0},{4,0}};
    QVector<double> y = {1.0, 1.0, 1.0};

    gpSmall.fit(X, y);
    gpLarge.fit(X, y);

    const auto [mS, vS] = gpSmall.predictWithUncertainty(1.0, 0.0);
    const auto [mL, vL] = gpLarge.predictWithUncertainty(1.0, 0.0);
    Q_UNUSED(mS) Q_UNUSED(mL)

    QVERIFY2(vL < vS,
             qPrintable(QString("vSmall=%1  vLarge=%2").arg(vS).arg(vL)));
}

// ─────────────────────────────────────────────────────────────────────────────

static int runTest(QObject* obj, const char* logFile)
{
    QStringList args = { "test", "-o", QString("%1,txt").arg(logFile) };
    return QTest::qExec(obj, args);
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    int r = 0;
    { TestGPRegression t; r |= runTest(&t, "gp_regression.txt"); }
    return r;
}
#include "test_gp_regression.moc"
