// test_all_models_perf.cpp — Comprehensive performance benchmarks for ALL core models
//
// Each test creates realistic synthetic data, times the target operation, and
// asserts it completes within the specified limit (with 2x safety margin over
// the design-spec targets to accommodate varying build environments).
//
// Time limits used here are 2× the design targets:
//   PoissonBaseline::fit         1000 events  < 1000ms  (spec 500ms)
//   PoissonBaseline::predict     1000 calls   < 200ms   (spec 100ms)
//   HawkesProcess::fit           200 events   < 6000ms  (spec 3000ms)
//   SeriesDetector::detect       200 events   < 2000ms  (spec 1000ms)
//   KDEHotspot::compute          500 pts      < 2000ms  (spec 1000ms)
//   KDEHotspot density query     500 pts      < 400ms   (spec 200ms)
//   RiskForecaster::forecast     100 zones    < 4000ms  (spec 2000ms)
//   BayesianHierarchical::fit    500 events   < 1000ms  (spec 500ms)
//   EnsemblePredictor::predict   100 calls    < 400ms   (spec 200ms)
//   GPRegression::fit            50 pts       < 4000ms  (spec 2000ms)
//   MOAnalyser::fit              100 cases    < 1000ms  (spec 500ms)
//   MOAnalyser::findSimilar      100 cases    < 400ms   (spec 200ms)
//   GeographicProfiler::profile  50 pts       < 200ms   (spec 100ms)
//   AnomalyDetector              100 events   < 1000ms  (spec 500ms)
//   HintEngine::generate         50 events    < 2000ms  (spec 1000ms)
//   End-to-end pipeline          200 events   < 20000ms (spec 10000ms)

#include <QTest>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QDateTime>
#include <QTimeZone>
#include <QVector>
#include <QPair>
#include <QString>
#include <cmath>
#include <random>

#include "core/CrimeEvent.h"
#include "models/PoissonBaseline.h"
#include "models/HawkesProcess.h"
#include "models/SeriesDetector.h"
#include "models/KDEHotspot.h"
#include "models/RiskForecaster.h"
#include "models/BayesianHierarchical.h"
#include "models/EnsemblePredictor.h"
#include "models/GPRegression.h"
#include "inference/MOAnalyser.h"
#include "inference/GeographicProfiler.h"
#include "inference/AnomalyDetector.h"
#include "inference/HintEngine.h"

// ─────────────────────────────────────────────────────────────────────────────
// Deterministic pseudo-random helpers
// ─────────────────────────────────────────────────────────────────────────────
namespace {

static std::mt19937 rng(12345);

static double rnd(double lo, double hi) {
    return std::uniform_real_distribution<double>(lo, hi)(rng);
}
static int rndI(int lo, int hi) {
    return std::uniform_int_distribution<int>(lo, hi)(rng);
}

static const QStringList kCrimeTypes = {
    "burglary", "theft", "robbery", "assault", "vandalism"
};
static const QStringList kZones = {
    "ZoneA", "ZoneB", "ZoneC", "ZoneD", "ZoneE",
    "ZoneF", "ZoneG", "ZoneH", "ZoneI", "ZoneJ"
};
static const QStringList kMoWords = {
    "forced", "entry", "rear", "window", "lock", "pick", "gloves",
    "mask", "night", "dawn", "residential", "commercial", "vehicle",
    "cash", "jewellery", "electronics", "solo", "group", "armed",
    "smashed", "crowbar", "silent", "quick", "escape", "car"
};

static QString randomMO(int wordCount = 6) {
    QStringList chosen;
    chosen.reserve(wordCount);
    for (int i = 0; i < wordCount; ++i)
        chosen << kMoWords[rndI(0, kMoWords.size() - 1)];
    return chosen.join(" ");
}

static QDateTime baseTime() {
    return QDateTime(QDate(2024, 1, 1), QTime(0, 0, 0), QTimeZone::utc());
}

// Build N CrimeEvents with realistic spatial/temporal scatter
static QVector<CrimeEvent> makeCrimeEvents(int n) {
    QVector<CrimeEvent> events;
    events.reserve(n);
    const QDateTime base = baseTime();
    for (int i = 0; i < n; ++i) {
        CrimeEvent ev;
        ev.eventId   = QStringLiteral("EVT%1").arg(i, 6, 10, QChar('0'));
        ev.suburb    = kZones[i % kZones.size()];
        ev.crimeType = kCrimeTypes[i % kCrimeTypes.size()];
        ev.occurredAt = base.addSecs(rndI(0, 365 * 24 * 3600));
        ev.timestamp  = ev.occurredAt.value_or(base);
        ev.lat        = rnd(-33.9, -33.7);
        ev.lon        = rnd(151.0, 151.2);
        ev.latitude   = ev.lat.value_or(-33.8);
        ev.longitude  = ev.lon.value_or(151.1);
        ev.narrative  = randomMO(8);
        events.append(ev);
    }
    return events;
}

} // namespace

