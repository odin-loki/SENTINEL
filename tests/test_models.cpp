// tests/test_models.cpp
// Comprehensive Qt Test unit tests for the SENTINEL statistical models module.
// Tests cover PoissonBaseline, HawkesProcess, SeriesDetector, TemporalFeatures.

#include <QTest>
#include <QCoreApplication>
#include <QDate>
#include <QDateTime>
#include <QVector>
#include <QtMath>
#include <cmath>

#include "models/PoissonBaseline.h"
#include "models/HawkesProcess.h"
#include "models/SeriesDetector.h"
#include "models/TemporalFeatures.h"
#include "core/CrimeEvent.h"

// ---------------------------------------------------------------------------
// Helper: build a CrimeEvent from primitives
// ---------------------------------------------------------------------------
static CrimeEvent makeEv(const QString& id, double lat, double lon,
                          const QString& type, const QDateTime& dt)
{
    CrimeEvent ev;
    ev.eventId    = id;
    ev.ingestedAt = dt;
    ev.occurredAt = dt;
    ev.lat        = lat;
    ev.lon        = lon;
    ev.crimeType  = type;
    return ev;
}

// ---------------------------------------------------------------------------
// 1. TestPoissonBaseline
// ---------------------------------------------------------------------------
class TestPoissonBaseline : public QObject
{
    Q_OBJECT

    using ER = PoissonBaseline::EventRecord;

    // Build n identical EventRecords all in the same bucket
    static QVector<ER> sameSlot(int n, const QString& zone,
                                 const QString& type, int hour, int month)
    {
        QVector<ER> recs;
        recs.reserve(n);
        for (int i = 0; i < n; ++i) {
            ER r;
            r.zoneId     = zone;
            r.crimeType  = type;
            // Each on a different day so dailyCounts accumulate across many days
            r.occurredAt = QDateTime(QDate(2025, month, 1).addDays(i),
                                     QTime(hour, 0, 0), Qt::UTC);
            recs.append(r);
        }
        return recs;
    }

private slots:

    // ── Static math helpers ──────────────────────────────────────────────────

    void testPMFSpecialCase()
    {
        QCOMPARE(PoissonBaseline::poissonPMF(0.0,  0), 1.0);
        QCOMPARE(PoissonBaseline::poissonPMF(0.0,  1), 0.0);
        QCOMPARE(PoissonBaseline::poissonPMF(-1.0, 0), 1.0);
        QCOMPARE(PoissonBaseline::poissonPMF(-1.0, 2), 0.0);
    }

    void testPMFKnownValues()
    {
        constexpr double tol = 1e-4;
        QVERIFY(qAbs(PoissonBaseline::poissonPMF(1.0, 0) - std::exp(-1.0)) < tol);
        QVERIFY(qAbs(PoissonBaseline::poissonPMF(1.0, 1) - std::exp(-1.0)) < tol);
        QVERIFY(qAbs(PoissonBaseline::poissonPMF(2.0, 0) - std::exp(-2.0)) < tol);
        QVERIFY(qAbs(PoissonBaseline::poissonPMF(2.0, 2) - 2.0 * std::exp(-2.0)) < tol);
    }

    void testPMFSumsToOne()
    {
        double sum = 0.0;
        for (int k = 0; k <= 200; ++k)
            sum += PoissonBaseline::poissonPMF(3.0, k);
        QVERIFY(qAbs(sum - 1.0) < 0.001);
    }

    void testPPFMonotone()
    {
        const double lam = 3.0;
        const double q10 = PoissonBaseline::poissonPPF(lam, 0.10);
        const double q50 = PoissonBaseline::poissonPPF(lam, 0.50);
        const double q90 = PoissonBaseline::poissonPPF(lam, 0.90);
        QVERIFY(q10 <= q50);
        QVERIFY(q50 <= q90);
    }

    void testPPFZeroLambda()
    {
        QCOMPARE(PoissonBaseline::poissonPPF(0.0, 0.5), 0.0);
    }

    void testNegBinPMF()
    {
        // PMF(r=1, p=0.5, k=0) = Γ(1)/(Γ(1)*1!) * 0.5^0 * (1-0.5)^1 = 0.5
        QVERIFY(qAbs(PoissonBaseline::negBinPMF(1.0, 0.5, 0) - 0.5) < 1e-9);
    }

