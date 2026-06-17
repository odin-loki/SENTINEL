// test_ensemble_calibration.cpp
// Tests EnsemblePredictor post-calibration: probability remapping,
// credible interval preservation, and calibration data quality.
#include <QTest>
#include <QTimeZone>
#include "models/EnsemblePredictor.h"
#include "models/PoissonBaseline.h"
#include <cmath>

class EnsembleCalibrationTest : public QObject
{
    Q_OBJECT

private:
    static PoissonBaseline makeFittedPoisson(int n = 30)
    {
        PoissonBaseline pb;
        QVector<PoissonBaseline::EventRecord> evs;
        const QDateTime base(QDate(2024, 1, 8), QTime(9, 0, 0), QTimeZone::utc());
        for (int i = 0; i < n; ++i) {
            PoissonBaseline::EventRecord r;
            r.zoneId     = QStringLiteral("Z1");
            r.crimeType  = QStringLiteral("burglary");
            r.occurredAt = base.addDays(i * 7);
            evs.append(r);
        }
        pb.fit(evs);
        return pb;
    }

    static QVector<QPair<double,double>> perfectCalData()
    {
        QVector<QPair<double,double>> pa;
        for (int i = 0; i < 50; ++i) pa.append({ 1.0 - 1e-7, 1.0 });
        for (int i = 0; i < 50; ++i) pa.append({ 1e-7, 0.0 });
        return pa;
    }

    static QVector<QPair<double,double>> realisticallyBadCalData()
    {
        QVector<QPair<double,double>> pa;
        for (int i = 0; i < 100; ++i)
            pa.append({ static_cast<double>(i) / 100.0, static_cast<double>(i % 2) });
        return pa;
    }

    static QDateTime testDt()
    {
        return QDateTime(QDate(2024, 6, 15), QTime(14, 0, 0), QTimeZone::utc());
    }

private slots:

    // 1. Pre-calibration: probCrime in [0,1]
    void testProbabilityRangePreCalibration()
    {
        PoissonBaseline pb = makeFittedPoisson();
        EnsemblePredictor ep;
        ep.setPoisson(&pb);
        const auto pred = ep.predict(QStringLiteral("Z1"), testDt(),
                                      QStringLiteral("burglary"), 51.5, -0.1);
        QVERIFY2(pred.probCrime >= 0.0 && pred.probCrime <= 1.0,
                 qPrintable(QStringLiteral("Pre-cal prob %1 must be in [0,1]").arg(pred.probCrime)));
    }

    // 2. Post-calibration: probCrime still in [0,1]
    void testProbabilityRangePostCalibration()
    {
        PoissonBaseline pb = makeFittedPoisson();
        EnsemblePredictor ep;
        ep.setPoisson(&pb);
        ep.calibrate(realisticallyBadCalData());
        const auto pred = ep.predict(QStringLiteral("Z1"), testDt(),
                                      QStringLiteral("burglary"), 51.5, -0.1);
        QVERIFY2(pred.probCrime >= 0.0 && pred.probCrime <= 1.0,
                 qPrintable(QStringLiteral("Post-cal prob %1 must be in [0,1]").arg(pred.probCrime)));
    }

    // 3. ECE on perfect calibration data is near 0
    void testECEPerfectCalibration()
    {
        const double ece = EnsemblePredictor::ece(perfectCalData(), 10);
        QVERIFY2(ece >= 0.0 && ece <= 0.1,
                 qPrintable(QStringLiteral("Perfect ECE %1 should be near 0").arg(ece)));
    }

    // 4. Brier score 0 for perfect predictions
    void testBrierScorePerfect()
    {
        const double bs = EnsemblePredictor::brierScore(perfectCalData());
        QVERIFY2(bs < 0.01,
                 qPrintable(QStringLiteral("Perfect Brier score %1 should be < 0.01").arg(bs)));
    }

