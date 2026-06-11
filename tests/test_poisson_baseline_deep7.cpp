// test_poisson_baseline_deep7.cpp — Deep audit iteration 18: hour-bin boundaries,
// NB parameter guards, PMF normalization, exposure accounting, quantile monotonicity.
#include <QtTest>
#include <cmath>
#include "models/PoissonBaseline.h"

class TestPoissonBaselineDeep7 : public QObject
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

    void testHourBinBoundarySeparatesAdjacentTwoHourSlots()
    {
        PoissonBaseline model;
        model.fit({
            rec(QStringLiteral("HB"), dt(2023, 11, 6, 10)),   // bin 5
            rec(QStringLiteral("HB"), dt(2023, 11, 13, 12)),  // bin 6
        });

        const PoissonPrediction at10 = model.predict(
            QStringLiteral("HB"), dt(2024, 11, 4, 10), QStringLiteral("Burglary"));
        const PoissonPrediction at12 = model.predict(
            QStringLiteral("HB"), dt(2024, 11, 4, 12), QStringLiteral("Burglary"));

        QCOMPARE(at10.nObservations, 1);
        QCOMPARE(at12.nObservations, 1);
        QVERIFY2(std::abs(at10.lambda - 1.0) < 1e-9,
                 qPrintable(QStringLiteral("10:00 λ=%1").arg(at10.lambda)));
        QVERIFY2(std::abs(at12.lambda - 1.0) < 1e-9,
                 qPrintable(QStringLiteral("12:00 λ=%1").arg(at12.lambda)));
    }

    void testNegBinPMFRejectsInvalidParameters()
    {
        QCOMPARE(PoissonBaseline::negBinPMF(0.0, 0.5, 1), 0.0);
        QCOMPARE(PoissonBaseline::negBinPMF(2.0, 0.0, 1), 0.0);
        QCOMPARE(PoissonBaseline::negBinPMF(2.0, 1.0, 1), 0.0);
        QCOMPARE(PoissonBaseline::negBinPPF(0.0, 0.5, 0.5), 0.0);
    }

    void testPoissonPMFApproximateNormalization()
    {
        const double lambda = 3.5;
        double sum = 0.0;
        for (int k = 0; k <= 30; ++k)
            sum += PoissonBaseline::poissonPMF(lambda, k);

        QVERIFY2(std::abs(sum - 1.0) < 1e-6,
                 qPrintable(QStringLiteral("Poisson PMF sum=%1 for λ=%2").arg(sum).arg(lambda)));
    }

    void testSameCalendarDayEventsShareOneExposureDay()
    {
        QVector<PoissonBaseline::EventRecord> events;
        for (int i = 0; i < 20; ++i)
            events.append(rec(QStringLiteral("Day"), dt(2023, 12, 5, 14)));

        PoissonBaseline model;
        model.fit(events);
        const PoissonPrediction pred = model.predict(
            QStringLiteral("Day"), dt(2024, 12, 3, 14), QStringLiteral("Burglary"));

        QCOMPARE(pred.nObservations, 1);
        QVERIFY2(std::abs(pred.lambda - 20.0) < 1e-9,
                 qPrintable(QStringLiteral("20 events one day → λ=20, got %1").arg(pred.lambda)));
    }

    void testOverdispersionSkippedWhenFiveOrFewerExposureDays()
    {
        struct DaySlot { int day; int count; };
        const DaySlot daySlots[] = {
            { 3, 10 }, { 10, 1 }, { 17, 10 }, { 24, 1 }, { 31, 10 },
        };
        QVector<PoissonBaseline::EventRecord> events;
        for (const auto& s : daySlots) {
            for (int i = 0; i < s.count; ++i)
                events.append(rec(QStringLiteral("OD"), dt(2023, 1, s.day, 10)));
        }

        PoissonBaseline model;
        model.fit(events);
        const PoissonPrediction pred = model.predict(
            QStringLiteral("OD"), dt(2024, 1, 2, 10), QStringLiteral("Burglary"));

        QCOMPARE(pred.nObservations, 5);
        QCOMPARE(pred.model, QStringLiteral("NonHomogeneousPoisson"));
    }

    void testPoissonPPFMonotoneInQuantile()
    {
        const double lambda = 4.0;
        double prev = -1.0;
        for (double q = 0.1; q <= 0.9; q += 0.1) {
            const double v = PoissonBaseline::poissonPPF(lambda, q);
            QVERIFY2(v >= prev,
                     qPrintable(QStringLiteral("PPF not monotone: q=%1 v=%2 prev=%3")
                                    .arg(q).arg(v).arg(prev)));
            prev = v;
        }
    }

    void testTotalEventsReflectsInputSizeNotBucketCount()
    {
        QVector<PoissonBaseline::EventRecord> events;
        for (int i = 0; i < 17; ++i)
            events.append(rec(QStringLiteral("T"), dt(2023, 2, 1 + i)));

        PoissonBaseline model;
        model.fit(events);
        QCOMPARE(model.totalEvents(), 17);
        QVERIFY(model.isFitted());
    }

    void testKnownBucketCi90OrderedAndBracketingLambda()
    {
        QVector<PoissonBaseline::EventRecord> events;
        for (int i = 0; i < 8; ++i)
            events.append(rec(QStringLiteral("CI"), dt(2023, 3, 6 + i * 7, 10)));

        PoissonBaseline model;
        model.fit(events);
        const PoissonPrediction pred = model.predict(
            QStringLiteral("CI"), dt(2024, 3, 4, 10), QStringLiteral("Burglary"));

        QVERIFY2(pred.ci90.first <= pred.ci90.second,
                 qPrintable(QStringLiteral("CI [%1,%2] inverted")
                                .arg(pred.ci90.first).arg(pred.ci90.second)));
        QVERIFY2(pred.ci90.first <= pred.lambda && pred.lambda <= pred.ci90.second + 1.0,
                 qPrintable(QStringLiteral("λ=%1 outside CI [%2,%3]")
                                .arg(pred.lambda).arg(pred.ci90.first).arg(pred.ci90.second)));
    }
};

QTEST_GUILESS_MAIN(TestPoissonBaselineDeep7)
#include "test_poisson_baseline_deep7.moc"
