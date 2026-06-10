// test_ensemble_ci.cpp — EnsemblePredictor confidence-interval unit tests
#include <QTest>
#include <QCoreApplication>
#include <QUuid>
#include <QDateTime>
#include <cmath>

#include "models/EnsemblePredictor.h"
#include "models/PoissonBaseline.h"
#include "models/HawkesProcess.h"
#include "core/CrimeEvent.h"
#include "audit/ProvenanceLog.h"

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

static QVector<SpatiotemporalEvent> makeEvents(int n, double lat = 51.5, double lon = -0.1)
{
    QVector<SpatiotemporalEvent> events;
    for (int i = 0; i < n; ++i) {
        SpatiotemporalEvent ev;
        ev.tDays = i * 1.0;
        ev.lat   = lat + (i % 5) * 0.01;
        ev.lon   = lon + (i % 3) * 0.01;
        events.push_back(ev);
    }
    return events;
}

static CrimeEvent makeCrimeEvent(const QString& zone, int daysOffset = 0)
{
    CrimeEvent e;
    e.eventId    = QUuid::createUuid().toString();
    e.id         = e.eventId;
    e.crimeType  = QStringLiteral("burglary");
    e.suburb     = zone;
    e.lat        = 51.5;
    e.lon        = -0.1;
    e.latitude   = 51.5;
    e.longitude  = -0.1;
    e.occurredAt = QDateTime::currentDateTimeUtc().addDays(-daysOffset);
    e.qualityScore = 0.9;
    return e;
}

// Fixed prediction datetime (Monday 2024-01-01 10:00 UTC)
static QDateTime testDt()
{
    return QDateTime(QDate(2024, 1, 1), QTime(10, 0, 0), Qt::UTC);
}

// tDays base corresponding to testDt()
static double tBase()
{
    return static_cast<double>(QDateTime::fromSecsSinceEpoch(0).daysTo(testDt()));
}

// Fitted PoissonBaseline for zone "zone_hi" (lambda≈5 at testDt())
// — 5 events each on 5 consecutive Mondays at 10:00 UTC
static PoissonBaseline makePoissonHigh()
{
    PoissonBaseline p;
    QVector<PoissonBaseline::EventRecord> recs;
    const QList<QDate> mondays = {
        QDate(2024, 1,  1), QDate(2024, 1,  8), QDate(2024, 1, 15),
        QDate(2024, 1, 22), QDate(2024, 1, 29),
    };
    for (const QDate& d : mondays) {
        for (int i = 0; i < 5; ++i) {
            PoissonBaseline::EventRecord er;
            er.zoneId     = QStringLiteral("zone_hi");
            er.occurredAt = QDateTime(d, QTime(10, 0, 0), Qt::UTC);
            er.crimeType  = QStringLiteral("burglary");
            recs.append(er);
        }
    }
    p.fit(recs);
    return p;
}

// Fitted PoissonBaseline for zone "zone_lo" (unknown zone returns λ≈0.01)
static PoissonBaseline makePoissonLow()
{
    PoissonBaseline p;
    QVector<PoissonBaseline::EventRecord> recs;
    const QDate d = QDate(2024, 1, 1);
    PoissonBaseline::EventRecord er;
    er.zoneId     = QStringLiteral("zone_hi");  // only zone_hi has history
    er.occurredAt = QDateTime(d, QTime(10, 0, 0), Qt::UTC);
    er.crimeType  = QStringLiteral("burglary");
    recs.append(er);
    recs.append(er);
    recs.append(er);
    p.fit(recs);
    return p;
}

// HawkesProcess with recent events → higher intensity at testDt()
static HawkesProcess makeHawkesFitted()
{
    HawkesProcess h;
    QVector<SpatiotemporalEvent> hevents;
    const double tb = tBase();
    for (int i = 0; i < 20; ++i) {
        SpatiotemporalEvent ev;
        ev.tDays     = tb - 3.0 + i * 0.1;
        ev.lat       = 51.5;
        ev.lon       = -0.1;
        ev.crimeType = QStringLiteral("burglary");
        hevents.append(ev);
    }
    h.fit(hevents, 5);
    return h;
}

