#include <QtTest>
#include "benchmark/CalibrationAnalyser.h"
#include <cmath>

class TestCalibrationAnalyserDeep2 : public QObject {
    Q_OBJECT

    using PA = QPair<double, double>;

private slots:

    void testPerfectCalibration_ECE_zero()
    {
        // pred=0.0 → bin 0: avgPred=0.0, empirical=0.0, error=0
        // pred=1.0 → bin 9: avgPred=1.0, empirical=1.0, error=0
        QVector<PA> pa;
        for (int i = 0; i < 50; ++i) pa.append({0.0, 0.0});
        for (int i = 0; i < 50; ++i) pa.append({1.0, 1.0});

        CalibrationAnalyser ca(10);
        const auto res = ca.analyse(pa);

        QVERIFY2(std::abs(res.ece) < 1e-9,
                 qPrintable(QStringLiteral("Perfect calibration ECE should be 0, got %1").arg(res.ece)));
    }

    void testMaxMiscalibration_ECE_one()
    {
        // All pred=1.0 but actual=0: all in last bin, avgPred=1.0, empirical=0.0
        // fraction=1.0, error=1.0 → ECE=1.0
        QVector<PA> pa;
        for (int i = 0; i < 100; ++i) pa.append({1.0, 0.0});

        CalibrationAnalyser ca(10);
        const auto res = ca.analyse(pa);

        QVERIFY2(std::abs(res.ece - 1.0) < 1e-9,
                 qPrintable(QStringLiteral("Max miscalibration ECE should be 1.0, got %1").arg(res.ece)));
    }

    void testMCE_geq_ECE()
    {
        // MCE is max bin error, ECE is weighted average — MCE >= ECE always
        QVector<PA> pa;
        for (int i = 0; i < 100; ++i) {
            const double pred = static_cast<double>(i) / 100.0;
            const double act  = (i < 40) ? 0.0 : 1.0;
            pa.append({pred, act});
        }

        CalibrationAnalyser ca(10);
        const auto res = ca.analyse(pa);

        QVERIFY2(res.mce >= res.ece - 1e-9,
                 qPrintable(QStringLiteral("MCE %1 must be >= ECE %2").arg(res.mce).arg(res.ece)));
    }

    void testReliabilityDiagramMonotonic()
    {
        // Spread predictions uniformly across [0,1] with matching actuals to get
        // points in every bin; avgPred within each bin should be non-decreasing.
        QVector<PA> pa;
        for (int i = 0; i < 100; ++i) {
            const double pred = static_cast<double>(i) / 100.0;
            pa.append({pred, (i % 2 == 0) ? 0.0 : 1.0});
        }

        CalibrationAnalyser ca(10);
        const auto points = ca.reliabilityDiagram(pa);

        QVERIFY2(points.size() >= 2, "Reliability diagram should have at least 2 points");
        for (int i = 1; i < points.size(); ++i) {
            QVERIFY2(points[i].first >= points[i-1].first - 1e-9,
                     qPrintable(QStringLiteral("Reliability diagram not monotonic at index %1: "
                                               "prev=%2 curr=%3")
                                    .arg(i).arg(points[i-1].first).arg(points[i].first)));
        }
    }

    void testEmptyData_ECE_zero()
    {
        CalibrationAnalyser ca(10);
        const auto res = ca.analyse({});

        QVERIFY2(std::abs(res.ece) < 1e-9,
                 qPrintable(QStringLiteral("Empty data ECE should be 0, got %1").arg(res.ece)));
        QCOMPARE(res.nSamples, 0);
    }

    void testSingleDataPoint()
    {
        // pred=0.7, actual=1 → bin 7, avgPred=0.7, empirical=1.0
        // fraction=1.0, error=0.3 → ECE=0.3
        QVector<PA> pa;
        pa.append({0.7, 1.0});

        CalibrationAnalyser ca(10);
        const auto res = ca.analyse(pa);

        QCOMPARE(res.nSamples, 1);
        QVERIFY2(std::abs(res.ece - 0.3) < 1e-9,
                 qPrintable(QStringLiteral("Single point ECE should be 0.3, got %1").arg(res.ece)));
        QVERIFY2(std::abs(res.mce - 0.3) < 1e-9,
                 qPrintable(QStringLiteral("Single point MCE should be 0.3, got %1").arg(res.mce)));
    }

    void testBinEdgeHandling_pred_zero()
    {
        // pred=0.0 must land in bin 0 (not below)
        QVector<PA> pa;
        pa.append({0.0, 0.0});

        CalibrationAnalyser ca(10);
        const auto res = ca.analyse(pa);

        QCOMPARE(res.bins.size(), 10);
        QCOMPARE(res.bins[0].count, 1);
    }

    void testBinEdgeHandling_pred_one()
    {
        // pred=1.0 must land in bin 9 (last bin), not overflow
        QVector<PA> pa;
        pa.append({1.0, 1.0});

        CalibrationAnalyser ca(10);
        const auto res = ca.analyse(pa);

        QCOMPARE(res.bins.size(), 10);
        QCOMPARE(res.bins[9].count, 1);
        QCOMPARE(res.bins[0].count, 0);
    }
};

QTEST_GUILESS_MAIN(TestCalibrationAnalyserDeep2)
#include "test_calibration_analyser_deep2.moc"
