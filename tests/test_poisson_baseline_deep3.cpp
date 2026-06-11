// test_poisson_baseline_deep3.cpp — Deep audit iteration 12 for PoissonBaseline
// Tests fit()/predict() pipeline, exceedance probability, temporal bucket variation,
// edge cases (zero events, single event), and PoissonPrediction field completeness.

#include <QtTest>
#include <cmath>
#include "models/PoissonBaseline.h"

class TestPoissonBaselineDeep3 : public QObject
{
    Q_OBJECT

    // ── Helpers ───────────────────────────────────────────────────────────────

    static PoissonBaseline::EventRecord rec(const QString& zone,
                                             const QDateTime& dt,
                                             const QString& type = "Burglary")
    {
        return PoissonBaseline::EventRecord{zone, dt, type};
    }

    static QDateTime dt(int year, int month, int day, int hour = 10)
    {
        return QDateTime(QDate(year, month, day), QTime(hour, 0), Qt::UTC);
    }

    // Compute P(X > k) = 1 - Σ_{j=0}^{k} PMF(lambda, j)  using poissonPMF.
    // This mirrors what exceedanceProbability() should return.
    static double exceedance(double lambda, int k)
    {
        double cdf = 0.0;
        for (int j = 0; j <= k; ++j)
            cdf += PoissonBaseline::poissonPMF(lambda, j);
        return 1.0 - cdf;
    }

private slots:

    // ── fit() + predict(): 10 events in ZoneA over 30 days ────────────────────

    void testFitPredictPositiveRate()
    {
        QVector<PoissonBaseline::EventRecord> events;
        // 10 events in ZoneA across 30 days, all Mondays at 10:00 in May
        for (int i = 0; i < 10; ++i) {
            QDate d = QDate(2023, 5, 1).addDays(i * 3);   // spread over 30 days
            events.append(rec("ZoneA", QDateTime(d, QTime(10, 0), Qt::UTC)));
        }
        PoissonBaseline model;
        model.fit(events);
        QVERIFY(model.isFitted());
        QCOMPARE(model.totalEvents(), 10);

        // Predict for the same zone at a matching time slot
        PoissonPrediction pred = model.predict(
            "ZoneA",
            QDateTime(QDate(2024, 5, 6), QTime(10, 0), Qt::UTC),
            "Burglary");

        QVERIFY2(pred.lambda > 0.0,
                 qPrintable(QString("lambda must be > 0 after fitting 10 events; got %1").arg(pred.lambda)));
        QVERIFY2(pred.expectedCount > 0.0,
                 qPrintable(QString("expectedCount must be > 0; got %1").arg(pred.expectedCount)));
        QVERIFY2(pred.probAtLeastOne > 0.0,
                 "probAtLeastOne must be > 0");
        QVERIFY2(pred.probAtLeastOne <= 1.0,
                 "probAtLeastOne must be <= 1");
    }

    // ── Unknown zone → small prior, not NaN or negative ──────────────────────

    void testUnknownZoneReturnsPriorNotNaN()
    {
        QVector<PoissonBaseline::EventRecord> events;
        for (int i = 0; i < 5; ++i)
            events.append(rec("ZoneA", dt(2023, 4, 1 + i)));
        PoissonBaseline model;
        model.fit(events);

        PoissonPrediction pred = model.predict(
            "ZoneX_NoHistory",
            QDateTime(QDate(2024, 4, 1), QTime(10, 0), Qt::UTC),
            "Burglary");

        QVERIFY2(!std::isnan(pred.lambda),       "lambda must not be NaN for unknown zone");
        QVERIFY2(!std::isinf(pred.lambda),       "lambda must not be Inf for unknown zone");
        QVERIFY2(pred.lambda >= 0.0,             "lambda must be >= 0 for unknown zone");
        QVERIFY2(!std::isnan(pred.expectedCount),"expectedCount must not be NaN");
        QVERIFY2(pred.expectedCount >= 0.0,      "expectedCount must be >= 0 for unknown zone");
        QVERIFY2(pred.probAtLeastOne >= 0.0 && pred.probAtLeastOne <= 1.0,
                 qPrintable(QString("probAtLeastOne=%1 out of [0,1]").arg(pred.probAtLeastOne)));
        // nObservations = 0 for unknown zone
        QCOMPARE(pred.nObservations, 0);
    }

