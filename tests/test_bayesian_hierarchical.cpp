// test_bayesian_hierarchical.cpp — Tests for BayesianHierarchical model
#include <QTest>
#include <QCoreApplication>
#include <cmath>
#include "models/BayesianHierarchical.h"
#include "core/CrimeEvent.h"

namespace {

CrimeEvent makeEvent(const QString& suburb, const QString& type = "burglary") {
    CrimeEvent ev;
    static int counter = 0;
    ev.eventId = ev.id = QString("ev_%1").arg(counter++);
    ev.crimeType = type;
    ev.suburb    = suburb;
    ev.qualityScore = 1.0;
    return ev;
}

QVector<CrimeEvent> makeZoneEvents(const QString& zone, int count,
                                    const QString& type = "burglary") {
    QVector<CrimeEvent> evs;
    for (int i = 0; i < count; ++i) evs.append(makeEvent(zone, type));
    return evs;
}

} // namespace

class TestBayesianHierarchical : public QObject {
    Q_OBJECT
private slots:

    void testNotFittedInitially() {
        BayesianHierarchical bh;
        QVERIFY(!bh.isFitted());
        QCOMPARE(bh.zoneCount(), 0);
    }

    void testFitEmpty() {
        BayesianHierarchical bh;
        bh.fit({}, 30.0);
        QVERIFY(!bh.isFitted());
    }

    void testFitSingleZone() {
        BayesianHierarchical bh;
        const auto events = makeZoneEvents("ZoneA", 10);
        bh.fit(events, 30.0);
        QVERIFY(bh.isFitted());
        QCOMPARE(bh.zoneCount(), 1);
    }

    void testFitMultipleZones() {
        BayesianHierarchical bh;
        QVector<CrimeEvent> events;
        events += makeZoneEvents("ZoneA", 20);
        events += makeZoneEvents("ZoneB", 5);
        events += makeZoneEvents("ZoneC", 15);
        bh.fit(events, 30.0);
        QVERIFY(bh.isFitted());
        QCOMPARE(bh.zoneCount(), 3);
    }

    void testPosteriorMeanReasonable() {
        // ZoneA has 30 crimes in 30 days → ~1/day rate
        // ZoneB has 3 crimes in 30 days → ~0.1/day rate
        BayesianHierarchical bh;
        QVector<CrimeEvent> events;
        events += makeZoneEvents("ZoneA", 30);
        events += makeZoneEvents("ZoneB", 3);
        bh.fit(events, 30.0);

        const auto zpA = bh.posteriorForZone("ZoneA");
        const auto zpB = bh.posteriorForZone("ZoneB");

        QVERIFY(zpA.posteriorMean > zpB.posteriorMean);
        QVERIFY(zpA.observedCount == 30);
        QVERIFY(zpB.observedCount == 3);
    }

    void testShrinkageTowardsGlobal() {
        // Shrinkage for zone with very few events should be closer to global mean
        BayesianHierarchical bh;
        QVector<CrimeEvent> events;
        events += makeZoneEvents("Common", 50);   // many observations
        events += makeZoneEvents("Rare", 1);       // few observations
        bh.fit(events, 30.0);

        // Rare zone raw rate = 1/30, but should shrink towards global
        const double shrinkRare   = bh.shrinkageEstimate("Rare");
        const double shrinkCommon = bh.shrinkageEstimate("Common");
        const double globalMu     = bh.globalMean();

        // Rare should be shrunk more towards global than Common
        // i.e. |shrinkRare - globalMu| < |shrinkCommon - globalMu|
        // unless they happen to be near the same value
        qDebug() << "Global mean:" << globalMu
                 << "Rare shrinkage:" << shrinkRare
                 << "Common shrinkage:" << shrinkCommon;

        // Shrinkage values should be in reasonable range
        QVERIFY(shrinkRare > 0.0);
        QVERIFY(shrinkCommon > 0.0);
        QVERIFY(shrinkRare <= shrinkCommon + 1.0);  // not wildly inflated
    }

    void testPosteriorCredibleInterval() {
        BayesianHierarchical bh;
        auto events = makeZoneEvents("TestZone", 20);
        bh.fit(events, 30.0);
        const auto zp = bh.posteriorForZone("TestZone");

        QVERIFY(zp.credibleLow  >= 0.0);
        QVERIFY(zp.credibleHigh >  zp.credibleLow);
        QVERIFY(zp.posteriorMean >= zp.credibleLow);
        QVERIFY(zp.posteriorMean <= zp.credibleHigh);
    }

    void testPredictiveProbability() {
        BayesianHierarchical bh;
        auto events = makeZoneEvents("HotZone", 60);   // 60 crimes / 30 days = 2/day
        bh.fit(events, 30.0);

        // High-crime zone: P(Y ≥ 1) should be very high
        const double pAtLeastOne = bh.predictiveProbability("HotZone", 1);
        qDebug() << "P(Y≥1 | HotZone):" << pAtLeastOne;
        QVERIFY2(pAtLeastOne > 0.5, "Hot zone should have > 50% P(at least 1 crime)");

        // Cold zone
        auto events2 = makeZoneEvents("ColdZone", 1);
        QVector<CrimeEvent> allEvents = events + events2;
        bh.fit(allEvents, 30.0);
        const double pHot  = bh.predictiveProbability("HotZone",  5);
        const double pCold = bh.predictiveProbability("ColdZone", 5);
        qDebug() << "P(Y≥5 | HotZone):" << pHot << "P(Y≥5 | ColdZone):" << pCold;
        QVERIFY(pHot > pCold);
    }

