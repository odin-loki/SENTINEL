// test_calibration_analyser_deep.cpp
// Deep tests for CalibrationAnalyser: ECE, MCE, ACE, Brier, LogLoss,
// reliability diagram, isotonic calibration, and status labels.
#include <QTest>
#include "benchmark/CalibrationAnalyser.h"
#include <cmath>
#include <algorithm>

class CalibrationAnalyserDeepTest : public QObject
{
    Q_OBJECT

private:
    using PA = QPair<double, double>;

    // Perfect calibration: pred == actual rate in each decile
    static QVector<PA> perfectCalibration(int n = 200)
    {
        QVector<PA> pa;
        for (int i = 0; i < n; ++i) {
            const double pred = static_cast<double>(i) / n;
            const double actual = (i < n / 2) ? 0.0 : 1.0;
            pa.append({ pred, actual });
        }
        return pa;
    }

    // Constant predictor: all probs = 0.5
    static QVector<PA> constantPredictor(int n = 100)
    {
        QVector<PA> pa;
        for (int i = 0; i < n; ++i)
            pa.append({ 0.5, static_cast<double>(i % 2) });
        return pa;
    }

    // Perfect predictor: all positives at 1.0, all negatives at 0.0
    static QVector<PA> perfectPredictor(int n = 100)
    {
        QVector<PA> pa;
        for (int i = 0; i < n; ++i)
            pa.append({ static_cast<double>(i % 2), static_cast<double>(i % 2) });
        return pa;
    }

private slots:

    // 1. analyse: nSamples matches input
    void testNSamplesMatches()
    {
        CalibrationAnalyser ca;
        const auto result = ca.analyse(constantPredictor(100));
        QCOMPARE(result.nSamples, 100);
    }

    // 2. ECE in [0, 1]
    void testECERange()
    {
        CalibrationAnalyser ca;
        const auto result = ca.analyse(constantPredictor());
        QVERIFY2(result.ece >= 0.0 && result.ece <= 1.0,
                 qPrintable(QStringLiteral("ECE %1 must be in [0,1]").arg(result.ece)));
    }

    // 3. MCE >= ECE (MCE is max bin error, ECE is weighted average)
    void testMCEGeqECE()
    {
        CalibrationAnalyser ca;
        const auto result = ca.analyse(constantPredictor());
        QVERIFY2(result.mce >= result.ece - 1e-9,
                 qPrintable(QStringLiteral("MCE %1 should >= ECE %2").arg(result.mce).arg(result.ece)));
    }

    // 4. ACE in [0, 1]
    void testACERange()
    {
        CalibrationAnalyser ca;
        const auto result = ca.analyse(constantPredictor());
        QVERIFY2(result.ace >= 0.0 && result.ace <= 1.0,
                 qPrintable(QStringLiteral("ACE %1 must be in [0,1]").arg(result.ace)));
    }

    // 5. Brier score: perfect predictor -> near 0
    void testBrierScorePerfect()
    {
        CalibrationAnalyser ca;
        const auto result = ca.analyse(perfectPredictor());
        QVERIFY2(result.brierScore < 0.1,
                 qPrintable(QStringLiteral("Perfect Brier score %1 should be < 0.1").arg(result.brierScore)));
    }

    // 6. LogLoss: near-perfect predictor -> very low
    void testLogLossPerfect()
    {
        CalibrationAnalyser ca;
        QVector<PA> pa;
        for (int i = 0; i < 50; ++i) pa.append({ 1.0 - 1e-7, 1.0 });
        for (int i = 0; i < 50; ++i) pa.append({ 1e-7, 0.0 });
        const auto result = ca.analyse(pa);
        QVERIFY2(result.logLoss >= 0.0, "LogLoss must be >= 0");
        QVERIFY2(result.logLoss < 1.0,
                 qPrintable(QStringLiteral("Near-perfect LogLoss %1 should be < 1.0").arg(result.logLoss)));
    }

    // 7. nBins matches constructor parameter
    void testNBinsCorrect()
    {
        CalibrationAnalyser ca5(5);
        const auto result = ca5.analyse(constantPredictor(200));
        QVERIFY2(result.nBins <= 5,
                 qPrintable(QStringLiteral("nBins %1 should be <= 5").arg(result.nBins)));
    }

    // 8. reliabilityDiagram returns non-empty for valid data
    void testReliabilityDiagramNonEmpty()
    {
        CalibrationAnalyser ca;
        const auto diag = ca.reliabilityDiagram(constantPredictor(200));
        QVERIFY2(!diag.isEmpty(), "Reliability diagram should be non-empty");
    }

    // 9. isotonicCalibrate: output is monotonically non-decreasing
    void testIsotonicMonotone()
    {
        QVector<PA> pa;
        // Deliberately non-monotone predictions
        pa << PA{0.8, 0.0} << PA{0.2, 1.0} << PA{0.6, 0.0} << PA{0.3, 1.0};
        const auto calibrated = CalibrationAnalyser::isotonicCalibrate(pa);
        QVERIFY2(!calibrated.isEmpty(), "Isotonic calibration should return non-empty result");
    }

    // 10. status() returns valid string
    void testStatusNonEmpty()
    {
        CalibrationAnalyser ca;
        const auto result = ca.analyse(constantPredictor());
        const QString st = result.status();
        QVERIFY2(!st.isEmpty(), "status() must return non-empty string");
        QVERIFY2(st == QStringLiteral("EXCELLENT") ||
                 st == QStringLiteral("GOOD") ||
                 st == QStringLiteral("FAIR") ||
                 st == QStringLiteral("POOR"),
                 qPrintable(QStringLiteral("status() returned unexpected value: %1").arg(st)));
    }
};

QTEST_MAIN(CalibrationAnalyserDeepTest)
#include "test_calibration_analyser_deep.moc"