// HawkesProcess fitted but then parameters forced to near-zero intensity
static HawkesProcess makeHawkesNearZero()
{
    HawkesProcess h;
    // Fit with 3 minimal events to set m_fitted=true
    QVector<SpatiotemporalEvent> seed = makeEvents(3);
    for (auto& e : seed) e.tDays += tBase() - 200.0;  // far in the past
    h.fit(seed, 3);
    // Override params: mu=0.01, alpha=0 → intensity at any future t = mu
    HawkesParams p;
    p.mu    = 0.01;
    p.alpha = 0.0;
    p.beta  = 1.0;
    p.sigma = 0.01;
    h.setParams(p);
    h.setHistory({});  // no recent events → intensity = mu = 0.01
    return h;
}

// HawkesProcess forced to near-zero with extremely small mu
static HawkesProcess makeHawkesVeryLow()
{
    HawkesProcess h;
    QVector<SpatiotemporalEvent> seed = makeEvents(3);
    for (auto& e : seed) e.tDays += tBase() - 500.0;
    h.fit(seed, 3);
    HawkesParams p;
    p.mu    = 0.001;
    p.alpha = 0.0;
    p.beta  = 1.0;
    p.sigma = 0.01;
    h.setParams(p);
    h.setHistory({});
    return h;
}

// ─────────────────────────────────────────────────────────────────────────────
// Test class
// ─────────────────────────────────────────────────────────────────────────────

class TestEnsembleCI : public QObject
{
    Q_OBJECT

private slots:

    // 1. aleatoric + epistemic are both >= 0 after a full predict
    void testPredictionHasUncertainty()
    {
        PoissonBaseline poisson = makePoissonHigh();
        HawkesProcess   hawkes  = makeHawkesFitted();

        EnsemblePredictor ep;
        ep.setPoisson(&poisson);
        ep.setHawkes(&hawkes);

        const auto pred = ep.predict(QStringLiteral("zone_hi"), testDt(),
                                     QStringLiteral("burglary"), 51.5, -0.1);
        QVERIFY(pred.uncertaintyAleatoric >= 0.0);
        QVERIFY(pred.uncertaintyEpistemic >= 0.0);
    }

    // 2. ciLow95 <= probCrime
    void testLowerBoundBelowMean()
    {
        PoissonBaseline poisson = makePoissonHigh();
        HawkesProcess   hawkes  = makeHawkesFitted();

        EnsemblePredictor ep;
        ep.setPoisson(&poisson);
        ep.setHawkes(&hawkes);

        const auto pred = ep.predict(QStringLiteral("zone_hi"), testDt(),
                                     QStringLiteral("burglary"), 51.5, -0.1);
        QVERIFY(pred.ciLow95 <= pred.probCrime + 1e-9);
    }

    // 3. ciHigh95 >= probCrime
    void testUpperBoundAboveMean()
    {
        PoissonBaseline poisson = makePoissonHigh();
        HawkesProcess   hawkes  = makeHawkesFitted();

        EnsemblePredictor ep;
        ep.setPoisson(&poisson);
        ep.setHawkes(&hawkes);

        const auto pred = ep.predict(QStringLiteral("zone_hi"), testDt(),
                                     QStringLiteral("burglary"), 51.5, -0.1);
        QVERIFY(pred.ciHigh95 >= pred.probCrime - 1e-9);
    }

    // 4. ciLow95 <= probCrime <= ciHigh95 for various zones
    void testConfidenceIntervalContainsEstimate()
    {
        PoissonBaseline poisson = makePoissonHigh();
        HawkesProcess   hawkes  = makeHawkesFitted();

        EnsemblePredictor ep;
        ep.setPoisson(&poisson);
        ep.setHawkes(&hawkes);

        const QList<QString> zones = {
            QStringLiteral("zone_hi"),
            QStringLiteral("zone_unknown"),
            QStringLiteral("zone_other"),
        };
        for (const QString& z : zones) {
            const auto pred = ep.predict(z, testDt(),
                                         QStringLiteral("burglary"), 51.5, -0.1);
            QVERIFY2(pred.ciLow95 <= pred.probCrime + 1e-9,
                     qPrintable(QStringLiteral("ciLow95 > probCrime for zone ") + z));
            QVERIFY2(pred.ciHigh95 >= pred.probCrime - 1e-9,
                     qPrintable(QStringLiteral("ciHigh95 < probCrime for zone ") + z));
        }
    }

