// test_bayesian_hierarchical_deep5.cpp — Deep audit iteration 19:
// crime-type filter, empty-suburb bucketing, exposure floor, predictive
// probability, predictMean scaling, and shrinkage at zero observations.

#include <QTest>
#include <cmath>

#include "models/BayesianHierarchical.h"
#include "core/CrimeEvent.h"

class TestBayesianHierarchicalDeep5 : public QObject
{
    Q_OBJECT

private slots:
    void testEmptySuburbMapsToUnknownZone();
    void testCrimeTypeFilterExcludesNonMatching();
    void testExposureFloorClampsToMinimumOne();
    void testPosteriorBetaEqualsPriorPlusExposure();
    void testPredictMeanScalesLinearlyWithExposure();
    void testPredictiveProbabilityMonotoneInMinCount();
    void testPredictiveProbabilityZeroThresholdReturnsOne();
    void testShrinkageWithZeroObservationsEqualsGlobal();
};

// ─── Helper ──────────────────────────────────────────────────────────────────

static CrimeEvent makeEvent(const QString& suburb,
                            const QString& crimeType = QStringLiteral("theft"))
{
    CrimeEvent ev;
    ev.suburb    = suburb;
    ev.crimeType = crimeType;
    return ev;
}

static QVector<CrimeEvent> repeatEvent(const CrimeEvent& proto, int count)
{
    QVector<CrimeEvent> events;
    events.reserve(count);
    for (int i = 0; i < count; ++i)
        events.append(proto);
    return events;
}

// ─── Tests ───────────────────────────────────────────────────────────────────

void TestBayesianHierarchicalDeep5::testEmptySuburbMapsToUnknownZone()
{
    QVector<CrimeEvent> events;
    events += repeatEvent(makeEvent(QString()), 5);
    events += repeatEvent(makeEvent(QStringLiteral("Named")), 3);

    BayesianHierarchical model;
    model.fit(events, 30.0);
    QVERIFY(model.isFitted());
    QCOMPARE(model.zoneCount(), 2);

    const auto unknown = model.posteriorForZone(QStringLiteral("Unknown"));
    QVERIFY2(unknown.observedCount == 5,
             qPrintable(QStringLiteral("Unknown zone count expected 5, got %1")
                            .arg(unknown.observedCount)));
}

void TestBayesianHierarchicalDeep5::testCrimeTypeFilterExcludesNonMatching()
{
    QVector<CrimeEvent> events;
    events += repeatEvent(makeEvent(QStringLiteral("A"), QStringLiteral("theft")), 10);
    events += repeatEvent(makeEvent(QStringLiteral("A"), QStringLiteral("assault")), 20);
    events += repeatEvent(makeEvent(QStringLiteral("B"), QStringLiteral("theft")), 4);

    BayesianHierarchical model;
    model.fit(events, 30.0, QStringLiteral("theft"));
    QVERIFY(model.isFitted());
    QCOMPARE(model.zoneCount(), 2);

    const auto zoneA = model.posteriorForZone(QStringLiteral("A"));
    QCOMPARE(zoneA.observedCount, 10);
    const auto zoneB = model.posteriorForZone(QStringLiteral("B"));
    QCOMPARE(zoneB.observedCount, 4);
}

void TestBayesianHierarchicalDeep5::testExposureFloorClampsToMinimumOne()
{
    QVector<CrimeEvent> events = repeatEvent(makeEvent(QStringLiteral("Z")), 10);

    BayesianHierarchical model;
    model.fit(events, 0.0);
    QVERIFY(model.isFitted());

    const auto zp = model.posteriorForZone(QStringLiteral("Z"));
    // betaPost = betaPrior + max(exposure, 1.0) → exposure field should be 1.0
    QVERIFY2(qAbs(zp.exposure - 1.0) < 1e-9,
             qPrintable(QStringLiteral("Exposure floor expected 1.0, got %1")
                            .arg(zp.exposure)));
    // MLE with 10 events / 1 day = 10; posterior mean should be well above 1/day
    QVERIFY2(zp.posteriorMean > 1.0,
             qPrintable(QStringLiteral("Posterior mean with floored exposure expected >1, got %1")
                            .arg(zp.posteriorMean)));
}

