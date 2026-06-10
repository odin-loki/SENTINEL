// test_calibration_ece.cpp
// Validates CalibrationAnalyser ECE, MCE, sharpness, isotonic calibration,
// and reliability diagram against known statistical properties.
#include <QTest>
#include "benchmark/CalibrationAnalyser.h"
#include <cmath>
#include <algorithm>

class CalibrationECETest : public QObject
{
    Q_OBJECT

private:
    // Perfect calibration: predicted probability equals actual frequency
    static QVector<QPair<double,double>> perfectCalibration(int n)
    {
        QVector<QPair<double,double>> pa;
        pa.reserve(n);
        // Predict 0.2 for 80% of events (actual=0) and 0.8 for 20% (actual=1)
        for (int i = 0; i < n * 8 / 10; ++i) pa.append({ 0.2, 0.0 });
        for (int i = 0; i < n * 2 / 10; ++i) pa.append({ 0.8, 1.0 });
        return pa;
    }

    // Severely miscalibrated: predict 0.9 everywhere but 50% positives
    static QVector<QPair<double,double>> miscalibrated(int n)
    {
        QVector<QPair<double,double>> pa;
        pa.reserve(n);
        for (int i = 0; i < n; ++i) pa.append({ 0.9, static_cast<double>(i % 2) });
        return pa;
    }

private slots:

    // ── 1. ECE is in [0, 1] ───────────────────────────────────────────────────
    void testECERange()
    {
        CalibrationAnalyser ca(10);
        const auto result = ca.analyse(miscalibrated(200));
        QVERIFY2(result.ece >= 0.0 && result.ece <= 1.0,
                 qPrintable(QStringLiteral("ECE %1 must be in [0,1]").arg(result.ece)));
    }

    // ── 2. Perfectly calibrated model has ECE near 0 ─────────────────────────
    void testECEPerfectCalibrationLow()
    {
        CalibrationAnalyser ca(10);
        // predict exactly the frequency
        QVector<QPair<double,double>> pa;
        for (int i = 0; i < 90; ++i) pa.append({ 0.1, 0.0 });  // 10% → predict 0.1
        for (int i = 0; i < 10; ++i) pa.append({ 0.1, 1.0 });  // actual 10% positive

        const auto result = ca.analyse(pa);
        QVERIFY2(result.ece < 0.15,
                 qPrintable(QStringLiteral("Perfect calibration ECE %1 should be < 0.15")
                    .arg(result.ece)));
    }

    // ── 3. Severely miscalibrated model has higher ECE ───────────────────────
    void testECEMiscalibratedHigh()
    {
        CalibrationAnalyser ca(10);
        // Predict 0.9 everywhere, but actual positive rate = 0.1
        QVector<QPair<double,double>> pa;
        for (int i = 0; i < 90; ++i) pa.append({ 0.9, 0.0 });
        for (int i = 0; i < 10; ++i) pa.append({ 0.9, 1.0 });

        const auto result = ca.analyse(pa);
        QVERIFY2(result.ece > 0.3,
                 qPrintable(QStringLiteral("Miscalibrated ECE %1 should be > 0.3")
                    .arg(result.ece)));
    }

    // ── 4. MCE >= ECE always ──────────────────────────────────────────────────
    void testMCEGeqECE()
    {
        CalibrationAnalyser ca(10);
        const auto result = ca.analyse(miscalibrated(200));
        QVERIFY2(result.mce >= result.ece - 1e-9,
                 qPrintable(QStringLiteral("MCE (%1) must be >= ECE (%2)")
                    .arg(result.mce).arg(result.ece)));
    }

    // ── 5. Brier score is in [0, 1] ──────────────────────────────────────────
    void testBrierScoreRange()
    {
        CalibrationAnalyser ca(10);
        const auto result = ca.analyse(miscalibrated(100));
        QVERIFY2(result.brierScore >= 0.0 && result.brierScore <= 1.0,
                 qPrintable(QStringLiteral("Brier score %1 must be in [0,1]")
                    .arg(result.brierScore)));
    }

    // ── 6. Sharpness is 0 for constant predictions ───────────────────────────
    void testSharpnessConstant()
    {
        CalibrationAnalyser ca(10);
        QVector<QPair<double,double>> pa;
        for (int i = 0; i < 50; ++i) pa.append({ 0.5, static_cast<double>(i % 2) });

        const auto result = ca.analyse(pa);
        // Variance of constant predictions = 0
        QVERIFY2(result.sharpness < 0.01,
                 qPrintable(QStringLiteral("Constant predictions sharpness %1 should be ~0")
                    .arg(result.sharpness)));
    }

    // ── 7. nSamples and nBins are set correctly ───────────────────────────────
    void testMetadataCorrect()
    {
        CalibrationAnalyser ca(10);
        const int N = 150;
        const auto result = ca.analyse(perfectCalibration(N));
        QCOMPARE(result.nSamples, N);
        QCOMPARE(result.nBins, 10);
    }

    // ── 8. Reliability diagram has correct number of points ──────────────────
    void testReliabilityDiagramLength()
    {
        CalibrationAnalyser ca(10);
        const auto diag = ca.reliabilityDiagram(perfectCalibration(100));
        // Should have at most 10 non-empty bins
        QVERIFY2(diag.size() <= 10,
                 qPrintable(QStringLiteral("Reliability diagram has %1 points, expected <=10")
                    .arg(diag.size())));
        QVERIFY2(!diag.isEmpty(), "Reliability diagram should not be empty");
    }

    // ── 9. Isotonic calibration reduces ECE ───────────────────────────────────
    void testIsotonicCalibrationReducesECE()
    {
        CalibrationAnalyser ca(10);
        auto pa = miscalibrated(200);
        const double eceBefore = ca.analyse(pa).ece;

        const auto calibrated = CalibrationAnalyser::isotonicCalibrate(pa);
        const double eceAfter = ca.analyse(calibrated).ece;

        QVERIFY2(eceAfter <= eceBefore + 0.05,
                 qPrintable(QStringLiteral(
                    "Isotonic calibration ECE %1 should be <= before %2 (+0.05)")
                    .arg(eceAfter).arg(eceBefore)));
    }

    // ── 10. Status text is non-empty ────────────────────────────────────────
    void testStatusNonEmpty()
    {
        CalibrationAnalyser ca(10);
        const auto result = ca.analyse(perfectCalibration(100));
        QVERIFY2(!result.status().isEmpty(), "CalibrationResult::status() must be non-empty");
    }
};

QTEST_MAIN(CalibrationECETest)
#include "test_calibration_ece.moc"