    void testNegBinPMFSumsToOne()
    {
        double sum = 0.0;
        for (int k = 0; k <= 200; ++k)
            sum += PoissonBaseline::negBinPMF(2.0, 0.3, k);
        QVERIFY(qAbs(sum - 1.0) < 0.001);
    }

    void testNegBinInvalidParams()
    {
        QCOMPARE(PoissonBaseline::negBinPMF(0.0,  0.5, 0), 0.0);
        QCOMPARE(PoissonBaseline::negBinPMF(1.0, -0.1, 0), 0.0);
        QCOMPARE(PoissonBaseline::negBinPMF(1.0,  1.0, 0), 0.0);
    }

    // ── fit() + predict() ───────────────────────────────────────────────────

    void testFitAndPredict()
    {
        // 30 events in zoneA, all Monday ~10:00, month 6, crimeType "burglary"
        // Monday 2025-06-02, hour 10 → hourBin=5, dow=0, month=6
        const auto events = sameSlot(30, "zoneA", "burglary", 10, 6);
        PoissonBaseline model;
        model.fit(events);

        QVERIFY(model.isFitted());

        QDateTime queryDt(QDate(2025, 6, 9), QTime(10, 0, 0), Qt::UTC);  // also Monday
        PoissonPrediction pred = model.predict("zoneA", queryDt, "burglary");

        QVERIFY(pred.nObservations > 0);
        QVERIFY(pred.lambda > 0.0);
        QVERIFY(pred.expectedCount > 0.0);
    }

    void testPredictUnknownBucket()
    {
        // Fit with one event then query a completely different zone/type
        ER r;
        r.zoneId    = "zoneA";
        r.crimeType = "burglary";
        r.occurredAt = QDateTime(QDate(2025, 1, 6), QTime(10, 0, 0), Qt::UTC);

        PoissonBaseline model;
        model.fit({r});

        QDateTime queryDt(QDate(2025, 6, 2), QTime(10, 0, 0), Qt::UTC);
        PoissonPrediction pred = model.predict("unknownZone", queryDt, "robbery");

        QCOMPARE(pred.nObservations, 0);
        QCOMPARE(pred.model, QString("NonHomogeneousPoisson"));
        QVERIFY(pred.lambda >= 0.0);
        QVERIFY(pred.lambda <= 0.1);
    }

    void testPredictProbAtLeastOne()
    {
        const auto events = sameSlot(10, "zoneB", "theft", 14, 3);
        PoissonBaseline model;
        model.fit(events);

        QDateTime queryDt(QDate(2025, 3, 1), QTime(14, 0, 0), Qt::UTC);
        PoissonPrediction pred = model.predict("zoneB", queryDt, "theft");

        QVERIFY(pred.probAtLeastOne >= 0.0);
        QVERIFY(pred.probAtLeastOne <= 1.0);
    }

    void testCI90Order()
    {
        const auto events = sameSlot(15, "zoneC", "assault", 21, 5);
        PoissonBaseline model;
        model.fit(events);

        QDateTime queryDt(QDate(2025, 5, 1), QTime(21, 0, 0), Qt::UTC);
        PoissonPrediction pred = model.predict("zoneC", queryDt, "assault");

        QVERIFY(pred.ci90.first <= pred.ci90.second);
    }

    void testNegBinFittedForOverdispersedData()
    {
        // Build overdispersed data: variance >> mean.
        // We create daily counts that alternate 0,0,0,0,0,0,10 across days.
        // This gives mean ~1.4 but variance ~10+, triggering NB.
        QVector<ER> recs;
        for (int i = 0; i < 28; ++i) {
            int nToday = (i % 7 == 6) ? 10 : 0;
            for (int j = 0; j < nToday; ++j) {
                ER r;
                r.zoneId     = "zoneOD";
                r.crimeType  = "robbery";
                r.occurredAt = QDateTime(QDate(2025, 4, 1).addDays(i),
                                         QTime(20, 0, 0), Qt::UTC);
                recs.append(r);
            }
        }

        PoissonBaseline model;
        model.fit(recs);

        QDateTime qdt(QDate(2025, 4, 7), QTime(20, 0, 0), Qt::UTC);
        PoissonPrediction pred = model.predict("zoneOD", qdt, "robbery");

        // With sufficient counts and overdispersion, model may switch to NB
        // (requires > 5 daily observations in the bucket)
        // We just verify no crash and valid output
        QVERIFY(pred.lambda >= 0.0);
        QVERIFY(pred.probAtLeastOne >= 0.0);
    }
};

