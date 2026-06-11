// test_poisson_baseline_deep6.cpp — Deep audit iteration 15: lambda bounds,
// unknown-zone prior, zero-event edge cases.
#include <QtTest>
#include <cmath>
#include "models/PoissonBaseline.h"

class TestPoissonBaselineDeep6 : public QObject
{
    Q_OBJECT

    static PoissonBaseline::EventRecord rec(const QString& zone,
                                             const QDateTime& dt,
                                             const QString& type = QStringLiteral("Burglary"))
    {
        return PoissonBaseline::EventRecord{zone, dt, type};
    }

    static QDateTime dt(int year, int month, int day, int hour = 10)
    {
        return QDateTime(QDate(year, month, day), QTime(hour, 0), Qt::UTC);
    }

private slots:

    void testEmptyFitPredictUsesPriorLambda()
    {
        PoissonBaseline model;
        model.fit({});
        QVERIFY(!model.isFitted());
        QCOMPARE(model.totalEvents(), 0);

        const PoissonPrediction pred = model.predict(
            QStringLiteral("AnyZone"),
            dt(2024, 5, 1),
            QStringLiteral("Burglary"));

        QCOMPARE(pred.nObservations, 0);
        QCOMPARE(pred.lambda, 0.01);
        QCOMPARE(pred.expectedCount, 0.01);
        QVERIFY(pred.probAtLeastOne >= 0.0 && pred.probAtLeastOne <= 1.0);
    }

    void testUnknownZoneLambdaFloorIs001()
    {
        PoissonBaseline model;
        model.fit({ rec(QStringLiteral("Known"), dt(2023, 5, 1)) });

        const PoissonPrediction pred = model.predict(
            QStringLiteral("Unknown"),
            dt(2024, 5, 1),
            QStringLiteral("Burglary"));

        QCOMPARE(pred.nObservations, 0);
        QCOMPARE(pred.lambda, 0.01);
        QVERIFY2(pred.lambda >= 0.01,
                 qPrintable(QStringLiteral("lambda=%1 below prior floor 0.01").arg(pred.lambda)));
    }

    void testUnknownZoneProbMatchesPoissonMass()
    {
        PoissonBaseline model;
        model.fit({ rec(QStringLiteral("Known"), dt(2023, 6, 1)) });

        const PoissonPrediction pred = model.predict(
            QStringLiteral("NoHistory"),
            dt(2024, 6, 1),
            QStringLiteral("Burglary"));

        const double expected = 1.0 - PoissonBaseline::poissonPMF(0.01, 0);
        QVERIFY2(std::abs(pred.probAtLeastOne - expected) < 1e-9,
                 qPrintable(QStringLiteral("P(X>=1)=%1 expected %2")
                                .arg(pred.probAtLeastOne).arg(expected)));
    }

    void testUnknownZoneCi90OrderedAndNonNegative()
    {
        PoissonBaseline model;
        model.fit({ rec(QStringLiteral("Known"), dt(2023, 7, 1)) });

        const PoissonPrediction pred = model.predict(
            QStringLiteral("Unknown"),
            dt(2024, 7, 1),
            QStringLiteral("Burglary"));

        QVERIFY2(pred.ci90.first <= pred.ci90.second,
                 qPrintable(QStringLiteral("CI [%1,%2] not ordered")
                                .arg(pred.ci90.first).arg(pred.ci90.second)));
        QVERIFY(pred.ci90.first >= 0.0);
    }

    void testHighLambdaFromSingleDaySpike()
    {
        // Aug 15 2023 and Aug 13 2024 are both Tuesdays (same hour/dow/month bucket).
        QVector<PoissonBaseline::EventRecord> events;
        for (int i = 0; i < 50; ++i)
            events.append(rec(QStringLiteral("Spike"), dt(2023, 8, 15)));

        PoissonBaseline model;
        model.fit(events);
        const PoissonPrediction pred = model.predict(
            QStringLiteral("Spike"),
            dt(2024, 8, 13),
            QStringLiteral("Burglary"));

        QCOMPARE(pred.nObservations, 1);
        QVERIFY2(std::abs(pred.lambda - 50.0) < 1e-9,
                 qPrintable(QStringLiteral("spike λ=50, got %1").arg(pred.lambda)));
        QVERIFY(pred.probAtLeastOne > 0.99);
    }

    void testZeroEventsInMatchingBucketUsesPrior()
    {
        // Known zone, but crime type bucket mismatch → same as unknown bucket.
        PoissonBaseline model;
        model.fit({ rec(QStringLiteral("Z"), dt(2023, 9, 4), QStringLiteral("Burglary")) });

        const PoissonPrediction pred = model.predict(
            QStringLiteral("Z"),
            dt(2024, 9, 2),
            QStringLiteral("Robbery"));

        QCOMPARE(pred.nObservations, 0);
        QCOMPARE(pred.lambda, 0.01);
    }

    void testLambdaNeverNegativeAcrossBuckets()
    {
        QVector<PoissonBaseline::EventRecord> events;
        for (int i = 0; i < 12; ++i)
            events.append(rec(QStringLiteral("Stable"), dt(2023, 10, 1 + i)));

        PoissonBaseline model;
        model.fit(events);

        for (int hour = 0; hour < 24; hour += 2) {
            const PoissonPrediction pred = model.predict(
                QStringLiteral("Stable"),
                dt(2024, 10, 7, hour),
                QStringLiteral("Burglary"));
            QVERIFY2(pred.lambda >= 0.0,
                     qPrintable(QStringLiteral("negative λ=%1 at hour %2")
                                    .arg(pred.lambda).arg(hour)));
        }
    }

    void testPoissonPMFZeroLambdaIsDegenerateAtZero()
    {
        QCOMPARE(PoissonBaseline::poissonPMF(0.0, 0), 1.0);
        QCOMPARE(PoissonBaseline::poissonPMF(0.0, 1), 0.0);
        QCOMPARE(PoissonBaseline::poissonPPF(0.0, 0.5), 0.0);
    }
};

QTEST_GUILESS_MAIN(TestPoissonBaselineDeep6)
#include "test_poisson_baseline_deep6.moc"
