// test_bayesian_hierarchical_deep6.cpp — Deep audit iteration 23:
// BayesianHierarchical credible intervals, predictive CDF floor, unknown-zone
// predictMean, shrinkage vs MLE, zone bucketing, and gamma quantile floor.

#include <QTest>
#include <cmath>

#include "models/BayesianHierarchical.h"
#include "core/CrimeEvent.h"

class TestBayesianHierarchicalDeep6 : public QObject
{
    Q_OBJECT

private slots:
    void testCredibleIntervalLowMeanHighOrdering();
    void testNegBinomCDFFloorViaPredictiveThreshold();
    void testPredictMeanUnknownZoneReturnsGlobalPrior();
    void testShrinkageHighCountCloserToMleThanLowCount();
    void testZoneCountMatchesUniqueSuburbs();
    void testGammaPPFZeroQuantileFloorNonNegative();
    void testPredictiveProbabilityStrictlyBetweenZeroAndOne();
};

// ─── Helper ──────────────────────────────────────────────────────────────────

static CrimeEvent makeEvent(const QString& suburb, int index = 0)
{
    CrimeEvent ev;
    ev.eventId   = QStringLiteral("EV_%1_%2").arg(suburb).arg(index);
    ev.suburb    = suburb;
    ev.crimeType = QStringLiteral("theft");
    return ev;
}

static QVector<CrimeEvent> repeatSuburb(const QString& suburb, int count)
{
    QVector<CrimeEvent> events;
    events.reserve(count);
    for (int i = 0; i < count; ++i)
        events.append(makeEvent(suburb, i));
    return events;
}

// ─── Tests ───────────────────────────────────────────────────────────────────

void TestBayesianHierarchicalDeep6::testCredibleIntervalLowMeanHighOrdering()
{
    QVector<CrimeEvent> events;
    events += repeatSuburb(QStringLiteral("North"), 18);
    events += repeatSuburb(QStringLiteral("South"),  4);

    BayesianHierarchical model;
    model.fit(events, 30.0);
    QVERIFY(model.isFitted());

    for (const auto& zp : model.allPosteriors()) {
        QVERIFY2(zp.credibleLow < zp.posteriorMean,
                 qPrintable(QStringLiteral("Zone '%1': credibleLow (%2) must be < mean (%3)")
                                .arg(zp.zoneId).arg(zp.credibleLow).arg(zp.posteriorMean)));
        QVERIFY2(zp.posteriorMean < zp.credibleHigh,
                 qPrintable(QStringLiteral("Zone '%1': mean (%2) must be < credibleHigh (%3)")
                                .arg(zp.zoneId).arg(zp.posteriorMean).arg(zp.credibleHigh)));
    }
}

void TestBayesianHierarchicalDeep6::testNegBinomCDFFloorViaPredictiveThreshold()
{
    QVector<CrimeEvent> events = repeatSuburb(QStringLiteral("CDFZone"), 6);

    BayesianHierarchical model;
    model.fit(events, 30.0);
    QVERIFY(model.isFitted());

    // minCount <= 0 bypasses CDF; semantic P(Y >= 0) = 1.0 (negBinomCDF(-1) = 0).
    QCOMPARE(model.predictiveProbability(QStringLiteral("CDFZone"), 0), 1.0);
    QCOMPARE(model.predictiveProbability(QStringLiteral("CDFZone"), -1), 1.0);

    const double pAtLeastOne = model.predictiveProbability(QStringLiteral("CDFZone"), 1);
    QVERIFY2(pAtLeastOne > 0.0 && pAtLeastOne < 1.0,
             qPrintable(QStringLiteral("P(Y>=1) should be in (0,1), got %1")
                            .arg(pAtLeastOne)));
}

void TestBayesianHierarchicalDeep6::testPredictMeanUnknownZoneReturnsGlobalPrior()
{
    QVector<CrimeEvent> events = repeatSuburb(QStringLiteral("Known"), 22);

    const double exposure = 45.0;
    BayesianHierarchical model;
    model.fit(events, exposure);
    QVERIFY(model.isFitted());

    const double globalMu = model.globalMean();
    const double unseenMean = model.predictMean(QStringLiteral("NeverSeen"), exposure);

    QVERIFY2(qAbs(unseenMean - globalMu * exposure) < 1e-9,
             qPrintable(QStringLiteral("Unknown zone predictMean (%1) should equal "
                                        "global prior rate × exposure (%2)")
                            .arg(unseenMean).arg(globalMu * exposure)));
}

