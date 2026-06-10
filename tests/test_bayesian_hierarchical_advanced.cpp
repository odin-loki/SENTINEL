// test_bayesian_hierarchical_advanced.cpp — Advanced tests for BayesianHierarchical
#include <QTest>
#include <QCoreApplication>
#include <cmath>
#include "models/BayesianHierarchical.h"
#include "core/CrimeEvent.h"

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────
namespace {

static int s_counter = 0;

static CrimeEvent makeEvent(const QString& suburb, const QString& type = "burglary") {
    CrimeEvent ev;
    ev.eventId = ev.id = QString("ev_adv_%1").arg(s_counter++);
    ev.crimeType = type;
    ev.suburb    = suburb;
    ev.qualityScore = 1.0;
    return ev;
}

static QVector<CrimeEvent> makeZoneEvents(const QString& zone, int count,
                                           const QString& type = "burglary") {
    QVector<CrimeEvent> evs;
    evs.reserve(count);
    for (int i = 0; i < count; ++i) evs.append(makeEvent(zone, type));
    return evs;
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// TestBayesianHierarchicalAdvanced
// ─────────────────────────────────────────────────────────────────────────────

class TestBayesianHierarchicalAdvanced : public QObject {
    Q_OBJECT

private slots:

    // 1. shrinkageEstimate() always returns > 0
    void testShrinkageEstimateRange() {
        BayesianHierarchical bh;
        QVector<CrimeEvent> events;
        events += makeZoneEvents("ZoneHigh", 40);
        events += makeZoneEvents("ZoneLow",  2);
        bh.fit(events, 30.0);

        QVERIFY(bh.isFitted());
        const double sHigh = bh.shrinkageEstimate("ZoneHigh");
        const double sLow  = bh.shrinkageEstimate("ZoneLow");
        QVERIFY2(sHigh > 0.0, "Shrinkage estimate for high-count zone must be > 0");
        QVERIFY2(sLow  > 0.0, "Shrinkage estimate for low-count zone must be > 0");
    }

    // 2. Zone with many observations has shrinkage weight closer to its own rate
    void testHighCountZonePullsTowardSelf() {
        BayesianHierarchical bh;
        QVector<CrimeEvent> events;
        events += makeZoneEvents("Common", 60);  // large dataset, own rate well established
        events += makeZoneEvents("Rare",   1);
        bh.fit(events, 30.0);

        const double globalMu      = bh.globalMean();
        const double shrinkCommon  = bh.shrinkageEstimate("Common");
        const double posteriorCommon = bh.posteriorForZone("Common").posteriorMean;

        // For the high-count zone, shrinkage estimate should be closer to its
        // posterior mean than to the global mean
        const double distToSelf   = std::abs(shrinkCommon - posteriorCommon);
        const double distToGlobal = std::abs(shrinkCommon - globalMu);

        qDebug() << "Common: shrink=" << shrinkCommon
                 << "posterior=" << posteriorCommon
                 << "global=" << globalMu;

        // The shrinkage estimate should NOT be closer to global than to its own rate
        // when a zone has many observations
        QVERIFY2(distToSelf <= distToGlobal + 1e-9,
                 "High-count zone shrinkage should pull toward its own posterior mean");
    }

    // 3. Zone with 1 observation shrinks toward global mean
    void testLowCountZonePullsTowardGlobal() {
        BayesianHierarchical bh;
        QVector<CrimeEvent> events;
        events += makeZoneEvents("Background", 50);  // anchor global mean high
        events += makeZoneEvents("Singleton",  1);
        bh.fit(events, 30.0);

        const double globalMu     = bh.globalMean();
        const double shrinkSingle = bh.shrinkageEstimate("Singleton");
        const double rawSingle    = bh.posteriorForZone("Singleton").posteriorMean;

        qDebug() << "Singleton: shrink=" << shrinkSingle
                 << "posterior=" << rawSingle
                 << "global=" << globalMu;

        // Shrinkage should be strictly between the raw single-zone rate and global mean
        // OR pulled toward global (i.e. closer to global than raw rate is)
        const double distShrinkToGlobal = std::abs(shrinkSingle - globalMu);
        const double distRawToGlobal    = std::abs(rawSingle - globalMu);

        QVERIFY2(distShrinkToGlobal <= distRawToGlobal + 1e-9,
                 "Low-count zone shrinkage must pull toward global mean");
    }

    // 4. predictiveProbability() always returns [0, 1]
    void testPredictiveProbabilityRange() {
        BayesianHierarchical bh;
        QVector<CrimeEvent> events;
        events += makeZoneEvents("Hot",  50);
        events += makeZoneEvents("Cold", 1);
        bh.fit(events, 30.0);

        for (const QString& zone : { "Hot", "Cold" }) {
            for (int k : { 0, 1, 5, 10, 20 }) {
                const double p = bh.predictiveProbability(zone, k);
                QVERIFY2(p >= 0.0 && p <= 1.0,
                         qPrintable(QStringLiteral("P(Y≥%1|%2)=%3 out of [0,1]")
                                    .arg(k).arg(zone).arg(p)));
            }
        }
    }

    // 5. predictMean() always returns > 0 after fit
    void testPredictMeanPositive() {
        BayesianHierarchical bh;
        QVector<CrimeEvent> events;
        events += makeZoneEvents("ZoneA", 20);
        events += makeZoneEvents("ZoneB", 5);
        bh.fit(events, 30.0);

        for (const QString& zone : { "ZoneA", "ZoneB" }) {
            for (double days : { 1.0, 7.0, 30.0 }) {
                const double mean = bh.predictMean(zone, days);
                QVERIFY2(mean > 0.0,
                         qPrintable(QStringLiteral("predictMean(%1, %2)=%3 must be > 0")
                                    .arg(zone).arg(days).arg(mean)));
            }
        }
    }

    // 6. globalMean() matches expectation from data
    void testGlobalMeanFromFit() {
        // Fit two zones with known counts to bound the global mean
        BayesianHierarchical bh;
        QVector<CrimeEvent> events;
        events += makeZoneEvents("Z1", 10);
        events += makeZoneEvents("Z2", 20);
        bh.fit(events, 30.0);

        const double mu = bh.globalMean();
        qDebug() << "globalMean:" << mu;

        // With 10 and 20 events in 30 days, the cross-zone mean should be
        // between 0.1 and 1.0 events/day — sanity check only
        QVERIFY2(mu > 0.0,  "globalMean must be positive");
        QVERIFY2(mu < 5.0,  "globalMean should not be unreasonably large");
    }

    // 7. allPosteriors() returns one entry per zone
    void testAllPosteriorsCount() {
        BayesianHierarchical bh;
        QVector<CrimeEvent> events;
        const QStringList zones = { "Alpha", "Beta", "Gamma", "Delta" };
        for (const QString& z : zones)
            events += makeZoneEvents(z, 10);
        bh.fit(events, 30.0);

        const auto posts = bh.allPosteriors();
        QCOMPARE(posts.size(), zones.size());
    }

    // 8. allPosteriors() is sorted by posteriorMean descending
    void testAllPosteriorsOrdering() {
        BayesianHierarchical bh;
        QVector<CrimeEvent> events;
        events += makeZoneEvents("High",   50);
        events += makeZoneEvents("Medium", 15);
        events += makeZoneEvents("Low",     3);
        bh.fit(events, 30.0);

        const auto posts = bh.allPosteriors();
        QCOMPARE(posts.size(), 3);

        for (int i = 1; i < posts.size(); ++i) {
            QVERIFY2(posts[i-1].posteriorMean >= posts[i].posteriorMean,
                     "allPosteriors() must be sorted by posteriorMean descending");
        }
    }

    // 9. credibleLow <= posteriorMean <= credibleHigh
    void testCredibleIntervalCoversMode() {
        BayesianHierarchical bh;
        QVector<CrimeEvent> events;
        for (int z = 0; z < 5; ++z)
            events += makeZoneEvents(QStringLiteral("Zone%1").arg(z), (z + 1) * 8);
        bh.fit(events, 30.0);

        for (const ZonePosterior& zp : bh.allPosteriors()) {
            QVERIFY2(zp.credibleLow >= 0.0,
                     qPrintable(QStringLiteral("%1: credibleLow=%2 must be >= 0")
                                .arg(zp.zoneId).arg(zp.credibleLow)));
            QVERIFY2(zp.credibleLow <= zp.posteriorMean,
                     qPrintable(QStringLiteral("%1: credibleLow=%2 > posteriorMean=%3")
                                .arg(zp.zoneId).arg(zp.credibleLow).arg(zp.posteriorMean)));
            QVERIFY2(zp.posteriorMean <= zp.credibleHigh,
                     qPrintable(QStringLiteral("%1: posteriorMean=%2 > credibleHigh=%3")
                                .arg(zp.zoneId).arg(zp.posteriorMean).arg(zp.credibleHigh)));
        }
    }

    // 10. Zone with 0 observed events still has a valid posterior
    void testZeroCountZone() {
        BayesianHierarchical bh;
        // Fit on some events, then query an unseen zone (0 obs)
        QVector<CrimeEvent> events;
        events += makeZoneEvents("KnownZone", 15);
        bh.fit(events, 30.0);

        const ZonePosterior zp = bh.posteriorForZone("ZeroZone");
        QCOMPARE(zp.observedCount, 0);
        QVERIFY2(zp.alphaPost > 0.0, "Zero-count zone alphaPost must be positive");
        QVERIFY2(zp.betaPost  > 0.0, "Zero-count zone betaPost must be positive");
        QVERIFY2(zp.posteriorMean >= 0.0, "Zero-count zone posteriorMean must be >= 0");
        // Credible interval must be valid
        QVERIFY(zp.credibleLow  >= 0.0);
        QVERIFY(zp.credibleHigh >= zp.credibleLow);
    }

    // 11. fit() with crimeType="burglary" only uses burglary events
    void testCrimeTypeFilter() {
        BayesianHierarchical bh;
        QVector<CrimeEvent> events;
        events += makeZoneEvents("ZoneX", 12, "burglary");
        events += makeZoneEvents("ZoneX", 25, "assault");   // should be ignored
        events += makeZoneEvents("ZoneY", 8,  "burglary");
        events += makeZoneEvents("ZoneY", 3,  "robbery");   // should be ignored

        bh.fit(events, 30.0, "burglary");
        QVERIFY(bh.isFitted());

        const ZonePosterior zpX = bh.posteriorForZone("ZoneX");
        const ZonePosterior zpY = bh.posteriorForZone("ZoneY");

        // Only burglary counts should be included
        QCOMPARE(zpX.observedCount, 12);
        QCOMPARE(zpY.observedCount, 8);
    }

    // 12. fit() on empty event vector doesn't crash and leaves model unfitted
    void testEmptyFitReturnsDefaults() {
        BayesianHierarchical bh;
        bh.fit({}, 30.0);           // must not crash

        QVERIFY(!bh.isFitted());
        QCOMPARE(bh.zoneCount(), 0);

        // globalMean() on unfitted model must return 0.0
        QVERIFY(std::abs(bh.globalMean()) < 1e-12);
    }

    // 13. Fit 100 events across 5 zones, all 5 zones get posteriors
    void testMultipleZonesUpdated() {
        BayesianHierarchical bh;
        QVector<CrimeEvent> events;
        const int nZones = 5;
        for (int z = 0; z < nZones; ++z)
            events += makeZoneEvents(QStringLiteral("District%1").arg(z + 1), 20);

        QCOMPARE(events.size(), 100);
        bh.fit(events, 30.0);

        QVERIFY(bh.isFitted());
        QCOMPARE(bh.zoneCount(), nZones);

        const auto posts = bh.allPosteriors();
        QCOMPARE(posts.size(), nZones);

        for (const ZonePosterior& zp : posts) {
            QVERIFY2(zp.observedCount == 20,
                     qPrintable(QStringLiteral("%1: observedCount=%2, expected 20")
                                .arg(zp.zoneId).arg(zp.observedCount)));
            QVERIFY(zp.posteriorMean > 0.0);
        }
    }
};

// ─── main ─────────────────────────────────────────────────────────────────────

static int runTest(QObject* obj, const char* logFile) {
    QStringList args = { "test", "-o", QString("%1,txt").arg(logFile) };
    return QTest::qExec(obj, args);
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    TestBayesianHierarchicalAdvanced t1;
    return runTest(&t1, "bayesian_hierarchical_advanced.txt");
}

#include "test_bayesian_hierarchical_advanced.moc"
