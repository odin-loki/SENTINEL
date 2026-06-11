// test_ensemble_predictor_deep3.cpp — iteration-13 deep audit: weights, PAVA, ECE, Brier, risk grid
#include <QTest>
#include <QDateTime>
#include <QTimeZone>
#include <cmath>

#include "models/EnsemblePredictor.h"
#include "models/PoissonBaseline.h"
#include "models/HawkesProcess.h"

class TestEnsemblePredictorDeep3 : public QObject
{
    Q_OBJECT

    static QDateTime utcDt()
    {
        return QDateTime(QDate(2024, 6, 15), QTime(12, 0, 0), QTimeZone::utc());
    }

    static PoissonBaseline fittedPoisson(const QString& zone, int n = 25)
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

    static QVector<QPair<double, double>> monotonicCalData()
    {
        QVector<QPair<double, double>> data;
        data.reserve(100);
        for (int i = 0; i < 100; ++i) {
            const double p = static_cast<double>(i) / 99.0;
            data.append({p, p});
        }
        return data;
    }

    static QVector<QPair<double, double>> violatorCalData()
    {
        // Deliberately non-monotonic bin means after naive averaging
        QVector<QPair<double, double>> data;
        data.reserve(120);
        for (int i = 0; i < 60; ++i)
            data.append({0.2 + (i % 10) * 0.01, 0.8});
        for (int i = 0; i < 60; ++i)
            data.append({0.5 + (i % 10) * 0.01, 0.2});
        return data;
    }

private slots:

    void testWeightNormalizationSumsToOne()
    {
        PoissonBaseline pb = fittedPoisson(QStringLiteral("W"));
        HawkesProcess   hp = fittedHawkes();

        const QList<QPair<double, double>> raw = {
            {3.0, 7.0}, {1.0, 1.0}, {0.1, 0.9}, {10.0, 1.0},
        };

        for (const auto& [wP, wH] : raw) {
            EnsemblePredictor ep;
            ep.setPoisson(&pb);
            ep.setHawkes(&hp);
            ep.setWeights(wP, wH);

            const auto pred = ep.predict(QStringLiteral("W"), utcDt(),
                                         QStringLiteral("burglary"), 51.5, -0.1);
            const double sum = pred.poissonWeight + pred.hawkesWeight;
            QVERIFY2(std::abs(sum - 1.0) < 1e-9,
                     qPrintable(QStringLiteral("weights (%1,%2) sum=%3")
                                    .arg(wP).arg(wH).arg(sum)));
        }
    }

    void testZeroWeightsFallbackEqual()
    {
        PoissonBaseline pb = fittedPoisson(QStringLiteral("Z"));
        HawkesProcess   hp = fittedHawkes();
        EnsemblePredictor ep;
        ep.setPoisson(&pb);
        ep.setHawkes(&hp);
        ep.setWeights(0.0, 0.0);

        const auto pred = ep.predict(QStringLiteral("Z"), utcDt(),
                                     QStringLiteral("burglary"), 51.5, -0.1);
        QVERIFY(std::abs(pred.poissonWeight - 0.5) < 1e-9);
        QVERIFY(std::abs(pred.hawkesWeight  - 0.5) < 1e-9);
    }

    void testIsotonicCalibrationMonotone()
    {
        EnsemblePredictor ep;
        ep.calibrate(monotonicCalData());

        double prev = -1.0;
        for (int i = 0; i <= 20; ++i) {
            const double raw = static_cast<double>(i) / 20.0;
            const double cal = ep.applyCalibration(raw);
            QVERIFY2(cal >= prev - 1e-9,
                     qPrintable(QStringLiteral("non-monotone at raw=%1 cal=%2 prev=%3")
                                    .arg(raw).arg(cal).arg(prev)));
            prev = cal;
        }
    }

