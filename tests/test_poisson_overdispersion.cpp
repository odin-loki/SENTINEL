// tests/test_poisson_overdispersion.cpp
// Overdispersion / model-selection tests for PoissonBaseline.
//
// Key API facts:
//   - PoissonBaseline has NO dispersionRatio(), modelName(), zoneCount(),
//     dailyTrend() methods.  We use the closest available API:
//       * predict().model        → "NonHomogeneousPoisson" | "NegativeBinomial"
//       * predict().expectedCount → empirical mean for the bucket
//       * predict().ci90         → 90 % CI pair<double,double>
//       * totalEvents()          → count of records passed to fit()
//       * isFitted()             → !m_rates.isEmpty()
//       * static negBinPMF / negBinPPF for parameter validation
//
// Bucket key = zone|hourBin|dow|month|crimeType   (year NOT included).
// Negative-binomial fitting requires n > 5 distinct days per bucket.
// We achieve this by using the same day-of-week (Monday) in January
// across multiple years — all map to the SAME bucket because year is absent.
//
// Confirmed Monday dates in January:
//   2020: 6,13,20,27   (4)
//   2021: 4,11,18,25   (4)
//   2022: 3,10,17,24,31 (5)
//   2023: 2,9,16,23,30  (5)
//   2024: 1,8,15,22,29  (5)
// Total = 23 data points, well above the n>5 threshold.

#include <QTest>
#include <QCoreApplication>
#include <QVector>
#include <QDate>
#include <QDateTime>
#include <cmath>

#include "models/PoissonBaseline.h"
#include "core/CrimeEvent.h"

// ─────────────────────────────────────────────────────────────────────────────
// Low-level record builder
// ─────────────────────────────────────────────────────────────────────────────

static PoissonBaseline::EventRecord makeRec(const QString& zone,
                                             int year, int month, int day,
                                             int hour,
                                             const QString& crimeType)
{
    PoissonBaseline::EventRecord r;
    r.zoneId     = zone;
    r.occurredAt = QDateTime(QDate(year, month, day), QTime(hour, 0, 0), Qt::UTC);
    r.crimeType  = crimeType;
    return r;
}

// Appends `count` identical records for one date to `out`.
static void appendRecs(QVector<PoissonBaseline::EventRecord>& out,
                        const QString& zone, int year, int month, int day,
                        int hour, const QString& crimeType, int count)
{
    for (int i = 0; i < count; ++i)
        out.append(makeRec(zone, year, month, day, hour, crimeType));
}

// ─────────────────────────────────────────────────────────────────────────────
// Composite data-set builders
// ─────────────────────────────────────────────────────────────────────────────

// makeOverdispersed: 23 Mondays in January across 2020-2024, alternating
//   counts 10 / 1.  Produces mean ≈ 5.7 and variance ≈ 20.2 (var > mean)
//   so the NB path is always taken.
static QVector<PoissonBaseline::EventRecord>
makeOverdispersed(const QString& zone, const QString& crimeType, int hour = 10)
{
    struct Slot { int year, day, count; };
    static const Slot kSlots[] = {
        {2020,  6, 10}, {2020, 13,  1}, {2020, 20, 10}, {2020, 27,  1},
        {2021,  4, 10}, {2021, 11,  1}, {2021, 18, 10}, {2021, 25,  1},
        {2022,  3, 10}, {2022, 10,  1}, {2022, 17, 10}, {2022, 24,  1}, {2022, 31, 10},
        {2023,  2,  1}, {2023,  9, 10}, {2023, 16,  1}, {2023, 23, 10}, {2023, 30,  1},
        {2024,  1, 10}, {2024,  8,  1}, {2024, 15, 10}, {2024, 22,  1}, {2024, 29, 10},
    };
    QVector<PoissonBaseline::EventRecord> recs;
    for (const auto& s : kSlots)
        appendRecs(recs, zone, s.year, 1, s.day, hour, crimeType, s.count);
    return recs;
}

// makeUniform: 6 Mondays in January (2020 + 2021 head), each with `countPerDay`
//   events.  This gives n=6 > 5 points all with the same count →
//   variance = 0 < mean → Poisson branch (not NB).
static QVector<PoissonBaseline::EventRecord>
makeUniform(const QString& zone, const QString& crimeType,
            int countPerDay = 1, int hour = 10)
{
    static const int kYears[] = {2020, 2020, 2020, 2020, 2021, 2021};
    static const int kDays[]  = {   6,   13,   20,   27,    4,   11};
    QVector<PoissonBaseline::EventRecord> recs;
    for (int i = 0; i < 6; ++i)
        appendRecs(recs, zone, kYears[i], 1, kDays[i], hour, crimeType, countPerDay);
    return recs;
}