void TestBayesianHierarchicalDeep5::testPosteriorBetaEqualsPriorPlusExposure()
{
    QVector<CrimeEvent> events;
    events += repeatEvent(makeEvent(QStringLiteral("North")), 12);
    events += repeatEvent(makeEvent(QStringLiteral("South")),  6);

    const double exposure = 45.0;
    BayesianHierarchical model;
    model.fit(events, exposure);
    QVERIFY(model.isFitted());

    for (const auto& zp : model.allPosteriors()) {
        const double expectedBeta = zp.betaPrior + exposure;
        QVERIFY2(qAbs(zp.betaPost - expectedBeta) < 1e-9,
                 qPrintable(QStringLiteral("Zone '%1': betaPost (%2) != betaPrior+exposure (%3)")
                                .arg(zp.zoneId).arg(zp.betaPost).arg(expectedBeta)));
    }
}

void TestBayesianHierarchicalDeep5::testPredictMeanScalesLinearlyWithExposure()
{
    QVector<CrimeEvent> events = repeatEvent(makeEvent(QStringLiteral("RateZone")), 15);

    BayesianHierarchical model;
    model.fit(events, 30.0);
    QVERIFY(model.isFitted());

    const double mean30 = model.predictMean(QStringLiteral("RateZone"), 30.0);
    const double mean60 = model.predictMean(QStringLiteral("RateZone"), 60.0);
    const double mean15 = model.predictMean(QStringLiteral("RateZone"), 15.0);

    QVERIFY2(qAbs(mean60 - 2.0 * mean30) < 1e-9,
             qPrintable(QStringLiteral("60-day predictMean (%1) should be 2× 30-day (%2)")
                            .arg(mean60).arg(mean30)));
    QVERIFY2(qAbs(mean15 - 0.5 * mean30) < 1e-9,
             qPrintable(QStringLiteral("15-day predictMean (%1) should be ½× 30-day (%2)")
                            .arg(mean15).arg(mean30)));
}

void TestBayesianHierarchicalDeep5::testPredictiveProbabilityMonotoneInMinCount()
{
    QVector<CrimeEvent> events = repeatEvent(makeEvent(QStringLiteral("PredZone")), 8);

    BayesianHierarchical model;
    model.fit(events, 30.0);
    QVERIFY(model.isFitted());

    const double p0 = model.predictiveProbability(QStringLiteral("PredZone"), 1);
    const double p1 = model.predictiveProbability(QStringLiteral("PredZone"), 2);
    const double p2 = model.predictiveProbability(QStringLiteral("PredZone"), 5);

    QVERIFY2(p0 >= p1,
             qPrintable(QStringLiteral("P(Y≥1)=%1 should be ≥ P(Y≥2)=%2")
                            .arg(p0).arg(p1)));
    QVERIFY2(p1 >= p2,
             qPrintable(QStringLiteral("P(Y≥2)=%1 should be ≥ P(Y≥5)=%2")
                            .arg(p1).arg(p2)));
    QVERIFY2(p0 > 0.0 && p0 <= 1.0,
             qPrintable(QStringLiteral("Predictive probability must be in (0,1], got %1")
                            .arg(p0)));
}

void TestBayesianHierarchicalDeep5::testPredictiveProbabilityZeroThresholdReturnsOne()
{
    QVector<CrimeEvent> events = repeatEvent(makeEvent(QStringLiteral("Thresh")), 3);

    BayesianHierarchical model;
    model.fit(events, 30.0);

    QCOMPARE(model.predictiveProbability(QStringLiteral("Thresh"), 0), 1.0);
    QCOMPARE(model.predictiveProbability(QStringLiteral("Thresh"), -5), 1.0);
}

void TestBayesianHierarchicalDeep5::testShrinkageWithZeroObservationsEqualsGlobal()
{
    QVector<CrimeEvent> events = repeatEvent(makeEvent(QStringLiteral("Known")), 25);

    BayesianHierarchical model;
    model.fit(events, 30.0);
    QVERIFY(model.isFitted());

    const double globalMu = model.globalMean();
    const double shrinkUnseen = model.shrinkageEstimate(QStringLiteral("Unseen"));
    const double shrinkKnown  = model.shrinkageEstimate(QStringLiteral("Known"));

    QVERIFY2(qAbs(shrinkUnseen - globalMu) < 1e-9,
             qPrintable(QStringLiteral("Zero-obs shrinkage (%1) should equal global mean (%2)")
                            .arg(shrinkUnseen).arg(globalMu)));

    const auto zpKnown = model.posteriorForZone(QStringLiteral("Known"));
    QVERIFY2(qAbs(shrinkKnown - zpKnown.posteriorMean) < 1e-9,
             qPrintable(QStringLiteral("High-count zone shrinkage (%1) should equal "
                                        "posterior mean (%2) when w≈1")
                            .arg(shrinkKnown).arg(zpKnown.posteriorMean)));
}

QTEST_GUILESS_MAIN(TestBayesianHierarchicalDeep5)
#include "test_bayesian_hierarchical_deep5.moc"