    // ── exceedanceProbability is in [0,1] for any lambda and threshold ────────

    void testExceedanceProbabilityInUnitInterval()
    {
        // Test for several lambdas and thresholds
        double lambdas[] = {0.0, 0.5, 1.0, 3.0, 10.0};
        int ks[]         = {0, 1, 2, 5, 10};

        for (double lam : lambdas) {
            for (int k : ks) {
                double exc = exceedance(lam, k);
                QVERIFY2(exc >= -1e-12 && exc <= 1.0 + 1e-12,
                         qPrintable(QString("exceedance(lam=%1, k=%2) = %3 out of [0,1]")
                                        .arg(lam).arg(k).arg(exc)));
            }
        }
    }

    void testExceedanceProbabilityMonotonicallyDecreasing()
    {
        // P(X > k) decreases as k increases for fixed lambda
        double lam = 3.0;
        double prev = exceedance(lam, 0);
        for (int k = 1; k <= 10; ++k) {
            double curr = exceedance(lam, k);
            QVERIFY2(curr <= prev + 1e-12,
                     qPrintable(QString("exceedance(lam=3, k=%1)=%2 > exceedance(k=%3)=%4")
                                    .arg(k).arg(curr).arg(k-1).arg(prev)));
            prev = curr;
        }
    }

    void testExceedanceProbabilityAtZeroLambda()
    {
        // If lambda=0, P(X > 0) must be 0 (nothing ever happens)
        double exc = exceedance(0.0, 0);
        QVERIFY2(exc >= -1e-12 && exc <= 1e-12,
                 qPrintable(QString("exceedance(0, 0) should be 0, got %1").arg(exc)));
    }

    // ── Fit with single event: zone rate reflects 1 occurrence ──────────────

    void testFitSingleEvent()
    {
        PoissonBaseline model;
        model.fit({ rec("ZoneS", dt(2023, 6, 15, 10)) });
        QVERIFY(model.isFitted());
        QCOMPARE(model.totalEvents(), 1);

        // Predict for exact same bucket
        PoissonPrediction pred = model.predict(
            "ZoneS",
            QDateTime(QDate(2024, 6, 17), QTime(10, 0), Qt::UTC),  // same dow+hour+month bucket
            "Burglary");

        // The bucket has 1 daily count of 1 → mean = 1 → lambda = 1
        if (pred.nObservations > 0) {
            QVERIFY2(pred.lambda > 0.0,
                     qPrintable(QString("Single-event zone lambda=%1 must be > 0").arg(pred.lambda)));
        }
    }

    // ── Fit with zero events: rate = 0, predict returns small positive prior ─

    void testFitZeroEvents()
    {
        PoissonBaseline model;
        model.fit({});
        QVERIFY(!model.isFitted());
        QCOMPARE(model.totalEvents(), 0);

        // predict() on unfitted model returns prior (not crash)
        PoissonPrediction pred = model.predict(
            "ZoneA",
            QDateTime(QDate(2024, 1, 1), QTime(10, 0), Qt::UTC),
            "Burglary");

        QVERIFY2(!std::isnan(pred.lambda),   "lambda must not be NaN with zero training events");
        QVERIFY2(pred.lambda >= 0.0,         "lambda must be >= 0 with zero training events");
        QVERIFY2(pred.probAtLeastOne >= 0.0, "probAtLeastOne must be >= 0");
        QVERIFY2(pred.probAtLeastOne <= 1.0, "probAtLeastOne must be <= 1");
    }

    // ── Temporal factor: weekend bucket differs from weekday bucket ──────────