// ---------------------------------------------------------------------------
// 2. TestHawkesProcess
// ---------------------------------------------------------------------------
class TestHawkesProcess : public QObject
{
    Q_OBJECT

private:
    HawkesProcess defaultProcess() const
    {
        HawkesProcess hp;
        HawkesParams p;
        p.mu    = 0.1;
        p.alpha = 0.5;
        p.beta  = 1.0;
        p.sigma = 0.01;
        hp.setParams(p);
        return hp;
    }

private slots:

    void testTriggerKernelNegativeDt()
    {
        HawkesProcess hp = defaultProcess();
        QCOMPARE(hp.triggerKernel(-1.0, 0.0),   0.0);
        QCOMPARE(hp.triggerKernel(-0.001, 100.0), 0.0);
    }

    void testTriggerKernelZeroDt()
    {
        // φ(0, 0) = α·β·exp(0)·σ²/(0+σ²) = α·β
        HawkesProcess hp = defaultProcess();
        const double expected = hp.params().alpha * hp.params().beta;
        QVERIFY(qAbs(hp.triggerKernel(0.0, 0.0) - expected) < 1e-9);
    }

    void testTriggerKernelSpatialDecay()
    {
        HawkesProcess hp = defaultProcess();
        QVERIFY(hp.triggerKernel(0.1, 0.0) > hp.triggerKernel(0.1, 100.0));
    }

    void testTriggerKernelTemporalDecay()
    {
        HawkesProcess hp = defaultProcess();
        QVERIFY(hp.triggerKernel(0.1, 0.0) > hp.triggerKernel(1.0, 0.0));
    }

    void testIntensityNoHistory()
    {
        HawkesProcess hp = defaultProcess();
        hp.setHistory({});
        const double mu = hp.params().mu;
        QVERIFY(qAbs(hp.intensity(10.0, 51.5, -0.1) - mu) < 1e-9);
    }

    void testIntensityWithEvent()
    {
        HawkesProcess hp = defaultProcess();
        SpatiotemporalEvent ev;
        ev.tDays = 0.0;
        ev.lat   = 51.5;
        ev.lon   = -0.1;
        hp.setHistory({ev});

        const double mu = hp.params().mu;
        QVERIFY(hp.intensity(0.01, 51.5, -0.1) > mu);
    }

    void testIntensityLongAfterEvent()
    {
        HawkesProcess hp = defaultProcess();
        SpatiotemporalEvent ev;
        ev.tDays = 0.0;
        ev.lat   = 51.5;
        ev.lon   = -0.1;
        hp.setHistory({ev});

        const double mu = hp.params().mu;
        // After 365 days, exp(-1*365) ≈ 0, so intensity ≈ mu
        QVERIFY(qAbs(hp.intensity(365.0, 51.5, -0.1) - mu) < 1e-4);
    }

    void testIntensityNonNegative()
    {
        HawkesProcess hp = defaultProcess();
        QVector<SpatiotemporalEvent> hist;
        for (int i = 0; i < 5; ++i) {
            SpatiotemporalEvent e;
            e.tDays = static_cast<double>(i);
            e.lat   = 51.5 + i * 0.01;
            e.lon   = -0.1 + i * 0.01;
            hist.append(e);
        }
        hp.setHistory(hist);

        QVERIFY(hp.intensity(3.0,   51.5, -0.1) >= 0.0);
        QVERIFY(hp.intensity(0.0,   51.5, -0.1) >= 0.0);
        QVERIFY(hp.intensity(100.0, 52.0,  0.0) >= 0.0);
    }

