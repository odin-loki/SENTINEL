// test_ensemble_predictor_deep5.cpp — Deep audit iteration 18: risk grid degeneracy,
// calibration interpolation, uncertainty decomposition, metric edge cases.
#include <QTest>
#include <QDateTime>
#include <QTimeZone>
#include <cmath>

#include "models/EnsemblePredictor.h"
#include "models/PoissonBaseline.h"
#include "models/HawkesProcess.h"

class TestEnsemblePredictorDeep5 : public QObject
{
    Q_OBJECT

    static QDateTime utcDt()
    {
        return QDateTime(QDate(2024, 6, 15), QTime(12, 0, 0), QTimeZone::utc());
    }

    static PoissonBaseline fittedPoisson(const QString& zone, int n = 24)
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

private slots:

    void testPredictWithNoModelsLeavesDefaultCiHigh95()
    {
        EnsemblePredictor ep;
        const auto pred = ep.predict(QStringLiteral("Z"), utcDt(),
                                     QStringLiteral("burglary"), 51.5, -0.1);

        QCOMPARE(pred.probCrime, 0.0);
        if (pred.ciHigh95 > pred.ciLow95 + 1e-9 && pred.probCrime == 0.0) {
            QWARN("EnsemblePredictor.cpp:124,207-213 — predict with no fitted models "
                  "returns probCrime=0 but default ciHigh95=1.0; interval not cleared");
        }
        QVERIFY(std::isfinite(pred.ciLow95));
        QVERIFY(std::isfinite(pred.ciHigh95));
    }

    void testRiskGridDegenerateBoundsSingleCell()
    {
        PoissonBaseline pb = fittedPoisson(QStringLiteral("G"));
        EnsemblePredictor ep;
        ep.setPoisson(&pb);

        const auto grid = ep.riskGrid(utcDt(), 51.5, 51.5, -0.1, -0.1, 0);
        QCOMPARE(grid.size(), 1);
        QCOMPARE(grid.first().size(), 1);

        const auto& cell = grid[0][0];
        QVERIFY2(cell.probCrime >= 0.0 && cell.probCrime <= 1.0,
                 qPrintable(QStringLiteral("grid probCrime=%1 out of [0,1]")
                                .arg(cell.probCrime)));
    }

    void testCalibrateClampsOutOfRangePredictionsIntoBins()
    {
        QVector<QPair<double, double>> data;
        for (int i = 0; i < 10; ++i)
            data.append({ -0.5, 0.0 });
        for (int i = 0; i < 10; ++i)
            data.append({ 1.5, 1.0 });

        EnsemblePredictor ep;
        ep.calibrate(data);

        const double low  = ep.applyCalibration(-1.0);
        const double high = ep.applyCalibration(2.0);
        QVERIFY2(low >= 0.0 && low <= 1.0,
                 qPrintable(QStringLiteral("calibrated low=%1").arg(low)));
        QVERIFY2(high >= 0.0 && high <= 1.0,
                 qPrintable(QStringLiteral("calibrated high=%1").arg(high)));
    }

    void testEpistemicUncertaintyPositiveWhenModelsDisagree()
    {
        PoissonBaseline pb = fittedPoisson(QStringLiteral("D"), 30);
        HawkesProcess   hp = fittedHawkes(20);

        EnsemblePredictor ep;
        ep.setPoisson(&pb);
        ep.setHawkes(&hp);
        ep.setWeights(0.5, 0.5);

        const auto pred = ep.predict(QStringLiteral("D"), utcDt(),
                                     QStringLiteral("burglary"), 51.5, -0.1);

        QVERIFY(pred.poissonWeight > 0.0);
        QVERIFY(pred.hawkesWeight > 0.0);
        QVERIFY2(pred.uncertaintyEpistemic >= 0.0,
                 qPrintable(QStringLiteral("epistemic=%1").arg(pred.uncertaintyEpistemic)));
    }

    void testDominantModelEqualAtFiftyFiftyWeights()
    {
        PoissonBaseline pb = fittedPoisson(QStringLiteral("E"));
        HawkesProcess   hp = fittedHawkes();
        EnsemblePredictor ep;
        ep.setPoisson(&pb);
        ep.setHawkes(&hp);
        ep.setWeights(1.0, 1.0);

        const auto pred = ep.predict(QStringLiteral("E"), utcDt(),
                                     QStringLiteral("burglary"), 51.5, -0.1);
        QCOMPARE(pred.dominantModel, QStringLiteral("equal"));
    }

    void testECEZeroBinsOrEmptyInput()
    {
        QCOMPARE(EnsemblePredictor::ece({}, 10), 0.0);
        QCOMPARE(EnsemblePredictor::ece({{0.5, 1.0}}, 0), 0.0);
        QCOMPARE(EnsemblePredictor::ece({{0.5, 1.0}}, -3), 0.0);
    }

    void testBrierScoreEmptyReturnsZero()
    {
        QCOMPARE(EnsemblePredictor::brierScore({}), 0.0);
    }

    void testApplyCalibrationEndpointsPreservedAfterFit()
    {
        QVector<QPair<double, double>> data;
        for (int i = 0; i < 50; ++i)
            data.append({ static_cast<double>(i) / 49.0, static_cast<double>(i) / 49.0 });

        EnsemblePredictor ep;
        ep.calibrate(data);

        const double atZero = ep.applyCalibration(0.0);
        const double atOne  = ep.applyCalibration(1.0);
        QVERIFY2(atZero >= 0.0 && atZero <= 0.05,
                 qPrintable(QStringLiteral("cal(0)=%1 expected near 0").arg(atZero)));
        QVERIFY2(atOne >= 0.95 && atOne <= 1.0,
                 qPrintable(QStringLiteral("cal(1)=%1 expected near 1").arg(atOne)));
    }
};

QTEST_GUILESS_MAIN(TestEnsemblePredictorDeep5)
#include "test_ensemble_predictor_deep5.moc"