// Reference predict() datetime: Monday 1 Jan 2024, hour 10
//   → hourBin=5, dow=0 (Mon), month=1  ← matches all builder functions above.
static QDateTime mondayJan()
{
    return QDateTime(QDate(2024, 1, 1), QTime(10, 0, 0), Qt::UTC);
}

// ─────────────────────────────────────────────────────────────────────────────
// Test class
// ─────────────────────────────────────────────────────────────────────────────

class TestPoissonOverdispersion : public QObject
{
    Q_OBJECT

private slots:

    // ── 1. After fitting, predict().lambda is finite and ≥ 0 ─────────────────

    void testFitReturnsValidDispersion()
    {
        PoissonBaseline pb;
        auto recs = makeOverdispersed("Z1", "burglary");
        QVERIFY(recs.size() > 50);
        pb.fit(recs);
        QVERIFY(pb.isFitted());

        auto pred = pb.predict("Z1", mondayJan(), "burglary");
        QVERIFY(std::isfinite(pred.lambda));
        QVERIFY(pred.lambda >= 0.0);
    }

    // ── 2. Uniform data (variance=0) → Poisson model selected ────────────────

    void testPoissonModeWhenVarianceEqualsLambda()
    {
        // All 6 days have count=2 → var=0 ≤ mean=2 → no NB fitting
        PoissonBaseline pb;
        pb.fit(makeUniform("Z2", "burglary", 2));

        auto pred = pb.predict("Z2", mondayJan(), "burglary");
        QVERIFY(pred.model.contains(QStringLiteral("Poisson")));
    }

    // ── 3. Overdispersed data → NegativeBinomial model ───────────────────────

    void testNegBinomModeWhenOverdispersed()
    {
        PoissonBaseline pb;
        pb.fit(makeOverdispersed("Z3", "burglary"));

        auto pred = pb.predict("Z3", mondayJan(), "burglary");
        QCOMPARE(pred.model, QStringLiteral("NegativeBinomial"));
    }

    // ── 4. Overdispersed data → model name contains "Binomial" ───────────────

    void testNegativeBinomialModelSelected()
    {
        PoissonBaseline pb;
        pb.fit(makeOverdispersed("Z4", "robbery"));

        auto pred = pb.predict("Z4", mondayJan(), "robbery");
        QVERIFY(pred.model.contains(QStringLiteral("Binomial"))
             || pred.model.contains(QStringLiteral("NegBin")));
    }

    // ── 5. Uniform data → NOT NegativeBinomial ───────────────────────────────

    void testPoissonModelSelectedForUnderdispersed()
    {
        PoissonBaseline pb;
        pb.fit(makeUniform("Z5", "assault", 3));

        auto pred = pb.predict("Z5", mondayJan(), "assault");
        QVERIFY(!pred.model.contains(QStringLiteral("Negative")));
        QVERIFY(pred.model.contains(QStringLiteral("Poisson")));
    }

    // ── 6. NB r parameter > 0: validated via static negBinPMF boundary tests ─

    void testNegBinomParameterR()
    {
        // Valid r > 0 → PMF in (0, 1]
        double pmfValid = PoissonBaseline::negBinPMF(2.0, 0.3, 0);
        QVERIFY(std::isfinite(pmfValid));
        QVERIFY(pmfValid > 0.0 && pmfValid <= 1.0);

        // r = 0 → PMF = 0 (guard in implementation)
        QCOMPARE(PoissonBaseline::negBinPMF(0.0, 0.3, 0), 0.0);

        // Overdispersed fit picks NB (which requires r > 0 internally)
        PoissonBaseline pb;
        pb.fit(makeOverdispersed("Z6", "burglary"));
        auto pred = pb.predict("Z6", mondayJan(), "burglary");
        QCOMPARE(pred.model, QStringLiteral("NegativeBinomial"));
    }

    // ── 7. NB p parameter ∈ (0, 1): boundary checks via static negBinPMF ─────

    void testNegBinomParameterP()
    {
        // Valid p ∈ (0,1) → PMF > 0
        double pmfValid = PoissonBaseline::negBinPMF(2.0, 0.5, 1);
        QVERIFY(pmfValid > 0.0 && pmfValid <= 1.0);

        // p = 0 → PMF = 0
        QCOMPARE(PoissonBaseline::negBinPMF(2.0, 0.0, 1), 0.0);

        // p = 1 → PMF = 0
        QCOMPARE(PoissonBaseline::negBinPMF(2.0, 1.0, 1), 0.0);
    }

    // ── 8. predict().expectedCount ≈ historical mean ──────────────────────────

    void testPredictMeanConsistent()
    {
        // 6 Mondays each with exactly 3 events → historical mean = 3.0
        PoissonBaseline pb;
        pb.fit(makeUniform("Z8", "vehicle_crime", 3));

        auto pred = pb.predict("Z8", mondayJan(), "vehicle_crime");
        QVERIFY(std::abs(pred.expectedCount - 3.0) < 1e-9);
    }

