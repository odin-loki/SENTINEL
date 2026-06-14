// test_poisson_baseline_deep10.cpp — Deep audit iteration 28: PoissonBaseline
// totalEvents, probAtLeastOne bounds, crime-type buckets, lambda non-negative.
#include <QTest>
#include <QDateTime>
#include <QTimeZone>
#include "models/PoissonBaseline.h"

class TestPoissonBaselineDeep10 : public QObject
{
    Q_OBJECT

    static QDateTime utcDt()
    {
        return QDateTime(QDate(2024, 7, 10), QTime(14, 0, 0), QTimeZone::utc());
    }

private slots:

    void testTotalEventsMatchesFitSize()
    {
        PoissonBaseline pb;
        QVector<PoissonBaseline::EventRecord> recs;
        const QDateTime base(QDate(2024, 1, 1), QTime(0, 0, 0), QTimeZone::utc());
        for (int i = 0; i < 18; ++i) {
            PoissonBaseline::EventRecord r;
            r.zoneId     = QStringLiteral("T10");
            r.crimeType  = QStringLiteral("theft");
            r.occurredAt = base.addDays(i);
            recs.append(r);
        }
        pb.fit(recs);
        QCOMPARE(pb.totalEvents(), 18);
    }

    void testProbAtLeastOneInUnitInterval()
    {
        PoissonBaseline pb;
        QVector<PoissonBaseline::EventRecord> recs;
        const QDateTime base(QDate(2024, 2, 1), QTime(0, 0, 0), QTimeZone::utc());
        for (int i = 0; i < 10; ++i) {
            PoissonBaseline::EventRecord r;
            r.zoneId     = QStringLiteral("P10");
            r.crimeType  = QStringLiteral("burglary");
            r.occurredAt = base.addDays(i);
            recs.append(r);
        }
        pb.fit(recs);
        const auto pred = pb.predict(QStringLiteral("P10"), utcDt(), QStringLiteral("burglary"));
        QVERIFY2(pred.probAtLeastOne >= 0.0 && pred.probAtLeastOne <= 1.0,
                 qPrintable(QStringLiteral("prob=%1").arg(pred.probAtLeastOne)));
    }

    void testLambdaNonNegative()
    {
        PoissonBaseline pb;
        QVector<PoissonBaseline::EventRecord> recs;
        const QDateTime base(QDate(2024, 3, 1), QTime(0, 0, 0), QTimeZone::utc());
        for (int i = 0; i < 6; ++i) {
            PoissonBaseline::EventRecord r;
            r.zoneId     = QStringLiteral("L10");
            r.crimeType  = QStringLiteral("robbery");
            r.occurredAt = base.addSecs(i * 4 * 3600);
            recs.append(r);
        }
        pb.fit(recs);
        const auto pred = pb.predict(QStringLiteral("L10"), utcDt(), QStringLiteral("robbery"));
        QVERIFY(pred.lambda >= 0.0);
    }

    void testDifferentCrimeTypeBuckets()
    {
        PoissonBaseline pb;
        QVector<PoissonBaseline::EventRecord> recs;
        const QDateTime base(QDate(2024, 4, 1), QTime(10, 0, 0), QTimeZone::utc());
        for (int i = 0; i < 8; ++i) {
            PoissonBaseline::EventRecord r;
            r.zoneId     = QStringLiteral("Mix");
            r.crimeType  = QStringLiteral("theft");
            r.occurredAt = base.addDays(i);
            recs.append(r);
        }
        for (int i = 0; i < 3; ++i) {
            PoissonBaseline::EventRecord r;
            r.zoneId     = QStringLiteral("Mix");
            r.crimeType  = QStringLiteral("burglary");
            r.occurredAt = base.addDays(i);
            recs.append(r);
        }
        pb.fit(recs);
        const auto theftPred = pb.predict(QStringLiteral("Mix"), utcDt(), QStringLiteral("theft"));
        const auto burgPred  = pb.predict(QStringLiteral("Mix"), utcDt(), QStringLiteral("burglary"));
        QVERIFY(theftPred.nObservations >= burgPred.nObservations);
    }

    void testUnfittedReturnsBoundedProb()
    {
        PoissonBaseline pb;
        const auto pred = pb.predict(QStringLiteral("X"), utcDt(), QStringLiteral("theft"));
        QVERIFY(pred.probAtLeastOne >= 0.0 && pred.probAtLeastOne <= 1.0);
    }
};

QTEST_GUILESS_MAIN(TestPoissonBaselineDeep10)
#include "test_poisson_baseline_deep10.moc"