// ═══════════════════════════════════════════════════════════════════════════
// TestAllModelsPerf
// ═══════════════════════════════════════════════════════════════════════════

class TestAllModelsPerf : public QObject {
    Q_OBJECT

private slots:

    // ── 1. PoissonBaseline::fit — 1000 events ──────────────────────────────
    void testPoissonBaselineFit() {
        QVector<PoissonBaseline::EventRecord> records;
        records.reserve(1000);
        const QDateTime base = baseTime();
        for (int i = 0; i < 1000; ++i) {
            PoissonBaseline::EventRecord r;
            r.zoneId     = kZones[i % kZones.size()];
            r.crimeType  = kCrimeTypes[i % kCrimeTypes.size()];
            r.occurredAt = base.addSecs(rndI(0, 365 * 24 * 3600));
            records.append(r);
        }

        QElapsedTimer t;
        t.start();
        PoissonBaseline model;
        model.fit(records);
        const qint64 elapsed = t.elapsed();

        QVERIFY2(model.isFitted(),
                 "PoissonBaseline::fit should succeed on 1000 events");
        QVERIFY2(elapsed < 1000,
                 qPrintable(QStringLiteral("PoissonBaseline::fit took %1ms (limit 1000ms)").arg(elapsed)));
        qDebug("PoissonBaseline::fit 1000 events: %lldms", elapsed);
    }

    // ── 2. PoissonBaseline::predict — 1000 predictions ─────────────────────
    void testPoissonBaselinePredict() {
        QVector<PoissonBaseline::EventRecord> records;
        records.reserve(1000);
        const QDateTime base = baseTime();
        for (int i = 0; i < 1000; ++i) {
            PoissonBaseline::EventRecord r;
            r.zoneId     = kZones[i % kZones.size()];
            r.crimeType  = kCrimeTypes[i % kCrimeTypes.size()];
            r.occurredAt = base.addSecs(i * 3600);
            records.append(r);
        }
        PoissonBaseline model;
        model.fit(records);
        QVERIFY(model.isFitted());

        QElapsedTimer t;
        t.start();
        for (int i = 0; i < 1000; ++i) {
            auto pred = model.predict(
                kZones[i % kZones.size()],
                base.addSecs(i * 3600),
                kCrimeTypes[i % kCrimeTypes.size()]);
            Q_UNUSED(pred)
        }
        const qint64 elapsed = t.elapsed();

        QVERIFY2(elapsed < 200,
                 qPrintable(QStringLiteral("PoissonBaseline::predict 1000 calls took %1ms (limit 200ms)").arg(elapsed)));
        qDebug("PoissonBaseline::predict 1000 calls: %lldms", elapsed);
    }

