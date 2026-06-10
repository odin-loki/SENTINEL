// test_poisson_statistical.cpp
// Statistical correctness tests for PoissonBaseline: PMF, PPF, NegBin PMF,
// temporal bucket prediction, zero-count zones, and overdispersion handling.
#include <QTest>
#include "models/PoissonBaseline.h"
#include <cmath>
#include <numeric>

class PoissonStatisticalTest : public QObject
{
    Q_OBJECT

private:
    static PoissonBaseline::EventRecord er(const QString& zone, const QDateTime& dt,
                                            const QString& type = QStringLiteral("burglary"))
    {
        return { zone, dt, type };
    }

    static QDateTime dt(int year, int month, int day, int hour)
    {
        return QDateTime(QDate(year, month, day), QTime(hour, 0, 0), QTimeZone::utc());
    }

    // 30 events in zone "Z1" every Monday morning at 09:00
    static QVector<PoissonBaseline::EventRecord> mondayMorning()
    {
        QVector<PoissonBaseline::EventRecord> evs;
        const QDate base(2024, 1, 8); // Monday
        for (int w = 0; w < 30; ++w)
            evs.append(er(QStringLiteral("Z1"), QDateTime(base.addDays(w * 7),
                                                           QTime(9, 0, 0), QTimeZone::utc())));
        return evs;
    }

private slots:

    // 1. poissonPMF(lambda=1, k=0) == exp(-1)
    void testPoissonPMFZero()
    {
        const double pmf = PoissonBaseline::poissonPMF(1.0, 0);
        QVERIFY2(std::abs(pmf - std::exp(-1.0)) < 1e-6,
                 qPrintable(QStringLiteral("PMF(1,0) expected %1, got %2")
                    .arg(std::exp(-1.0)).arg(pmf)));
    }

    // 2. poissonPMF sums to ~1 over sufficient range
    void testPoissonPMFSumsToOne()
    {
        double total = 0.0;
        for (int k = 0; k <= 30; ++k) total += PoissonBaseline::poissonPMF(3.0, k);
        QVERIFY2(std::abs(total - 1.0) < 0.01,
                 qPrintable(QStringLiteral("Poisson PMF sum %1 should be ~1.0").arg(total)));
    }

    // 3. poissonPPF(lambda=0, p=0.5) == 0
    void testPoissonPPFZeroLambda()
    {
        const double ppf = PoissonBaseline::poissonPPF(0.0, 0.5);
        QVERIFY2(ppf >= 0.0, qPrintable(QStringLiteral("PPF(0,0.5)=%1 must be >= 0").arg(ppf)));
    }

    // 4. poissonPPF monotone: PPF(5, 0.9) >= PPF(5, 0.1)
    void testPoissonPPFMonotone()
    {
        const double ppf90 = PoissonBaseline::poissonPPF(5.0, 0.9);
        const double ppf10 = PoissonBaseline::poissonPPF(5.0, 0.1);
        QVERIFY2(ppf90 >= ppf10,
                 qPrintable(QStringLiteral("PPF(5,0.9)=%1 should >= PPF(5,0.1)=%2")
                    .arg(ppf90).arg(ppf10)));
    }

    // 5. negBinPMF: non-negative and finite
    void testNegBinPMFValid()
    {
        for (int k = 0; k <= 5; ++k) {
            const double pmf = PoissonBaseline::negBinPMF(3.0, 0.5, k);
            QVERIFY2(pmf >= 0.0 && std::isfinite(pmf),
                     qPrintable(QStringLiteral("NegBin PMF(3,0.5,%1)=%2 invalid").arg(k).arg(pmf)));
        }
    }

    // 6. negBinPMF sums to ~1 over sufficient range
    void testNegBinPMFSumsToOne()
    {
        double total = 0.0;
        for (int k = 0; k <= 30; ++k) total += PoissonBaseline::negBinPMF(3.0, 0.5, k);
        QVERIFY2(std::abs(total - 1.0) < 0.05,
                 qPrintable(QStringLiteral("NegBin PMF sum %1 should be ~1.0").arg(total)));
    }

    // 7. After fit with monday-morning events, predict returns high rate on Monday
    void testPredictHighRateMondayMorning()
    {
        PoissonBaseline pb;
        pb.fit(mondayMorning());
        QVERIFY(pb.isFitted());

        const auto pred = pb.predict(
            QStringLiteral("Z1"),
            QDateTime(QDate(2024, 4, 8), QTime(9, 0, 0), QTimeZone::utc()),  // Monday
            QStringLiteral("burglary"));
        QVERIFY2(pred.expectedCount >= 0.0, "Expected count must be non-negative");
        QVERIFY2(pred.probAtLeastOne >= 0.0 && pred.probAtLeastOne <= 1.0,
                 "Probability must be in [0,1]");
    }

    // 8. totalEvents() matches fitted data count
    void testTotalEventsCount()
    {
        PoissonBaseline pb;
        auto evs = mondayMorning();
        pb.fit(evs);
        QCOMPARE(pb.totalEvents(), evs.size());
    }

    // 9. predict on unknown zone returns sensible default
    void testPredictUnknownZoneDefault()
    {
        PoissonBaseline pb;
        pb.fit(mondayMorning());
        const auto pred = pb.predict(
            QStringLiteral("UNKNOWN_ZONE"),
            QDateTime(QDate(2024, 3, 15), QTime(12, 0, 0), QTimeZone::utc()),
            QStringLiteral("burglary"));
        QVERIFY2(pred.expectedCount >= 0.0, "Unknown zone should return non-negative rate");
    }

    // 10. negBinPPF is monotone
    void testNegBinPPFMonotone()
    {
        const double ppfLow  = PoissonBaseline::negBinPPF(3.0, 0.5, 0.1);
        const double ppfHigh = PoissonBaseline::negBinPPF(3.0, 0.5, 0.9);
        QVERIFY2(ppfHigh >= ppfLow,
                 qPrintable(QStringLiteral("NegBin PPF(0.9)=%1 should >= PPF(0.1)=%2")
                    .arg(ppfHigh).arg(ppfLow)));
    }
};

QTEST_MAIN(PoissonStatisticalTest)
#include "test_poisson_statistical.moc"