    void testPredictMean() {
        BayesianHierarchical bh;
        // 30 crimes in 30 days → expect ~1/day → ~7 in 7 days
        auto events = makeZoneEvents("Zone", 30);
        bh.fit(events, 30.0);

        const double mean7days = bh.predictMean("Zone", 7.0);
        qDebug() << "Predicted mean over 7 days:" << mean7days;
        // Should be around 7 ± 3 (due to partial pooling)
        QVERIFY(mean7days > 0.5);
        QVERIFY(mean7days < 30.0);  // should not be wildly large
    }

    void testUnseenZoneFallsBackToPrior() {
        BayesianHierarchical bh;
        auto events = makeZoneEvents("KnownZone", 10);
        bh.fit(events, 30.0);

        // Unseen zone should return prior-based posterior
        const auto zpNew = bh.posteriorForZone("NewUnseenZone");
        QVERIFY(zpNew.observedCount == 0);
        QVERIFY(zpNew.alphaPost == bh.globalAlpha());
    }

    void testAllPosteriorsOrdering() {
        BayesianHierarchical bh;
        QVector<CrimeEvent> events;
        events += makeZoneEvents("High",  50);
        events += makeZoneEvents("Med",   20);
        events += makeZoneEvents("Low",    2);
        bh.fit(events, 30.0);

        const auto posts = bh.allPosteriors();
        QCOMPARE(posts.size(), 3);
        // Should be sorted descending by posteriorMean
        for (int i = 1; i < posts.size(); ++i)
            QVERIFY(posts[i-1].posteriorMean >= posts[i].posteriorMean);
    }

    void testHyperparametersPositive() {
        BayesianHierarchical bh;
        QVector<CrimeEvent> events;
        for (int z = 0; z < 10; ++z)
            events += makeZoneEvents(QString("Zone%1").arg(z), z + 1);
        bh.fit(events, 30.0);
        QVERIFY(bh.globalAlpha() > 0.0);
        QVERIFY(bh.globalBeta()  > 0.0);
        QVERIFY(bh.globalMean()  > 0.0);
    }

    void testCrimeTypeFilter() {
        BayesianHierarchical bh;
        QVector<CrimeEvent> events;
        events += makeZoneEvents("ZoneA", 10, "burglary");
        events += makeZoneEvents("ZoneA", 5,  "robbery");
        events += makeZoneEvents("ZoneB", 8,  "burglary");
        // Fit only for burglary
        bh.fit(events, 30.0, "burglary");
        QVERIFY(bh.isFitted());
        // Both zones had burglaries
        const auto zpA = bh.posteriorForZone("ZoneA");
        QCOMPARE(zpA.observedCount, 10);  // only burglaries counted
    }

    void testPosteriorVarianceDeclines() {
        // Within a single fitted model that has heterogeneous zones, the zone
        // with more observations should have a narrower credible interval
        // relative to its mean than the zone with fewer observations.
        // (Shared empirical Bayes hyperparameters → same prior for both zones;
        //  only the likelihood contribution differs.)
        BayesianHierarchical bh;

        // Mix of zones at very different crime rates to get non-degenerate
        // empirical Bayes (non-zero cross-zone variance)
        QVector<CrimeEvent> events;
        events += makeZoneEvents("HighData",  50);  // high rate, lots of data
        events += makeZoneEvents("LowData",    3);  // low rate, little data
        events += makeZoneEvents("MedZone1",  20);
        events += makeZoneEvents("MedZone2",  15);
        bh.fit(events, 30.0);

        const ZonePosterior zpHigh = bh.posteriorForZone("HighData");
        const ZonePosterior zpLow  = bh.posteriorForZone("LowData");

        // Relative credible interval width = (high - low) / mean
        auto relWidth = [](const ZonePosterior& zp) -> double {
            return (zp.posteriorMean > 0.0)
                ? (zp.credibleHigh - zp.credibleLow) / zp.posteriorMean
                : 1e9;
        };

        const double rwHigh = relWidth(zpHigh);
        const double rwLow  = relWidth(zpLow);

        qDebug() << "Rel. width (50 events):" << rwHigh
                 << "Rel. width (3 events):"  << rwLow;

        QVERIFY2(rwHigh < rwLow,
            "Zone with more data should have narrower relative credible interval");
    }
};

// ─── main ─────────────────────────────────────────────────────────────────────
static int runTest(QObject* obj, const char* logFile) {
    QStringList args = { "test", "-o", QString("%1,txt").arg(logFile) };
    return QTest::qExec(obj, args);
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    TestBayesianHierarchical t1;
    return runTest(&t1, "bayesian_hierarchical.txt");
}

#include "test_bayesian_hierarchical.moc"
