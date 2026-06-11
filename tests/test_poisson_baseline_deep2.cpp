#include <QtTest>
#include <cmath>
#include <numeric>
#include "models/PoissonBaseline.h"

class TestPoissonBaselineDeep2 : public QObject
{
    Q_OBJECT

private:
    static PoissonBaseline::EventRecord makeRecord(const QString& zone,
                                                    const QDateTime& dt,
                                                    const QString& type = "Burglary")
    {
        return PoissonBaseline::EventRecord{zone, dt, type};
    }

    static QDateTime dt(int year, int month, int day, int hour = 10)
    {
        return QDateTime(QDate(year, month, day), QTime(hour, 0), Qt::UTC);
    }

private slots:

    // ── poissonPMF ────────────────────────────────────────────────────────────

    void testPoissonPMFAtZero()
    {
        // P(X=0|λ=1) = e^-1 ≈ 0.3679
        double p = PoissonBaseline::poissonPMF(1.0, 0);
        QVERIFY2(std::abs(p - std::exp(-1.0)) < 1e-10,
                 qPrintable(QString("P(X=0|λ=1) expected %1, got %2")
                                .arg(std::exp(-1.0)).arg(p)));
    }

    void testPoissonPMFAtOne()
    {
        // P(X=1|λ=1) = 1*e^-1 ≈ 0.3679
        double p = PoissonBaseline::poissonPMF(1.0, 1);
        QVERIFY2(std::abs(p - std::exp(-1.0)) < 1e-10,
                 qPrintable(QString("P(X=1|λ=1) expected %1, got %2")
                                .arg(std::exp(-1.0)).arg(p)));
    }

    void testPoissonPMFAt2()
    {
        // P(X=2|λ=2) = e^-2 * 4/2 = 2*e^-2 ≈ 0.2707
        double p = PoissonBaseline::poissonPMF(2.0, 2);
        double expected = 2.0 * std::exp(-2.0);
        QVERIFY2(std::abs(p - expected) < 1e-10,
                 qPrintable(QString("P(X=2|λ=2) expected %1, got %2").arg(expected).arg(p)));
    }

    void testPoissonPMFSumsToOne()
    {
        // Sum over k=0..100 of PMF(λ=3, k) should be very close to 1
        double sum = 0.0;
        for (int k = 0; k <= 100; ++k) sum += PoissonBaseline::poissonPMF(3.0, k);
        QVERIFY2(std::abs(sum - 1.0) < 1e-6,
                 qPrintable(QString("Poisson PMF sum should be ~1, got %1").arg(sum)));
    }

    void testPoissonPMFZeroLambdaAtZero()
    {
        QCOMPARE(PoissonBaseline::poissonPMF(0.0, 0), 1.0);
        QCOMPARE(PoissonBaseline::poissonPMF(0.0, 1), 0.0);
    }

    void testPoissonPMFNegativeLambda()
    {
        // Negative lambda should treat as zero lambda
        QCOMPARE(PoissonBaseline::poissonPMF(-1.0, 0), 1.0);
        QCOMPARE(PoissonBaseline::poissonPMF(-1.0, 5), 0.0);
    }

    // ── poissonPPF ────────────────────────────────────────────────────────────

    void testPoissonPPFMedianNearMean()
    {
        // Median of Poisson(λ=5) is approximately 5
        double median = PoissonBaseline::poissonPPF(5.0, 0.5);
        QVERIFY2(median >= 4.0 && median <= 6.0,
                 qPrintable(QString("Poisson(5) median should be ~5, got %1").arg(median)));
    }

    void testPoissonPPFP95()
    {
        // 95th percentile of Poisson(1) should be around 4
        double q95 = PoissonBaseline::poissonPPF(1.0, 0.95);
        QVERIFY2(q95 >= 3.0 && q95 <= 5.0,
                 qPrintable(QString("Poisson(1) 95th pct should be ~3-4, got %1").arg(q95)));
    }

    void testPoissonPPFP05LessThanP95()
    {
        double lo = PoissonBaseline::poissonPPF(3.0, 0.05);
        double hi = PoissonBaseline::poissonPPF(3.0, 0.95);
        QVERIFY2(lo < hi,
                 qPrintable(QString("5th pct (%1) should be < 95th pct (%2)").arg(lo).arg(hi)));
    }

    // ── negBinPMF ─────────────────────────────────────────────────────────────

    void testNegBinPMFSumsToOne()
    {
        // NegBin(r=2, p=0.5) PMF must sum to 1
        double sum = 0.0;
        for (int k = 0; k <= 200; ++k)
            sum += PoissonBaseline::negBinPMF(2.0, 0.5, k);
        QVERIFY2(std::abs(sum - 1.0) < 1e-5,
                 qPrintable(QString("NegBin PMF sum should be ~1, got %1").arg(sum)));
    }

    void testNegBinPMFAtZero()
    {
        // P(X=0|r=1, p=0.5) = (1-p)^r = 0.5
        double p = PoissonBaseline::negBinPMF(1.0, 0.5, 0);
        QVERIFY2(std::abs(p - 0.5) < 1e-10,
                 qPrintable(QString("NegBin(1,0.5) P(0) should be 0.5, got %1").arg(p)));
    }

    void testNegBinPMFInvalidParams()
    {
        QCOMPARE(PoissonBaseline::negBinPMF(-1.0, 0.5, 0), 0.0);
        QCOMPARE(PoissonBaseline::negBinPMF(1.0, 0.0, 0), 0.0);
        QCOMPARE(PoissonBaseline::negBinPMF(1.0, 1.0, 0), 0.0);
    }