    // 5. When both models predict near-zero, epistemic is very small
    void testNarrowCIWithLowEpistemic()
    {
        PoissonBaseline poisson = makePoissonLow();
        HawkesProcess   hawkes  = makeHawkesNearZero();

        EnsemblePredictor ep;
        ep.setPoisson(&poisson);
        ep.setHawkes(&hawkes);

        // Predict for "zone_lo" (no history for Poisson → λ=0.01, prob≈0.01)
        // Hawkes with mu=0.01, no history → intensity=0.01, prob≈0.01
        const auto pred = ep.predict(QStringLiteral("zone_lo"), testDt(),
                                     QStringLiteral("burglary"), 51.5, -0.1);
        QVERIFY(pred.uncertaintyEpistemic >= 0.0);
        // Both models agree near zero → epistemic should be very small
        QVERIFY(pred.uncertaintyEpistemic < 0.05);
    }

    // 6. When models produce substantially different predictions, epistemic > 0
    void testWideEpistemicWhenModelsDisagree()
    {
        // Poisson: zone_hi has lambda≈5 → prob close to 1
        PoissonBaseline poisson = makePoissonHigh();
        // Hawkes: mu=0.001 → intensity≈0 → prob≈0
        HawkesProcess hawkes = makeHawkesVeryLow();

        EnsemblePredictor ep;
        ep.setPoisson(&poisson);
        ep.setHawkes(&hawkes);

        const auto pred = ep.predict(QStringLiteral("zone_hi"), testDt(),
                                     QStringLiteral("burglary"), 51.5, -0.1);
        QVERIFY(pred.uncertaintyEpistemic > 0.0);
    }

    // 7. probCrime is always in [0,1]
    void testProbCrimeInUnitInterval()
    {
        PoissonBaseline poisson = makePoissonHigh();
        HawkesProcess   hawkes  = makeHawkesFitted();

        EnsemblePredictor ep;
        ep.setPoisson(&poisson);
        ep.setHawkes(&hawkes);

        const QList<QString> zones = {
            QStringLiteral("zone_hi"),
            QStringLiteral("zone_unknown"),
        };
        for (const QString& z : zones) {
            const auto pred = ep.predict(z, testDt(),
                                         QStringLiteral("burglary"), 51.5, -0.1);
            QVERIFY(pred.probCrime >= 0.0);
            QVERIFY(pred.probCrime <= 1.0);
        }
    }

    // 8. dominantModel is either "poisson" or "hawkes"
    void testDominantModelReported()
    {
        PoissonBaseline poisson = makePoissonHigh();
        HawkesProcess   hawkes  = makeHawkesFitted();

        EnsemblePredictor ep;
        ep.setPoisson(&poisson);
        ep.setHawkes(&hawkes);

        const auto pred = ep.predict(QStringLiteral("zone_hi"), testDt(),
                                     QStringLiteral("burglary"), 51.5, -0.1);
        const bool valid = (pred.dominantModel == QStringLiteral("poisson"))
                        || (pred.dominantModel == QStringLiteral("hawkes"));
        QVERIFY(valid);
    }

    // 9. Poisson-only (no Hawkes) → valid prediction
    void testPoissonOnlyPrediction()
    {
        PoissonBaseline poisson = makePoissonHigh();

        EnsemblePredictor ep;
        ep.setPoisson(&poisson);
        // hawkes is null — not set

        QVERIFY(ep.isReady());
        const auto pred = ep.predict(QStringLiteral("zone_hi"), testDt(),
                                     QStringLiteral("burglary"), 51.5, -0.1);
        QVERIFY(pred.probCrime >= 0.0);
        QVERIFY(pred.probCrime <= 1.0);
        QCOMPARE(pred.dominantModel, QStringLiteral("poisson"));
        QCOMPARE(pred.poissonWeight, 1.0);
        QCOMPARE(pred.hawkesWeight,  0.0);
    }

    // 10. Hawkes-only (no Poisson) → valid prediction
    void testHawkesOnlyPrediction()
    {
        HawkesProcess hawkes = makeHawkesFitted();

        EnsemblePredictor ep;
        ep.setHawkes(&hawkes);
        // poisson is null — not set

        QVERIFY(ep.isReady());
        const auto pred = ep.predict(QStringLiteral("zone_x"), testDt(),
                                     QStringLiteral("burglary"), 51.5, -0.1);
        QVERIFY(pred.probCrime >= 0.0);
        QVERIFY(pred.probCrime <= 1.0);
        QCOMPARE(pred.dominantModel, QStringLiteral("hawkes"));
        QCOMPARE(pred.poissonWeight, 0.0);
        QCOMPARE(pred.hawkesWeight,  1.0);
    }