    // ── 3. HawkesProcess::fit — 200 events ─────────────────────────────────
    void testHawkesProcessFit() {
        QVector<SpatiotemporalEvent> events;
        events.reserve(200);
        for (int i = 0; i < 200; ++i) {
            SpatiotemporalEvent ev;
            ev.tDays     = i * 0.5 + rnd(0.0, 0.3);
            ev.lat       = rnd(-33.9, -33.7);
            ev.lon       = rnd(151.0, 151.2);
            ev.crimeType = kCrimeTypes[i % kCrimeTypes.size()];
            events.append(ev);
        }

        QElapsedTimer t;
        t.start();
        HawkesProcess model;
        model.fit(events, /*maxIterations=*/10);
        const qint64 elapsed = t.elapsed();

        QVERIFY2(model.isFitted(),
                 "HawkesProcess::fit should mark model as fitted");
        QVERIFY2(elapsed < 6000,
                 qPrintable(QStringLiteral("HawkesProcess::fit 200 events took %1ms (limit 6000ms)").arg(elapsed)));
        qDebug("HawkesProcess::fit 200 events (maxIter=10): %lldms", elapsed);
    }

    // ── 4. SeriesDetector::detect — 200 events ─────────────────────────────
    void testSeriesDetectorDetect() {
        QVector<CrimeEvent> events = makeCrimeEvents(200);

        SeriesDetector detector;

        QElapsedTimer t;
        t.start();
        QVector<CrimeSeries> series = detector.detect(events);
        const qint64 elapsed = t.elapsed();

        Q_UNUSED(series)
        QVERIFY2(elapsed < 2000,
                 qPrintable(QStringLiteral("SeriesDetector::detect 200 events took %1ms (limit 2000ms)").arg(elapsed)));
        qDebug("SeriesDetector::detect 200 events: %lldms", elapsed);
    }

    // ── 5. KDEHotspot::compute — 500 points ────────────────────────────────
    void testKDEHotspotCompute() {
        QVector<QPair<double,double>> locs;
        locs.reserve(500);
        for (int i = 0; i < 500; ++i)
            locs.append({rnd(-33.9, -33.7), rnd(151.0, 151.2)});

        KDEHotspot kde(50, 1.0);

        QElapsedTimer t;
        t.start();
        auto surface = kde.compute(locs, -33.9, -33.7, 151.0, 151.2);
        const qint64 elapsed = t.elapsed();

        QVERIFY(!surface.empty());
        QVERIFY2(elapsed < 2000,
                 qPrintable(QStringLiteral("KDEHotspot::compute 500 pts took %1ms (limit 2000ms)").arg(elapsed)));
        qDebug("KDEHotspot::compute 500 pts: %lldms", elapsed);
    }

    // ── 6. KDEHotspot density — findHotspots over 500 points ───────────────
    void testKDEHotspotFindHotspots() {
        QVector<QPair<double,double>> locs;
        locs.reserve(500);
        for (int i = 0; i < 500; ++i)
            locs.append({rnd(-33.9, -33.7), rnd(151.0, 151.2)});

        KDEHotspot kde(50, 1.0);

        QElapsedTimer t;
        t.start();
        auto hotspots = kde.findHotspots(locs, -33.9, -33.7, 151.0, 151.2, 5);
        const qint64 elapsed = t.elapsed();

        Q_UNUSED(hotspots)
        QVERIFY2(elapsed < 400,
                 qPrintable(QStringLiteral("KDEHotspot::findHotspots 500 pts took %1ms (limit 400ms)").arg(elapsed)));
        qDebug("KDEHotspot::findHotspots 500 pts: %lldms", elapsed);
    }

    // ── 7. RiskForecaster::forecast — 100 zones ────────────────────────────
    void testRiskForecasterForecast() {
        // Build 1000 events spread across 100 zones
        QVector<CrimeEvent> events;
        events.reserve(1000);
        const QDateTime base = baseTime();
        for (int i = 0; i < 1000; ++i) {
            CrimeEvent ev;
            ev.eventId   = QStringLiteral("E%1").arg(i);
            ev.suburb    = QStringLiteral("Zone%1").arg(i % 100);
            ev.crimeType = "burglary";
            ev.occurredAt = base.addDays(rndI(0, 364));
            ev.timestamp  = ev.occurredAt.value_or(base);
            ev.lat        = rnd(-33.9, -33.7);
            ev.lon        = rnd(151.0, 151.2);
            events.append(ev);
        }

        RiskForecaster forecaster(7);
        forecaster.fit(events);
        QVERIFY(forecaster.isFitted());

        QElapsedTimer t;
        t.start();
        auto forecasts = forecaster.forecast(base.addDays(365));
        const qint64 elapsed = t.elapsed();

        Q_UNUSED(forecasts)
        QVERIFY2(elapsed < 4000,
                 qPrintable(QStringLiteral("RiskForecaster::forecast 100 zones took %1ms (limit 4000ms)").arg(elapsed)));
        qDebug("RiskForecaster::forecast 100 zones: %lldms", elapsed);
    }

