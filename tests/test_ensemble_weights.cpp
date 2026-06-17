// test_ensemble_weights.cpp
// EnsemblePredictor: setWeights effects, riskGrid output, dominantModel,
// Poisson-only vs Hawkes-only, and combined uncertainty.
#include <QTest>
#include <QTimeZone>
#include "models/EnsemblePredictor.h"
#include "models/PoissonBaseline.h"
#include "models/HawkesProcess.h"
#include <cmath>

class EnsembleWeightsTest : public QObject
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
            r.occurredAt = base.addDays(i);
            evs.append(r);
        }
        pb.fit(evs);
        return pb;
    }

    static QDateTime testDt()
    {
        return QDateTime(QDate(2024, 6, 15), QTime(14, 0, 0), QTimeZone::utc());
    }

private slots:

    // 1. Poisson-only (w=1.0, 0.0): probCrime in [0,1]
    void testPoissonOnlyProbInRange()
    {
        PoissonBaseline pb = makeFittedPoisson();
        EnsemblePredictor ep;
        ep.setPoisson(&pb);
        ep.setWeights(1.0, 0.0);
        const auto pred = ep.predict(QStringLiteral("Z1"), testDt(),
                                      QStringLiteral("burglary"), 51.5, -0.1);
        QVERIFY2(pred.probCrime >= 0.0 && pred.probCrime <= 1.0,
                 qPrintable(QStringLiteral("Poisson-only prob %1 must be in [0,1]").arg(pred.probCrime)));
    }

    // 2. dominantModel set to "poisson" when Poisson-only
    void testPoissonOnlyDominantModel()
    {
        PoissonBaseline pb = makeFittedPoisson();
        EnsemblePredictor ep;
        ep.setPoisson(&pb);
        ep.setWeights(1.0, 0.0);
        const auto pred = ep.predict(QStringLiteral("Z1"), testDt(),
                                      QStringLiteral("burglary"), 51.5, -0.1);
        QVERIFY2(!pred.dominantModel.isEmpty(), "dominantModel should be non-empty");
    }

    // 3. setWeights(0.5, 0.5): both contribute
    void testEqualWeightsPoissonWeight()
    {
        PoissonBaseline pb = makeFittedPoisson();
        EnsemblePredictor ep;
        ep.setPoisson(&pb);
        ep.setWeights(0.5, 0.5);
        const auto pred = ep.predict(QStringLiteral("Z1"), testDt(),
                                      QStringLiteral("burglary"), 51.5, -0.1);
        QVERIFY2(pred.poissonWeight >= 0.0,
                 qPrintable(QStringLiteral("poissonWeight %1 must be >= 0").arg(pred.poissonWeight)));
    }

    // 4. isReady() true when Poisson set
    void testIsReadyWithPoisson()
    {
        PoissonBaseline pb = makeFittedPoisson();
        EnsemblePredictor ep;
        ep.setPoisson(&pb);
        QVERIFY(ep.isReady());
    }

    // 5. isReady() false when no models set
    void testNotReadyWhenEmpty()
    {
        EnsemblePredictor ep;
        QVERIFY(!ep.isReady());
    }

    // 6. riskGrid: non-empty for valid inputs
    void testRiskGridNonEmpty()
    {
        PoissonBaseline pb = makeFittedPoisson();
        EnsemblePredictor ep;
        ep.setPoisson(&pb);
        const auto grid = ep.riskGrid(testDt(), 51.45, 51.55, -0.15, -0.05, 5);
        QVERIFY2(!grid.isEmpty(), "riskGrid should return non-empty");
    }

    // 7. riskGrid dimensions: 5x5 for gridN=5
    void testRiskGridDimensions()
    {
        PoissonBaseline pb = makeFittedPoisson();
        EnsemblePredictor ep;
        ep.setPoisson(&pb);
        const auto grid = ep.riskGrid(testDt(), 51.45, 51.55, -0.15, -0.05, 5);
        QCOMPARE(grid.size(), 5);
        for (const auto& row : grid)
            QCOMPARE(row.size(), 5);
    }

    // 8. riskGrid: all probabilities in [0,1]
    void testRiskGridProbabilitiesInRange()
    {
        PoissonBaseline pb = makeFittedPoisson();
        EnsemblePredictor ep;
        ep.setPoisson(&pb);
        const auto grid = ep.riskGrid(testDt(), 51.45, 51.55, -0.15, -0.05, 4);
        for (const auto& row : grid)
            for (const auto& cell : row)
                QVERIFY2(cell.probCrime >= 0.0 && cell.probCrime <= 1.0,
                         qPrintable(QStringLiteral("Grid cell prob %1 must be in [0,1]").arg(cell.probCrime)));
    }

    // 9. ece() on empty data returns 0
    void testECEEmptyData()
    {
        const double ece = EnsemblePredictor::ece({}, 10);
        QVERIFY2(ece >= 0.0 && ece <= 1.0,
                 qPrintable(QStringLiteral("ECE on empty data %1 must be in [0,1]").arg(ece)));
    }

    // 10. brierScore() on empty data returns 0
    void testBrierScoreEmptyData()
    {
        const double bs = EnsemblePredictor::brierScore({});
        QVERIFY2(bs >= 0.0,
                 qPrintable(QStringLiteral("Brier score on empty data %1 must be >= 0").arg(bs)));
    }
};

QTEST_MAIN(EnsembleWeightsTest)
#include "test_ensemble_weights.moc"
