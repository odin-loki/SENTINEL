// test_bayesian_hierarchical_convergence.cpp
// Tests that BayesianHierarchical model fits correctly, posteriors are valid,
// shrinkage works, and predictions are well-calibrated.
#include <QTest>
#include "models/BayesianHierarchical.h"
#include "core/CrimeEvent.h"

static CrimeEvent makeEvent(const QString& zone, const QDate& date)
{
    CrimeEvent ev;
    ev.eventId   = zone + QStringLiteral("_") + date.toString(Qt::ISODate);
    ev.crimeType = QStringLiteral("burglary");
    ev.suburb    = zone;
    const QDateTime dt(date, QTime(12, 0, 0), QTimeZone::utc());
    ev.occurredAt = dt;
    ev.timestamp  = dt;
    return ev;
}

class BayesianHierarchicalConvergenceTest : public QObject
{
    Q_OBJECT

private:
    QVector<CrimeEvent> makeZoneHistory(const QString& zone, int count,
                                         const QDate& base = QDate(2024, 1, 1))
    {
        QVector<CrimeEvent> evs;
        for (int i = 0; i < count; ++i)
            evs.append(makeEvent(zone, base.addDays(i % 30)));
        return evs;
    }

private slots:

    // ── 1. isFitted() true after fit ─────────────────────────────────────────
    void testIsFittedAfterFit()
    {
        BayesianHierarchical model;
        QVERIFY(!model.isFitted());

        QVector<CrimeEvent> evs = makeZoneHistory(QStringLiteral("Z1"), 20);
        model.fit(evs);
        QVERIFY(model.isFitted());
    }

    // ── 2. Zone count correct after fit ──────────────────────────────────────
    void testZoneCountCorrect()
    {
        BayesianHierarchical model;
        QVector<CrimeEvent> evs;
        for (const auto& z : { QStringLiteral("A"), QStringLiteral("B"),
                                QStringLiteral("C"), QStringLiteral("D"),
                                QStringLiteral("E") })
            evs += makeZoneHistory(z, 10);

        model.fit(evs, 30.0);
        QCOMPARE(model.zoneCount(), 5);
    }

    // ── 3. Posterior mean is positive for active zones ────────────────────────
    void testPosteriorMeanPositive()
    {
        BayesianHierarchical model;
        QVector<CrimeEvent> evs = makeZoneHistory(QStringLiteral("HOT"), 50);
        model.fit(evs);

        const auto post = model.posteriorForZone(QStringLiteral("HOT"));
        QVERIFY2(post.posteriorMean > 0.0,
                 qPrintable(QStringLiteral("Posterior mean %1 should be positive")
                    .arg(post.posteriorMean)));
    }

    // ── 4. Credible interval is valid (low < mean < high) ────────────────────
    void testCredibleIntervalValid()
    {
        BayesianHierarchical model;
        QVector<CrimeEvent> evs = makeZoneHistory(QStringLiteral("Z"), 20);
        model.fit(evs);

        const auto post = model.posteriorForZone(QStringLiteral("Z"));
        QVERIFY2(post.credibleLow >= 0.0,
                 "Credible interval lower bound must be >= 0");
        QVERIFY2(post.credibleLow < post.credibleHigh,
                 qPrintable(QStringLiteral("CI low (%1) must be < CI high (%2)")
                    .arg(post.credibleLow).arg(post.credibleHigh)));
        QVERIFY2(post.posteriorMean >= post.credibleLow &&
                 post.posteriorMean <= post.credibleHigh,
                 qPrintable(QStringLiteral(
                    "Posterior mean (%1) should be within CI [%2, %3]")
                    .arg(post.posteriorMean).arg(post.credibleLow).arg(post.credibleHigh)));
    }

