// test_bayesian_hierarchical_deep4.cpp — Deep audit iteration 16:
// zone posteriors, credible intervals, and empty-fit behaviour.

#include <QTest>
#include <cmath>

#include "models/BayesianHierarchical.h"
#include "core/CrimeEvent.h"

class TestBayesianHierarchicalDeep4 : public QObject
{
    Q_OBJECT

private slots:
    void testEmptyFitLeavesModelUnfitted();
    void testEmptyFitGlobalMeanZero();
    void testAllPosteriorsSortedDescending();
    void testPosteriorVarianceFormula();
    void testCredibleIntervalHasPositiveWidth();
    void testCredibleIntervalNarrowsWithMoreData();
    void testZoneCountMatchesDistinctSuburbs();
    void testUnseenZonePosteriorUsesGlobalPrior();
};

// ─── Helper ──────────────────────────────────────────────────────────────────

static QVector<CrimeEvent> makeZoneEvents(const QString& suburb, int count)
{
    QVector<CrimeEvent> events;
    events.reserve(count);
    for (int i = 0; i < count; ++i) {
        CrimeEvent ev;
        ev.eventId   = QString("EV_%1_%2").arg(suburb).arg(i);
        ev.suburb    = suburb;
        ev.crimeType = QStringLiteral("theft");
        events.append(ev);
    }
    return events;
}

// ─── Tests ───────────────────────────────────────────────────────────────────

void TestBayesianHierarchicalDeep4::testEmptyFitLeavesModelUnfitted()
{
    BayesianHierarchical model;
    model.fit({}, 30.0);

    QVERIFY(!model.isFitted());
    QCOMPARE(model.zoneCount(), 0);
    QVERIFY(model.allPosteriors().isEmpty());
}

void TestBayesianHierarchicalDeep4::testEmptyFitGlobalMeanZero()
{
    BayesianHierarchical model;
    model.fit({}, 30.0);

    QCOMPARE(model.globalMean(), 0.0);
    const auto zp = model.posteriorForZone(QStringLiteral("GhostZone"));
    QVERIFY2(zp.posteriorMean > 0.0,
             "Unseen zone prior mean should still be positive (α₀/β₀)");
}

void TestBayesianHierarchicalDeep4::testAllPosteriorsSortedDescending()
{
    QVector<CrimeEvent> events;
    events += makeZoneEvents(QStringLiteral("Low"),   2);
    events += makeZoneEvents(QStringLiteral("Mid"),  15);
    events += makeZoneEvents(QStringLiteral("High"), 40);

    BayesianHierarchical model;
    model.fit(events, 30.0);
    QVERIFY(model.isFitted());

    const auto posteriors = model.allPosteriors();
    QCOMPARE(posteriors.size(), 3);

    for (int i = 1; i < posteriors.size(); ++i) {
        QVERIFY2(posteriors[i - 1].posteriorMean >= posteriors[i].posteriorMean,
                 qPrintable(QString("Posteriors not sorted: '%1' (%2) before '%3' (%4)")
                                .arg(posteriors[i - 1].zoneId)
                                .arg(posteriors[i - 1].posteriorMean)
                                .arg(posteriors[i].zoneId)
                                .arg(posteriors[i].posteriorMean)));
    }
}

// Var[λ|data] = α_post / β_post² per Gamma(rate) parameterisation.
void TestBayesianHierarchicalDeep4::testPosteriorVarianceFormula()
{
    QVector<CrimeEvent> events;
    events += makeZoneEvents(QStringLiteral("ZoneX"), 12);
    events += makeZoneEvents(QStringLiteral("ZoneY"),  4);

    BayesianHierarchical model;
    model.fit(events, 20.0);
    QVERIFY(model.isFitted());

    for (const auto& zp : model.allPosteriors()) {
        const double expectedVar = zp.alphaPost / (zp.betaPost * zp.betaPost);
        const double delta = qAbs(zp.posteriorVar - expectedVar);
        QVERIFY2(delta < 1e-9,
                 qPrintable(QString("Zone '%1': posteriorVar (%2) != α/β² (%3)")
                                .arg(zp.zoneId).arg(zp.posteriorVar).arg(expectedVar)));
    }
}

