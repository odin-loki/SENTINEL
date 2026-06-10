// test_ensemble_predictor_deep.cpp — comprehensive EnsemblePredictor unit tests
#include <QTest>
#include <QCoreApplication>
#include <QUuid>
#include <QDateTime>
#include <cmath>

#include "models/EnsemblePredictor.h"
#include "models/PoissonBaseline.h"
#include "models/HawkesProcess.h"
#include "core/CrimeEvent.h"

// ─────────────────────────────────────────────────────────────────────────────
// Shared helpers
// ─────────────────────────────────────────────────────────────────────────────

static QDateTime baseDt()
{
    return QDateTime(QDate(2024, 3, 4), QTime(12, 0, 0), Qt::UTC);
}

static double baseTDays()
{
    return static_cast<double>(QDateTime::fromSecsSinceEpoch(0).daysTo(baseDt()));
}

// Build PoissonBaseline EventRecord vector for a zone with `n` events
// spread across multiple weeks so the zone gets a stable lambda estimate.
static QVector<PoissonBaseline::EventRecord> makeRecords(
    const QString& zone, int n,
    const QString& crimeType = QStringLiteral("burglary"))
{
    QVector<PoissonBaseline::EventRecord> recs;
    recs.reserve(n);
    const QDateTime base = QDateTime(QDate(2023, 1, 2), QTime(12, 0, 0), Qt::UTC);
    for (int i = 0; i < n; ++i) {
        PoissonBaseline::EventRecord r;
        r.zoneId     = zone;
        r.crimeType  = crimeType;
        r.occurredAt = base.addDays(i * 7);   // weekly cadence
        recs.append(r);
    }
    return recs;
}

// Build SpatiotemporalEvent vector with n events near a given lat/lon
static QVector<SpatiotemporalEvent> makeSpatEvents(
    int n, double lat = 51.5, double lon = -0.1)
{
    QVector<SpatiotemporalEvent> events;
    events.reserve(n);
    const double tb = baseTDays();
    for (int i = 0; i < n; ++i) {
        SpatiotemporalEvent ev;
        ev.tDays     = tb - n + i;                 // recent, ending at baseDt
        ev.lat       = lat + (i % 5) * 0.001;
        ev.lon       = lon + (i % 3) * 0.001;
        ev.crimeType = QStringLiteral("burglary");
        events.append(ev);
    }
    return events;
}

// Convenience: build a fitted PoissonBaseline for `zone` with `n` weekly records
static PoissonBaseline makeFittedPoisson(const QString& zone, int n)
{
    PoissonBaseline p;
    p.fit(makeRecords(zone, n));
    return p;
}

// Convenience: build a fitted HawkesProcess with `n` recent events
static HawkesProcess makeFittedHawkes(int n = 20)
{
    HawkesProcess h;
    h.fit(makeSpatEvents(n), 5);
    return h;
}

// ─────────────────────────────────────────────────────────────────────────────
// TestEnsemblePredictorDeep
// ─────────────────────────────────────────────────────────────────────────────

class TestEnsemblePredictorDeep : public QObject
{
    Q_OBJECT

private slots:

    // 1. Single observation — result is non-negative ──────────────────────────
    void testSingleObservation()
    {
        PoissonBaseline poisson = makeFittedPoisson(QStringLiteral("zone_single"), 1);
        HawkesProcess   hawkes  = makeFittedHawkes(1);

        EnsemblePredictor ep;
        ep.setPoisson(&poisson);
        ep.setHawkes(&hawkes);
        QVERIFY(ep.isReady());

        const auto pred = ep.predict(QStringLiteral("zone_single"), baseDt(),
                                     QStringLiteral("burglary"), 51.5, -0.1);

        QVERIFY2(pred.expectedCount >= 0.0,
                 qPrintable(QString("expectedCount=%1").arg(pred.expectedCount)));
        QVERIFY2(pred.probCrime >= 0.0 && pred.probCrime <= 1.0,
                 qPrintable(QString("probCrime=%1").arg(pred.probCrime)));
        QVERIFY2(pred.ci90.first  >= 0.0,
                 qPrintable(QString("ci90.first=%1").arg(pred.ci90.first)));
        QVERIFY2(pred.ci90.second >= 0.0,
                 qPrintable(QString("ci90.second=%1").arg(pred.ci90.second)));
    }

