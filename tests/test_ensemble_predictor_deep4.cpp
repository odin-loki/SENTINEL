// test_ensemble_predictor_deep4.cpp — Deep audit iteration 15: unfitted models,
// calibration edge cases, Brier/ECE bounds.
#include <QTest>
#include <QDateTime>
#include <QTimeZone>
#include <cmath>

#include "models/EnsemblePredictor.h"
#include "models/PoissonBaseline.h"
#include "models/HawkesProcess.h"

class TestEnsemblePredictorDeep4 : public QObject
{
    Q_OBJECT

    static QDateTime utcDt()
    {
        return QDateTime(QDate(2024, 6, 15), QTime(12, 0, 0), QTimeZone::utc());
    }

    static PoissonBaseline fittedPoisson(const QString& zone, int n = 20)
    {
        PoissonBaseline pb;
        QVector<PoissonBaseline::EventRecord> recs;
        const QDateTime base(QDate(2023, 1, 2), QTime(9, 0, 0), QTimeZone::utc());
        for (int i = 0; i < n; ++i) {
            PoissonBaseline::EventRecord r;
            r.zoneId     = zone;
            r.crimeType  = QStringLiteral("burglary");
            r.occurredAt = base.addDays(i * 7);
            recs.append(r);
        }
        pb.fit(recs);
        return pb;
    }

    static HawkesProcess fittedHawkes(int n = 15)
    {
        HawkesProcess hp;
        QVector<SpatiotemporalEvent> evs;
        for (int i = 0; i < n; ++i) {
            SpatiotemporalEvent e;
            e.tDays     = static_cast<double>(i);
            e.lat       = 51.5 + i * 0.001;
            e.lon       = -0.1 + i * 0.001;
            e.crimeType = QStringLiteral("burglary");
            evs.append(e);
        }
        hp.fit(evs, 5);
        return hp;
    }

    static QVector<QPair<double, double>> calData(int n, double pred, double act)
    {
        QVector<QPair<double, double>> data;
        data.reserve(n);
        for (int i = 0; i < n; ++i)
            data.append({pred, act});
        return data;
    }

private slots:

    void testPredictWithNoModelsReturnsZeroProbability()
    {
        EnsemblePredictor ep;
        QVERIFY(!ep.isReady());

        const auto pred = ep.predict(QStringLiteral("Z"), utcDt(),
                                     QStringLiteral("burglary"), 51.5, -0.1);
        QCOMPARE(pred.probCrime, 0.0);
        QCOMPARE(pred.expectedCount, 0.0);
        QVERIFY(!pred.calibrated);
    }

    void testPredictWithUnfittedPoissonOnly()
    {
        PoissonBaseline pb;
        pb.fit({});
        QVERIFY(!pb.isFitted());

        EnsemblePredictor ep;
        ep.setPoisson(&pb);
        QVERIFY(ep.isReady());

        const auto pred = ep.predict(QStringLiteral("Z"), utcDt(),
                                     QStringLiteral("burglary"), 51.5, -0.1);
        QCOMPARE(pred.probCrime, 0.0);
        QCOMPARE(pred.expectedCount, 0.0);
        QCOMPARE(pred.poissonWeight, 0.0);
        QCOMPARE(pred.hawkesWeight, 0.0);
    }

    void testPredictWithUnfittedHawkesOnly()
    {
        HawkesProcess hp;
        hp.fit({}, 5);
        QVERIFY(!hp.isFitted());

        EnsemblePredictor ep;
        ep.setHawkes(&hp);

        const auto pred = ep.predict(QStringLiteral("Z"), utcDt(),
                                     QStringLiteral("burglary"), 51.5, -0.1);
        QCOMPARE(pred.probCrime, 0.0);
        QCOMPARE(pred.hawkesWeight, 0.0);
    }

