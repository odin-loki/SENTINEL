// test_bayesian_posterior.cpp
// BayesianHierarchical posterior update, shrinkage, partial pooling,
// and credible interval tests.
#include <QTest>
#include "models/BayesianHierarchical.h"
#include "core/CrimeEvent.h"
#include <cmath>

class BayesianPosteriorTest : public QObject
{
    Q_OBJECT

private:
    static CrimeEvent makeEvent(const QString& zone, const QString& type = QStringLiteral("burglary"))
    {
        CrimeEvent ev;
        ev.id        = QUuid::createUuid().toString(QUuid::WithoutBraces);
        ev.suburb    = zone;
        ev.crimeType = type;
        ev.latitude  = 51.5;
        ev.longitude = -0.1;
        ev.timestamp = QDateTime(QDate::currentDate().addDays(-10), QTime(9, 0), QTimeZone::utc());
        return ev;
    }

    // 3 zones, 10 crimes in Z1, 5 in Z2, 1 in Z3
    static QVector<CrimeEvent> threeZones()
    {
        QVector<CrimeEvent> evs;
        for (int i = 0; i < 10; ++i) evs.append(makeEvent(QStringLiteral("Z1")));
        for (int i = 0; i < 5;  ++i) evs.append(makeEvent(QStringLiteral("Z2")));
        for (int i = 0; i < 1;  ++i) evs.append(makeEvent(QStringLiteral("Z3")));
        return evs;
    }

private slots:

    // 1. isFitted() false before fit
    void testNotFittedBeforeFit()
    {
        BayesianHierarchical bh;
        QVERIFY(!bh.isFitted());
    }

    // 2. isFitted() true after fit
    void testFittedAfterFit()
    {
        BayesianHierarchical bh;
        bh.fit(threeZones());
        QVERIFY(bh.isFitted());
    }

    // 3. zoneCount() matches distinct zones in data
    void testZoneCount()
    {
        BayesianHierarchical bh;
        bh.fit(threeZones());
        QCOMPARE(bh.zoneCount(), 3);
    }

    // 4. allPosteriors() sorted by posteriorMean descending
    void testAllPosteriorsSorted()
    {
        BayesianHierarchical bh;
        bh.fit(threeZones());
        const auto posts = bh.allPosteriors();
        QVERIFY2(posts.size() == 3, "Expected 3 posteriors");
        for (int i = 1; i < posts.size(); ++i)
            QVERIFY2(posts[i-1].posteriorMean >= posts[i].posteriorMean,
                     qPrintable(QStringLiteral("Posteriors should be sorted descending: %1 vs %2")
                        .arg(posts[i-1].posteriorMean).arg(posts[i].posteriorMean)));
    }

    // 5. Credible interval width is positive (credibleHigh > credibleLow)
    void testCredibleIntervalPositiveWidth()
    {
        BayesianHierarchical bh;
        bh.fit(threeZones());
        for (const auto& p : bh.allPosteriors()) {
            QVERIFY2(p.credibleHigh > p.credibleLow,
                     qPrintable(QStringLiteral("Zone %1: CI width must be positive [%2, %3]")
                        .arg(p.zoneId).arg(p.credibleLow).arg(p.credibleHigh)));
        }
    }

    // 6. Global mean is positive after fit
    void testGlobalMeanPositive()
    {
        BayesianHierarchical bh;
        bh.fit(threeZones());
        QVERIFY2(bh.globalMean() > 0.0,
                 qPrintable(QStringLiteral("globalMean %1 must be > 0").arg(bh.globalMean())));
    }

    // 7. High-crime zone has higher posteriorMean than low-crime zone
    void testHighCrimeZoneHigherMean()
    {
        BayesianHierarchical bh;
        bh.fit(threeZones());
        const auto z1 = bh.posteriorForZone(QStringLiteral("Z1"));
        const auto z3 = bh.posteriorForZone(QStringLiteral("Z3"));
        QVERIFY2(z1.posteriorMean >= z3.posteriorMean,
                 qPrintable(QStringLiteral("Z1 mean %1 should >= Z3 mean %2")
                    .arg(z1.posteriorMean).arg(z3.posteriorMean)));
    }

    // 8. predictiveProbability: P(y >= 0 | zone) == 1
    void testPredictiveProbabilityZero()
    {
        BayesianHierarchical bh;
        bh.fit(threeZones());
        const double p = bh.predictiveProbability(QStringLiteral("Z1"), 0);
        QVERIFY2(std::abs(p - 1.0) < 0.01,
                 qPrintable(QStringLiteral("P(y>=0|Z1) = %1, expected ~1.0").arg(p)));
    }

    // 9. shrinkageEstimate in (0, ∞)
    void testShrinkageEstimatePositive()
    {
        BayesianHierarchical bh;
        bh.fit(threeZones());
        const double s = bh.shrinkageEstimate(QStringLiteral("Z1"));
        QVERIFY2(s > 0.0,
                 qPrintable(QStringLiteral("shrinkageEstimate %1 must be > 0").arg(s)));
    }

    // 10. posteriorVar is positive for observed zones
    void testPosteriorVariancePositive()
    {
        BayesianHierarchical bh;
        bh.fit(threeZones());
        const auto p = bh.posteriorForZone(QStringLiteral("Z1"));
        QVERIFY2(p.posteriorVar > 0.0,
                 qPrintable(QStringLiteral("posteriorVar %1 must be > 0").arg(p.posteriorVar)));
    }
};

QTEST_MAIN(BayesianPosteriorTest)
#include "test_bayesian_posterior.moc"