void TestBayesianHierarchicalDeep4::testCredibleIntervalHasPositiveWidth()
{
    QVector<CrimeEvent> events;
    events += makeZoneEvents(QStringLiteral("Alpha"), 8);
    events += makeZoneEvents(QStringLiteral("Beta"),  3);

    BayesianHierarchical model;
    model.fit(events, 30.0);

    for (const auto& zp : model.allPosteriors()) {
        QVERIFY2(zp.credibleLow < zp.credibleHigh,
                 qPrintable(QString("Zone '%1': credible interval width must be positive")
                                .arg(zp.zoneId)));
        QVERIFY2(zp.credibleLow >= 0.0,
                 qPrintable(QString("Zone '%1': credibleLow must be non-negative")
                                .arg(zp.zoneId)));
    }
}

// Relative uncertainty sqrt(Var[λ])/E[λ] = 1/sqrt(α_post) decreases as k grows
// (same β_post for all zones in one fit).
void TestBayesianHierarchicalDeep4::testCredibleIntervalNarrowsWithMoreData()
{
    QVector<CrimeEvent> events;
    events += makeZoneEvents(QStringLiteral("Sparse"), 2);
    events += makeZoneEvents(QStringLiteral("Dense"), 50);
    events += makeZoneEvents(QStringLiteral("Other"), 10);

    BayesianHierarchical model;
    model.fit(events, 30.0);
    QVERIFY(model.isFitted());

    const auto zpSparse = model.posteriorForZone(QStringLiteral("Sparse"));
    const auto zpDense  = model.posteriorForZone(QStringLiteral("Dense"));

    const double relUncSparse = std::sqrt(zpSparse.posteriorVar) / zpSparse.posteriorMean;
    const double relUncDense  = std::sqrt(zpDense.posteriorVar)  / zpDense.posteriorMean;

    QVERIFY2(relUncDense < relUncSparse,
             qPrintable(QString("Dense zone relative uncertainty (%1) should be "
                                "lower than sparse (%2)")
                            .arg(relUncDense).arg(relUncSparse)));

    const double ciRelSparse = (zpSparse.credibleHigh - zpSparse.credibleLow)
                               / zpSparse.posteriorMean;
    const double ciRelDense  = (zpDense.credibleHigh - zpDense.credibleLow)
                               / zpDense.posteriorMean;
    QVERIFY2(ciRelDense < ciRelSparse,
             qPrintable(QString("Dense zone relative CI width (%1) should be "
                                "narrower than sparse (%2)")
                            .arg(ciRelDense).arg(ciRelSparse)));
}

void TestBayesianHierarchicalDeep4::testZoneCountMatchesDistinctSuburbs()
{
    QVector<CrimeEvent> events;
    events += makeZoneEvents(QStringLiteral("A"), 5);
    events += makeZoneEvents(QStringLiteral("B"), 3);
    events += makeZoneEvents(QStringLiteral("C"), 7);

    BayesianHierarchical model;
    model.fit(events, 30.0);

    QCOMPARE(model.zoneCount(), 3);
    QCOMPARE(model.allPosteriors().size(), 3);
}

// Zones absent from training data receive the global prior, not a zero-rate estimate.
void TestBayesianHierarchicalDeep4::testUnseenZonePosteriorUsesGlobalPrior()
{
    QVector<CrimeEvent> events = makeZoneEvents(QStringLiteral("Known"), 20);

    BayesianHierarchical model;
    model.fit(events, 30.0);
    QVERIFY(model.isFitted());

    const double globalMu = model.globalMean();
    const auto unseen = model.posteriorForZone(QStringLiteral("Unseen"));

    QVERIFY2(unseen.observedCount == 0,
             "Unseen zone should report zero observed crimes");
    QVERIFY2(qAbs(unseen.posteriorMean - globalMu) < 1e-9,
             qPrintable(QString("Unseen zone mean (%1) should equal global prior (%2)")
                            .arg(unseen.posteriorMean).arg(globalMu)));
    QVERIFY2(unseen.alphaPost == unseen.alphaPrior,
             "Unseen zone alphaPost should remain at prior");
    QVERIFY2(unseen.betaPost == unseen.betaPrior,
             "Unseen zone betaPost should remain at prior");
}

QTEST_GUILESS_MAIN(TestBayesianHierarchicalDeep4)
#include "test_bayesian_hierarchical_deep4.moc"