    // ── 8. BayesianHierarchical::fit — 500 events ──────────────────────────
    void testBayesianHierarchicalFit() {
        QVector<CrimeEvent> events = makeCrimeEvents(500);

        QElapsedTimer t;
        t.start();
        BayesianHierarchical model;
        model.fit(events, 30.0);
        const qint64 elapsed = t.elapsed();

        QVERIFY2(model.isFitted(),
                 "BayesianHierarchical should be fitted after 500 events");
        QVERIFY2(elapsed < 1000,
                 qPrintable(QStringLiteral("BayesianHierarchical::fit 500 events took %1ms (limit 1000ms)").arg(elapsed)));
        qDebug("BayesianHierarchical::fit 500 events: %lldms", elapsed);
    }

    // ── 9. EnsemblePredictor::predict — 100 calls ──────────────────────────
    void testEnsemblePredictorPredict() {
        // Build and fit component models
        QVector<PoissonBaseline::EventRecord> records;
        records.reserve(500);
        const QDateTime base = baseTime();
        for (int i = 0; i < 500; ++i) {
            PoissonBaseline::EventRecord r;
            r.zoneId     = kZones[i % kZones.size()];
            r.crimeType  = kCrimeTypes[i % kCrimeTypes.size()];
            r.occurredAt = base.addSecs(i * 7200);
            records.append(r);
        }
        PoissonBaseline poisson;
        poisson.fit(records);

        QVector<SpatiotemporalEvent> hawkesEvents;
        hawkesEvents.reserve(100);
        for (int i = 0; i < 100; ++i) {
            SpatiotemporalEvent ev;
            ev.tDays     = i * 1.0;
            ev.lat       = rnd(-33.9, -33.7);
            ev.lon       = rnd(151.0, 151.2);
            ev.crimeType = kCrimeTypes[i % kCrimeTypes.size()];
            hawkesEvents.append(ev);
        }
        HawkesProcess hawkes;
        hawkes.fit(hawkesEvents, 5);

        EnsemblePredictor ensemble;
        ensemble.setPoisson(&poisson);
        ensemble.setHawkes(&hawkes);
        ensemble.setWeights(0.6, 0.4);

        QElapsedTimer t;
        t.start();
        for (int i = 0; i < 100; ++i) {
            auto pred = ensemble.predict(
                kZones[i % kZones.size()],
                base.addSecs(i * 3600),
                kCrimeTypes[i % kCrimeTypes.size()],
                rnd(-33.9, -33.7),
                rnd(151.0, 151.2));
            Q_UNUSED(pred)
        }
        const qint64 elapsed = t.elapsed();

        QVERIFY2(elapsed < 400,
                 qPrintable(QStringLiteral("EnsemblePredictor::predict 100 calls took %1ms (limit 400ms)").arg(elapsed)));
        qDebug("EnsemblePredictor::predict 100 calls: %lldms", elapsed);
    }

    // ── 10. GPRegression::fit — 50 points ──────────────────────────────────
    void testGPRegressionFit() {
        QVector<QPair<double,double>> X;
        QVector<double> y;
        X.reserve(50);
        y.reserve(50);
        for (int i = 0; i < 50; ++i) {
            double lat = rnd(-33.9, -33.7);
            double lon = rnd(151.0, 151.2);
            X.append({lat, lon});
            y.append(rnd(0.0, 5.0));
        }

        QElapsedTimer t;
        t.start();
        GPRegression gp;
        gp.fit(X, y);
        const qint64 elapsed = t.elapsed();

        QVERIFY2(gp.isFitted(),
                 "GPRegression should be fitted after 50 points");
        QVERIFY2(elapsed < 4000,
                 qPrintable(QStringLiteral("GPRegression::fit 50 pts took %1ms (limit 4000ms)").arg(elapsed)));
        qDebug("GPRegression::fit 50 pts: %lldms", elapsed);
    }