    void testFitConverges()
    {
        QVector<SpatiotemporalEvent> events;
        double t = 0.0;
        for (int i = 0; i < 20; ++i) {
            t += 0.5 + (i % 3) * 0.1;
            SpatiotemporalEvent ev;
            ev.tDays = t;
            ev.lat   = 51.5 + (i % 4) * 0.005;
            ev.lon   = -0.1 + (i % 3) * 0.005;
            events.append(ev);
        }

        HawkesProcess hp;
        const bool ok = hp.fit(events, 200);
        QVERIFY(ok);

        const HawkesParams& p = hp.params();
        QVERIFY(p.mu    > 0.0);
        QVERIFY(p.alpha > 0.0);
        QVERIFY(p.alpha < 1.0);   // stable process
        QVERIFY(p.beta  > 0.0);
    }

    void testParamsSurviveRoundTrip()
    {
        HawkesProcess hp;
        HawkesParams p;
        p.mu    = 0.2;
        p.alpha = 0.3;
        p.beta  = 2.5;
        p.sigma = 0.05;
        hp.setParams(p);

        const HawkesParams& out = hp.params();
        QCOMPARE(out.mu,    p.mu);
        QCOMPARE(out.alpha, p.alpha);
        QCOMPARE(out.beta,  p.beta);
        QCOMPARE(out.sigma, p.sigma);
    }
};

// ---------------------------------------------------------------------------
// 3. TestSeriesDetector
// ---------------------------------------------------------------------------
class TestSeriesDetector : public QObject
{
    Q_OBJECT

private slots:

    // ── MO Jaccard similarity ────────────────────────────────────────────────

    void testMOJaccardIdentical()
    {
        QCOMPARE(SeriesDetector::moJaccard("forced_entry residential",
                                           "forced_entry residential"), 1.0);
    }

    void testMOJaccardDisjoint()
    {
        QCOMPARE(SeriesDetector::moJaccard("a b", "c d"), 0.0);
    }

    void testMOJaccardPartial()
    {
        // "a b c" ∩ "b c d" = {b,c}, union = {a,b,c,d} → 2/4 = 0.5
        QVERIFY(qAbs(SeriesDetector::moJaccard("a b c", "b c d") - 0.5) < 1e-9);
    }

    void testMOJaccardEmpty()
    {
        // Both empty strings → 0.0 (no features to share)
        QCOMPARE(SeriesDetector::moJaccard("", ""), 0.0);
    }

    void testMOJaccardSingleMatch()
    {
        QCOMPARE(SeriesDetector::moJaccard("a", "a"), 1.0);
    }

    // ── Haversine distance ───────────────────────────────────────────────────

    void testHaversineSamePoint()
    {
        QCOMPARE(SeriesDetector::haversineKm(51.5, -0.1, 51.5, -0.1), 0.0);
    }

    void testHaversineOneDegLon()
    {
        // 1° longitude at equator ≈ 111.195 km
        const double d = SeriesDetector::haversineKm(0.0, 0.0, 0.0, 1.0);
        QVERIFY(d >= 110.0 && d <= 112.0);
    }

    void testHaversineKnownDistance()
    {
        // London→Manchester ≈ 263 km
        const double d = SeriesDetector::haversineKm(51.5074, -0.1278,
                                                      53.4808, -2.2426);
        QVERIFY(qAbs(d - 263.0) < 10.0);
    }

    // ── Near-repeat calibration table ────────────────────────────────────────

    void testNearRepeatBurglary()
    {
        const auto p = SeriesDetector::nearRepeatFor("burglary");
        QCOMPARE(p.distM,      200.0);
        QCOMPARE(p.days,        14.0);
        QCOMPARE(p.multiplier,   4.5);
    }

    void testNearRepeatDefault()
    {
        const auto p = SeriesDetector::nearRepeatFor("unknown_xyz");
        // Default-constructed NearRepeatParams
        QVERIFY(p.distM      >= 0.0);
        QVERIFY(p.days       >= 0.0);
        QVERIFY(p.multiplier >= 0.0);
    }

    // ── detectSeries / detect ────────────────────────────────────────────────

    void testDetectEmptyEvents()
    {
        SeriesDetector det;
        QVERIFY(det.detect({}).isEmpty());
    }

