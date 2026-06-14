// test_ensemble_predictor_deep8.cpp — Deep audit iteration 26: EnsemblePredictor
// CI90 ordering, calibrated flag, weight fractions, expectedCount.
#include <QtTest/QtTest>
#include "models/EnsemblePredictor.h"
#include "models/PoissonBaseline.h"
#include "models/HawkesProcess.h"
#include "core/CrimeEvent.h"

class EnsemblePredictorDeep8Test : public QObject
{
    Q_OBJECT

    static QDateTime utcDt()
    {
        return QDateTime(QDate(2024, 4, 15), QTime(23, 0), Qt::UTC);
    }

    static PoissonBaseline fittedPoisson(const QString& zone)
    {
        PoissonBaseline pb;
        QVector<PoissonBaseline::EventRecord> recs;
        for (int i = 0; i < 40; ++i) {
            PoissonBaseline::EventRecord r;
            r.zoneId     = zone;
            r.crimeType  = QStringLiteral("burglary");
            r.occurredAt = utcDt().addDays(-i);
            recs.append(r);
        }
        pb.fit(recs);
        return pb;
    }

private slots:

    void testCi90Ordering()
    {
        PoissonBaseline pb = fittedPoisson(QStringLiteral("CI"));
        EnsemblePredictor ep;
        ep.setPoisson(&pb);
        ep.setWeights(1.0, 0.0);

        const auto pred = ep.predict(QStringLiteral("CI"), utcDt(),
                                     QStringLiteral("burglary"), 51.5, -0.1);
        QVERIFY(pred.ci90.first <= pred.ci90.second);
        QVERIFY(pred.ci90.first >= 0.0);
    }

    void testWeightFractionsSumToOne()
    {
        PoissonBaseline pb = fittedPoisson(QStringLiteral("W"));
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
        ep.setWeights(0.6, 0.4);

        const auto pred = ep.predict(QStringLiteral("W"), utcDt(),
                                     QStringLiteral("burglary"), 51.5, -0.1);
        QVERIFY2(std::abs(pred.poissonWeight + pred.hawkesWeight - 1.0) < 0.05,
                 qPrintable(QStringLiteral("weights=%1+%2")
                                .arg(pred.poissonWeight).arg(pred.hawkesWeight)));
    }

    void testCalibratedAfterSetCalibrationTable()
    {
        QVector<QPair<double, double>> table;
        for (int i = 0; i < 10; ++i)
            table.append({ static_cast<double>(i) / 9.0, static_cast<double>(i) / 9.0 });

        PoissonBaseline pb = fittedPoisson(QStringLiteral("CAL"));
        EnsemblePredictor ep;
        ep.setPoisson(&pb);
        ep.setWeights(1.0, 0.0);
        ep.calibrate(table);

        const auto pred = ep.predict(QStringLiteral("CAL"), utcDt(),
                                     QStringLiteral("burglary"), 51.5, -0.1);
        QVERIFY(pred.calibrated);
    }

    void testExpectedCountNonNegative()
    {
        PoissonBaseline pb = fittedPoisson(QStringLiteral("EC"));
        EnsemblePredictor ep;
        ep.setPoisson(&pb);
        ep.setWeights(1.0, 0.0);

        const auto pred = ep.predict(QStringLiteral("EC"), utcDt(),
                                     QStringLiteral("burglary"), 51.5, -0.1);
        QVERIFY(pred.expectedCount >= 0.0);
    }

    void testCi95BoundsBracketProbCrime()
    {
        PoissonBaseline pb = fittedPoisson(QStringLiteral("BND"));
        EnsemblePredictor ep;
        ep.setPoisson(&pb);
        ep.setWeights(1.0, 0.0);

        const auto pred = ep.predict(QStringLiteral("BND"), utcDt(),
                                     QStringLiteral("burglary"), 51.5, -0.1);
        QVERIFY(pred.ciLow95 <= pred.probCrime + 1e-6);
        QVERIFY(pred.ciHigh95 >= pred.probCrime - 1e-6);
    }
};

QTEST_GUILESS_MAIN(EnsemblePredictorDeep8Test)
#include "test_ensemble_predictor_deep8.moc"
