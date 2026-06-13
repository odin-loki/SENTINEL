// test_calibration_analyser_deep5.cpp — Iteration 22 deep audit: empty-input defaults,
// negative bin clamping, isotonic monotonicity, reliability diagram filtering,
// log-loss stability, perfect-calibration status, and nBins constructor clamp.
#include <QTest>
#include <cmath>

#include "benchmark/CalibrationAnalyser.h"

class TestCalibrationAnalyserDeep5 : public QObject
{
    Q_OBJECT

    using PA = QPair<double, double>;

private slots:
    void testEmptyInputZeroMetrics();
    void testNegativePredBinClamped();
    void testIsotonicCalibrateMonotone();
    void testReliabilityDiagramSkipsEmptyBins();
    void testLogLossFiniteAtPredExtremes();
    void testStatusExcellentForPerfectCalibration();
    void testNBinsConstructorClamp();
};

void TestCalibrationAnalyserDeep5::testEmptyInputZeroMetrics()
{
    CalibrationAnalyser ca(10);
    const auto res = ca.analyse({});

    QCOMPARE(res.nSamples, 0);
    QCOMPARE(res.nBins, 0);
    QVERIFY(res.bins.isEmpty());
    QVERIFY(std::abs(res.ece) < 1e-12);
    QVERIFY(std::abs(res.mce) < 1e-12);
    QVERIFY(std::abs(res.ace) < 1e-12);
    QVERIFY(std::abs(res.brierScore) < 1e-12);
    QVERIFY(std::abs(res.logLoss) < 1e-12);
    QVERIFY(std::abs(res.sharpness) < 1e-12);
    QCOMPARE(res.status(), QStringLiteral("EXCELLENT"));
}

void TestCalibrationAnalyserDeep5::testNegativePredBinClamped()
{
    CalibrationAnalyser ca(10);
    const auto res = ca.analyse({ {-0.25, 0.0}, {-0.01, 1.0} });

    QCOMPARE(res.bins[0].count, 2);
    QCOMPARE(res.bins[1].count, 0);
    QVERIFY2(res.bins[0].avgPred < 0.0,
             qPrintable(QStringLiteral("Negative predictions should land in bin 0, avgPred=%1")
                            .arg(res.bins[0].avgPred)));
}

void TestCalibrationAnalyserDeep5::testIsotonicCalibrateMonotone()
{
    QVector<PA> pa;
    pa.append({0.15, 0.0});
    pa.append({0.35, 1.0});
    pa.append({0.25, 0.0});
    pa.append({0.55, 1.0});
    pa.append({0.45, 0.0});

    const auto calibrated = CalibrationAnalyser::isotonicCalibrate(pa);
    QCOMPARE(calibrated.size(), pa.size());

    for (int i = 1; i < calibrated.size(); ++i) {
        QVERIFY2(calibrated[i].first >= calibrated[i - 1].first - 1e-9,
                 qPrintable(QStringLiteral("Isotonic violation at %1").arg(i)));
    }
}

void TestCalibrationAnalyserDeep5::testReliabilityDiagramSkipsEmptyBins()
{
    CalibrationAnalyser ca(5);
    QVector<PA> pa;
    for (int i = 0; i < 20; ++i)
        pa.append({0.1, i % 2}); // only bin 0 populated

    const auto points = ca.reliabilityDiagram(pa);
    QCOMPARE(points.size(), 1);
    QVERIFY2(points[0].first > 0.0 && points[0].first < 0.3,
             "Single populated bin should report its average prediction");

    QVector<CalibrationBin> bins(3);
    bins[0].midpoint = 0.1;
    bins[0].count    = 5;
    bins[0].empirical = 0.4;
    bins[1].midpoint = 0.5;
    bins[1].count    = 0;
    bins[2].midpoint = 0.9;
    bins[2].count    = 3;
    bins[2].empirical = 0.8;

    const auto structured = ca.reliabilityDiagram(bins);
    QCOMPARE(structured.size(), 2);
    QCOMPARE(structured[0].count, 5);
    QCOMPARE(structured[1].count, 3);
}

void TestCalibrationAnalyserDeep5::testLogLossFiniteAtPredExtremes()
{
    CalibrationAnalyser ca(4);
    const auto res = ca.analyse({
        {0.0, 0.0},
        {1.0, 1.0},
        {0.0, 1.0},
        {1.0, 0.0}
    });

    QVERIFY(std::isfinite(res.logLoss));
    QVERIFY2(res.logLoss > 0.0,
             qPrintable(QStringLiteral("Mixed extremes should yield positive log-loss, got %1")
                            .arg(res.logLoss)));
}

void TestCalibrationAnalyserDeep5::testStatusExcellentForPerfectCalibration()
{
    QVector<PA> pa;
    for (int i = 0; i < 50; ++i) pa.append({0.05, 0.0});
    for (int i = 0; i < 50; ++i) pa.append({0.95, 1.0});

    CalibrationAnalyser ca(10);
    const auto res = ca.analyse(pa);

    QVERIFY2(res.ece < 0.05,
             qPrintable(QStringLiteral("Perfect calibration ECE expected <0.05, got %1")
                            .arg(res.ece)));
    QCOMPARE(res.status(), QStringLiteral("EXCELLENT"));
}

void TestCalibrationAnalyserDeep5::testNBinsConstructorClamp()
{
    CalibrationAnalyser caZero(0);
    CalibrationAnalyser caNeg(-3);
    CalibrationAnalyser caOne(1);

    const QVector<PA> pa = {{0.2, 1.0}, {0.8, 0.0}};
    const auto resZero = caZero.analyse(pa);
    const auto resNeg  = caNeg.analyse(pa);
    const auto resOne  = caOne.analyse(pa);

    QCOMPARE(resZero.nBins, 2);
    QCOMPARE(resNeg.nBins, 2);
    QCOMPARE(resOne.nBins, 2);
    QCOMPARE(resZero.bins.size(), 2);
    QCOMPARE(resNeg.bins.size(), 2);
    QCOMPARE(resOne.bins.size(), 2);
}

QTEST_GUILESS_MAIN(TestCalibrationAnalyserDeep5)
#include "test_calibration_analyser_deep5.moc"