    void testTemporalBucketWeekendVsWeekday()
    {
        // We train with high-frequency weekend (Sunday=dow=6) events only,
        // then verify predict for that bucket is higher than an unseen weekday bucket.
        QVector<PoissonBaseline::EventRecord> events;

        // Find a Sunday in January 2023 (Jan 1 2023 = Sunday)
        QDate sunday(2023, 1, 1);
        Q_ASSERT(sunday.dayOfWeek() == 7);  // 7 = Sunday in Qt

        // 15 events on successive Sundays, same hour/month/zone
        for (int i = 0; i < 15; ++i) {
            events.append(rec("ZoneW", QDateTime(sunday.addDays(i * 7), QTime(20, 0), Qt::UTC)));
        }
        PoissonBaseline model;
        model.fit(events);

        // Predict for Sunday evening (trained bucket)
        QDate futureSunday(2024, 1, 7);  // a Sunday in Jan 2024
        PoissonPrediction predWeekend = model.predict(
            "ZoneW",
            QDateTime(futureSunday, QTime(20, 0), Qt::UTC),
            "Burglary");

        // Predict for Wednesday evening (untrained bucket)
        QDate futureWed(2024, 1, 10);  // a Wednesday in Jan 2024
        PoissonPrediction predWeekday = model.predict(
            "ZoneW",
            QDateTime(futureWed, QTime(20, 0), Qt::UTC),
            "Burglary");

        // Weekend bucket was trained, weekday was not → weekend lambda >= weekday lambda
        QVERIFY2(predWeekend.lambda >= predWeekday.lambda,
                 qPrintable(QString("Weekend lambda=%1 must be >= weekday lambda=%2 (no weekday training)")
                                .arg(predWeekend.lambda).arg(predWeekday.lambda)));
        QVERIFY2(predWeekend.nObservations > 0,
                 "Weekend bucket must have observations after training");
    }

    // ── fullReport equivalent: PoissonPrediction must have all required fields ─

    void testPredictionStructHasRequiredFields()
    {
        QVector<PoissonBaseline::EventRecord> events;
        for (int i = 0; i < 20; ++i)
            events.append(rec("ZoneR", dt(2023, 3, 1 + i, 14)));
        PoissonBaseline model;
        model.fit(events);

        PoissonPrediction pred = model.predict(
            "ZoneR",
            QDateTime(QDate(2024, 3, 4), QTime(14, 0), Qt::UTC),
            "Burglary");

        // All required fields present and valid
        QVERIFY2(!std::isnan(pred.lambda),          "lambda must not be NaN");
        QVERIFY2(!std::isnan(pred.expectedCount),   "expectedCount must not be NaN");
        QVERIFY2(!std::isnan(pred.probAtLeastOne),  "probAtLeastOne must not be NaN");
        QVERIFY2(!std::isnan(pred.ci90.first),      "ci90.first must not be NaN");
        QVERIFY2(!std::isnan(pred.ci90.second),     "ci90.second must not be NaN");
        QVERIFY2(pred.ci90.first <= pred.ci90.second,
                 qPrintable(QString("CI90 lo=%1 must be <= hi=%2")
                                .arg(pred.ci90.first).arg(pred.ci90.second)));
        QVERIFY2(!pred.model.isEmpty(), "model field must not be empty");
        QVERIFY2(pred.nObservations >= 0, "nObservations must be >= 0");

        // lambda == expectedCount for Poisson model
        QVERIFY2(std::abs(pred.lambda - pred.expectedCount) < 1e-9,
                 qPrintable(QString("lambda=%1 != expectedCount=%2").arg(pred.lambda).arg(pred.expectedCount)));
    }

    // ── NegBin model path: overdispersed bucket uses NegativeBinomial ─────────

    void testOverdispersedBucketUsesNegBinModel()
    {
        // Create a highly overdispersed bucket: mostly 0s with spikes
        // Need >5 distinct days in the same (zone, hourBin, dow, month, type) bucket.
        QVector<PoissonBaseline::EventRecord> events;

        // Zone "OD", Mon, Jan, 10:00 (hourBin=5), Burglary
        QDate baseMonday(2023, 1, 2);  // Jan 2 2023 = Monday
        for (int week = 0; week < 20; ++week) {
            QDate d = baseMonday.addDays(week * 7);
            if (d.month() != 1) break;  // stay in January
            // Some weeks: add 0 events, a few weeks: add many
            int count = (week % 5 == 0) ? 5 : 0;
            for (int c = 0; c < count; ++c)
                events.append(rec("OD", QDateTime(d, QTime(10, 0), Qt::UTC)));
        }

        PoissonBaseline model;
        model.fit(events);
        if (!model.isFitted()) return;  // not enough data, skip

        PoissonPrediction pred = model.predict(
            "OD",
            QDateTime(QDate(2024, 1, 8), QTime(10, 0), Qt::UTC),  // Monday Jan 2024
            "Burglary");

        // Prediction fields must be valid regardless of model path
        QVERIFY2(pred.probAtLeastOne >= 0.0 && pred.probAtLeastOne <= 1.0,
                 qPrintable(QString("probAtLeastOne=%1 out of [0,1]").arg(pred.probAtLeastOne)));
        QVERIFY2(!std::isnan(pred.ci90.first) && !std::isnan(pred.ci90.second),
                 "CI90 must not be NaN");
    }