    // ── 9. CI is ordered and mean lies within [low90, high90] ─────────────────

    void testConfidenceInterval90Coverage()
    {
        // Poisson data (mean=3) gives well-calibrated CI from poissonPPF
        PoissonBaseline pb;
        pb.fit(makeUniform("Z9", "burglary", 3));

        auto pred = pb.predict("Z9", mondayJan(), "burglary");
        QVERIFY(pred.ci90.first <= pred.ci90.second);
        // Mean should be within the 90 % credible interval
        QVERIFY(pred.expectedCount >= pred.ci90.first);
        QVERIFY(pred.expectedCount <= pred.ci90.second);
    }

    // ── 10. Lower CI bound is always ≥ 0 ─────────────────────────────────────

    void testConfidenceIntervalNeverNegative()
    {
        PoissonBaseline pb;
        pb.fit(makeOverdispersed("Z10", "burglary"));

        auto pred = pb.predict("Z10", mondayJan(), "burglary");
        QVERIFY(pred.ci90.first  >= 0.0);
        QVERIFY(pred.ci90.second >= 0.0);
    }

    // ── 11. Fit with 3 zones → predict works for all three ───────────────────

    void testFitMultipleZones()
    {
        PoissonBaseline pb;
        QVector<PoissonBaseline::EventRecord> recs;
        for (const QString& z : {QStringLiteral("ZA"), QStringLiteral("ZB"), QStringLiteral("ZC")})
            recs += makeUniform(z, "burglary", 2);
        pb.fit(recs);
        QVERIFY(pb.isFitted());

        QDateTime dt = mondayJan();
        for (const QString& z : {QStringLiteral("ZA"), QStringLiteral("ZB"), QStringLiteral("ZC")}) {
            auto pred = pb.predict(z, dt, "burglary");
            QVERIFY(std::isfinite(pred.expectedCount));
            QVERIFY(pred.expectedCount >= 0.0);
        }
    }

    // ── 12. totalEvents() == number of records passed to fit() ───────────────

    void testZoneCountAfterFit()
    {
        QVector<PoissonBaseline::EventRecord> recs;
        recs += makeUniform("ZX1", "burglary", 2);   // 6×2 = 12
        recs += makeUniform("ZX2", "robbery",  3);   // 6×3 = 18
        recs += makeUniform("ZX3", "assault",  1);   // 6×1 =  6
        int total = recs.size();                       // 36

        PoissonBaseline pb;
        pb.fit(recs);
        QCOMPARE(pb.totalEvents(), total);
    }

    // ── 13. Unknown zone → near-zero default prediction ──────────────────────

    void testPredictUnfittedZoneReturnsDefault()
    {
        PoissonBaseline pb;
        pb.fit(makeUniform("Known", "burglary", 2));

        auto pred = pb.predict("Unknown", mondayJan(), "burglary");
        // Implementation returns lambda=0.01 for missing buckets
        QVERIFY(pred.lambda < 0.1);
        QCOMPARE(pred.nObservations, 0);
    }

    // ── 14. High count variance >> mean → NegativeBinomial selected ──────────

    void testHighCountDataOverdispersion()
    {
        // Same mathematical structure as test 3 but with a different zone / type
        PoissonBaseline pb;
        pb.fit(makeOverdispersed("Z14", "drug_offence"));

        auto pred = pb.predict("Z14", mondayJan(), "drug_offence");
        QCOMPARE(pred.model, QStringLiteral("NegativeBinomial"));
    }

    // ── 15. Predictions are available for all 7 days-of-week ─────────────────
    //    (proxy for the non-existent dailyTrend() API)

    void testDailyTrendExtracted()
    {
        // Create 2 events for each of the 7 days Jan 1-7 2024 (Mon-Sun)
        PoissonBaseline pb;
        QVector<PoissonBaseline::EventRecord> recs;
        for (int day = 1; day <= 7; ++day)
            appendRecs(recs, "ZDOW", 2024, 1, day, 10, "burglary", 2);
        pb.fit(recs);

        // Each DOW bucket has 1 observation with count=2 (nObservations=1)
        int validPredictions = 0;
        for (int day = 1; day <= 7; ++day) {
            QDateTime dt(QDate(2024, 1, day), QTime(10, 0, 0), Qt::UTC);
            auto pred = pb.predict("ZDOW", dt, "burglary");
            if (pred.nObservations > 0) ++validPredictions;
        }
        // All 7 DOWs must have data
        QCOMPARE(validPredictions, 7);
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Entry point
// ─────────────────────────────────────────────────────────────────────────────

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    TestPoissonOverdispersion t;
    return QTest::qExec(&t, argc, argv);
}

#include "test_poisson_overdispersion.moc"