    void testPredictBothAttachedButUnfitted()
    {
        PoissonBaseline pb;
        HawkesProcess   hp;
        pb.fit({});
        hp.fit({}, 5);

        EnsemblePredictor ep;
        ep.setPoisson(&pb);
        ep.setHawkes(&hp);
        QVERIFY(ep.isReady());

        const auto pred = ep.predict(QStringLiteral("Z"), utcDt(),
                                     QStringLiteral("burglary"), 51.5, -0.1);
        QCOMPARE(pred.probCrime, 0.0);
        QVERIFY(std::isfinite(pred.ciLow95));
        QVERIFY(std::isfinite(pred.ciHigh95));
    }

    void testCalibrateNineSamplesLeavesUncalibrated()
    {
        EnsemblePredictor ep;
        ep.calibrate(calData(9, 0.5, 1.0));
        QCOMPARE(ep.applyCalibration(0.5), 0.5);

        PoissonBaseline pb = fittedPoisson(QStringLiteral("C"));
        ep.setPoisson(&pb);
        const auto pred = ep.predict(QStringLiteral("C"), utcDt(),
                                     QStringLiteral("burglary"), 51.5, -0.1);
        QVERIFY(!pred.calibrated);
    }

    void testCalibrateExactlyTenSamplesEnablesCalibration()
    {
        EnsemblePredictor ep;
        ep.calibrate(calData(10, 0.4, 1.0));
        const double cal = ep.applyCalibration(0.4);
        QVERIFY(cal >= 0.0 && cal <= 1.0);
        QVERIFY2(cal >= 0.4 - 1e-9,
                 qPrintable(QStringLiteral("calibrated=%1 should reflect positive outcomes")
                                .arg(cal)));

        PoissonBaseline pb = fittedPoisson(QStringLiteral("T"));
        ep.setPoisson(&pb);
        const auto pred = ep.predict(QStringLiteral("T"), utcDt(),
                                     QStringLiteral("burglary"), 51.5, -0.1);
        QVERIFY(pred.calibrated);
    }

    void testApplyCalibrationWithoutFitIsIdentity()
    {
        EnsemblePredictor ep;
        for (double p : {0.0, 0.25, 0.5, 0.75, 1.0})
            QCOMPARE(ep.applyCalibration(p), p);
    }

    void testECEBoundedZeroToOne()
    {
        QVector<QPair<double, double>> mixed;
        for (int i = 0; i < 200; ++i)
            mixed.append({static_cast<double>(i % 10) / 10.0, (i % 3 == 0) ? 1.0 : 0.0});

        const double e = EnsemblePredictor::ece(mixed, 10);
        QVERIFY2(e >= 0.0 && e <= 1.0,
                 qPrintable(QStringLiteral("ECE=%1 outside [0,1]").arg(e)));

        QVector<QPair<double, double>> worst;
        for (int i = 0; i < 100; ++i)
            worst.append({0.0, 1.0});
        const double eWorst = EnsemblePredictor::ece(worst, 1);
        QVERIFY2(eWorst >= 0.0 && eWorst <= 1.0,
                 qPrintable(QStringLiteral("worst-case ECE=%1 outside [0,1]").arg(eWorst)));
    }

    void testBrierScoreBoundedZeroToOne()
    {
        QVector<QPair<double, double>> data;
        for (int i = 0; i < 50; ++i)
            data.append({0.7, (i % 2) ? 1.0 : 0.0});

        const double b = EnsemblePredictor::brierScore(data);
        QVERIFY2(b >= 0.0 && b <= 1.0,
                 qPrintable(QStringLiteral("Brier=%1 outside [0,1]").arg(b)));
        QVERIFY(b > 0.0);

        QCOMPARE(EnsemblePredictor::brierScore({{0.0, 1.0}, {1.0, 0.0}}), 1.0);
    }
};

QTEST_GUILESS_MAIN(TestEnsemblePredictorDeep4)
#include "test_ensemble_predictor_deep4.moc"