    // 11. Unknown zone → valid prediction (no crash)
    void testPredictionForUnknownZone()
    {
        PoissonBaseline poisson = makePoissonHigh();
        HawkesProcess   hawkes  = makeHawkesFitted();

        EnsemblePredictor ep;
        ep.setPoisson(&poisson);
        ep.setHawkes(&hawkes);

        // "zone_totally_unknown" has no history in either model
        const auto pred = ep.predict(QStringLiteral("zone_totally_unknown"),
                                     testDt(), QStringLiteral("theft"),
                                     52.0, 0.0);
        QVERIFY(pred.probCrime >= 0.0);
        QVERIFY(pred.probCrime <= 1.0);
        QVERIFY(pred.ciLow95  >= 0.0);
        QVERIFY(pred.ciHigh95 <= 1.0);
    }

    // 12. Predict at a past datetime → valid output
    void testPredictAtPastDateTime()
    {
        PoissonBaseline poisson = makePoissonHigh();
        HawkesProcess   hawkes  = makeHawkesFitted();

        EnsemblePredictor ep;
        ep.setPoisson(&poisson);
        ep.setHawkes(&hawkes);

        const QDateTime pastDt = QDateTime(QDate(2020, 6, 15), QTime(8, 0, 0), Qt::UTC);
        const auto pred = ep.predict(QStringLiteral("zone_hi"), pastDt,
                                     QStringLiteral("burglary"), 51.5, -0.1);
        QVERIFY(pred.probCrime >= 0.0);
        QVERIFY(pred.probCrime <= 1.0);
        QVERIFY(pred.ciLow95 <= pred.probCrime + 1e-9);
        QVERIFY(pred.ciHigh95 >= pred.probCrime - 1e-9);
    }

    // 13. Weights normalise internally so poissonWeight + hawkesWeight ≈ 1
    void testWeightSumNormalized()
    {
        PoissonBaseline poisson = makePoissonHigh();
        HawkesProcess   hawkes  = makeHawkesFitted();

        EnsemblePredictor ep;
        ep.setPoisson(&poisson);
        ep.setHawkes(&hawkes);
        ep.setWeights(2.0, 3.0);   // raw 2:3 → normalised 0.4 : 0.6

        const auto pred = ep.predict(QStringLiteral("zone_hi"), testDt(),
                                     QStringLiteral("burglary"), 51.5, -0.1);
        const double wSum = pred.poissonWeight + pred.hawkesWeight;
        QVERIFY(std::abs(wSum - 1.0) < 1e-9);
        // Also verify split matches input ratio
        QVERIFY(std::abs(pred.poissonWeight - 0.4) < 1e-9);
        QVERIFY(std::abs(pred.hawkesWeight  - 0.6) < 1e-9);
    }

    // Bonus: formatHtml returns non-empty HTML for a known event
    void testFormatHtmlNonEmpty()
    {
        ProvenanceLog log;
        log.record(QStringLiteral("ev-001"), QStringLiteral("ingest"),
                   QStringLiteral("import"), QStringLiteral("parsed row 42"),
                   QStringLiteral("abc123"));
        log.record(QStringLiteral("ev-001"), QStringLiteral("model"),
                   QStringLiteral("predict"), QStringLiteral("ensemble scored"));

        const QString html = log.formatHtml(QStringLiteral("ev-001"));
        QVERIFY(!html.isEmpty());
        QVERIFY(html.contains(QStringLiteral("ev-001")));
        QVERIFY(html.contains(QStringLiteral("#e94560")));
        QVERIFY(html.contains(QStringLiteral("ingest")));
        QVERIFY(html.contains(QStringLiteral("abc123")));
    }

    // Bonus: formatHtml returns empty string for unknown event
    void testFormatHtmlUnknownEventReturnsEmpty()
    {
        ProvenanceLog log;
        log.record(QStringLiteral("ev-999"), QStringLiteral("ingest"),
                   QStringLiteral("import"), QStringLiteral("detail"));

        const QString html = log.formatHtml(QStringLiteral("ev-DOESNOTEXIST"));
        QVERIFY(html.isEmpty());
    }
};

QTEST_MAIN(TestEnsembleCI)
#include "test_ensemble_ci.moc"