    // ── fit() and predict() ────────────────────────────────────────────────────

    void testFitEmptyReturnsFalse()
    {
        PoissonBaseline model;
        model.fit({});
        QVERIFY(!model.isFitted());
    }

    void testFitProducesPositiveProbability()
    {
        QVector<PoissonBaseline::EventRecord> events;
        // Create 30 burglaries in zone A, Mondays at 10:00 across different months
        for (int week = 0; week < 30; ++week) {
            events.append(makeRecord("ZoneA", dt(2023, 1 + (week % 12), 2 + (week % 28), 10)));
        }
        PoissonBaseline model;
        model.fit(events);
        QVERIFY(model.isFitted());

        PoissonPrediction pred = model.predict(
            "ZoneA",
            QDateTime(QDate(2024, 3, 4), QTime(10, 30), Qt::UTC),
            "Burglary");

        QVERIFY2(pred.probAtLeastOne > 0.0 && pred.probAtLeastOne <= 1.0,
                 qPrintable(QString("probAtLeastOne=%1").arg(pred.probAtLeastOne)));
        QVERIFY2(pred.expectedCount > 0.0,
                 qPrintable(QString("expectedCount=%1").arg(pred.expectedCount)));
    }

    void testPredictUnknownZoneReturnsPrior()
    {
        PoissonBaseline model;
        QVector<PoissonBaseline::EventRecord> events;
        events.append(makeRecord("ZoneA", dt(2023, 6, 1)));
        model.fit(events);

        PoissonPrediction pred = model.predict(
            "ZoneX_Unknown",
            QDateTime(QDate(2024, 6, 1), QTime(10, 0), Qt::UTC),
            "Burglary");

        // Should return near-zero prior, not crash
        QVERIFY2(pred.probAtLeastOne > 0.0 && pred.probAtLeastOne < 0.1,
                 qPrintable(QString("Unknown zone prior probAtLeastOne=%1").arg(pred.probAtLeastOne)));
        QCOMPARE(pred.nObservations, 0);
    }

    void testPredictCI90LowLessThanHigh()
    {
        QVector<PoissonBaseline::EventRecord> events;
        for (int i = 0; i < 50; ++i)
            events.append(makeRecord("ZoneA", dt(2023, 3, 1 + (i % 28), 10)));
        PoissonBaseline model;
        model.fit(events);

        PoissonPrediction pred = model.predict(
            "ZoneA",
            QDateTime(QDate(2024, 3, 4), QTime(10, 0), Qt::UTC),
            "Burglary");

        QVERIFY2(pred.ci90.first <= pred.ci90.second,
                 qPrintable(QString("CI90 lo=%1 > hi=%2").arg(pred.ci90.first).arg(pred.ci90.second)));
    }

    void testHighFrequencyZonePredictHighProb()
    {
        // If a zone has crime every single day in the same time slot, prob should be high
        QVector<PoissonBaseline::EventRecord> events;
        // 50 events all on Mondays in March at 10:00
        for (int i = 0; i < 50; ++i) {
            QDate d = QDate(2023, 3, 6).addDays(i * 7);  // every Monday
            events.append(makeRecord("HotZone", QDateTime(d, QTime(10, 30), Qt::UTC)));
        }
        PoissonBaseline model;
        model.fit(events);

        // Predict for the same pattern
        PoissonPrediction pred = model.predict(
            "HotZone",
            QDateTime(QDate(2024, 3, 4), QTime(10, 0), Qt::UTC),
            "Burglary");

        QVERIFY2(pred.probAtLeastOne > 0.5,
                 qPrintable(QString("High-frequency zone prob should be > 0.5, got %1")
                                .arg(pred.probAtLeastOne)));
    }

    void testProbAtLeastOneIsDerivedFromPMF()
    {
        // probAtLeastOne = 1 - P(X=0) = 1 - exp(-lambda)
        QVector<PoissonBaseline::EventRecord> events;
        for (int i = 0; i < 10; ++i)
            events.append(makeRecord("Z", dt(2023, 5, 1 + i, 10)));
        PoissonBaseline model;
        model.fit(events);

        PoissonPrediction pred = model.predict(
            "Z",
            QDateTime(QDate(2024, 5, 6), QTime(10, 0), Qt::UTC),
            "Burglary");

        if (pred.nObservations > 0) {
            // probAtLeastOne + P(X=0) should ≈ 1
            double pZero = PoissonBaseline::poissonPMF(pred.lambda, 0);
            QVERIFY2(std::abs(pred.probAtLeastOne + pZero - 1.0) < 0.01,
                     qPrintable(QString("probAtLeastOne + P(X=0) = %1 + %2 != 1.0")
                                    .arg(pred.probAtLeastOne).arg(pZero)));
        }
    }

    void testTotalEventsTracked()
    {
        QVector<PoissonBaseline::EventRecord> events;
        for (int i = 0; i < 42; ++i)
            events.append(makeRecord("Z", dt(2023, 1 + (i % 12), 1 + (i % 28))));
        PoissonBaseline model;
        model.fit(events);
        QCOMPARE(model.totalEvents(), 42);
    }
};

QTEST_GUILESS_MAIN(TestPoissonBaselineDeep2)
#include "test_poisson_baseline_deep2.moc"