    // ── 11. MOAnalyser::fit — 100 cases ────────────────────────────────────
    void testMOAnalyserFit() {
        QVector<MOCaseRecord> cases;
        cases.reserve(100);
        for (int i = 0; i < 100; ++i) {
            MOCaseRecord c;
            c.caseId         = QStringLiteral("C%1").arg(i);
            c.moText         = randomMO(rndI(5, 12));
            c.resolved       = (i % 3 == 0);
            c.outcome        = c.resolved ? "convicted" : "unsolved";
            c.suspectProfile = randomMO(4);
            cases.append(c);
        }

        QElapsedTimer t;
        t.start();
        MOAnalyser analyser;
        analyser.fit(cases);
        const qint64 elapsed = t.elapsed();

        QVERIFY2(analyser.isFitted(),
                 "MOAnalyser should be fitted after 100 cases");
        QVERIFY2(elapsed < 1000,
                 qPrintable(QStringLiteral("MOAnalyser::fit 100 cases took %1ms (limit 1000ms)").arg(elapsed)));
        qDebug("MOAnalyser::fit 100 cases: %lldms", elapsed);
    }

    // ── 12. MOAnalyser::findSimilar — query against 100 cases ──────────────
    void testMOAnalyserFindSimilar() {
        QVector<MOCaseRecord> cases;
        cases.reserve(100);
        for (int i = 0; i < 100; ++i) {
            MOCaseRecord c;
            c.caseId   = QStringLiteral("C%1").arg(i);
            c.moText   = randomMO(rndI(5, 12));
            c.resolved = (i % 3 == 0);
            cases.append(c);
        }
        MOAnalyser analyser;
        analyser.fit(cases);
        QVERIFY(analyser.isFitted());

        QElapsedTimer t;
        t.start();
        for (int q = 0; q < 50; ++q) {
            auto matches = analyser.findSimilar(randomMO(6), 10, 0.0);
            Q_UNUSED(matches)
        }
        const qint64 elapsed = t.elapsed();

        QVERIFY2(elapsed < 400,
                 qPrintable(QStringLiteral("MOAnalyser::findSimilar 50 queries took %1ms (limit 400ms)").arg(elapsed)));
        qDebug("MOAnalyser::findSimilar 50 queries against 100 cases: %lldms", elapsed);
    }

    // ── 13. GeographicProfiler::profile — 50 points ────────────────────────
    void testGeographicProfilerProfile() {
        QVector<QPair<double,double>> locs;
        locs.reserve(50);
        for (int i = 0; i < 50; ++i)
            locs.append({rnd(-33.9, -33.7), rnd(151.0, 151.2)});

        GeographicProfiler profiler(1.2, 1.2, 0.5, 40);

        QElapsedTimer t;
        t.start();
        auto geoProfile = profiler.profile(locs);
        const qint64 elapsed = t.elapsed();

        QVERIFY(!geoProfile.probabilitySurface.empty());
        QVERIFY2(elapsed < 200,
                 qPrintable(QStringLiteral("GeographicProfiler::profile 50 pts took %1ms (limit 200ms)").arg(elapsed)));
        qDebug("GeographicProfiler::profile 50 pts: %lldms", elapsed);
    }

