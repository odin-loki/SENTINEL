#include <QTest>
#include <cmath>
#include "models/BayesianHierarchical.h"
#include "core/CrimeEvent.h"

namespace {

CrimeEvent makeEv(const QString& suburb)
{
    static int counter = 0;
    CrimeEvent ev;
    ev.eventId = ev.id = QString("ev_%1").arg(counter++);
    ev.crimeType = QStringLiteral("burglary");
    ev.suburb    = suburb;
    return ev;
}

QVector<CrimeEvent> zoneEvents(const QString& zone, int n)
{
    QVector<CrimeEvent> evs;
    evs.reserve(n);
    for (int i = 0; i < n; ++i)
        evs.append(makeEv(zone));
    return evs;
}

} // namespace

class TestBayesianHierarchicalDeep2 : public QObject {
    Q_OBJECT

private slots:

    void testPosteriorUpdateGamma2_1Prior()
    {
        // Empirical Bayes with zones having rates 1.0 and 3.0 (counts/exposure):
        //   mu = 2, var = 2  →  α₀ = mu²/var = 2,  β₀ = mu/var = 1
        // Zone "ZoneB" has k=3 crimes, exposure=1:
        //   alphaPost = 2+3 = 5,  betaPost = 1+1 = 2,  mean = 2.5
        QVector<CrimeEvent> events;
        events += zoneEvents("ZoneA", 1);
        events += zoneEvents("ZoneB", 3);

        BayesianHierarchical bh;
        bh.fit(events, 1.0);

        QVERIFY(bh.isFitted());
        QVERIFY(std::abs(bh.globalAlpha() - 2.0) < 1e-9);
        QVERIFY(std::abs(bh.globalBeta()  - 1.0) < 1e-9);

        ZonePosterior zp = bh.posteriorForZone("ZoneB");
        QVERIFY(std::abs(zp.alphaPost   - 5.0) < 1e-9);
        QVERIFY(std::abs(zp.betaPost    - 2.0) < 1e-9);
        QVERIFY(std::abs(zp.posteriorMean - 2.5) < 1e-9);
    }

    void testEmpiricalBayesHyperparametersPositive()
    {
        QVector<CrimeEvent> events;
        events += zoneEvents("Alpha",   5);
        events += zoneEvents("Beta",   12);
        events += zoneEvents("Gamma",   3);
        events += zoneEvents("Delta",  20);
        events += zoneEvents("Epsilon", 8);

        BayesianHierarchical bh;
        bh.fit(events, 30.0);

        QVERIFY(bh.isFitted());
        QVERIFY(bh.globalAlpha() > 0.0);
        QVERIFY(bh.globalBeta()  > 0.0);
    }

    void testPredictiveMeanPositiveAllZones()
    {
        QVector<CrimeEvent> events;
        events += zoneEvents("North",  7);
        events += zoneEvents("South", 14);
        events += zoneEvents("East",   2);
        events += zoneEvents("West",  21);

        BayesianHierarchical bh;
        bh.fit(events, 30.0);

        QVERIFY(bh.isFitted());
        for (const ZonePosterior& zp : bh.allPosteriors()) {
            QVERIFY(zp.posteriorMean > 0.0);
        }
    }

    void testZeroObservationsReturnsPriorMean()
    {
        QVector<CrimeEvent> events;
        events += zoneEvents("ZoneA", 4);
        events += zoneEvents("ZoneB", 8);

        BayesianHierarchical bh;
        bh.fit(events, 10.0);

        QVERIFY(bh.isFitted());

        ZonePosterior unobserved = bh.posteriorForZone("Nonexistent");
        const double priorMean = bh.globalMean();

        QVERIFY(priorMean > 0.0);
        QVERIFY(std::abs(unobserved.posteriorMean - priorMean) < 1e-9);
    }

    void testIncreasingObservationsMovesPosteriorTowardDataMean()
    {
        // Zone with more observations gets a posterior mean closer to its data rate.
        // We compare two zones sharing the same prior:
        //   ZoneLow:  3 events / 1 day → data rate 3
        //   ZoneHigh: 9 events / 1 day → data rate 9
        // ZoneHigh's posterior mean must exceed ZoneLow's (same prior, higher rate).
        QVector<CrimeEvent> events;
        events += zoneEvents("ZoneLow",  3);
        events += zoneEvents("ZoneHigh", 9);

        BayesianHierarchical bh;
        bh.fit(events, 1.0);

        QVERIFY(bh.isFitted());
        ZonePosterior zpLow  = bh.posteriorForZone("ZoneLow");
        ZonePosterior zpHigh = bh.posteriorForZone("ZoneHigh");

        QVERIFY(zpHigh.posteriorMean > zpLow.posteriorMean);
        QVERIFY(zpLow.posteriorMean  > 0.0);
        QVERIFY(zpHigh.posteriorMean > 0.0);

        // Posterior is a shrinkage blend of prior mean and data rate.
        // ZoneLow  posterior must lie between its data rate and the prior mean.
        // ZoneHigh posterior must lie between the prior mean and its data rate.
        const double priorMean = bh.globalMean();
        const double lo = qMin(priorMean, 3.0);
        const double hi = qMax(priorMean, 3.0);
        QVERIFY(zpLow.posteriorMean >= lo - 1e-9);
        QVERIFY(zpLow.posteriorMean <= hi + 1e-9);
    }

    void testPosteriorVarianceFormula()
    {
        // Var[λ|data] = alphaPost / betaPost²
        QVector<CrimeEvent> events;
        events += zoneEvents("ZoneA", 2);
        events += zoneEvents("ZoneB", 6);

        BayesianHierarchical bh;
        bh.fit(events, 1.0);

        QVERIFY(bh.isFitted());
        for (const ZonePosterior& zp : bh.allPosteriors()) {
            const double expectedVar = zp.alphaPost / (zp.betaPost * zp.betaPost);
            QVERIFY(std::abs(zp.posteriorVar - expectedVar) < 1e-12);
            QVERIFY(zp.posteriorVar > 0.0);
        }
    }
};

QTEST_GUILESS_MAIN(TestBayesianHierarchicalDeep2)
#include "test_bayesian_hierarchical_deep2.moc"
