// test_ensemble_predictor_deep7.cpp — Deep audit iteration 24: EnsemblePredictor
// calibration, weight normalisation, dominantModel, uncertainty decomposition.
#include <QTest>
#include <QDateTime>
#include <QTimeZone>
#include <cmath>
#include "models/EnsemblePredictor.h"
#include "models/PoissonBaseline.h"
#include "models/HawkesProcess.h"

class TestEnsemblePredictorDeep7 : public QObject
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

private slots:

    void testIsotonicCalibrationMonotone()
    {
        QVector<QPair<double, double>> data;
        for (int i = 0; i < 20; ++i)
            data.append({ static_cast<double>(i) / 19.0, static_cast<double>(i) / 19.0 });

        EnsemblePredictor ep;
        ep.calibrate(data);

        double prev = -1.0;
        for (int i = 0; i <= 10; ++i) {
            const double raw = i / 10.0;
            const double cal = ep.applyCalibration(raw);
            QVERIFY2(cal >= prev - 1e-9,
                     qPrintable(QStringLiteral("calibration not monotone at %1").arg(raw)));
            prev = cal;
        }
    }

    void testWeightsNormaliseToOne()
    {
        PoissonBaseline pb = fittedPoisson(QStringLiteral("W"));
        EnsemblePredictor ep;
        ep.setPoisson(&pb);
        ep.setWeights(0.7, 0.3);

        const auto pred = ep.predict(QStringLiteral("W"), utcDt(),
                                     QStringLiteral("burglary"), 51.5, -0.1);
        QVERIFY2(std::abs(pred.poissonWeight + pred.hawkesWeight - 1.0) < 1e-6,
                 qPrintable(QStringLiteral("weights sum=%1")
                                .arg(pred.poissonWeight + pred.hawkesWeight)));
    }

    void testDominantModelFieldPopulated()
    {
        PoissonBaseline pb = fittedPoisson(QStringLiteral("D"));
        HawkesProcess hp;
        QVector<SpatiotemporalEvent> evs;
        for (int i = 0; i < 15; ++i) {
            SpatiotemporalEvent e;
            e.tDays     = static_cast<double>(i);
            e.lat       = 51.5;
            e.lon       = -0.1;
            e.crimeType = QStringLiteral("burglary");
            evs.append(e);
        }
        hp.fit(evs, 5);

        EnsemblePredictor ep;
        ep.setPoisson(&pb);
        ep.setHawkes(&hp);
        ep.setWeights(0.9, 0.1);

        const auto pred = ep.predict(QStringLiteral("D"), utcDt(),
                                     QStringLiteral("burglary"), 51.5, -0.1);
        QVERIFY(!pred.dominantModel.isEmpty());
        QVERIFY(pred.dominantModel == QStringLiteral("poisson")
                || pred.dominantModel == QStringLiteral("hawkes")
                || pred.dominantModel == QStringLiteral("equal"));
    }

    void testUncertaintyFieldsNonNegative()
    {
        PoissonBaseline pb = fittedPoisson(QStringLiteral("U"));
        EnsemblePredictor ep;
        ep.setPoisson(&pb);
        ep.setWeights(1.0, 0.0);

        const auto pred = ep.predict(QStringLiteral("U"), utcDt(),
                                     QStringLiteral("burglary"), 51.5, -0.1);
        QVERIFY2(pred.uncertaintyAleatoric >= 0.0,
                 qPrintable(QStringLiteral("aleatoric=%1").arg(pred.uncertaintyAleatoric)));
        QVERIFY2(pred.uncertaintyEpistemic >= 0.0,
                 qPrintable(QStringLiteral("epistemic=%1").arg(pred.uncertaintyEpistemic)));
    }

    void testUnfittedPredictStillBounded()
    {
        EnsemblePredictor ep;
        const auto pred = ep.predict(QStringLiteral("X"), utcDt(),
                                     QStringLiteral("burglary"), 51.5, -0.1);
        QVERIFY2(pred.probCrime >= 0.0 && pred.probCrime <= 1.0,
                 qPrintable(QStringLiteral("prob=%1").arg(pred.probCrime)));
    }

    void testECEAfterCalibrationData()
    {
        QVector<QPair<double, double>> data;
        for (int i = 0; i < 50; ++i) {
            const double p = static_cast<double>(i) / 49.0;
            data.append({ p, p > 0.5 ? 1.0 : 0.0 });
        }
        const double ece = EnsemblePredictor::ece(data, 10);
        QVERIFY(std::isfinite(ece));
        QVERIFY2(ece >= 0.0, qPrintable(QStringLiteral("ECE=%1").arg(ece)));
    }

    void testBrierScorePerfectCalibration()
    {
        QVector<QPair<double, double>> perfect;
        perfect.append({ 0.0, 0.0 });
        perfect.append({ 1.0, 1.0 });
        const double brier = EnsemblePredictor::brierScore(perfect);
        QVERIFY2(std::abs(brier) < 1e-9,
                 qPrintable(QStringLiteral("brier=%1").arg(brier)));
    }
};

QTEST_GUILESS_MAIN(TestEnsemblePredictorDeep7)
#include "test_ensemble_predictor_deep7.moc"
