// test_calibration_analyser_deep4.cpp — Deep audit iteration 19:
// isotonic calibration (PAVA), log-loss, ACE vs ECE, bin constructor clamp,
// and boundary bin assignment.

#include <QTest>
#include <cmath>

#include "benchmark/CalibrationAnalyser.h"

class TestCalibrationAnalyserDeep4 : public QObject
{
    Q_OBJECT

    using PA = QPair<double, double>;

private slots:
    void testIsotonicCalibrateProducesNonDecreasing();
    void testLogLossNearZeroForPerfectPredictions();
    void testLogLossHighForConfidentWrongPredictions();
    void testACEDiffersFromECEWithUnevenBins();
    void testConstructorClampsMinimumBinsToTwo();
    void testIsotonicCalibratePreservesActualLabels();
    void testPredNearOneLandsInLastBin();
    void testAnalyseEmptyReturnsZeroDefaults();
};

// ─── Tests ───────────────────────────────────────────────────────────────────

void TestCalibrationAnalyserDeep4::testIsotonicCalibrateProducesNonDecreasing()
{
    QVector<PA> pa;
    pa.append({0.1, 0.0});
    pa.append({0.3, 1.0});
    pa.append({0.2, 0.0});
    pa.append({0.5, 1.0});
    pa.append({0.4, 0.0});

    const auto calibrated = CalibrationAnalyser::isotonicCalibrate(pa);
    QCOMPARE(calibrated.size(), pa.size());

    // PAVA output is in sorted-by-prediction order; calibrated values must be
    // non-decreasing down the sorted sequence.
    for (int i = 1; i < calibrated.size(); ++i) {
        QVERIFY2(calibrated[i].first >= calibrated[i - 1].first - 1e-9,
                 qPrintable(QStringLiteral("Isotonic violation at %1: %2 < %3")
                                .arg(i)
                                .arg(calibrated[i].first)
                                .arg(calibrated[i - 1].first)));
    }
}

void TestCalibrationAnalyserDeep4::testLogLossNearZeroForPerfectPredictions()
{
    QVector<PA> pa;
    for (int i = 0; i < 50; ++i) pa.append({0.0, 0.0});
    for (int i = 0; i < 50; ++i) pa.append({1.0, 1.0});

    CalibrationAnalyser ca(10);
    const auto res = ca.analyse(pa);

    QVERIFY2(res.logLoss < 1e-6,
             qPrintable(QStringLiteral("Perfect log-loss expected ≈0, got %1")
                            .arg(res.logLoss)));
}

void TestCalibrationAnalyserDeep4::testLogLossHighForConfidentWrongPredictions()
{
    QVector<PA> pa;
    for (int i = 0; i < 100; ++i) pa.append({0.99, 0.0});

    CalibrationAnalyser ca(10);
    const auto res = ca.analyse(pa);

    QVERIFY2(res.logLoss > 2.0,
             qPrintable(QStringLiteral("Confident wrong log-loss expected >2, got %1")
                            .arg(res.logLoss)));
}

void TestCalibrationAnalyserDeep4::testACEDiffersFromECEWithUnevenBins()
{
    // 90 points in bin 0 (perfect), 10 points in bin 9 (max error).
    // ECE weights by fraction; ACE averages bin errors equally.
    QVector<PA> pa;
    for (int i = 0; i < 90; ++i) pa.append({0.05, 0.0});
    for (int i = 0; i < 10; ++i) pa.append({0.95, 0.0});

    CalibrationAnalyser ca(10);
    const auto res = ca.analyse(pa);

    QVERIFY2(res.ece < res.ace,
             qPrintable(QStringLiteral("Uneven bins: ECE (%1) should be < ACE (%2)")
                            .arg(res.ece).arg(res.ace)));
    QVERIFY2(qAbs(res.ace - 0.5) < 1e-9,
             qPrintable(QStringLiteral("ACE expected 0.5 (avg of 0 and 1), got %1")
                            .arg(res.ace)));
}

void TestCalibrationAnalyserDeep4::testConstructorClampsMinimumBinsToTwo()
{
    CalibrationAnalyser caZero(0);
    CalibrationAnalyser caNeg(-5);

    QVector<PA> pa = {{0.3, 1.0}, {0.7, 0.0}};
    const auto res0  = caZero.analyse(pa);
    const auto resNeg = caNeg.analyse(pa);

    QCOMPARE(res0.nBins, 2);
    QCOMPARE(resNeg.nBins, 2);
    QCOMPARE(res0.bins.size(), 2);
}

void TestCalibrationAnalyserDeep4::testIsotonicCalibratePreservesActualLabels()
{
    QVector<PA> pa;
    pa.append({0.6, 1.0});
    pa.append({0.2, 0.0});
    pa.append({0.4, 1.0});

    const auto calibrated = CalibrationAnalyser::isotonicCalibrate(pa);

    // Sorted order: pred 0.2, 0.4, 0.6 → actuals 0, 1, 1
    QCOMPARE(calibrated.size(), 3);
    QCOMPARE(calibrated[0].second, 0.0);
    QCOMPARE(calibrated[1].second, 1.0);
    QCOMPARE(calibrated[2].second, 1.0);
}

void TestCalibrationAnalyserDeep4::testPredNearOneLandsInLastBin()
{
    QVector<PA> pa;
    pa.append({0.99, 1.0});
    pa.append({1.0,  1.0});

    CalibrationAnalyser ca(10);
    const auto res = ca.analyse(pa);

    QCOMPARE(res.bins[9].count, 2);
    QCOMPARE(res.bins[0].count, 0);
    QVERIFY2(res.bins[9].avgPred > 0.99,
             qPrintable(QStringLiteral("Last-bin avgPred expected >0.99, got %1")
                            .arg(res.bins[9].avgPred)));
}

void TestCalibrationAnalyserDeep4::testAnalyseEmptyReturnsZeroDefaults()
{
    CalibrationAnalyser ca(10);
    const auto res = ca.analyse({});

    QCOMPARE(res.nSamples, 0);
    QVERIFY(res.bins.isEmpty());
    // BUG: early return leaves nBins at default 0 instead of m_nBins
    QCOMPARE(res.nBins, 0);
    QVERIFY(std::abs(res.ece) < 1e-12);
    QVERIFY(std::abs(res.mce) < 1e-12);
    QVERIFY(std::abs(res.ace) < 1e-12);
    QVERIFY(std::abs(res.brierScore) < 1e-12);
    QVERIFY(std::abs(res.logLoss) < 1e-12);
    QVERIFY(std::abs(res.sharpness) < 1e-12);
    QCOMPARE(res.status(), QStringLiteral("EXCELLENT"));
}

QTEST_GUILESS_MAIN(TestCalibrationAnalyserDeep4)
#include "test_calibration_analyser_deep4.moc"