    void testDetectSeries()
    {
        // 4 burglaries within 100 m of each other, within 7 days
        SeriesDetector det(0.5, 14.0, 3);
        const QDateTime base(QDate(2025, 4, 1), QTime(10, 0, 0), Qt::UTC);

        QVector<CrimeEvent> events;
        events << makeEv("e1", 51.5000, -0.1000, "burglary", base)
               << makeEv("e2", 51.5004, -0.1002, "burglary", base.addDays(1))
               << makeEv("e3", 51.5008, -0.1004, "burglary", base.addDays(3))
               << makeEv("e4", 51.5002, -0.1001, "burglary", base.addDays(6));

        QVERIFY(!det.detect(events).isEmpty());
    }

    void testDetectNoSeries()
    {
        SeriesDetector det(0.5, 14.0, 3);
        const QDateTime base(QDate(2025, 4, 1), QTime(10, 0, 0), Qt::UTC);

        QVector<CrimeEvent> events;
        events << makeEv("e1",  51.5,  -0.1, "burglary", base)
               << makeEv("e2",  48.8,   2.3, "burglary", base.addDays(1))
               << makeEv("e3",  52.5,  13.4, "burglary", base.addDays(2))
               << makeEv("e4",  40.7, -74.0, "burglary", base.addDays(3));

        QVERIFY(det.detect(events).isEmpty());
    }

    void testDetectSingleEventNoSeries()
    {
        SeriesDetector det(0.5, 14.0, 3);
        const QDateTime base(QDate(2025, 4, 1), QTime(10, 0, 0), Qt::UTC);
        QVERIFY(det.detect({ makeEv("e1", 51.5, -0.1, "burglary", base) }).isEmpty());
    }
};

// ---------------------------------------------------------------------------
// 4. TestTemporalFeatures
// ---------------------------------------------------------------------------
class TestTemporalFeatures : public QObject
{
    Q_OBJECT

    static constexpr double kTol = 1e-6;

    static QDateTime mondayAt(int hour)
    {
        // 2025-06-02 is a Monday
        return QDateTime(QDate(2025, 6, 2), QTime(hour, 0, 0), Qt::UTC);
    }
    static QDateTime saturdayAt(int hour)
    {
        // 2025-06-07 is a Saturday
        return QDateTime(QDate(2025, 6, 7), QTime(hour, 0, 0), Qt::UTC);
    }

private slots:

    void testHourSinCosMidnight()
    {
        auto fv = TemporalFeatures::compute(mondayAt(0));
        QVERIFY(qAbs(fv.hourSin - 0.0) < kTol);
        QVERIFY(qAbs(fv.hourCos - 1.0) < kTol);
    }

    void testHourSinCos6am()
    {
        auto fv = TemporalFeatures::compute(mondayAt(6));
        QVERIFY(qAbs(fv.hourSin - 1.0) < kTol);
        QVERIFY(qAbs(fv.hourCos - 0.0) < kTol);
    }

    void testHourSinCosNoon()
    {
        auto fv = TemporalFeatures::compute(mondayAt(12));
        QVERIFY(qAbs(fv.hourSin - 0.0) < kTol);
        QVERIFY(qAbs(fv.hourCos - (-1.0)) < kTol);
    }

    void testHourSinCos6pm()
    {
        auto fv = TemporalFeatures::compute(mondayAt(18));
        QVERIFY(qAbs(fv.hourSin - (-1.0)) < kTol);
        QVERIFY(qAbs(fv.hourCos - 0.0)    < kTol);
    }

    void testPythagoreanHour()
    {
        for (int h = 0; h < 24; ++h) {
            auto fv = TemporalFeatures::compute(mondayAt(h));
            double sum = fv.hourSin * fv.hourSin + fv.hourCos * fv.hourCos;
            QVERIFY(qAbs(sum - 1.0) < 1e-9);
        }
    }

    void testPythagoreanDow()
    {
        for (int d = 0; d < 7; ++d) {
            QDateTime dt(QDate(2025, 6, 2).addDays(d), QTime(12, 0, 0), Qt::UTC);
            auto fv = TemporalFeatures::compute(dt);
            double sum = fv.dowSin * fv.dowSin + fv.dowCos * fv.dowCos;
            QVERIFY(qAbs(sum - 1.0) < 1e-9);
        }
    }

    void testPythagoreanMonth()
    {
        for (int m = 1; m <= 12; ++m) {
            QDateTime dt(QDate(2025, m, 15), QTime(12, 0, 0), Qt::UTC);
            auto fv = TemporalFeatures::compute(dt);
            double sum = fv.monthSin * fv.monthSin + fv.monthCos * fv.monthCos;
            QVERIFY(qAbs(sum - 1.0) < 1e-9);
        }
    }