    void testPavaFixesViolatorCalibration()
    {
        EnsemblePredictor ep;
        ep.calibrate(violatorCalData());

        double prev = -1.0;
        for (int i = 0; i <= 20; ++i) {
            const double raw = static_cast<double>(i) / 20.0;
            const double cal = ep.applyCalibration(raw);
            QVERIFY2(cal >= prev - 1e-9,
                     qPrintable(QStringLiteral("PAVA failed at raw=%1").arg(raw)));
            prev = cal;
        }
    }

    void testECEPerfectCalibration()
    {
        QVector<QPair<double, double>> data;
        for (int i = 0; i < 100; ++i) {
            const double v = (i < 50) ? 1.0 : 0.0;
            data.append({v, v});
        }
        QCOMPARE(EnsemblePredictor::ece(data, 10), 0.0);
    }

    void testECEMiscalibratedPositive()
    {
        QVector<QPair<double, double>> data;
        for (int i = 0; i < 100; ++i)
            data.append({0.9, (i < 50) ? 1.0 : 0.0});
        QVERIFY(EnsemblePredictor::ece(data, 10) > 0.3);
    }

    void testECEEmptyInput()
    {
        QCOMPARE(EnsemblePredictor::ece({}, 10), 0.0);
    }

    void testBrierScorePerfect()
    {
        QVector<QPair<double, double>> data;
        for (int i = 0; i < 50; ++i)
            data.append({1.0, 1.0});
        for (int i = 0; i < 50; ++i)
            data.append({0.0, 0.0});
        QCOMPARE(EnsemblePredictor::brierScore(data), 0.0);
    }

    void testBrierScoreWorst()
    {
        QVector<QPair<double, double>> data;
        for (int i = 0; i < 100; ++i)
            data.append({1.0, 0.0});
        QCOMPARE(EnsemblePredictor::brierScore(data), 1.0);
    }

    void testBrierScoreEmptyInput()
    {
        QCOMPARE(EnsemblePredictor::brierScore({}), 0.0);
    }

    void testRiskGridProbabilitiesInUnitInterval()
    {
        PoissonBaseline pb = fittedPoisson(QStringLiteral("G"));
        HawkesProcess   hp = fittedHawkes();
        EnsemblePredictor ep;
        ep.setPoisson(&pb);
        ep.setHawkes(&hp);

        const auto grid = ep.riskGrid(utcDt(), 51.48, 51.52, -0.12, -0.08, 5);
        QCOMPARE(grid.size(), 5);
        for (const auto& row : grid) {
            QCOMPARE(row.size(), 5);
            for (const auto& cell : row) {
                QVERIFY2(cell.probCrime >= 0.0 && cell.probCrime <= 1.0,
                         qPrintable(QStringLiteral("probCrime=%1 out of [0,1]")
                                        .arg(cell.probCrime)));
            }
        }
    }

    void testCalibrateInsufficientDataLeavesUncalibrated()
    {
        PoissonBaseline pb = fittedPoisson(QStringLiteral("C"));
        EnsemblePredictor ep;
        ep.setPoisson(&pb);

        QVector<QPair<double, double>> tiny;
        for (int i = 0; i < 5; ++i)
            tiny.append({0.1 * i, 0.0});

        ep.calibrate(tiny);
        const auto pred = ep.predict(QStringLiteral("C"), utcDt(),
                                     QStringLiteral("burglary"), 51.5, -0.1);
        QVERIFY(!pred.calibrated);
    }

    void testApplyCalibrationEndpoints()
    {
        EnsemblePredictor ep;
        ep.calibrate(monotonicCalData());
        QVERIFY(ep.applyCalibration(0.0) >= 0.0);
        QVERIFY(ep.applyCalibration(1.0) <= 1.0);
        QVERIFY(std::abs(ep.applyCalibration(0.0) - 0.0) < 0.05);
        QVERIFY(std::abs(ep.applyCalibration(1.0) - 1.0) < 0.05);
    }
};

QTEST_GUILESS_MAIN(TestEnsemblePredictorDeep3)
#include "test_ensemble_predictor_deep3.moc"
