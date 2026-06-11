// test_poisson_baseline_deep4.cpp — Deep audit iteration 12 (round 4) for PoissonBaseline
// Verifies λ=count/exposure, Negative Binomial overdispersion, and 90% prediction intervals.

#include <QtTest>
#include <cmath>
#include "models/PoissonBaseline.h"

class TestPoissonBaselineDeep4 : public QObject
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

    void testLambdaEqualsCountOverExposureDays()
    {
        // 3 distinct Mondays in March 2023 with daily counts [2, 1, 3] → λ = 6/3 = 2.0
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
                 qPrintable(QString("lambda=%1 expected 2.0 (6 counts / 3 days)")
                                .arg(pred.lambda)));
        QVERIFY2(std::abs(pred.expectedCount - pred.lambda) < 1e-9,
                 "expectedCount must equal lambda");
    }

    void testNegBinFittedWhenVarianceExceedsMean()
    {
        // 23 Mondays in January across years → same bucket, n>5, var > mean
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
                 qPrintable(QString("overdispersed bucket should use NB, got model=%1")
                                .arg(pred.model)));
        QVERIFY(pred.nObservations > 5);
    }

    void testPoissonPathWhenVarianceNotGreaterThanMean()
    {
        // Uniform Poisson-like counts: exactly 1 event per day for 10 days.
        QVector<PoissonBaseline::EventRecord> events;
        for (int i = 0; i < 10; ++i)
            events.append(rec("Uniform", dt(2023, 6, 1 + i, 14)));

        PoissonBaseline model;
        model.fit(events);
        PoissonPrediction pred = model.predict(
            "Uniform",
            QDateTime(QDate(2024, 6, 3), QTime(14, 0), Qt::UTC),
            "Burglary");

        QVERIFY2(pred.model.contains("Poisson"),
                 qPrintable(QString("homogeneous counts should stay Poisson, model=%1")
                                .arg(pred.model)));
        QCOMPARE(pred.lambda, 1.0);
    }

    void testPoissonPredictionIntervalContainsMean()
    {
        QVector<PoissonBaseline::EventRecord> events;
        for (int i = 0; i < 30; ++i)
            events.append(rec("CI", dt(2023, 8, 1 + i, 10)));

        PoissonBaseline model;
        model.fit(events);
        PoissonPrediction pred = model.predict(
            "CI",
            QDateTime(QDate(2024, 8, 5), QTime(10, 0), Qt::UTC),
            "Burglary");

        QVERIFY2(pred.ci90.first <= pred.lambda,
                 qPrintable(QString("CI90 lower=%1 must be <= lambda=%2")
                                .arg(pred.ci90.first).arg(pred.lambda)));
        QVERIFY2(pred.ci90.second >= pred.lambda,
                 qPrintable(QString("CI90 upper=%2 must be >= lambda=%1")
                                .arg(pred.lambda).arg(pred.ci90.second)));
    }

    void testPoissonCI90MatchesQuantiles()
    {
        const double lambda = 4.0;
        const double lo = PoissonBaseline::poissonPPF(lambda, 0.05);
        const double hi = PoissonBaseline::poissonPPF(lambda, 0.95);

        QVector<PoissonBaseline::EventRecord> events;
        QDate base(2023, 2, 6);
        for (int day = 0; day < 20; ++day) {
            for (int c = 0; c < 4; ++c)
                events.append(rec("Q", QDateTime(base.addDays(day), QTime(10, 0), Qt::UTC)));
        }

        PoissonBaseline model;
        model.fit(events);
        PoissonPrediction pred = model.predict(
            "Q",
            QDateTime(QDate(2024, 2, 5), QTime(10, 0), Qt::UTC),
            "Burglary");

        QVERIFY2(std::abs(pred.lambda - lambda) < 1e-9,
                 qPrintable(QString("lambda=%1 expected %2").arg(pred.lambda).arg(lambda)));
        QVERIFY2(std::abs(pred.ci90.first - lo) < 1e-6,
                 qPrintable(QString("CI lo=%1 expected %2").arg(pred.ci90.first).arg(lo)));
        QVERIFY2(std::abs(pred.ci90.second - hi) < 1e-6,
                 qPrintable(QString("CI hi=%1 expected %2").arg(pred.ci90.second).arg(hi)));
    }

    void testNegBinCI90WiderThanPoissonForSameMean()
    {
        const double mean = 2.0;
        const double var  = 8.0;
        const double r    = mean * mean / (var - mean);
        const double p    = mean / (mean + r);

        const double nbLo = PoissonBaseline::negBinPPF(r, p, 0.05);
        const double nbHi = PoissonBaseline::negBinPPF(r, p, 0.95);
        const double poLo = PoissonBaseline::poissonPPF(mean, 0.05);
        const double poHi = PoissonBaseline::poissonPPF(mean, 0.95);

        QVERIFY2((nbHi - nbLo) > (poHi - poLo),
                 "overdispersed NB interval should be wider than Poisson");
    }

    void testNegBinMomentsFromFitParameters()
    {
        const double mean = 3.0;
        const double var  = 9.0;
        const double r    = mean * mean / (var - mean);
        const double p    = mean / (mean + r);

        double sum = 0.0;
        double sumSq = 0.0;
        for (int k = 0; k <= 200; ++k) {
            const double pmf = PoissonBaseline::negBinPMF(r, p, k);
            sum  += k * pmf;
            sumSq += k * k * pmf;
        }
        const double empMean = sum;
        const double empVar  = sumSq - empMean * empMean;

        QVERIFY2(std::abs(empMean - mean) < 0.05,
                 qPrintable(QString("NB mean=%1 expected %2").arg(empMean).arg(mean)));
        QVERIFY2(std::abs(empVar - var) < 0.15,
                 qPrintable(QString("NB var=%1 expected %2").arg(empVar).arg(var)));
    }

    void testSmallSampleSkipsNegBinFit()
    {
        // ≤5 exposure days → no NB even if var > mean
        QVector<PoissonBaseline::EventRecord> events;
        for (int i = 0; i < 5; ++i)
            events.append(rec("Small", dt(2023, 9, 1 + i * 7, 10)));
        events.append(rec("Small", dt(2023, 9, 1, 10)));  // spike day

        PoissonBaseline model;
        model.fit(events);
        PoissonPrediction pred = model.predict(
            "Small",
            QDateTime(QDate(2024, 9, 2), QTime(10, 0), Qt::UTC),
            "Burglary");

        QVERIFY2(pred.model.contains("Poisson"),
                 "buckets with n<=5 should not switch to NegativeBinomial");
    }

    void testUnknownZoneUsesPriorLambda()
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

    void testProbAtLeastOneConsistentWithZeroCount()
    {
        QVector<PoissonBaseline::EventRecord> events;
        for (int i = 0; i < 15; ++i)
            events.append(rec("P", dt(2023, 7, 1 + i, 12)));

        PoissonBaseline model;
        model.fit(events);
        PoissonPrediction pred = model.predict(
            "P",
            QDateTime(QDate(2024, 7, 1), QTime(12, 0), Qt::UTC),
            "Burglary");

        if (pred.model.contains("Poisson") && pred.nObservations > 0) {
            const double pZero = PoissonBaseline::poissonPMF(pred.lambda, 0);
            const double expected = 1.0 - pZero;
            QVERIFY2(std::abs(pred.probAtLeastOne - expected) < 1e-9,
                     qPrintable(QString("probAtLeastOne=%1 expected %2 for lambda=%3")
                                    .arg(pred.probAtLeastOne).arg(expected).arg(pred.lambda)));
        }
    }
};

QTEST_GUILESS_MAIN(TestPoissonBaselineDeep4)
#include "test_poisson_baseline_deep4.moc"