    // ── 14. AnomalyDetector — fit + detect on 100 events ───────────────────
    void testAnomalyDetector() {
        QVector<AnomalyFeatureVector> data;
        data.reserve(100);
        for (int i = 0; i < 100; ++i) {
            AnomalyFeatureVector fv;
            fv.eventId       = QStringLiteral("A%1").arg(i);
            fv.lat           = rnd(-33.9, -33.7);
            fv.lon           = rnd(151.0, 151.2);
            fv.tDays         = i * 1.0;
            fv.hourNorm      = rnd(0.0, 1.0);
            fv.crimeTypeCode = rndI(0, 4);
            data.append(fv);
        }

        QElapsedTimer t;
        t.start();
        AnomalyDetector detector(0.05);
        detector.fit(data);
        auto anomSignals = detector.detectAnomalies(data);
        const qint64 elapsed = t.elapsed();

        Q_UNUSED(anomSignals)
        QVERIFY2(detector.isFitted(),
                 "AnomalyDetector should be fitted");
        QVERIFY2(elapsed < 1000,
                 qPrintable(QStringLiteral("AnomalyDetector fit+detect 100 events took %1ms (limit 1000ms)").arg(elapsed)));
        qDebug("AnomalyDetector fit+detect 100 events: %lldms", elapsed);
    }

    // ── 15. HintEngine::generate — 50 events ───────────────────────────────
    void testHintEngineGenerateLeads() {
        // Build a fitted MOAnalyser for MO matches
        QVector<MOCaseRecord> cases;
        cases.reserve(100);
        for (int i = 0; i < 100; ++i) {
            MOCaseRecord c;
            c.caseId   = QStringLiteral("C%1").arg(i);
            c.moText   = randomMO(rndI(5, 12));
            c.resolved = (i % 3 == 0);
            cases.append(c);
        }
        MOAnalyser analyser;
        analyser.fit(cases);

        // Build a GeographicProfiler for geo profiles
        GeographicProfiler profiler(1.2, 1.2, 0.5, 40);

        // Build inputs for 50 events
        QVector<HintEngineInput> inputs;
        inputs.reserve(50);
        const QDateTime base = baseTime();
        for (int i = 0; i < 50; ++i) {
            HintEngineInput inp;
            // Core event
            CrimeEvent ev;
            ev.eventId   = QStringLiteral("E%1").arg(i);
            ev.crimeType = kCrimeTypes[i % kCrimeTypes.size()];
            ev.suburb    = kZones[i % kZones.size()];
            ev.occurredAt = base.addSecs(i * 3600);
            ev.timestamp  = ev.occurredAt.value_or(base);
            ev.lat        = rnd(-33.9, -33.7);
            ev.lon        = rnd(151.0, 151.2);
            ev.narrative  = randomMO(8);
            inp.event = ev;

            // MO matches
            inp.moMatches = analyser.findSimilar(randomMO(6), 5, 0.0);

            // Geo profile
            QVector<QPair<double,double>> locs;
            for (int j = 0; j < 5; ++j)
                locs.append({rnd(-33.9, -33.7), rnd(151.0, 151.2)});
            if (locs.size() >= 3)
                inp.geoProfile = profiler.profile(locs);

            inp.dataQuality = rnd(0.5, 1.0);
            inputs.append(inp);
        }

        HintEngine engine;

        QElapsedTimer t;
        t.start();
        for (const auto& inp : inputs) {
            auto leads = engine.generate(inp);
            Q_UNUSED(leads)
        }
        const qint64 elapsed = t.elapsed();

        QVERIFY2(elapsed < 2000,
                 qPrintable(QStringLiteral("HintEngine::generate 50 events took %1ms (limit 2000ms)").arg(elapsed)));
        qDebug("HintEngine::generate 50 events: %lldms", elapsed);
    }