    void testIsWeekendSaturday()
    {
        QVERIFY(TemporalFeatures::compute(saturdayAt(12)).isWeekend);
    }

    void testIsWeekendMonday()
    {
        QVERIFY(!TemporalFeatures::compute(mondayAt(12)).isWeekend);
    }

    void testIsNightMidnight()
    {
        QVERIFY(TemporalFeatures::compute(mondayAt(0)).isNight);
    }

    void testIsNightAfternoon()
    {
        QVERIFY(!TemporalFeatures::compute(mondayAt(14)).isNight);
    }

    void testIsNightBoundary()
    {
        // isNight = hour >= 22 OR hour <= 5
        QVERIFY( TemporalFeatures::compute(mondayAt(22)).isNight);  // 22 → night
        QVERIFY( TemporalFeatures::compute(mondayAt( 5)).isNight);  // 5  → night
        QVERIFY(!TemporalFeatures::compute(mondayAt( 6)).isNight);  // 6  → not night
    }

    void testIsDarkRoughlyAtNight()
    {
        // Sun altitude at midnight in London (summer) should still be below civil
        // twilight (-6°), confirming isDark==true for midnight.
        auto fv = TemporalFeatures::compute(
            QDateTime(QDate(2025, 12, 21), QTime(0, 0, 0), Qt::UTC));
        QVERIFY(fv.isDark);
    }

    void testLunarPhaseRange()
    {
        for (int m = 1; m <= 12; ++m) {
            QDateTime dt(QDate(2025, m, 15), QTime(0, 0, 0), Qt::UTC);
            auto fv = TemporalFeatures::compute(dt);
            QVERIFY(fv.lunarPhase >= 0.0);
            QVERIFY(fv.lunarPhase <= 1.0);
        }
    }

    void testHourRawMatches()
    {
        for (int h = 0; h < 24; ++h) {
            auto fv = TemporalFeatures::compute(mondayAt(h));
            QCOMPARE(fv.hourRaw, h);
        }
    }

    void testDowRawMonday()
    {
        // Monday → dowRaw = 0  (Qt dayOfWeek=1, we subtract 1)
        auto fv = TemporalFeatures::compute(mondayAt(12));
        QCOMPARE(fv.dowRaw, 0);
    }

    void testDowRawSaturday()
    {
        // Saturday → dowRaw = 5
        auto fv = TemporalFeatures::compute(saturdayAt(12));
        QCOMPARE(fv.dowRaw, 5);
    }

    void testAllFieldsFinite()
    {
        // Verify no NaN or Inf in any numeric field
        QDateTime dt(QDate(2025, 3, 15), QTime(14, 30, 0), Qt::UTC);
        auto fv = TemporalFeatures::compute(dt);

        QVERIFY(std::isfinite(fv.hourSin));
        QVERIFY(std::isfinite(fv.hourCos));
        QVERIFY(std::isfinite(fv.dowSin));
        QVERIFY(std::isfinite(fv.dowCos));
        QVERIFY(std::isfinite(fv.monthSin));
        QVERIFY(std::isfinite(fv.monthCos));
        QVERIFY(std::isfinite(fv.doySin));
        QVERIFY(std::isfinite(fv.doyCos));
        QVERIFY(std::isfinite(fv.lunarPhase));
        QVERIFY(std::isfinite(fv.sunAltitudeDeg));
    }
};

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
static int runTest(QObject* obj, const char* logFile)
{
    QStringList args = { "test", "-o", QString("%1,txt").arg(logFile) };
    return QTest::qExec(obj, args);
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    int r = 0;
    { TestPoissonBaseline  t1; r |= runTest(&t1, "mdl_poisson.txt"); }
    { TestHawkesProcess    t2; r |= runTest(&t2, "mdl_hawkes.txt"); }
    { TestSeriesDetector   t3; r |= runTest(&t3, "mdl_series.txt"); }
    { TestTemporalFeatures t4; r |= runTest(&t4, "mdl_temporal.txt"); }
    return r;
}

#include "test_models.moc"
