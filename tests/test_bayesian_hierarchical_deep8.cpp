// test_bayesian_hierarchical_deep8.cpp — Deep audit iteration 30: BayesianHierarchical
// allPosteriors ordering, shrinkage, predictive probability, global hyperparams.
#include <QtTest/QtTest>
#include <QTimeZone>
#include <cmath>
#include "models/BayesianHierarchical.h"
#include "core/CrimeEvent.h"

class BayesianHierarchicalDeep8Test : public QObject
{
    Q_OBJECT

    static CrimeEvent ev(const QString& zone, int offset = 0)
    {
        CrimeEvent e;
        e.eventId    = zone + QString::number(offset);
        e.suburb     = zone;
        e.crimeType  = QStringLiteral("burglary");
        e.occurredAt = QDateTime(QDate(2024, 4, 1), QTime(8, 0), QTimeZone::utc()).addDays(-offset);
        e.timestamp  = e.occurredAt.value();
        return e;
    }

private slots:

    void testAllPosteriorsSortedByMean()
    {
        QVector<CrimeEvent> events;
        for (int i = 0; i < 8; ++i) events.append(ev(QStringLiteral("High"), i));
        for (int i = 0; i < 2; ++i) events.append(ev(QStringLiteral("Low"), i));

        BayesianHierarchical bh;
        bh.fit(events, 30.0);
        const auto posts = bh.allPosteriors();
        QVERIFY(posts.size() >= 2);
        QVERIFY(posts.first().posteriorMean >= posts.last().posteriorMean);
    }

    void testShrinkageBetweenGlobalAndZone()
    {
        QVector<CrimeEvent> events;
        for (int i = 0; i < 6; ++i) events.append(ev(QStringLiteral("Shr"), i));

        BayesianHierarchical bh;
        bh.fit(events, 30.0);
        const double shrink = bh.shrinkageEstimate(QStringLiteral("Shr"));
        const double global = bh.globalMean();
        QVERIFY(shrink > 0.0);
        QVERIFY(std::isfinite(shrink));
        QVERIFY(std::abs(shrink - global) < global * 2.0 + 1e-6);
    }

    void testPredictiveProbabilityBounded()
    {
        QVector<CrimeEvent> events;
        for (int i = 0; i < 5; ++i) events.append(ev(QStringLiteral("PP"), i));

        BayesianHierarchical bh;
        bh.fit(events, 30.0);
        const double prob = bh.predictiveProbability(QStringLiteral("PP"), 1);
        QVERIFY(prob >= 0.0 && prob <= 1.0);
    }

    void testGlobalHyperparametersPositiveAfterFit()
    {
        QVector<CrimeEvent> events;
        for (int i = 0; i < 4; ++i) events.append(ev(QStringLiteral("G"), i));

        BayesianHierarchical bh;
        bh.fit(events, 30.0);
        QVERIFY(bh.isFitted());
        QVERIFY(bh.globalAlpha() > 0.0);
        QVERIFY(bh.globalBeta() > 0.0);
        QVERIFY(bh.globalMean() > 0.0);
    }

    void testUnfittedZoneCountZero()
    {
        BayesianHierarchical bh;
        QCOMPARE(bh.zoneCount(), 0);
        QVERIFY(!bh.isFitted());
    }
};

QTEST_GUILESS_MAIN(BayesianHierarchicalDeep8Test)
#include "test_bayesian_hierarchical_deep8.moc"
