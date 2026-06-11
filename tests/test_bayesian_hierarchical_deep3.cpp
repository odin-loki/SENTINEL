// test_bayesian_hierarchical_deep3.cpp
// Deep audit of BayesianHierarchical: shrinkage, MLE approximation,
// credible intervals, prior fallback, and conjugate alpha update.

#include <QTest>
#include <cmath>

#include "models/BayesianHierarchical.h"
#include "core/CrimeEvent.h"

class TestBayesianHierarchicalDeep3 : public QObject
{
    Q_OBJECT

private slots:
    void testShrinkageTowardGlobalMean();
    void testHighObservationsApproxMLE();
    void testCredibleIntervalContainsPosteriorMean();
    void testZeroObservationsPosteriorMeanPositive();
    void testPosteriorAlphaEqualsAlphaPriorPlusObservations();
};

// ─── Helper ──────────────────────────────────────────────────────────────────

// Create `count` CrimeEvents all in zone `suburb` with crimeType "robbery".
static QVector<CrimeEvent> makeEvents(const QString& suburb, int count)
{
    QVector<CrimeEvent> events;
    events.reserve(count);
    for (int i = 0; i < count; ++i) {
        CrimeEvent ev;
        ev.eventId   = QString("EV_%1_%2").arg(suburb).arg(i);
        ev.suburb    = suburb;
        ev.crimeType = QStringLiteral("robbery");
        ev.outcome   = QStringLiteral("unresolved");
        events.append(ev);
    }
    return events;
}

// ─── Tests ───────────────────────────────────────────────────────────────────

// A zone with few observations should have its posterior mean pulled toward
// the global mean (away from its MLE), demonstrating partial pooling / shrinkage.
//
//   Zone "HighZone": 100 events → MLE = 100/30 ≈ 3.33 /day
//   Zone "LowZone":  1  event  → MLE = 1/30  ≈ 0.033/day
//   Global mean ≈ 1.68 /day (midpoint of zone rates)
//
//   LowZone posterior mean should satisfy: MLE_low < posterior < globalMean
void TestBayesianHierarchicalDeep3::testShrinkageTowardGlobalMean()
{
    QVector<CrimeEvent> events;
    events += makeEvents(QStringLiteral("HighZone"), 100);
    events += makeEvents(QStringLiteral("LowZone"),  1);

    BayesianHierarchical model;
    const double exposure = 30.0;
    model.fit(events, exposure);
    QVERIFY2(model.isFitted(), "Model must be fitted after non-empty events");

    const double globalMu = model.globalMean();       // alpha0 / beta0
    const double lowMLE   = 1.0 / exposure;           // = 0.0333…
    const auto   lowZone  = model.posteriorForZone(QStringLiteral("LowZone"));

    // Posterior mean must lie strictly between MLE and global mean
    QVERIFY2(lowZone.posteriorMean > lowMLE,
             qPrintable(QString("LowZone posterior mean (%1) must exceed MLE (%2); "
                                "shrinkage should pull it toward global mean (%3)")
                        .arg(lowZone.posteriorMean).arg(lowMLE).arg(globalMu)));
    QVERIFY2(lowZone.posteriorMean < globalMu,
             qPrintable(QString("LowZone posterior mean (%1) must be below global mean (%2)")
                        .arg(lowZone.posteriorMean).arg(globalMu)));
}

// A zone with many observations should have its posterior mean close to the MLE
// (within 10 %). Large data overwhelms the prior — the Bayesian estimate
// converges to the frequentist one.
void TestBayesianHierarchicalDeep3::testHighObservationsApproxMLE()
{
    QVector<CrimeEvent> events;
    events += makeEvents(QStringLiteral("HighZone"), 100);
    events += makeEvents(QStringLiteral("LowZone"),  1);

    BayesianHierarchical model;
    const double exposure = 30.0;
    model.fit(events, exposure);

    const double highMLE  = 100.0 / exposure;
    const auto   highZone = model.posteriorForZone(QStringLiteral("HighZone"));

    const double relError = qAbs(highZone.posteriorMean - highMLE) / highMLE;
    QVERIFY2(relError < 0.10,
             qPrintable(QString("HighZone posterior mean (%1) should be within 10%% of MLE (%2); "
                                "relative error = %3")
                        .arg(highZone.posteriorMean).arg(highMLE).arg(relError)));
}

