// test_ensemble_predictor_deep6.cpp — Deep audit iteration 21: EnsemblePredictor
// Verifies: calibration sample gate, ECE bin safety, weight normalization,
// risk-grid degeneracy, uncertainty fields, Brier score, heuristic count CI.
#include <QTest>
#include <QDateTime>
#include <QTimeZone>
#include <cmath>

#include "models/EnsemblePredictor.h"
#include "models/PoissonBaseline.h"
#include "models/HawkesProcess.h"

class TestEnsemblePredictorDeep6 : public QObject
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

    void testCalibrateRequiresAtLeastTenSamples()
    {
        QVector<QPair<double, double>> nine;
        for (int i = 0; i < 9; ++i)
            nine.append({ static_cast<double>(i) / 8.0, static_cast<double>(i) / 8.0 });

        EnsemblePredictor ep;
        ep.calibrate(nine);

        const double raw = 0.42;
        const double out = ep.applyCalibration(raw);
        if (out != raw) {
            QWARN("EnsemblePredictor.cpp:60 — calibrate() should no-op when size < 10");
        }
        QCOMPARE(out, raw);
    }

    void testECENegativePredictionShouldBeClampedBeforeBinning()
    {
        QVector<QPair<double, double>> data;
        for (int i = 0; i < 20; ++i)
            data.append({ -0.25 + static_cast<double>(i) * 0.05, 0.0 });

        const double ece = EnsemblePredictor::ece(data, 5);
        QVERIFY(std::isfinite(ece));
        QVERIFY2(ece >= 0.0,
                 qPrintable(QStringLiteral("ECE=%1 must be non-negative").arg(ece)));
    }

    void testSetWeightsBothZeroFallsBackToEqual()
    {
        PoissonBaseline pb = fittedPoisson(QStringLiteral("W"));
        HawkesProcess hp;
        QVector<SpatiotemporalEvent> evs;
        for (int i = 0; i < 12; ++i) {
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
        ep.setWeights(0.0, 0.0);

        const auto pred = ep.predict(QStringLiteral("W"), utcDt(),
                                     QStringLiteral("burglary"), 51.5, -0.1);
        QVERIFY2(std::abs(pred.poissonWeight - 0.5) < 1e-9,
                 qPrintable(QStringLiteral("poissonWeight=%1").arg(pred.poissonWeight)));
        QVERIFY2(std::abs(pred.hawkesWeight - 0.5) < 1e-9,
                 qPrintable(QStringLiteral("hawkesWeight=%1").arg(pred.hawkesWeight)));
    }

    void testRiskGridInvertedLatBoundsStillFinite()
    {
        PoissonBaseline pb = fittedPoisson(QStringLiteral("G"));
        EnsemblePredictor ep;
        ep.setPoisson(&pb);

        const auto grid = ep.riskGrid(utcDt(), 52.0, 51.0, -0.2, -0.1, 2);
        QCOMPARE(grid.size(), 2);
        QCOMPARE(grid.first().size(), 2);

        for (const auto& row : grid) {
            for (const auto& cell : row) {
                QVERIFY(std::isfinite(cell.probCrime));
                QVERIFY2(cell.probCrime >= 0.0 && cell.probCrime <= 1.0,
                         qPrintable(QStringLiteral("probCrime=%1").arg(cell.probCrime)));
            }
        }

        if (grid[0][0].probCrime != grid[1][1].probCrime) {
            QWARN("EnsemblePredictor.cpp:233-241 — riskGrid() does not normalize when "
                  "latMin > latMax; cell coordinates may traverse inverted spans");
        }
    }

    void testPoissonOnlyLeavesEpistemicUncertaintyZero()
    {
        PoissonBaseline pb = fittedPoisson(QStringLiteral("P"));
        EnsemblePredictor ep;
        ep.setPoisson(&pb);

        const auto pred = ep.predict(QStringLiteral("P"), utcDt(),
                                     QStringLiteral("burglary"), 51.5, -0.1);
        QCOMPARE(pred.hawkesWeight, 0.0);
        QCOMPARE(pred.poissonWeight, 1.0);
        QCOMPARE(pred.uncertaintyEpistemic, 0.0);
        QVERIFY(pred.uncertaintyAleatoric >= 0.0);
    }

    void testCountCi90UsesFixedHeuristicMultipliers()
    {
        PoissonBaseline pb = fittedPoisson(QStringLiteral("C"), 24);
        EnsemblePredictor ep;
        ep.setPoisson(&pb);

        const auto pred = ep.predict(QStringLiteral("C"), utcDt(),
                                     QStringLiteral("burglary"), 51.5, -0.1);
        QVERIFY(pred.expectedCount > 0.0);

        const double lo = pred.expectedCount * 0.65;
        const double hi = pred.expectedCount * 1.55;
        if (std::abs(pred.ci90.first - lo) > 1e-9 ||
            std::abs(pred.ci90.second - hi) > 1e-9) {
            QWARN("EnsemblePredictor.cpp:203 — ci90 on count uses fixed 0.65/1.55 "
                  "multipliers rather than model credible intervals");
        }
        QCOMPARE(pred.ci90.first, lo);
        QCOMPARE(pred.ci90.second, hi);
    }

    void testBrierScorePerfectPredictions()
    {
        const QVector<QPair<double, double>> perfect = {
            { 0.0, 0.0 }, { 1.0, 1.0 }, { 0.5, 0.5 }
        };
        const double score = EnsemblePredictor::brierScore(perfect);
        QCOMPARE(score, 0.0);
    }

    void testCalibrateExactlyTenSamplesEnablesInterpolation()
    {
        QVector<QPair<double, double>> data;
        for (int i = 0; i < 10; ++i)
            data.append({ static_cast<double>(i) / 9.0, static_cast<double>(i) / 9.0 });

        EnsemblePredictor ep;
        ep.calibrate(data);

        const double mid = ep.applyCalibration(0.5);
        QVERIFY2(mid >= 0.0 && mid <= 1.0,
                 qPrintable(QStringLiteral("cal(0.5)=%1").arg(mid)));
        QVERIFY2(std::abs(mid - 0.5) < 0.15,
                 qPrintable(QStringLiteral("cal(0.5)=%1 expected near 0.5").arg(mid)));
    }
};

QTEST_GUILESS_MAIN(TestEnsemblePredictorDeep6)
#include "test_ensemble_predictor_deep6.moc"
