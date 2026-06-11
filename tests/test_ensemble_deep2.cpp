// test_ensemble_deep2.cpp — Deep audit of EnsemblePredictor weight normalization,
// ECE, Brier score, and calibration flag.
#include <QTest>
#include <QTimeZone>
#include <cmath>
#include "models/EnsemblePredictor.h"
#include "models/PoissonBaseline.h"
#include "models/HawkesProcess.h"

class TestEnsembleDeep2 : public QObject {
    Q_OBJECT

    static PoissonBaseline makeFittedPoisson()
    {
        PoissonBaseline pb;
        QVector<PoissonBaseline::EventRecord> evs;
        const QDateTime base(QDate(2024, 1, 1), QTime(9, 0, 0), QTimeZone::utc());
        for (int i = 0; i < 30; ++i) {
            PoissonBaseline::EventRecord r;
            r.zoneId    = QStringLiteral("Z1");
            r.crimeType = QStringLiteral("burglary");
            r.occurredAt = base.addDays(i);
            evs.append(r);
        }
        pb.fit(evs);
        return pb;
    }

    static HawkesProcess makeFittedHawkes()
    {
        HawkesProcess hp;
        QVector<SpatiotemporalEvent> evs;
        for (int i = 0; i < 15; ++i) {
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

    static QDateTime testDt()
    {
        return QDateTime(QDate(2024, 6, 15), QTime(14, 0, 0), QTimeZone::utc());
    }

private slots:

    void testSetWeights31NormalizesToPointSevenFive()
    {
        PoissonBaseline pb = makeFittedPoisson();
        HawkesProcess   hp = makeFittedHawkes();
        EnsemblePredictor ep;
        ep.setPoisson(&pb);
        ep.setHawkes(&hp);
        ep.setWeights(3.0, 1.0);

        const auto pred = ep.predict(QStringLiteral("Z1"), testDt(),
                                     QStringLiteral("burglary"), 51.5, -0.1);
        QVERIFY2(std::abs(pred.poissonWeight - 0.75) < 1e-9,
                 qPrintable(QStringLiteral("poissonWeight=%1, expected 0.75").arg(pred.poissonWeight)));
        QVERIFY2(std::abs(pred.hawkesWeight - 0.25) < 1e-9,
                 qPrintable(QStringLiteral("hawkesWeight=%1, expected 0.25").arg(pred.hawkesWeight)));
    }

    void testSetWeightsZeroFallbackToEqualWeights()
    {
        PoissonBaseline pb = makeFittedPoisson();
        HawkesProcess   hp = makeFittedHawkes();
        EnsemblePredictor ep;
        ep.setPoisson(&pb);
        ep.setHawkes(&hp);
        ep.setWeights(0.0, 0.0);

        const auto pred = ep.predict(QStringLiteral("Z1"), testDt(),
                                     QStringLiteral("burglary"), 51.5, -0.1);
        QVERIFY2(std::abs(pred.poissonWeight - 0.5) < 1e-9,
                 qPrintable(QStringLiteral("poissonWeight=%1, expected 0.5").arg(pred.poissonWeight)));
        QVERIFY2(std::abs(pred.hawkesWeight - 0.5) < 1e-9,
                 qPrintable(QStringLiteral("hawkesWeight=%1, expected 0.5").arg(pred.hawkesWeight)));
    }

    void testECEPerfectlyCalibrated()
    {
        QVector<QPair<double, double>> data;
        data.reserve(100);
        for (int i = 0; i < 100; ++i) {
            const double v = (i < 50) ? 1.0 : 0.0;
            data.append({v, v});
        }
        const double e = EnsemblePredictor::ece(data, 10);
        QVERIFY2(e < 1e-9,
                 qPrintable(QStringLiteral("ECE on perfect predictions=%1, expected 0.0").arg(e)));
    }

    void testBrierScorePerfectPredictions()
    {
        QVector<QPair<double, double>> data;
        data.reserve(100);
        for (int i = 0; i < 100; ++i) {
            const double v = (i < 50) ? 1.0 : 0.0;
            data.append({v, v});
        }
        const double bs = EnsemblePredictor::brierScore(data);
        QVERIFY2(bs < 1e-9,
                 qPrintable(QStringLiteral("Brier(perfect)=%1, expected 0.0").arg(bs)));
    }

    void testBrierScoreWorstPredictions()
    {
        QVector<QPair<double, double>> data;
        data.reserve(100);
        for (int i = 0; i < 100; ++i)
            data.append({1.0, 0.0});
        const double bs = EnsemblePredictor::brierScore(data);
        QVERIFY2(std::abs(bs - 1.0) < 1e-9,
                 qPrintable(QStringLiteral("Brier(worst)=%1, expected 1.0").arg(bs)));
    }

    void testCalibrateAndPredictSetsCalibratedFlag()
    {
        PoissonBaseline pb = makeFittedPoisson();
        EnsemblePredictor ep;
        ep.setPoisson(&pb);

        QVector<QPair<double, double>> calData;
        calData.reserve(20);
        for (int i = 0; i < 20; ++i)
            calData.append({i / 20.0, (i % 2 == 0) ? 1.0 : 0.0});

        ep.calibrate(calData);
        const auto pred = ep.predict(QStringLiteral("Z1"), testDt(),
                                     QStringLiteral("burglary"), 51.5, -0.1);
        QVERIFY2(pred.calibrated,
                 "calibrated flag must be true after calibrate() is called");
    }
};

QTEST_GUILESS_MAIN(TestEnsembleDeep2)
#include "test_ensemble_deep2.moc"