// For every zone, the 90% credible interval [credibleLow, credibleHigh]
// must contain the posterior mean.
void TestBayesianHierarchicalDeep3::testCredibleIntervalContainsPosteriorMean()
{
    QVector<CrimeEvent> events;
    events += makeEvents(QStringLiteral("Alpha"),  5);
    events += makeEvents(QStringLiteral("Beta"),  20);
    events += makeEvents(QStringLiteral("Gamma"), 50);

    BayesianHierarchical model;
    model.fit(events, 30.0);
    QVERIFY(model.isFitted());

    const auto posteriors = model.allPosteriors();
    QVERIFY2(!posteriors.isEmpty(), "allPosteriors() must be non-empty after fit");

    for (const auto& zp : posteriors) {
        QVERIFY2(zp.credibleLow <= zp.posteriorMean,
                 qPrintable(QString("Zone '%1': credibleLow (%2) > posteriorMean (%3)")
                            .arg(zp.zoneId).arg(zp.credibleLow).arg(zp.posteriorMean)));
        QVERIFY2(zp.posteriorMean <= zp.credibleHigh,
                 qPrintable(QString("Zone '%1': posteriorMean (%2) > credibleHigh (%3)")
                            .arg(zp.zoneId).arg(zp.posteriorMean).arg(zp.credibleHigh)));
    }
}

// Zones with zero observed events should fall back to the prior (alpha0/beta0).
// Since alpha0 >= 0.1 and beta0 >= 0.1 by construction, posteriorMean > 0.
//
// We test two scenarios:
//   (A) fit() on empty events — model falls back to default prior (alpha=1, beta=1),
//       so posteriorForZone returns mean = 1.0 > 0.
//   (B) Zone not seen during training — posteriorForZone returns prior mean > 0.
void TestBayesianHierarchicalDeep3::testZeroObservationsPosteriorMeanPositive()
{
    // (A) Empty events vector
    {
        BayesianHierarchical model;
        model.fit({}, 30.0);
        QVERIFY(!model.isFitted());   // nothing to fit on
        const auto zp = model.posteriorForZone(QStringLiteral("AnyZone"));
        QVERIFY2(zp.posteriorMean > 0.0,
                 qPrintable(QString("Prior mean after empty fit must be > 0, got %1")
                            .arg(zp.posteriorMean)));
    }

    // (B) Known fit, unseen zone
    {
        BayesianHierarchical model;
        model.fit(makeEvents(QStringLiteral("KnownZone"), 10), 30.0);
        QVERIFY(model.isFitted());
        const auto zp = model.posteriorForZone(QStringLiteral("UnseenZone"));
        QVERIFY2(zp.posteriorMean > 0.0,
                 qPrintable(QString("Prior mean for unseen zone must be > 0, got %1")
                            .arg(zp.posteriorMean)));
    }
}

// Gamma-Poisson conjugate update:
//   alphaPost = alphaPrior + sum(events) = alphaPrior + observedCount
// Verified directly from the ZonePosterior fields.
void TestBayesianHierarchicalDeep3::testPosteriorAlphaEqualsAlphaPriorPlusObservations()
{
    // Two zones so empirical Bayes has meaningful variance to estimate alpha0/beta0.
    QVector<CrimeEvent> events;
    events += makeEvents(QStringLiteral("ZoneA"), 7);
    events += makeEvents(QStringLiteral("ZoneB"), 3);

    BayesianHierarchical model;
    model.fit(events, 30.0);
    QVERIFY(model.isFitted());

    // Check ZoneA (k = 7)
    {
        const auto zp = model.posteriorForZone(QStringLiteral("ZoneA"));
        QVERIFY2(zp.observedCount == 7,
                 qPrintable(QString("ZoneA observedCount: expected 7, got %1")
                            .arg(zp.observedCount)));
        const double delta = qAbs(zp.alphaPost - zp.alphaPrior - zp.observedCount);
        QVERIFY2(delta < 1e-9,
                 qPrintable(QString("ZoneA: alphaPost (%1) != alphaPrior (%2) + "
                                    "observedCount (%3)")
                            .arg(zp.alphaPost).arg(zp.alphaPrior).arg(zp.observedCount)));
    }

    // Check ZoneB (k = 3)
    {
        const auto zp = model.posteriorForZone(QStringLiteral("ZoneB"));
        QVERIFY2(zp.observedCount == 3,
                 qPrintable(QString("ZoneB observedCount: expected 3, got %1")
                            .arg(zp.observedCount)));
        const double delta = qAbs(zp.alphaPost - zp.alphaPrior - zp.observedCount);
        QVERIFY2(delta < 1e-9,
                 qPrintable(QString("ZoneB: alphaPost (%1) != alphaPrior (%2) + "
                                    "observedCount (%3)")
                            .arg(zp.alphaPost).arg(zp.alphaPrior).arg(zp.observedCount)));
    }
}

QTEST_GUILESS_MAIN(TestBayesianHierarchicalDeep3)
#include "test_bayesian_hierarchical_deep3.moc"