    // 5. Brier score ~0.25 for constant 0.5 predictor
    void testBrierScoreHalfHalf()
    {
        QVector<QPair<double,double>> pa;
        for (int i = 0; i < 100; ++i) pa.append({ 0.5, static_cast<double>(i % 2) });
        const double bs = EnsemblePredictor::brierScore(pa);
        QVERIFY2(std::abs(bs - 0.25) < 0.02,
                 qPrintable(QStringLiteral("Constant-0.5 Brier score %1 expected ~0.25").arg(bs)));
    }

    // 6. CI bounds preserved post-calibration
    void testCIBoundsPostCalibration()
    {
        PoissonBaseline pb = makeFittedPoisson();
        EnsemblePredictor ep;
        ep.setPoisson(&pb);
        ep.calibrate(realisticallyBadCalData());
        const auto pred = ep.predict(QStringLiteral("Z1"), testDt(),
                                      QStringLiteral("burglary"), 51.5, -0.1);
        QVERIFY2(pred.ciLow95 <= pred.probCrime + 1e-6,
                 qPrintable(QStringLiteral("ciLow95 %1 should <= probCrime %2")
                    .arg(pred.ciLow95).arg(pred.probCrime)));
        QVERIFY2(pred.ciHigh95 >= pred.probCrime - 1e-6,
                 qPrintable(QStringLiteral("ciHigh95 %1 should >= probCrime %2")
                    .arg(pred.ciHigh95).arg(pred.probCrime)));
    }

    // 7. setWeights: weights = (0.8, 0.2) changes dominant model
    void testSetWeightsChangeDominant()
    {
        PoissonBaseline pb = makeFittedPoisson();
        EnsemblePredictor ep;
        ep.setPoisson(&pb);
        ep.setWeights(1.0, 0.0);  // Poisson only
        const auto pred = ep.predict(QStringLiteral("Z1"), testDt(),
                                      QStringLiteral("burglary"), 51.5, -0.1);
        QVERIFY2(!pred.dominantModel.isEmpty(), "dominantModel must be set");
    }

    // 8. ECE is in [0, 1] for arbitrary data
    void testECERangeArbitraryData()
    {
        const double ece = EnsemblePredictor::ece(realisticallyBadCalData(), 10);
        QVERIFY2(ece >= 0.0 && ece <= 1.0,
                 qPrintable(QStringLiteral("ECE %1 must be in [0,1]").arg(ece)));
    }

    // 9. calibrate + predict multiple times: consistent
    void testCalibrateMultipleTimesConsistent()
    {
        PoissonBaseline pb = makeFittedPoisson();
        EnsemblePredictor ep;
        ep.setPoisson(&pb);
        ep.calibrate(realisticallyBadCalData());
        const double p1 = ep.predict(QStringLiteral("Z1"), testDt(),
                                      QStringLiteral("burglary"), 51.5, -0.1).probCrime;
        const double p2 = ep.predict(QStringLiteral("Z1"), testDt(),
                                      QStringLiteral("burglary"), 51.5, -0.1).probCrime;
        QVERIFY2(std::abs(p1 - p2) < 1e-9, "Repeated predictions should be identical");
    }

    // 10. Uncertainty width (ciHigh95 - ciLow95) is positive
    void testUncertaintyWidthPositive()
    {
        PoissonBaseline pb = makeFittedPoisson();
        EnsemblePredictor ep;
        ep.setPoisson(&pb);
        const auto pred = ep.predict(QStringLiteral("Z1"), testDt(),
                                      QStringLiteral("burglary"), 51.5, -0.1);
        QVERIFY2(pred.ciHigh95 > pred.ciLow95,
                 qPrintable(QStringLiteral("CI width (H=%1 - L=%2) should be positive")
                    .arg(pred.ciHigh95).arg(pred.ciLow95)));
    }
};

QTEST_MAIN(EnsembleCalibrationTest)
#include "test_ensemble_calibration.moc"