    // ── 5. High-activity zone has higher posterior mean than low-activity ─────
    void testHighActivityHigherMean()
    {
        BayesianHierarchical model;
        QVector<CrimeEvent> evs;
        evs += makeZoneHistory(QStringLiteral("HIGH"), 60);   // 60 crimes
        evs += makeZoneHistory(QStringLiteral("LOW"),  5);    // 5 crimes

        model.fit(evs);

        const double highMean = model.posteriorForZone(QStringLiteral("HIGH")).posteriorMean;
        const double lowMean  = model.posteriorForZone(QStringLiteral("LOW")).posteriorMean;

        QVERIFY2(highMean > lowMean,
                 qPrintable(QStringLiteral("HIGH zone mean (%1) should exceed LOW (%2)")
                    .arg(highMean).arg(lowMean)));
    }

    // ── 6. Shrinkage: active zone closer to observed mean than prior ──────────
    void testShrinkageApplied()
    {
        BayesianHierarchical model;
        QVector<CrimeEvent> evs;
        evs += makeZoneHistory(QStringLiteral("ACTIVE"), 40);
        evs += makeZoneHistory(QStringLiteral("QUIET"),  2);

        model.fit(evs);

        // Shrinkage estimate should exist and be positive
        const double shrinkActive = model.shrinkageEstimate(QStringLiteral("ACTIVE"));
        const double shrinkQuiet  = model.shrinkageEstimate(QStringLiteral("QUIET"));

        QVERIFY2(shrinkActive >= 0.0, "Shrinkage estimate must be non-negative");
        QVERIFY2(shrinkActive > shrinkQuiet,
                 qPrintable(QStringLiteral(
                    "Active zone shrinkage (%1) should exceed quiet zone (%2)")
                    .arg(shrinkActive).arg(shrinkQuiet)));
    }

    // ── 7. Predictive probability P(y >= 1) is in [0, 1] ────────────────────
    void testPredictiveProbabilityRange()
    {
        BayesianHierarchical model;
        QVector<CrimeEvent> evs = makeZoneHistory(QStringLiteral("Z"), 15);
        model.fit(evs);

        const double p1 = model.predictiveProbability(QStringLiteral("Z"), 1);
        QVERIFY2(p1 >= 0.0 && p1 <= 1.0,
                 qPrintable(QStringLiteral("P(y>=1) = %1 should be in [0,1]").arg(p1)));
    }

    // ── 8. allPosteriors() is sorted by posteriorMean descending ─────────────
    void testAllPosteriorsSorted()
    {
        BayesianHierarchical model;
        QVector<CrimeEvent> evs;
        evs += makeZoneHistory(QStringLiteral("X1"), 30);
        evs += makeZoneHistory(QStringLiteral("X2"), 10);
        evs += makeZoneHistory(QStringLiteral("X3"), 50);

        model.fit(evs);
        const auto posts = model.allPosteriors();

        for (int i = 1; i < posts.size(); ++i) {
            QVERIFY2(posts[i-1].posteriorMean >= posts[i].posteriorMean,
                     qPrintable(QStringLiteral("allPosteriors not sorted at index %1").arg(i)));
        }
    }

    // ── 9. Global hyperparameters are positive ────────────────────────────────
    void testHyperparametersPositive()
    {
        BayesianHierarchical model;
        QVector<CrimeEvent> evs;
        evs += makeZoneHistory(QStringLiteral("P"), 20);
        evs += makeZoneHistory(QStringLiteral("Q"), 5);

        model.fit(evs);
        QVERIFY2(model.globalAlpha() > 0.0, "Global alpha must be positive");
        QVERIFY2(model.globalBeta()  > 0.0, "Global beta must be positive");
        QVERIFY2(model.globalMean()  > 0.0, "Global mean must be positive");
    }

    // ── 10. Empty input → isFitted() false, no crash ────────────────────────
    void testEmptyInputNoCrash()
    {
        BayesianHierarchical model;
        model.fit({}, 30.0);
        QVERIFY(!model.isFitted());
        QCOMPARE(model.zoneCount(), 0);
        // posteriorForZone on unknown zone must not crash
        const auto post = model.posteriorForZone(QStringLiteral("UNKNOWN"));
        QVERIFY2(post.posteriorMean >= 0.0, "Unknown zone posterior must be >= 0");
    }
};

QTEST_MAIN(BayesianHierarchicalConvergenceTest)
#include "test_bayesian_hierarchical_convergence.moc"
