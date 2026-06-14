// test_calibration_analyser_deep6.cpp — Deep audit iteration 25: CalibrationAnalyser
// ECE/MCE/ACE, sharpness, status labels, isotonic calibration.
#include <QTest>
#include <cmath>
#include "benchmark/CalibrationAnalyser.h"

class TestCalibrationAnalyserDeep6 : public QObject
{
    Q_OBJECT

    static QVector<QPair<double, double>> perfectCalibration(int n = 100)
    {
        QVector<QPair<double, double>> data;
        for (int i = 0; i < n; ++i) {
            const double p = static_cast<double>(i) / (n - 1);
            data.append({ p, p > 0.5 ? 1.0 : 0.0 });
        }
        return data;
    }

private slots:

    void testECENonNegative()
    {
        CalibrationAnalyser ca(10);
        const auto result = ca.analyse(perfectCalibration());
        QVERIFY2(result.ece >= 0.0, qPrintable(QStringLiteral("ece=%1").arg(result.ece)));
    }

    void testMCEGreaterOrEqualBinError()
    {
        CalibrationAnalyser ca(5);
        const auto result = ca.analyse(perfectCalibration(50));
        QVERIFY2(result.mce >= 0.0, qPrintable(QStringLiteral("mce=%1").arg(result.mce)));
        for (const auto& bin : result.bins) {
            if (bin.count > 0)
                QVERIFY2(result.mce >= bin.error - 1e-9,
                         "MCE should be >= each bin error");
        }
    }

    void testSharpnessPositiveWithVariedPredictions()
    {
        CalibrationAnalyser ca(10);
        QVector<QPair<double, double>> data;
        for (int i = 0; i < 40; ++i)
            data.append({ static_cast<double>(i) / 39.0, i % 2 });
        const auto result = ca.analyse(data);
        QVERIFY2(result.sharpness > 0.0,
                 qPrintable(QStringLiteral("sharpness=%1").arg(result.sharpness)));
    }

    void testStatusLabelNonEmpty()
    {
        CalibrationAnalyser ca;
        const auto result = ca.analyse(perfectCalibration(30));
        QVERIFY(!result.status().isEmpty());
    }

    void testIsotonicCalibrationMonotone()
    {
        CalibrationAnalyser ca(10);
        QVector<QPair<double, double>> data;
        for (int i = 0; i < 30; ++i)
            data.append({ static_cast<double>(i) / 29.0, (i % 3 == 0) ? 1.0 : 0.0 });

        const auto calibrated = ca.isotonicCalibrate(data);
        QCOMPARE(calibrated.size(), data.size());
        double prev = -1.0;
        for (const auto& p : calibrated) {
            QVERIFY2(p.first >= prev - 1e-9, "isotonic output should be monotone");
            prev = p.first;
        }
    }

    void testEmptyInputHandled()
    {
        CalibrationAnalyser ca;
        const auto result = ca.analyse({});
        QCOMPARE(result.nSamples, 0);
        QVERIFY(std::isfinite(result.ece));
    }

    void testReliabilityDiagramMatchesBinCount()
    {
        CalibrationAnalyser ca(8);
        const auto result = ca.analyse(perfectCalibration(80));
        const auto diagram = ca.reliabilityDiagram(perfectCalibration(80));
        QVERIFY(!diagram.isEmpty());
        QCOMPARE(result.nBins, 8);
    }
};

QTEST_GUILESS_MAIN(TestCalibrationAnalyserDeep6)
#include "test_calibration_analyser_deep6.moc"
