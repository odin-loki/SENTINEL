// test_calibration_analyser_deep7.cpp — Deep audit iteration 30: CalibrationAnalyser
// logLoss, brierScore, reliability diagram, bin counts, custom nBins.
#include <QTest>
#include <cmath>
#include "benchmark/CalibrationAnalyser.h"

class TestCalibrationAnalyserDeep7 : public QObject
{
    Q_OBJECT

private slots:

    void testLogLossNonNegative()
    {
        QVector<QPair<double, double>> data;
        for (int i = 0; i < 20; ++i)
            data.append({ 0.3 + i * 0.02, (i % 2 == 0) ? 1.0 : 0.0 });

        CalibrationAnalyser ca(8);
        const auto result = ca.analyse(data);
        QVERIFY(result.logLoss >= 0.0);
        QVERIFY(std::isfinite(result.logLoss));
    }

    void testBrierScorePerfectIsZero()
    {
        QVector<QPair<double, double>> data;
        data.append({ 1.0, 1.0 });
        data.append({ 0.0, 0.0 });

        const auto result = CalibrationAnalyser(5).analyse(data);
        QVERIFY(result.brierScore < 1e-9);
    }

    void testReliabilityDiagramFromData()
    {
        QVector<QPair<double, double>> data;
        for (int i = 0; i < 50; ++i)
            data.append({ static_cast<double>(i) / 49.0, i > 25 ? 1.0 : 0.0 });

        const auto diagram = CalibrationAnalyser(10).reliabilityDiagram(data);
        QVERIFY(!diagram.isEmpty());
        for (const auto& pt : diagram) {
            QVERIFY(pt.first >= 0.0 && pt.first <= 1.0);
            QVERIFY(pt.second >= 0.0 && pt.second <= 1.0);
        }
    }

    void testBinCountMatchesNBins()
    {
        QVector<QPair<double, double>> data;
        for (int i = 0; i < 100; ++i)
            data.append({ static_cast<double>(i) / 99.0, (i % 4 == 0) ? 1.0 : 0.0 });

        const auto result = CalibrationAnalyser(12).analyse(data);
        QCOMPARE(result.nBins, 12);
        QCOMPARE(result.bins.size(), 12);
    }

    void testNSamplesMatchesInput()
    {
        QVector<QPair<double, double>> data;
        for (int i = 0; i < 37; ++i)
            data.append({ 0.5, 1.0 });

        const auto result = CalibrationAnalyser().analyse(data);
        QCOMPARE(result.nSamples, 37);
    }
};

QTEST_GUILESS_MAIN(TestCalibrationAnalyserDeep7)
#include "test_calibration_analyser_deep7.moc"