void TestBayesianHierarchicalDeep6::testShrinkageHighCountCloserToMleThanLowCount()
{
    QVector<CrimeEvent> events;
    events += repeatSuburb(QStringLiteral("Sparse"), 3);
    events += repeatSuburb(QStringLiteral("Dense"),  45);

    const double exposure = 30.0;
    BayesianHierarchical model;
    model.fit(events, exposure);
    QVERIFY(model.isFitted());

    const auto sparse = model.posteriorForZone(QStringLiteral("Sparse"));
    const auto dense  = model.posteriorForZone(QStringLiteral("Dense"));

    const double mleSparse = static_cast<double>(sparse.observedCount) / exposure;
    const double mleDense  = static_cast<double>(dense.observedCount) / exposure;

    const double errSparse = qAbs(sparse.posteriorMean - mleSparse);
    const double errDense  = qAbs(dense.posteriorMean - mleDense);

    QVERIFY2(errDense < errSparse,
             qPrintable(QStringLiteral("Dense zone |posterior-MLE| (%1) should be "
                                        "smaller than sparse (%2)")
                            .arg(errDense).arg(errSparse)));

    const double shrinkSparse = model.shrinkageEstimate(QStringLiteral("Sparse"));
    const double shrinkDense  = model.shrinkageEstimate(QStringLiteral("Dense"));
    QVERIFY2(qAbs(shrinkDense - mleDense) <= qAbs(shrinkSparse - mleSparse) + 1e-9,
             "High-count shrinkage estimate should sit closer to zone MLE");
}

void TestBayesianHierarchicalDeep6::testZoneCountMatchesUniqueSuburbs()
{
    QVector<CrimeEvent> events;
    events += repeatSuburb(QStringLiteral("Alpha"), 5);
    events += repeatSuburb(QStringLiteral("Beta"),  5);
    events += repeatSuburb(QStringLiteral("Gamma"), 5);
    events += repeatSuburb(QStringLiteral("Alpha"), 2); // duplicate suburb

    BayesianHierarchical model;
    model.fit(events, 30.0);

    QCOMPARE(model.zoneCount(), 3);
    QCOMPARE(model.allPosteriors().size(), 3);
    QVERIFY(model.posteriorForZone(QStringLiteral("Alpha")).observedCount == 7);
}

void TestBayesianHierarchicalDeep6::testGammaPPFZeroQuantileFloorNonNegative()
{
    // gammaPPF returns 0 when p <= 0; credibleLow uses p=0.05 but must stay >= 0.
    QVector<CrimeEvent> events;
    events += repeatSuburb(QStringLiteral("Edge"), 1);
    events += repeatSuburb(QStringLiteral("Bulk"), 30);

    BayesianHierarchical model;
    model.fit(events, 30.0);
    QVERIFY(model.isFitted());

    for (const auto& zp : model.allPosteriors()) {
        QVERIFY2(zp.credibleLow >= 0.0,
                 qPrintable(QStringLiteral("Zone '%1': credibleLow must be non-negative")
                                .arg(zp.zoneId)));
    }

    const auto unseen = model.posteriorForZone(QStringLiteral("Unobserved"));
    QVERIFY2(unseen.credibleLow >= 0.0,
             "Unseen zone credibleLow respects gammaPPF zero floor");
}

void TestBayesianHierarchicalDeep6::testPredictiveProbabilityStrictlyBetweenZeroAndOne()
{
    QVector<CrimeEvent> events = repeatSuburb(QStringLiteral("ProbZone"), 10);

    BayesianHierarchical model;
    model.fit(events, 30.0);

    const double p = model.predictiveProbability(QStringLiteral("ProbZone"), 3);
    QVERIFY2(p > 0.0 && p <= 1.0,
             qPrintable(QStringLiteral("Predictive probability must be in (0,1], got %1")
                            .arg(p)));
}

QTEST_GUILESS_MAIN(TestBayesianHierarchicalDeep6)
#include "test_bayesian_hierarchical_deep6.moc"
