// test_calibration_analyser_deep3.cpp — Deep audit iteration 16:
// ECE, Brier score, and reliability-bin aggregation.

#include <QTest>
#include <cmath>

#include "benchmark/CalibrationAnalyser.h"

class TestCalibrationAnalyserDeep3 : public QObject
{
    Q_OBJECT

    using PA = QPair<double, double>;

private slots:
    void testBrierScoreZeroForPerfectPredictions();
    void testBrierScoreOneForMaximalError();
    void testBinCountsSumToNSamples();
    void testReliabilityDiagramOnlyPopulatedBins();
    void testECEHandComputedTwoPointBin();
    void testStatusReflectsECEThresholds();
    void testSharpnessZeroForConstantPredictor();
    void testStructuredReliabilityFromPrecomputedBins();
};

// ─── Tests ───────────────────────────────────────────────────────────────────

void TestCalibrationAnalyserDeep3::testBrierScoreZeroForPerfectPredictions()
{
    QVector<PA> pa;
    for (int i = 0; i < 50; ++i) pa.append({0.0, 0.0});
    for (int i = 0; i < 50; ++i) pa.append({1.0, 1.0});

    CalibrationAnalyser ca(10);
    const auto res = ca.analyse(pa);

    QVERIFY2(std::abs(res.brierScore) < 1e-12,
             qPrintable(QStringLiteral("Perfect Brier score expected 0, got %1")
                            .arg(res.brierScore)));
}

void TestCalibrationAnalyserDeep3::testBrierScoreOneForMaximalError()
{
    QVector<PA> pa;
    for (int i = 0; i < 100; ++i) pa.append({1.0, 0.0});

    CalibrationAnalyser ca(10);
    const auto res = ca.analyse(pa);

    QVERIFY2(std::abs(res.brierScore - 1.0) < 1e-12,
             qPrintable(QStringLiteral("Max-error Brier score expected 1, got %1")
                            .arg(res.brierScore)));
}

void TestCalibrationAnalyserDeep3::testBinCountsSumToNSamples()
{
    QVector<PA> pa;
    for (int i = 0; i < 100; ++i) {
        const double pred = static_cast<double>(i) / 100.0;
        pa.append({pred, (i % 3 == 0) ? 1.0 : 0.0});
    }

    CalibrationAnalyser ca(10);
    const auto res = ca.analyse(pa);

    int total = 0;
    for (const auto& bin : res.bins)
        total += bin.count;

    QCOMPARE(total, res.nSamples);
    QCOMPARE(res.nSamples, 100);
    QCOMPARE(res.bins.size(), 10);
}

void TestCalibrationAnalyserDeep3::testReliabilityDiagramOnlyPopulatedBins()
{
    // All predictions in bin 5 → diagram should have exactly one point.
    QVector<PA> pa;
    for (int i = 0; i < 40; ++i) pa.append({0.55, (i % 2 == 0) ? 1.0 : 0.0});

    CalibrationAnalyser ca(10);
    const auto res     = ca.analyse(pa);
    const auto diagram = ca.reliabilityDiagram(pa);

    int populated = 0;
    for (const auto& bin : res.bins) {
        if (bin.count > 0) ++populated;
    }

    QCOMPARE(diagram.size(), populated);
    QCOMPARE(diagram.size(), 1);
    QVERIFY2(std::abs(diagram[0].first - 0.55) < 1e-9,
             qPrintable(QStringLiteral("Diagram avgPred expected 0.55, got %1")
                            .arg(diagram[0].first)));
}

// Two identical predictions in one bin: ECE = |avgPred − empirical| × fraction.
void TestCalibrationAnalyserDeep3::testECEHandComputedTwoPointBin()
{
    QVector<PA> pa;
    pa.append({0.8, 0.0});
    pa.append({0.8, 1.0});
    // bin 8: avgPred=0.8, empirical=0.5, error=0.3, fraction=1.0 → ECE=0.3

    CalibrationAnalyser ca(10);
    const auto res = ca.analyse(pa);

    QCOMPARE(res.nSamples, 2);
    QVERIFY2(std::abs(res.ece - 0.3) < 1e-9,
             qPrintable(QStringLiteral("Hand-computed ECE expected 0.3, got %1")
                            .arg(res.ece)));
    QVERIFY2(std::abs(res.mce - 0.3) < 1e-9,
             qPrintable(QStringLiteral("MCE expected 0.3, got %1").arg(res.mce)));
}

void TestCalibrationAnalyserDeep3::testStatusReflectsECEThresholds()
{
    CalibrationAnalyser ca(10);

    {
        QVector<PA> perfect;
        for (int i = 0; i < 100; ++i) perfect.append({0.0, 0.0});
        const auto res = ca.analyse(perfect);
        QCOMPARE(res.status(), QStringLiteral("EXCELLENT"));
    }

    {
        QVector<PA> poor;
        for (int i = 0; i < 100; ++i) poor.append({0.9, 0.0});
        const auto res = ca.analyse(poor);
        QCOMPARE(res.status(), QStringLiteral("POOR"));
    }
}

void TestCalibrationAnalyserDeep3::testSharpnessZeroForConstantPredictor()
{
    QVector<PA> pa;
    for (int i = 0; i < 80; ++i) pa.append({0.6, (i % 2 == 0) ? 1.0 : 0.0});

    CalibrationAnalyser ca(10);
    const auto res = ca.analyse(pa);

    QVERIFY2(std::abs(res.sharpness) < 1e-12,
             qPrintable(QStringLiteral("Constant predictor sharpness expected 0, got %1")
                            .arg(res.sharpness)));
}

void TestCalibrationAnalyserDeep3::testStructuredReliabilityFromPrecomputedBins()
{
    QVector<CalibrationBin> bins(3);
    bins[0] = {0.15, 0.1, 0.0, 0.1, 10, 0.5};
    bins[1] = {0.50, 0.0, 0.0, 0.0,  0, 0.0};   // empty — must be skipped
    bins[2] = {0.85, 0.9, 0.8, 0.1, 10, 0.5};

    CalibrationAnalyser ca(3);
    const auto points = ca.reliabilityDiagram(bins);

    QCOMPARE(points.size(), 2);
    QCOMPARE(points[0].count, 10);
    QCOMPARE(points[0].fractionPositive, 0.0);
    QCOMPARE(points[1].count, 10);
    QCOMPARE(points[1].fractionPositive, 0.8);
}

QTEST_GUILESS_MAIN(TestCalibrationAnalyserDeep3)
#include "test_calibration_analyser_deep3.moc"
