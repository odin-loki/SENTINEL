// test_ensemble_predictor_deep9.cpp — Deep audit iteration 28: EnsemblePredictor
// brierScore, ece, applyCalibration, riskGrid dimensions.
#include <QtTest/QtTest>
#include <cmath>
#include "models/EnsemblePredictor.h"
#include "models/PoissonBaseline.h"

class EnsemblePredictorDeep9Test : public QObject
{
    Q_OBJECT

    static QDateTime utcDt()
    {
        return QDateTime(QDate(2024, 8, 1), QTime(23, 0), Qt::UTC);
    }

    static PoissonBaseline fittedPb(const QString& zone)
    {
        PoissonBaseline pb;
        QVector<PoissonBaseline::EventRecord> recs;
        for (int i = 0; i < 30; ++i) {
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

    void testBrierScorePerfectCalibration()
    {
        QVector<QPair<double, double>> data = {{0.0, 0.0}, {1.0, 1.0}};
        const double brier = EnsemblePredictor::brierScore(data);
        QCOMPARE(brier, 0.0);
    }

    void testECEBounded()
    {
        QVector<QPair<double, double>> data;
        for (int i = 0; i < 20; ++i)
            data.append({ i / 19.0, (i % 2 == 0) ? 0.0 : 1.0 });
        const double ece = EnsemblePredictor::ece(data, 5);
        QVERIFY(ece >= 0.0 && ece <= 1.0);
    }

    void testApplyCalibrationMonotoneTable()
    {
        QVector<QPair<double, double>> table = {{0.0, 0.0}, {0.5, 0.4}, {1.0, 1.0}};
        PoissonBaseline pb = fittedPb(QStringLiteral("CAL9"));
        EnsemblePredictor ep;
        ep.setPoisson(&pb);
        ep.setWeights(1.0, 0.0);
        ep.calibrate(table);

        const double raw = 0.5;
        const double cal = ep.applyCalibration(raw);
        QVERIFY2(cal >= 0.0 && cal <= 1.0,
                 qPrintable(QStringLiteral("cal=%1").arg(cal)));
    }

    void testRiskGridDimensions()
    {
        PoissonBaseline pb = fittedPb(QStringLiteral("GRID"));
        EnsemblePredictor ep;
        ep.setPoisson(&pb);
        ep.setWeights(1.0, 0.0);

        const auto grid = ep.riskGrid(utcDt(), 51.48, 51.52, -0.12, -0.08, 5);
        QCOMPARE(grid.size(), 5);
        for (const auto& row : grid)
            QCOMPARE(row.size(), 5);
    }

    void testIsReadyWithPoissonOnly()
    {
        PoissonBaseline pb = fittedPb(QStringLiteral("RDY"));
        EnsemblePredictor ep;
        ep.setPoisson(&pb);
        QVERIFY(ep.isReady());
    }
};

QTEST_GUILESS_MAIN(EnsemblePredictorDeep9Test)
#include "test_ensemble_predictor_deep9.moc"