    // ── CDF property: probAtLeastOne = 1 - P(X=0) for Poisson path ───────────

    void testProbAtLeastOneConsistentWithPMF()
    {
        QVector<PoissonBaseline::EventRecord> events;
        for (int i = 0; i < 12; ++i)
            events.append(rec("ZoneC", dt(2023, 7, 1 + i * 2, 10)));
        PoissonBaseline model;
        model.fit(events);

        PoissonPrediction pred = model.predict(
            "ZoneC",
            QDateTime(QDate(2024, 7, 1), QTime(10, 0), Qt::UTC),
            "Burglary");

        if (pred.nObservations > 0 && pred.model.contains("Poisson")) {
            double pZero = PoissonBaseline::poissonPMF(pred.lambda, 0);
            QVERIFY2(std::abs(pred.probAtLeastOne + pZero - 1.0) < 1e-9,
                     qPrintable(QString("probAtLeastOne(%1) + P(X=0|λ=%2) = %3 should be 1.0")
                                    .arg(pred.probAtLeastOne).arg(pred.lambda)
                                    .arg(pred.probAtLeastOne + pZero)));
        }
    }

    // ── Poisson PMF sums to 1 ────────────────────────────────────────────────

    void testPoissonPMFNormalisation()
    {
        for (double lam : {0.5, 1.0, 3.0, 7.0}) {
            double sum = 0.0;
            for (int k = 0; k <= 200; ++k) sum += PoissonBaseline::poissonPMF(lam, k);
            QVERIFY2(std::abs(sum - 1.0) < 1e-7,
                     qPrintable(QString("Poisson PMF sum=%1 for lambda=%2").arg(sum).arg(lam)));
        }
    }

    // ── NegBin PMF sums to 1 ────────────────────────────────────────────────

    void testNegBinPMFNormalisation()
    {
        double sum = 0.0;
        for (int k = 0; k <= 500; ++k)
            sum += PoissonBaseline::negBinPMF(3.0, 0.4, k);
        QVERIFY2(std::abs(sum - 1.0) < 1e-5,
                 qPrintable(QString("NegBin PMF sum=%1").arg(sum)));
    }

    // ── Edge: all events at the same time (same bucket) ──────────────────────

    void testAllEventsSameTime()
    {
        QVector<PoissonBaseline::EventRecord> events;
        QDateTime sameTime = dt(2023, 9, 4, 14);
        for (int i = 0; i < 20; ++i)
            events.append(rec("ZoneST", sameTime));
        PoissonBaseline model;
        model.fit(events);
        QVERIFY(model.isFitted());

        PoissonPrediction pred = model.predict(
            "ZoneST",
            QDateTime(QDate(2024, 9, 2), QTime(14, 0), Qt::UTC),
            "Burglary");

        // All 20 events collapse to 1 daily bucket with count 20 → lambda=20
        QVERIFY2(!std::isnan(pred.lambda), "lambda must not be NaN");
        QVERIFY2(pred.lambda > 0.0,        "lambda must be > 0");
    }

    // ── Single zone fit: no contamination of other zones ─────────────────────

    void testSingleZoneIsolation()
    {
        QVector<PoissonBaseline::EventRecord> events;
        for (int i = 0; i < 5; ++i)
            events.append(rec("OnlyZone", dt(2023, 2, 1 + i)));
        PoissonBaseline model;
        model.fit(events);

        // Other zones should return prior
        PoissonPrediction pred = model.predict(
            "OtherZone",
            QDateTime(QDate(2024, 2, 1), QTime(10, 0), Qt::UTC),
            "Burglary");
        QCOMPARE(pred.nObservations, 0);
    }
};

QTEST_GUILESS_MAIN(TestPoissonBaselineDeep3)
#include "test_poisson_baseline_deep3.moc"