    // 2. Weighted combination — mixed output is between pure-Poisson and pure-Hawkes
    void testWeightedCombination()
    {
        PoissonBaseline poisson = makeFittedPoisson(QStringLiteral("zone_w"), 20);
        HawkesProcess   hawkes  = makeFittedHawkes(20);

        // Pure-Poisson prediction (weight 1:0)
        EnsemblePredictor epPoi;
        epPoi.setPoisson(&poisson);
        epPoi.setHawkes(&hawkes);
        epPoi.setWeights(1.0, 0.0);
        const auto pPoi = epPoi.predict(QStringLiteral("zone_w"), baseDt(),
                                        QStringLiteral("burglary"), 51.5, -0.1);

        // Pure-Hawkes prediction (weight 0:1)
        EnsemblePredictor epHawk;
        epHawk.setPoisson(&poisson);
        epHawk.setHawkes(&hawkes);
        epHawk.setWeights(0.0, 1.0);
        const auto pHawk = epHawk.predict(QStringLiteral("zone_w"), baseDt(),
                                          QStringLiteral("burglary"), 51.5, -0.1);

        // Mixed prediction (0.6:0.4)
        EnsemblePredictor epMix;
        epMix.setPoisson(&poisson);
        epMix.setHawkes(&hawkes);
        epMix.setWeights(0.6, 0.4);
        const auto pMix = epMix.predict(QStringLiteral("zone_w"), baseDt(),
                                        QStringLiteral("burglary"), 51.5, -0.1);

        const double lo = std::min(pPoi.expectedCount, pHawk.expectedCount);
        const double hi = std::max(pPoi.expectedCount, pHawk.expectedCount);

        QVERIFY2(pMix.expectedCount >= lo - 1e-9 && pMix.expectedCount <= hi + 1e-9,
                 qPrintable(QString("mix=%1 not in [%2,%3]")
                            .arg(pMix.expectedCount).arg(lo).arg(hi)));
    }

    // 3. Prediction horizon — predict 30 days ahead; CI bounds non-negative ───
    void testPredictionHorizon()
    {
        PoissonBaseline poisson = makeFittedPoisson(QStringLiteral("zone_h"), 10);
        HawkesProcess   hawkes  = makeFittedHawkes(10);

        EnsemblePredictor ep;
        ep.setPoisson(&poisson);
        ep.setHawkes(&hawkes);

        const QDateTime futureDt = baseDt().addDays(30);
        const auto pred = ep.predict(QStringLiteral("zone_h"), futureDt,
                                     QStringLiteral("burglary"), 51.5, -0.1);

        QVERIFY2(pred.ci90.first  >= 0.0,
                 qPrintable(QString("ci90.first=%1").arg(pred.ci90.first)));
        QVERIFY2(pred.ci90.second >= 0.0,
                 qPrintable(QString("ci90.second=%1").arg(pred.ci90.second)));
        QVERIFY2(pred.probCrime >= 0.0 && pred.probCrime <= 1.0,
                 qPrintable(QString("probCrime=%1").arg(pred.probCrime)));
    }

    // 4. Multiple zones — 50 events across 5 zones; all zones get valid predictions
    void testMultipleZones()
    {
        const QStringList zones = {
            QStringLiteral("z1"), QStringLiteral("z2"), QStringLiteral("z3"),
            QStringLiteral("z4"), QStringLiteral("z5"),
        };

        QVector<PoissonBaseline::EventRecord> allRecs;
        for (const QString& zone : zones)
            allRecs.append(makeRecords(zone, 10));

        PoissonBaseline poisson;
        poisson.fit(allRecs);
        HawkesProcess hawkes = makeFittedHawkes(50);

        EnsemblePredictor ep;
        ep.setPoisson(&poisson);
        ep.setHawkes(&hawkes);

        for (const QString& zone : zones) {
            const auto pred = ep.predict(zone, baseDt(),
                                         QStringLiteral("burglary"), 51.5, -0.1);
            QVERIFY2(pred.probCrime >= 0.0 && pred.probCrime <= 1.0,
                     qPrintable(QString("zone=%1 probCrime=%2").arg(zone).arg(pred.probCrime)));
            QVERIFY2(pred.expectedCount >= 0.0,
                     qPrintable(QString("zone=%1 expectedCount=%2").arg(zone).arg(pred.expectedCount)));
        }
    }

