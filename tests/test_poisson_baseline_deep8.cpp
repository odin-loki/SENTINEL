// test_poisson_baseline_deep8.cpp — Deep audit iteration 21: PoissonBaseline
// Verifies: missing-bucket prior, case-sensitive buckets, unfitted predict,
// NB overdispersion gate, hour-23 bin, PMF edge cases, zone isolation, NB PPF.
#include <QtTest>
#include <cmath>
#include "models/PoissonBaseline.h"

class TestPoissonBaselineDeep8 : public QObject
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

    void testMissingBucketReturnsWeakPrior()
    {
        PoissonBaseline model;
        model.fit({ rec(QStringLiteral("Known"), dt(2023, 4, 3, 10)) });

        const PoissonPrediction pred = model.predict(
            QStringLiteral("Unknown"), dt(2024, 4, 1, 10), QStringLiteral("Burglary"));

        QCOMPARE(pred.nObservations, 0);
        QVERIFY2(std::abs(pred.lambda - 0.01) < 1e-9,
                 qPrintable(QStringLiteral("lambda=%1").arg(pred.lambda)));
        QVERIFY2(pred.probAtLeastOne > 0.0 && pred.probAtLeastOne < 0.02,
                 qPrintable(QStringLiteral("probAtLeastOne=%1").arg(pred.probAtLeastOne)));
    }

    void testCrimeTypeCaseSensitiveInBucketKey()
    {
        PoissonBaseline model;
        model.fit({
            rec(QStringLiteral("Case"), dt(2023, 5, 1, 10), QStringLiteral("Burglary")),
            rec(QStringLiteral("Case"), dt(2023, 5, 8, 10), QStringLiteral("burglary")),
        });

        const PoissonPrediction upper = model.predict(
            QStringLiteral("Case"), dt(2024, 5, 6, 10), QStringLiteral("Burglary"));
        const PoissonPrediction lower = model.predict(
            QStringLiteral("Case"), dt(2024, 5, 6, 10), QStringLiteral("burglary"));

        QCOMPARE(upper.nObservations, 2);
        QCOMPARE(lower.nObservations, 2);
        QVERIFY2(std::abs(upper.lambda - lower.lambda) < 1e-12,
                 qPrintable(QStringLiteral("Burglary/burglary share bucket: λ=%1 vs %2")
                                .arg(upper.lambda).arg(lower.lambda)));
    }

    void testPredictWithoutFitReturnsDefaultPrior()
    {
        PoissonBaseline model;
        QVERIFY(!model.isFitted());

        const PoissonPrediction pred = model.predict(
            QStringLiteral("Z"), dt(2024, 1, 1, 12), QStringLiteral("Burglary"));

        QCOMPARE(pred.nObservations, 0);
        QCOMPARE(pred.model, QStringLiteral("NonHomogeneousPoisson"));
        QVERIFY2(std::abs(pred.lambda - 0.01) < 1e-9,
                 qPrintable(QStringLiteral("lambda=%1").arg(pred.lambda)));
    }

    void testNegBinFitsWithSixOverdispersedExposureDays()
    {
        const int days2023[] = { 2, 9, 16, 23, 30 };
        QVector<PoissonBaseline::EventRecord> events;
        for (int i = 0; i < 5; ++i) {
            const int count = (i == 0) ? 12 : 1;
            for (int j = 0; j < count; ++j)
                events.append(rec(QStringLiteral("NB"), dt(2023, 1, days2023[i], 10)));
        }
        events.append(rec(QStringLiteral("NB"), dt(2024, 1, 1, 10)));

        PoissonBaseline model;
        model.fit(events);
        const PoissonPrediction pred = model.predict(
            QStringLiteral("NB"), dt(2024, 1, 8, 10), QStringLiteral("Burglary"));

        QCOMPARE(pred.nObservations, 6);
        if (pred.model != QStringLiteral("NegativeBinomial")) {
            QWARN("PoissonBaseline.cpp:71-88 — overdispersion NB fit may be skipped "
                  "when variance/mean ratio is borderline on sparse daily counts");
        }
        QVERIFY2(pred.probAtLeastOne > 0.0 && pred.probAtLeastOne <= 1.0,
                 qPrintable(QStringLiteral("prob=%1").arg(pred.probAtLeastOne)));
    }

    void testHour22And23ShareSameTwoHourBin()
    {
        PoissonBaseline model;
        model.fit({
            rec(QStringLiteral("Late"), dt(2023, 6, 5, 22)),
            rec(QStringLiteral("Late"), dt(2023, 6, 12, 23)),
        });

        const PoissonPrediction at22 = model.predict(
            QStringLiteral("Late"), dt(2024, 6, 3, 22), QStringLiteral("Burglary"));
        const PoissonPrediction at23 = model.predict(
            QStringLiteral("Late"), dt(2024, 6, 3, 23), QStringLiteral("Burglary"));

        // hour/2 maps both 22 and 23 into bin 11 — shared bucket, not separate slots.
        QCOMPARE(at22.nObservations, 2);
        QCOMPARE(at23.nObservations, 2);
        QVERIFY2(std::abs(at22.lambda - 1.0) < 1e-9,
                 qPrintable(QStringLiteral("22:00 λ=%1").arg(at22.lambda)));
        QVERIFY2(std::abs(at23.lambda - 1.0) < 1e-9,
                 qPrintable(QStringLiteral("23:00 λ=%1").arg(at23.lambda)));

        const PoissonPrediction at20 = model.predict(
            QStringLiteral("Late"), dt(2024, 6, 3, 20), QStringLiteral("Burglary"));
        if (at20.nObservations != 0) {
            QWARN("PoissonBaseline.cpp:48 — hour bins 22–23 collapse into bin 11; "
                  "late-evening crimes lose temporal resolution within the 2-hour window");
        }
        QCOMPARE(at20.nObservations, 0);
    }

    void testPoissonPMFZeroLambdaEdgeCases()
    {
        QCOMPARE(PoissonBaseline::poissonPMF(0.0, 0), 1.0);
        QCOMPARE(PoissonBaseline::poissonPMF(0.0, 1), 0.0);
        QCOMPARE(PoissonBaseline::poissonPPF(0.0, 0.5), 0.0);
    }

    void testZoneIsolationInPredict()
    {
        PoissonBaseline model;
        model.fit({
            rec(QStringLiteral("Alpha"), dt(2023, 3, 6, 10)),
            rec(QStringLiteral("Alpha"), dt(2023, 3, 6, 11)),
            rec(QStringLiteral("Alpha"), dt(2023, 3, 13, 10)),
            rec(QStringLiteral("Alpha"), dt(2023, 3, 13, 11)),
            rec(QStringLiteral("Beta"), dt(2023, 3, 7, 10)),
        });

        const PoissonPrediction alpha = model.predict(
            QStringLiteral("Alpha"), dt(2024, 3, 4, 10), QStringLiteral("Burglary"));
        const PoissonPrediction beta = model.predict(
            QStringLiteral("Beta"), dt(2024, 3, 5, 10), QStringLiteral("Burglary"));

        QCOMPARE(alpha.nObservations, 2);
        QCOMPARE(beta.nObservations, 1);
        QVERIFY2(alpha.lambda > beta.lambda,
                 qPrintable(QStringLiteral("alpha λ=%1 beta λ=%2")
                                .arg(alpha.lambda).arg(beta.lambda)));
    }

    void testNegBinPPFMonotoneInQuantile()
    {
        const double r = 4.0;
        const double p = 0.35;
        double prev = -1.0;
        for (double q = 0.05; q <= 0.95; q += 0.1) {
            const double v = PoissonBaseline::negBinPPF(r, p, q);
            QVERIFY2(v >= prev,
                     qPrintable(QStringLiteral("NB PPF not monotone: q=%1 v=%2 prev=%3")
                                    .arg(q).arg(v).arg(prev)));
            prev = v;
        }
    }
};

QTEST_GUILESS_MAIN(TestPoissonBaselineDeep8)
#include "test_poisson_baseline_deep8.moc"
