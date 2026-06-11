// test_poisson_baseline_deep5.cpp — Deep audit iteration 13 for PoissonBaseline
// λ=count/exposure, Negative Binomial overdispersion, prediction intervals.

#include <QtTest>
#include <cmath>
#include "models/PoissonBaseline.h"

class TestPoissonBaselineDeep5 : public QObject
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

    static void appendRecs(QVector<PoissonBaseline::EventRecord>& out,
                           const QString& zone, int year, int month, int day,
                           int hour, int count,
                           const QString& type = QStringLiteral("Burglary"))
    {
        for (int i = 0; i < count; ++i)
            out.append(rec(zone, dt(year, month, day, hour), type));
    }

private slots:

    void testLambdaIsCountOverExposureDays()
    {
        QVector<PoissonBaseline::EventRecord> events;
        appendRecs(events, "Z1", 2023, 3, 6,  10, 2);
        appendRecs(events, "Z1", 2023, 3, 13, 10, 1);
        appendRecs(events, "Z1", 2023, 3, 20, 10, 3);

        PoissonBaseline model;
        model.fit(events);

        PoissonPrediction pred = model.predict(
            "Z1",
            QDateTime(QDate(2024, 3, 4), QTime(10, 0), Qt::UTC),
            "Burglary");

        QCOMPARE(pred.nObservations, 3);
        QVERIFY2(std::abs(pred.lambda - 2.0) < 1e-9,
                 qPrintable(QStringLiteral("λ=6/3=2.0, got %1").arg(pred.lambda)));
        QCOMPARE(pred.expectedCount, pred.lambda);
    }

    void testLambdaZeroEventsPerDayIncludedInExposure()
    {
        // 10 events on one day only → λ = 10/1 = 10 (exposure = distinct days with data).
        QVector<PoissonBaseline::EventRecord> events;
        appendRecs(events, "Spike", 2023, 5, 15, 10, 10);

        PoissonBaseline model;
        model.fit(events);
        PoissonPrediction pred = model.predict(
            "Spike",
            QDateTime(QDate(2024, 5, 13), QTime(10, 0), Qt::UTC),
            "Burglary");

        QCOMPARE(pred.nObservations, 1);
        QVERIFY2(std::abs(pred.lambda - 10.0) < 1e-9,
                 qPrintable(QStringLiteral("single-day spike λ=10, got %1").arg(pred.lambda)));
    }

    void testNegBinSelectedForOverdispersedBucket()
    {
        struct Slot { int year, day, count; };
        static const Slot kSlots[] = {
            {2020,  6, 10}, {2020, 13,  1}, {2020, 20, 10}, {2020, 27,  1},
            {2021,  4, 10}, {2021, 11,  1}, {2021, 18, 10}, {2021, 25,  1},
            {2022,  3, 10}, {2022, 10,  1}, {2022, 17, 10}, {2022, 24,  1}, {2022, 31, 10},
            {2023,  2,  1}, {2023,  9, 10}, {2023, 16,  1}, {2023, 23, 10}, {2023, 30,  1},
            {2024,  1, 10}, {2024,  8,  1}, {2024, 15, 10}, {2024, 22,  1}, {2024, 29, 10},
        };
        QVector<PoissonBaseline::EventRecord> events;
        for (const auto& s : kSlots)
            appendRecs(events, "OD", s.year, 1, s.day, 10, s.count);

        PoissonBaseline model;
        model.fit(events);
        PoissonPrediction pred = model.predict(
            "OD",
            QDateTime(QDate(2024, 1, 1), QTime(10, 0), Qt::UTC),
            "Burglary");

        QVERIFY2(pred.model.contains("NegativeBinomial"),
                 qPrintable(QStringLiteral("overdispersed bucket should use NB, model=%1")
                                .arg(pred.model)));
    }

    void testPoissonPathWhenVarianceEqualsMean()
    {
        QVector<PoissonBaseline::EventRecord> events;
        for (int i = 0; i < 12; ++i)
            events.append(rec("Homog", dt(2023, 6, 1 + i, 14)));

        PoissonBaseline model;
        model.fit(events);
        PoissonPrediction pred = model.predict(
            "Homog",
            QDateTime(QDate(2024, 6, 3), QTime(14, 0), Qt::UTC),
            "Burglary");

        QVERIFY2(pred.model.contains("Poisson"),
                 qPrintable(QStringLiteral("Poisson-like bucket should stay Poisson, model=%1")
                                .arg(pred.model)));
        QCOMPARE(pred.lambda, 1.0);
    }

    void testPoissonCI90ContainsLambda()
    {
        QVector<PoissonBaseline::EventRecord> events;
        for (int i = 0; i < 25; ++i)
            events.append(rec("CI", dt(2023, 8, 1 + i, 10)));

        PoissonBaseline model;
        model.fit(events);
        PoissonPrediction pred = model.predict(
            "CI",
            QDateTime(QDate(2024, 8, 5), QTime(10, 0), Qt::UTC),
            "Burglary");

        QVERIFY2(pred.ci90.first <= pred.lambda,
                 qPrintable(QStringLiteral("CI lower=%1 must be <= λ=%2")
                                .arg(pred.ci90.first).arg(pred.lambda)));
        QVERIFY2(pred.ci90.second >= pred.lambda,
                 qPrintable(QStringLiteral("CI upper=%1 must be >= λ=%2")
                                .arg(pred.ci90.second).arg(pred.lambda)));
    }

    void testPoissonCI90MatchesQuantileFunctions()
    {
        const double lambda = 5.0;
        const double lo = PoissonBaseline::poissonPPF(lambda, 0.05);
        const double hi = PoissonBaseline::poissonPPF(lambda, 0.95);

        QVector<PoissonBaseline::EventRecord> events;
        for (int day = 0; day < 15; ++day) {
            for (int c = 0; c < 5; ++c)
                events.append(rec("Q", dt(2023, 2, 6 + day, 10)));
        }

        PoissonBaseline model;
        model.fit(events);
        PoissonPrediction pred = model.predict(
            "Q",
            QDateTime(QDate(2024, 2, 5), QTime(10, 0), Qt::UTC),
            "Burglary");

        QVERIFY2(std::abs(pred.lambda - lambda) < 1e-9,
                 qPrintable(QStringLiteral("λ=%1 expected %2").arg(pred.lambda).arg(lambda)));
        QVERIFY2(std::abs(pred.ci90.first - lo) < 1e-6,
                 qPrintable(QStringLiteral("CI lo=%1 expected %2").arg(pred.ci90.first).arg(lo)));
        QVERIFY2(std::abs(pred.ci90.second - hi) < 1e-6,
                 qPrintable(QStringLiteral("CI hi=%1 expected %2").arg(pred.ci90.second).arg(hi)));
    }

    void testNegBinMomentsFromFit()
    {
        const double mean = 2.5;
        const double var  = 10.0;
        const double r    = mean * mean / (var - mean);
        const double p    = mean / (mean + r);

        double sum = 0.0, sumSq = 0.0;
        for (int k = 0; k <= 250; ++k) {
            const double pmf = PoissonBaseline::negBinPMF(r, p, k);
            sum   += k * pmf;
            sumSq += k * k * pmf;
        }
        const double empMean = sum;
        const double empVar  = sumSq - empMean * empMean;

        QVERIFY2(std::abs(empMean - mean) < 0.05,
                 qPrintable(QStringLiteral("NB mean=%1 expected %2").arg(empMean).arg(mean)));
        QVERIFY2(std::abs(empVar - var) < 0.2,
                 qPrintable(QStringLiteral("NB var=%1 expected %2").arg(empVar).arg(var)));
    }

    void testProbAtLeastOneMatchesZeroMass()
    {
        QVector<PoissonBaseline::EventRecord> events;
        for (int i = 0; i < 18; ++i)
            events.append(rec("P1", dt(2023, 7, 1 + i, 12)));

        PoissonBaseline model;
        model.fit(events);
        PoissonPrediction pred = model.predict(
            "P1",
            QDateTime(QDate(2024, 7, 1), QTime(12, 0), Qt::UTC),
            "Burglary");

        if (pred.model.contains("Poisson")) {
            const double expected = 1.0 - PoissonBaseline::poissonPMF(pred.lambda, 0);
            QVERIFY2(std::abs(pred.probAtLeastOne - expected) < 1e-9,
                     qPrintable(QStringLiteral("P(X>=1)=%1 expected %2")
                                    .arg(pred.probAtLeastOne).arg(expected)));
        }
    }

    void testUnknownZonePrior()
    {
        PoissonBaseline model;
        model.fit({ rec("Known", dt(2023, 5, 1)) });

        PoissonPrediction pred = model.predict(
            "Unknown",
            QDateTime(QDate(2024, 5, 1), QTime(10, 0), Qt::UTC),
            "Burglary");

        QCOMPARE(pred.nObservations, 0);
        QCOMPARE(pred.lambda, 0.01);
        QVERIFY2(pred.ci90.first <= pred.ci90.second, "prior CI must be ordered");
    }
};

QTEST_GUILESS_MAIN(TestPoissonBaselineDeep5)
#include "test_poisson_baseline_deep5.moc"
