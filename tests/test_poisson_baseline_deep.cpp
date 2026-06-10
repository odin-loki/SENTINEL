// test_poisson_baseline_deep.cpp
// Deep tests for PoissonBaseline parameter estimation, PMF, PPF, and predictions.
#include <QTest>
#include "models/PoissonBaseline.h"
#include <cmath>

class PoissonBaselineDeepTest : public QObject
{
    Q_OBJECT

private:
    static PoissonBaseline::EventRecord rec(const QString& zone, const QDateTime& dt,
                                             const QString& type = QStringLiteral("burglary"))
    {
        PoissonBaseline::EventRecord r;
        r.zoneId     = zone;
        r.occurredAt = dt;
        r.crimeType  = type;
        return r;
    }

    static QDateTime monday9am(int weekOffset = 0)
    {
        // 2024-01-08 = Monday; 09:00 = hour 9
        return QDateTime(QDate(2024, 1, 8).addDays(weekOffset * 7),
                         QTime(9, 0, 0), QTimeZone::utc());
    }

private slots:

    // ── 1. isFitted() true after fit ────────────────────────────────────────
    void testIsFittedAfterFit()
    {
        PoissonBaseline pb;
        QVERIFY(!pb.isFitted());
        pb.fit({ rec(QStringLiteral("Z"), monday9am()) });
        QVERIFY(pb.isFitted());
    }

    // ── 2. totalEvents() matches input count ─────────────────────────────────
    void testTotalEventsCount()
    {
        PoissonBaseline pb;
        QVector<PoissonBaseline::EventRecord> evs;
        for (int i = 0; i < 30; ++i) evs.append(rec(QStringLiteral("A"), monday9am(i)));
        pb.fit(evs);
        QCOMPARE(pb.totalEvents(), 30);
    }

    // ── 3. Prediction for known zone returns valid lambda ─────────────────────
    void testPredictLambdaPositive()
    {
        PoissonBaseline pb;
        QVector<PoissonBaseline::EventRecord> evs;
        for (int i = 0; i < 20; ++i) evs.append(rec(QStringLiteral("HOT"), monday9am(i)));
        pb.fit(evs);

        const auto pred = pb.predict(QStringLiteral("HOT"), monday9am(100),
                                      QStringLiteral("burglary"));
        QVERIFY2(pred.lambda > 0.0,
                 qPrintable(QStringLiteral("Predicted lambda %1 must be positive").arg(pred.lambda)));
    }

    // ── 4. Prediction probability is in [0, 1] ────────────────────────────────
    void testPredictProbabilityRange()
    {
        PoissonBaseline pb;
        QVector<PoissonBaseline::EventRecord> evs;
        for (int i = 0; i < 15; ++i) evs.append(rec(QStringLiteral("B"), monday9am(i)));
        pb.fit(evs);

        const auto pred = pb.predict(QStringLiteral("B"), monday9am(), QStringLiteral("burglary"));
        QVERIFY2(pred.probAtLeastOne >= 0.0 && pred.probAtLeastOne <= 1.0,
                 qPrintable(QStringLiteral("Prediction prob %1 must be in [0,1]")
                    .arg(pred.probAtLeastOne)));
    }

    // ── 5. poissonPMF at mode is highest ─────────────────────────────────────
    void testPoissonPMFAtMode()
    {
        // For Poisson(lambda=3), mode=3 → P(3) > P(2) and P(3) >= P(4)
        const double p2 = PoissonBaseline::poissonPMF(3.0, 2);
        const double p3 = PoissonBaseline::poissonPMF(3.0, 3);
        const double p4 = PoissonBaseline::poissonPMF(3.0, 4);

        QVERIFY2(p3 >= p2 && p3 >= p4,
                 qPrintable(QStringLiteral("PMF at mode 3: p2=%1, p3=%2, p4=%3")
                    .arg(p2).arg(p3).arg(p4)));
    }

    // ── 6. poissonPMF sums to 1 ───────────────────────────────────────────────
    void testPoissonPMFSums()
    {
        double total = 0.0;
        for (int k = 0; k <= 50; ++k) total += PoissonBaseline::poissonPMF(5.0, k);
        QVERIFY2(std::abs(total - 1.0) < 0.001,
                 qPrintable(QStringLiteral("Poisson PMF sum %1 should be ~1").arg(total)));
    }

    // ── 7. poissonPPF: median of Poisson(5) is near 5 ────────────────────────
    void testPoissonPPFMedian()
    {
        const double median = PoissonBaseline::poissonPPF(5.0, 0.5);
        QVERIFY2(median >= 4.0 && median <= 6.0,
                 qPrintable(QStringLiteral("Poisson(5) median PPF %1 should be ~5").arg(median)));
    }

    // ── 8. Hot zone lambda is higher than near-zero fallback zone ────────────
    void testHotZoneHigherLambdaThanFallback()
    {
        PoissonBaseline pb;
        QVector<PoissonBaseline::EventRecord> evs;
        // Populate HOT zone in January, Monday, hour 9 with many events
        // Use same day (Jan 8) to accumulate counts in the same bucket
        for (int i = 0; i < 50; ++i) evs.append(rec(QStringLiteral("HOT"), monday9am(0)));
        pb.fit(evs);

        // HOT has many events in Jan-Mon-hr4 bucket
        const auto hotPred  = pb.predict(QStringLiteral("HOT"),  monday9am(), QStringLiteral("burglary"));
        // UNKNOWN zone has no history → falls back to near-zero prior (0.01)
        const auto unknownPred = pb.predict(QStringLiteral("UNKNOWN"), monday9am(), QStringLiteral("burglary"));
        QVERIFY2(hotPred.lambda > unknownPred.lambda,
                 qPrintable(QStringLiteral("HOT lambda (%1) should exceed UNKNOWN (%2)")
                    .arg(hotPred.lambda).arg(unknownPred.lambda)));
    }

    // ── 9. negBinPMF: mean is r*(1-p)/p ─────────────────────────────────────
    void testNegBinPMFMean()
    {
        // NB(r=5, p=0.5): E[X] = r*(1-p)/p = 5*0.5/0.5 = 5
        double total = 0.0;
        double mean  = 0.0;
        for (int k = 0; k <= 100; ++k) {
            const double prob = PoissonBaseline::negBinPMF(5.0, 0.5, k);
            total += prob;
            mean  += k * prob;
        }
        QVERIFY2(std::abs(total - 1.0) < 0.01,
                 qPrintable(QStringLiteral("NegBin PMF sum %1 should be ~1").arg(total)));
        QVERIFY2(std::abs(mean - 5.0) < 0.5,
                 qPrintable(QStringLiteral("NegBin mean %1 should be ~5").arg(mean)));
    }

    // ── 10. Empty fit → not fitted, predict returns sane defaults ─────────────
    void testEmptyFitNoCrash()
    {
        PoissonBaseline pb;
        pb.fit({});
        QVERIFY(!pb.isFitted());
        QCOMPARE(pb.totalEvents(), 0);
        // predict on unfitted model — should return something non-negative
        const auto pred = pb.predict(QStringLiteral("Z"), monday9am(), QStringLiteral("burglary"));
        QVERIFY2(pred.lambda >= 0.0, "Unfitted model must return non-negative lambda");
    }
};

QTEST_MAIN(PoissonBaselineDeepTest)
#include "test_poisson_baseline_deep.moc"