    // ── 16. End-to-end pipeline — 200 events ───────────────────────────────
    void testEndToEndPipelinePerf() {
        const int N = 200;
        QVector<CrimeEvent> events = makeCrimeEvents(N);
        const QDateTime base = baseTime();

        QElapsedTimer t;
        t.start();

        // Stage 1: Poisson baseline
        QVector<PoissonBaseline::EventRecord> records;
        records.reserve(N);
        for (const auto& ev : events) {
            PoissonBaseline::EventRecord r;
            r.zoneId     = ev.suburb;
            r.crimeType  = ev.crimeType;
            r.occurredAt = ev.occurredAt.value_or(base);
            records.append(r);
        }
        PoissonBaseline poisson;
        poisson.fit(records);

        // Stage 2: Hawkes process (small iter count for speed)
        QVector<SpatiotemporalEvent> hawkesEvents;
        hawkesEvents.reserve(N);
        for (int i = 0; i < N; ++i) {
            SpatiotemporalEvent ev;
            ev.tDays     = i * 0.5;
            ev.lat       = events[i].latitude;
            ev.lon       = events[i].longitude;
            ev.crimeType = events[i].crimeType;
            hawkesEvents.append(ev);
        }
        HawkesProcess hawkes;
        hawkes.fit(hawkesEvents, 5);

        // Stage 3: Ensemble
        EnsemblePredictor ensemble;
        ensemble.setPoisson(&poisson);
        ensemble.setHawkes(&hawkes);
        ensemble.setWeights(0.6, 0.4);

        // Stage 4: KDE hotspot
        QVector<QPair<double,double>> locs;
        locs.reserve(N);
        for (const auto& ev : events)
            locs.append({ev.latitude, ev.longitude});
        KDEHotspot kde(30, 1.0);
        auto kdeResult = kde.findHotspots(locs, -33.9, -33.7, 151.0, 151.2, 5);
        Q_UNUSED(kdeResult)

        // Stage 5: Bayesian hierarchical
        BayesianHierarchical bayes;
        bayes.fit(events, 30.0);

        // Stage 6: Series detector
        SeriesDetector detector;
        auto series = detector.detect(events);
        Q_UNUSED(series)

        // Stage 7: MO analysis
        QVector<MOCaseRecord> cases;
        cases.reserve(N);
        for (int i = 0; i < N; ++i) {
            MOCaseRecord c;
            c.caseId = QStringLiteral("C%1").arg(i);
            c.moText = events[i].narrative.value_or(randomMO(6));
            cases.append(c);
        }
        MOAnalyser moAnalyser;
        moAnalyser.fit(cases);

        // Stage 8: Anomaly detection
        QVector<AnomalyFeatureVector> fvs;
        fvs.reserve(N);
        for (int i = 0; i < N; ++i) {
            AnomalyFeatureVector fv;
            fv.eventId       = events[i].eventId;
            fv.lat           = events[i].latitude;
            fv.lon           = events[i].longitude;
            fv.tDays         = i * 0.5;
            fv.hourNorm      = rnd(0.0, 1.0);
            fv.crimeTypeCode = i % 5;
            fvs.append(fv);
        }
        AnomalyDetector anomDetector(0.05);
        anomDetector.fit(fvs);
        auto anomalies = anomDetector.detectAnomalies(fvs);
        Q_UNUSED(anomalies)

        // Stage 9: HintEngine on first 20 events
        GeographicProfiler profiler(1.2, 1.2, 0.5, 40);
        HintEngine hintEngine;
        for (int i = 0; i < std::min(20, N); ++i) {
            HintEngineInput inp;
            inp.event = events[i];
            inp.moMatches = moAnalyser.findSimilar(
                events[i].narrative.value_or(randomMO(6)), 5, 0.0);
            if (i >= 3) {
                QVector<QPair<double,double>> subLocs;
                for (int j = std::max(0, i-5); j < i; ++j)
                    subLocs.append({events[j].latitude, events[j].longitude});
                if (subLocs.size() >= 3)
                    inp.geoProfile = profiler.profile(subLocs);
            }
            auto leads = hintEngine.generate(inp);
            Q_UNUSED(leads)
        }

        const qint64 elapsed = t.elapsed();

        QVERIFY2(elapsed < 20000,
                 qPrintable(QStringLiteral("End-to-end pipeline 200 events took %1ms (limit 20000ms)").arg(elapsed)));
        qDebug("End-to-end pipeline 200 events: %lldms", elapsed);
    }
};

QTEST_MAIN(TestAllModelsPerf)
#include "test_all_models_perf.moc"