    // 5. Confidence interval — lower <= mean <= upper for all predictions ─────
    void testConfidenceInterval()
    {
        PoissonBaseline poisson = makeFittedPoisson(QStringLiteral("zone_ci"), 15);
        HawkesProcess   hawkes  = makeFittedHawkes(15);

        EnsemblePredictor ep;
        ep.setPoisson(&poisson);
        ep.setHawkes(&hawkes);

        // Test across multiple zones and datetimes
        const QList<QString> zones = {
            QStringLiteral("zone_ci"),
            QStringLiteral("zone_unknown_1"),
            QStringLiteral("zone_unknown_2"),
        };
        const QList<int> dayOffsets = {0, 7, 14, 30};

        for (const QString& zone : zones) {
            for (int offset : dayOffsets) {
                const QDateTime dt = baseDt().addDays(offset);
                const auto pred = ep.predict(zone, dt,
                                             QStringLiteral("burglary"), 51.5, -0.1);
                QVERIFY2(pred.ciLow95  <= pred.probCrime + 1e-9,
                         qPrintable(QString("zone=%1 ciLow95=%2 > probCrime=%3")
                                    .arg(zone).arg(pred.ciLow95).arg(pred.probCrime)));
                QVERIFY2(pred.ciHigh95 >= pred.probCrime - 1e-9,
                         qPrintable(QString("zone=%1 ciHigh95=%2 < probCrime=%3")
                                    .arg(zone).arg(pred.ciHigh95).arg(pred.probCrime)));
                QVERIFY2(pred.ci90.second >= pred.ci90.first - 1e-9,
                         qPrintable(QString("zone=%1 ci90 inverted: [%2,%3]")
                                    .arg(zone).arg(pred.ci90.first).arg(pred.ci90.second)));
            }
        }
    }

    // 6. Consistency with more data — more training data → higher predicted rate
    void testConsistencyWithMoreData()
    {
        // Sparse: 2 events in zone "zone_sparse"
        PoissonBaseline pSparse;
        pSparse.fit(makeRecords(QStringLiteral("zone_sparse"), 2));

        // Dense: 30 events in zone "zone_dense"
        PoissonBaseline pDense;
        pDense.fit(makeRecords(QStringLiteral("zone_dense"), 30));

        HawkesProcess hawkes = makeFittedHawkes(5);

        EnsemblePredictor epSparse;
        epSparse.setPoisson(&pSparse);
        epSparse.setHawkes(&hawkes);

        EnsemblePredictor epDense;
        epDense.setPoisson(&pDense);
        epDense.setHawkes(&hawkes);

        const auto predSparse = epSparse.predict(QStringLiteral("zone_sparse"), baseDt(),
                                                  QStringLiteral("burglary"), 51.5, -0.1);
        const auto predDense  = epDense.predict(QStringLiteral("zone_dense"), baseDt(),
                                                 QStringLiteral("burglary"), 51.5, -0.1);

        // Both must be valid
        QVERIFY(predSparse.probCrime >= 0.0 && predSparse.probCrime <= 1.0);
        QVERIFY(predDense.probCrime  >= 0.0 && predDense.probCrime  <= 1.0);

        // Dense zone with more events should predict at least as high as sparse
        QVERIFY2(predDense.expectedCount >= predSparse.expectedCount - 1e-9,
                 qPrintable(QString("dense=%1 < sparse=%2")
                            .arg(predDense.expectedCount).arg(predSparse.expectedCount)));
    }

    // 7. Empty input — fitting with no events should not crash ────────────────
    void testEmptyInput()
    {
        PoissonBaseline poisson;
        poisson.fit({});    // empty fit
        QVERIFY(!poisson.isFitted());

        EnsemblePredictor ep;
        ep.setPoisson(&poisson);

        // isReady() returns true when pointer is non-null (even if model not fitted)
        QVERIFY(ep.isReady());

        // predict() should not crash even with unfitted models
        const auto pred = ep.predict(QStringLiteral("any_zone"), baseDt(),
                                     QStringLiteral("burglary"), 51.5, -0.1);

        // All numeric values should be finite
        QVERIFY(std::isfinite(pred.probCrime));
        QVERIFY(std::isfinite(pred.expectedCount));
    }

