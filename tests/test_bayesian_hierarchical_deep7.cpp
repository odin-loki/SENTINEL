// test_bayesian_hierarchical_deep7.cpp — Deep audit iteration 26: BayesianHierarchical
// posterior mean ordering, credible interval width, predictMean, zone filter.
#include <QtTest/QtTest>
#include "models/BayesianHierarchical.h"
#include "core/CrimeEvent.h"

class BayesianHierarchicalDeep7Test : public QObject
{
    Q_OBJECT

    static CrimeEvent ev(const QString& zone, int dayOffset = 0)
    {
        CrimeEvent e;
        e.eventId    = zone + QString::number(dayOffset);
        e.suburb     = zone;
        e.crimeType  = QStringLiteral("burglary");
        e.occurredAt = QDateTime(QDate(2024, 3, 1), QTime(12, 0), Qt::UTC).addDays(dayOffset);
        e.timestamp  = e.occurredAt.value();
        return e;
    }

private slots:

    void testHighCountZoneHigherPosteriorMean()
    {
        QVector<CrimeEvent> events;
        for (int i = 0; i < 10; ++i) events.append(ev(QStringLiteral("Hot"), -i));
        for (int i = 0; i < 2; ++i)  events.append(ev(QStringLiteral("Cold"), -i));

        BayesianHierarchical bh;
        bh.fit(events, 30.0);

        const auto hot  = bh.posteriorForZone(QStringLiteral("Hot"));
        const auto cold = bh.posteriorForZone(QStringLiteral("Cold"));
        QVERIFY(hot.posteriorMean > cold.posteriorMean);
    }

    void testCredibleIntervalOrdering()
    {
        QVector<CrimeEvent> events;
        for (int i = 0; i < 5; ++i) events.append(ev(QStringLiteral("CI"), -i));

        BayesianHierarchical bh;
        bh.fit(events, 30.0);
        const auto post = bh.posteriorForZone(QStringLiteral("CI"));

        QVERIFY(post.credibleLow <= post.posteriorMean + 1e-9);
        QVERIFY(post.posteriorMean <= post.credibleHigh + 1e-9);
    }

    void testPredictMeanPositiveForFittedZone()
    {
        QVector<CrimeEvent> events;
        for (int i = 0; i < 8; ++i) events.append(ev(QStringLiteral("Pred"), -i));

        BayesianHierarchical bh;
        bh.fit(events, 30.0);
        const double mean = bh.predictMean(QStringLiteral("Pred"), 7.0);
        QVERIFY(mean > 0.0);
    }

    void testCrimeTypeFilterReducesZones()
    {
        QVector<CrimeEvent> events;
        events.append(ev(QStringLiteral("Z"), 0));
        auto theft = ev(QStringLiteral("Z"), -1);
        theft.crimeType = QStringLiteral("theft");
        events.append(theft);

        BayesianHierarchical all;
        all.fit(events, 30.0);

        BayesianHierarchical filtered;
        filtered.fit(events, 30.0, QStringLiteral("burglary"));

        QVERIFY(filtered.allPosteriors().size() <= all.allPosteriors().size());
    }

    void testUnknownZonePredictMeanUsesGlobalPrior()
    {
        BayesianHierarchical bh;
        bh.fit({ ev(QStringLiteral("Only"), 0) }, 30.0);
        const double mean = bh.predictMean(QStringLiteral("UNKNOWN"), 7.0);
        QVERIFY(mean >= 0.0);
    }
};

QTEST_GUILESS_MAIN(BayesianHierarchicalDeep7Test)
#include "test_bayesian_hierarchical_deep7.moc"