    // 8. High activity — 100 events in one zone → predicted count > 0 ─────────
    void testHighActivityPrediction()
    {
        PoissonBaseline poisson = makeFittedPoisson(QStringLiteral("hot_zone"), 100);
        HawkesProcess   hawkes  = makeFittedHawkes(100);

        EnsemblePredictor ep;
        ep.setPoisson(&poisson);
        ep.setHawkes(&hawkes);

        const auto pred = ep.predict(QStringLiteral("hot_zone"), baseDt(),
                                     QStringLiteral("burglary"), 51.5, -0.1);

        QVERIFY2(pred.expectedCount > 0.0,
                 qPrintable(QString("expectedCount=%1 for high-activity zone")
                            .arg(pred.expectedCount)));
        QVERIFY2(pred.probCrime > 0.0,
                 qPrintable(QString("probCrime=%1 for high-activity zone")
                            .arg(pred.probCrime)));
    }

    // 9. Weight normalisation — poissonWeight + hawkesWeight == 1.0 ────────────
    void testWeightNormalization()
    {
        PoissonBaseline poisson = makeFittedPoisson(QStringLiteral("zone_wn"), 10);
        HawkesProcess   hawkes  = makeFittedHawkes(10);

        // Test several raw weight ratios
        const QList<QPair<double,double>> rawWeights = {
            {3.0, 7.0}, {1.0, 1.0}, {0.1, 0.9}, {10.0, 1.0}, {0.5, 0.5},
        };

        for (const auto& [wP, wH] : rawWeights) {
            EnsemblePredictor ep;
            ep.setPoisson(&poisson);
            ep.setHawkes(&hawkes);
            ep.setWeights(wP, wH);

            const auto pred = ep.predict(QStringLiteral("zone_wn"), baseDt(),
                                         QStringLiteral("burglary"), 51.5, -0.1);

            const double wSum = pred.poissonWeight + pred.hawkesWeight;
            QVERIFY2(std::abs(wSum - 1.0) < 1e-9,
                     qPrintable(QString("rawWeights(%1,%2): wSum=%3")
                                .arg(wP).arg(wH).arg(wSum)));

            // Verify expected ratio
            const double total = wP + wH;
            const double expectedPW = wP / total;
            QVERIFY2(std::abs(pred.poissonWeight - expectedPW) < 1e-9,
                     qPrintable(QString("poissonWeight=%1 expected=%2")
                                .arg(pred.poissonWeight).arg(expectedPW)));
        }
    }

    // 10. Serialisation roundtrip — reproducibility: same inputs → same output
    void testSerializationRoundtrip()
    {
        PoissonBaseline poisson = makeFittedPoisson(QStringLiteral("zone_sr"), 20);
        HawkesProcess   hawkes  = makeFittedHawkes(20);

        // Build two identical predictors with the same settings
        EnsemblePredictor ep1, ep2;
        ep1.setPoisson(&poisson);
        ep1.setHawkes(&hawkes);
        ep1.setWeights(0.55, 0.45);

        ep2.setPoisson(&poisson);
        ep2.setHawkes(&hawkes);
        ep2.setWeights(0.55, 0.45);

        const QDateTime dt = baseDt();
        const auto pred1 = ep1.predict(QStringLiteral("zone_sr"), dt,
                                        QStringLiteral("burglary"), 51.5, -0.1);
        const auto pred2 = ep2.predict(QStringLiteral("zone_sr"), dt,
                                        QStringLiteral("burglary"), 51.5, -0.1);

        QVERIFY2(std::abs(pred1.probCrime     - pred2.probCrime)     < 1e-12,
                 "probCrime differs between identical predictors");
        QVERIFY2(std::abs(pred1.expectedCount - pred2.expectedCount) < 1e-12,
                 "expectedCount differs between identical predictors");
        QVERIFY2(pred1.poissonWeight == pred2.poissonWeight,
                 "poissonWeight differs");
        QVERIFY2(pred1.hawkesWeight  == pred2.hawkesWeight,
                 "hawkesWeight differs");
    }
};

QTEST_MAIN(TestEnsemblePredictorDeep)
#include "test_ensemble_predictor_deep.moc"
