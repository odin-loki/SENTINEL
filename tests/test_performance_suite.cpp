// test_performance_suite.cpp — Comprehensive performance regression suite for SENTINEL
// Uses QBENCHMARK for Qt benchmark reporting plus QElapsedTimer for hard time limits.
// Each benchmark generates realistic synthetic data and checks both timing and correctness.
#include <QTest>
#include <QCoreApplication>
#include <QDateTime>
#include <QElapsedTimer>
#include <QVector>
#include <QPair>
#include <QString>
#include <cmath>
#include <random>
#include <algorithm>

#include "core/CrimeEvent.h"
#include "core/Database.h"
#include "core/AppConfig.h"
#include "models/PoissonBaseline.h"
#include "models/HawkesProcess.h"
#include "models/SeriesDetector.h"
#include "models/KDEHotspot.h"
#include "models/EnsemblePredictor.h"
#include "benchmark/BenchmarkMetrics.h"
#include "nlp/MOExtractor.h"
#include "nlp/CrimeClassifier.h"

// ─────────────────────────────────────────────────────────────────────────────
// Deterministic helpers shared across all benchmarks
// ─────────────────────────────────────────────────────────────────────────────
namespace {

static std::mt19937 rng(9876);

static double randRange(double lo, double hi) {
    return std::uniform_real_distribution<double>(lo, hi)(rng);
}
static int randInt(int lo, int hi) {
    return std::uniform_int_distribution<int>(lo, hi)(rng);
}

static const QStringList kCrimeTypes = {
    "burglary", "theft", "robbery", "assault", "vandalism"
};
static const QStringList kSuburbs = {
    "Westminster", "Camden", "Islington", "Hackney", "Tower Hamlets",
    "Southwark", "Lambeth", "Wandsworth", "Hammersmith", "Kensington"
};
static const QStringList kMoWords = {
    "forced", "entry", "rear", "window", "lock", "pick", "gloves",
    "mask", "night", "dawn", "residential", "commercial", "vehicle",
    "cash", "jewellery", "electronics", "solo", "group", "armed", "crowbar"
};

// Realistic narrative templates that contain identifiable MO and crime-type keywords
static const QStringList kNarratives = {
    "Suspect forced entry through rear window using a crowbar at night. "
    "Residential property on Oak Street. Cash and jewellery taken. "
    "Solo offender wearing gloves and balaclava.",

    "Two suspects picked the lock on a commercial premises during daylight. "
    "Electronics including laptops and tablets stolen. "
    "Suspects wore hoodies and face masks.",

    "Vehicle broken into in car park. Window smashed with a hammer. "
    "GPS device and handbag taken from back seat. No suspects identified.",

    "Robbery at knifepoint near the train station at dawn. "
    "Victim's wallet and mobile phone taken by a group of three males. "
    "Armed with kitchen knife.",

    "Vandalism to residential property. Graffiti on front wall and windows smashed. "
    "Occurred overnight on a weekend. Gang activity suspected.",

    "Burglary via unlocked rear door in the early morning. "
    "Living room ransacked, television and gaming console taken. "
    "Occurred while occupants were at work.",

    "Shoplifting at convenience store during evening hours. "
    "Suspect concealed alcohol bottles under clothing. "
    "Detained by security but escaped through rear exit.",

    "Assault in city centre at night. Victim punched outside nightclub. "
    "Solo suspect fled on foot toward the commercial district. CCTV available.",

    "Bicycle theft from locked rack outside supermarket at dawn. "
    "Lock cut with bolt cutters. Mountain bike taken. No witnesses present.",

    "Residential burglary via forced entry on ground-floor window at night. "
    "Cash and jewellery stolen. Glove marks found. Solo offender suspected."
};

static QString randomNarrative() {
    return kNarratives[randInt(0, kNarratives.size() - 1)];
}

static QString randomMO(int wordCount = 8) {
    QStringList chosen;
    chosen.reserve(wordCount);
    for (int i = 0; i < wordCount; ++i)
        chosen << kMoWords[randInt(0, kMoWords.size() - 1)];
    return chosen.join(" ");
}

static QDateTime baseTime() {
    return QDateTime(QDate(2024, 1, 1), QTime(0, 0, 0), Qt::UTC);
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// TestPerformanceSuite
// ─────────────────────────────────────────────────────────────────────────────
class TestPerformanceSuite : public QObject {
    Q_OBJECT

private slots:

    // ── 1. Poisson fit — 1,000 events ─────────────────────────────────────────
    // Design target: ≤ 100ms   Safety margin: ≤ 200ms
    void benchPoissonFit_1000() {
        QVector<PoissonBaseline::EventRecord> events;
        events.reserve(1000);
        const QDateTime base = baseTime();
        for (int i = 0; i < 1000; ++i) {
            PoissonBaseline::EventRecord r;
            r.zoneId     = QStringLiteral("zone_%1").arg(randInt(0, 19));
            r.occurredAt = base.addSecs(randInt(0, 365 * 86400));
            r.crimeType  = kCrimeTypes[randInt(0, kCrimeTypes.size() - 1)];
            events.append(r);
        }

        // Hard timing check (one-shot run)
        PoissonBaseline model;
        QElapsedTimer t;
        t.start();
        model.fit(events);
        const qint64 elapsed = t.elapsed();

        qDebug("PoissonFit 1k events: %lld ms", elapsed);
        QVERIFY2(elapsed < 200,
            qPrintable(QStringLiteral("Poisson fit took %1 ms, limit 200 ms").arg(elapsed)));

        // Correctness
        QVERIFY(model.isFitted());
        QCOMPARE(model.totalEvents(), 1000);
        const auto pred = model.predict("zone_0", base.addDays(10), "burglary");
        QVERIFY(pred.lambda >= 0.0);
        QVERIFY(pred.probAtLeastOne >= 0.0 && pred.probAtLeastOne <= 1.0);
        QVERIFY(!pred.model.isEmpty());

        // Qt benchmark framework measurement
        QBENCHMARK {
            PoissonBaseline m;
            m.fit(events);
        }
    }

    // ── 2. Hawkes fit — 500 events ─────────────────────────────────────────────
    // Design target: ≤ 2500ms (50-iter coordinate descent)   Safety margin: ≤ 5000ms
    // Uses a local fixed-seed RNG so the same events are generated on every call
    // regardless of global RNG state — prevents the coordinate-descent optimizer
    // from visiting pathological data configurations across QBENCHMARK iterations.
    void benchHawkesFit_500() {
        std::mt19937 lrng(42);
        auto lR = [&](double lo, double hi) {
            return std::uniform_real_distribution<double>(lo, hi)(lrng);
        };
        auto lI = [&](int lo, int hi) {
            return std::uniform_int_distribution<int>(lo, hi)(lrng);
        };

        QVector<SpatiotemporalEvent> events;
        events.reserve(500);
        for (int i = 0; i < 500; ++i) {
            SpatiotemporalEvent e;
            e.tDays     = lR(0.0, 365.0);
            e.lat       = 51.5  + lR(-0.1, 0.1);
            e.lon       = -0.13 + lR(-0.1, 0.1);
            e.crimeType = kCrimeTypes[lI(0, kCrimeTypes.size() - 1)];
            events.append(e);
        }
        std::sort(events.begin(), events.end(),
                  [](const SpatiotemporalEvent& a, const SpatiotemporalEvent& b) {
                      return a.tDays < b.tDays;
                  });

        HawkesProcess model;
        QElapsedTimer t;
        t.start();
        model.fit(events, 50);
        const qint64 elapsed = t.elapsed();

        qDebug("HawkesFit 500 events (50 iter): %lld ms", elapsed);
        QVERIFY2(elapsed < 5000,
            qPrintable(QStringLiteral("Hawkes fit took %1 ms, limit 5000 ms").arg(elapsed)));

        // Correctness
        QVERIFY(model.isFitted());
        const auto& p = model.params();
        QVERIFY(p.mu    > 0.0);
        QVERIFY(p.alpha >= 0.0);
        QVERIFY(p.beta  > 0.0);
        QVERIFY(p.sigma > 0.0);

        // QBENCHMARK uses a reduced iteration count so each call takes ~100-300ms
        QBENCHMARK {
            HawkesProcess m;
            m.fit(events, 8);
        }
    }

    // ── 3. Series detection — 200 events ──────────────────────────────────────
    // Design target: ≤ 250ms   Safety margin: ≤ 500ms
    void benchSeriesDetection_200() {
        QVector<SeriesEvent> events;
        events.reserve(200);
        for (int i = 0; i < 200; ++i) {
            SeriesEvent e;
            e.eventId   = QStringLiteral("ev%1").arg(i);
            e.lat       = 51.5  + randRange(-0.05, 0.05);
            e.lon       = -0.13 + randRange(-0.05, 0.05);
            e.tDays     = randRange(0.0, 60.0);
            e.crimeType = kCrimeTypes[randInt(0, kCrimeTypes.size() - 1)];
            e.moText    = randomMO(6);
            events.append(e);
        }

        SeriesDetector detector(0.3, 14.0, 3);
        QElapsedTimer t;
        t.start();
        const QVector<CrimeSeries> series = detector.detectSeries(events);
        const qint64 elapsed = t.elapsed();

        qDebug("SeriesDetection 200 events: %lld ms, %d series found",
               elapsed, series.size());
        QVERIFY2(elapsed < 500,
            qPrintable(QStringLiteral("Series detection took %1 ms, limit 500 ms").arg(elapsed)));

        // Correctness: every detected series meets the minSamples constraint
        for (const auto& s : series) {
            QVERIFY(s.members.size() >= 3);
            QVERIFY(!s.seriesId.isEmpty());
            // Centroid must be within the data's spatial extent
            QVERIFY(s.centroidLat > 51.0 && s.centroidLat < 52.0);
            QVERIFY(s.centroidLon > -1.0 && s.centroidLon < 0.0);
        }

        QBENCHMARK {
            SeriesDetector det(0.3, 14.0, 3);
            det.detectSeries(events);
        }
    }

    // ── 4. KDE surface — 1,000 locations, 50×50 grid ──────────────────────────
    // Design target: ≤ 250ms   Safety margin: ≤ 500ms
    void benchKDE_1000() {
        QVector<QPair<double, double>> locations;
        locations.reserve(1000);
        for (int i = 0; i < 1000; ++i)
            locations.append({ 51.5  + randRange(-0.08, 0.08),
                               -0.13 + randRange(-0.08, 0.08) });

        constexpr double latMin = 51.42, latMax = 51.58;
        constexpr double lonMin = -0.21, lonMax = -0.05;
        KDEHotspot kde(50, 1.0);

        QElapsedTimer t;
        t.start();
        const auto surface = kde.compute(locations, latMin, latMax, lonMin, lonMax);
        const qint64 elapsed = t.elapsed();

        qDebug("KDE 1000 locations, 50×50 grid: %lld ms", elapsed);
        QVERIFY2(elapsed < 500,
            qPrintable(QStringLiteral("KDE compute took %1 ms, limit 500 ms").arg(elapsed)));

        // Correctness: correct dimensions, non-negative, non-trivial variation
        QCOMPARE(static_cast<int>(surface.size()), 50);
        QCOMPARE(static_cast<int>(surface[0].size()), 50);
        double totalMass = 0.0, maxCell = 0.0;
        for (const auto& row : surface)
            for (double v : row) {
                QVERIFY(v >= 0.0);
                totalMass += v;
                if (v > maxCell) maxCell = v;
            }
        QVERIFY2(totalMass > 0.0, "KDE surface has zero total mass");
        QVERIFY2(maxCell   > 0.0, "KDE surface peak is zero");

        QBENCHMARK {
            kde.compute(locations, latMin, latMax, lonMin, lonMax);
        }
    }

    // ── 5. Poisson predict — 10,000 zone predictions ──────────────────────────
    // Design target: ≤ 500ms   Safety margin: ≤ 1000ms
    void benchPoissonPredict_10000() {
        // Fit on 2000 training events across 10 zones
        QVector<PoissonBaseline::EventRecord> events;
        events.reserve(2000);
        const QDateTime base = baseTime();
        for (int i = 0; i < 2000; ++i) {
            PoissonBaseline::EventRecord r;
            r.zoneId     = QStringLiteral("zone_%1").arg(randInt(0, 9));
            r.occurredAt = base.addSecs(randInt(0, 180 * 86400));
            r.crimeType  = kCrimeTypes[randInt(0, kCrimeTypes.size() - 1)];
            events.append(r);
        }
        PoissonBaseline model;
        model.fit(events);
        QVERIFY(model.isFitted());

        const QDateTime queryDt = base.addDays(200);

        // Time 10,000 predictions
        QElapsedTimer t;
        t.start();
        double probSum = 0.0;
        for (int i = 0; i < 10000; ++i) {
            const QString zone = QStringLiteral("zone_%1").arg(randInt(0, 9));
            const QString ct   = kCrimeTypes[randInt(0, kCrimeTypes.size() - 1)];
            const auto    pred = model.predict(zone, queryDt, ct);
            probSum += pred.probAtLeastOne;
        }
        const qint64 elapsed = t.elapsed();

        const double avgProb = probSum / 10000.0;
        qDebug("PoissonPredict 10k: %lld ms  avg_prob=%.4f", elapsed, avgProb);
        QVERIFY2(elapsed < 1000,
            qPrintable(QStringLiteral("Poisson predict took %1 ms, limit 1000 ms").arg(elapsed)));
        // Non-trivial: well-trained model should produce positive expected rates
        QVERIFY2(avgProb > 0.0, "All predictions returned zero probability");
        QVERIFY2(avgProb <= 1.0, "Average probability exceeds 1.0");

        QBENCHMARK {
            const auto pred = model.predict("zone_0", queryDt, "burglary");
            Q_UNUSED(pred);
        }
    }

    // ── 6. Ensemble predict — 1,000 calls ─────────────────────────────────────
    // Design target: ≤ 1000ms   Safety margin: ≤ 2000ms
    void benchEnsemblePredict_1000() {
        const QDateTime base = baseTime();

        // Local RNG for Hawkes data — prevents optimizer from visiting slow regions
        // on repeated QBENCHMARK iterations due to global RNG state drift.
        std::mt19937 lrng(77);
        auto lR = [&](double lo, double hi) {
            return std::uniform_real_distribution<double>(lo, hi)(lrng);
        };
        auto lI = [&](int lo, int hi) {
            return std::uniform_int_distribution<int>(lo, hi)(lrng);
        };

        // Fit Poisson model
        QVector<PoissonBaseline::EventRecord> poissonEvents;
        poissonEvents.reserve(1000);
        for (int i = 0; i < 1000; ++i) {
            PoissonBaseline::EventRecord r;
            r.zoneId     = QStringLiteral("zone_%1").arg(lI(0, 9));
            r.occurredAt = base.addSecs(lI(0, 180 * 86400));
            r.crimeType  = kCrimeTypes[lI(0, kCrimeTypes.size() - 1)];
            poissonEvents.append(r);
        }
        PoissonBaseline poisson;
        poisson.fit(poissonEvents);

        // Fit Hawkes model (small: 150 events, 30 iterations to keep setup fast)
        QVector<SpatiotemporalEvent> hawkesEvents;
        hawkesEvents.reserve(150);
        for (int i = 0; i < 150; ++i) {
            SpatiotemporalEvent e;
            e.tDays     = lR(0.0, 180.0);
            e.lat       = 51.5  + lR(-0.08, 0.08);
            e.lon       = -0.13 + lR(-0.08, 0.08);
            e.crimeType = kCrimeTypes[lI(0, kCrimeTypes.size() - 1)];
            hawkesEvents.append(e);
        }
        std::sort(hawkesEvents.begin(), hawkesEvents.end(),
                  [](const SpatiotemporalEvent& a, const SpatiotemporalEvent& b) {
                      return a.tDays < b.tDays;
                  });
        HawkesProcess hawkes;
        hawkes.fit(hawkesEvents, 30);

        EnsemblePredictor ensemble;
        ensemble.setPoisson(&poisson);
        ensemble.setHawkes(&hawkes);
        ensemble.setWeights(0.6, 0.4);

        const QDateTime queryDt = base.addDays(200);

        QElapsedTimer t;
        t.start();
        double totalProb = 0.0;
        for (int i = 0; i < 1000; ++i) {
            const QString zone = QStringLiteral("zone_%1").arg(randInt(0, 9));
            const QString ct   = kCrimeTypes[randInt(0, kCrimeTypes.size() - 1)];
            const double  lat  = 51.5  + randRange(-0.08, 0.08);
            const double  lon  = -0.13 + randRange(-0.08, 0.08);
            const auto    pred = ensemble.predict(zone, queryDt, ct, lat, lon);
            totalProb += pred.probCrime;
        }
        const qint64 elapsed = t.elapsed();

        const double avgProb = totalProb / 1000.0;
        qDebug("EnsemblePredict 1k: %lld ms  avg_prob=%.4f", elapsed, avgProb);
        QVERIFY2(elapsed < 2000,
            qPrintable(QStringLiteral("Ensemble predict took %1 ms, limit 2000 ms").arg(elapsed)));
        QVERIFY2(avgProb > 0.0, "All ensemble predictions returned zero probability");
        QVERIFY2(avgProb <= 1.0, "Average ensemble probability exceeds 1.0");

        QBENCHMARK {
            const auto pred = ensemble.predict("zone_0", queryDt, "burglary",
                                               51.5, -0.13);
            Q_UNUSED(pred);
        }
    }

    // ── 7. Database insert — 5,000 events ─────────────────────────────────────
    // Design target: ≤ 2500ms   Safety margin: ≤ 5000ms
    void benchDBInsert_5000() {
        AppConfig cfg;
        cfg.databasePath = ":memory:";
        Database db(cfg);
        QVERIFY(db.open());

        const QDateTime base = baseTime();

        // Build the event vector once (not timed)
        QVector<CrimeEvent> events;
        events.reserve(5000);
        for (int i = 0; i < 5000; ++i) {
            CrimeEvent ev;
            ev.eventId      = ev.id = QStringLiteral("ev_%1").arg(i);
            ev.crimeType    = kCrimeTypes[randInt(0, kCrimeTypes.size() - 1)];
            const QDateTime dt = base.addSecs(randInt(0, 365 * 86400));
            ev.occurredAt   = dt;
            ev.timestamp    = dt;
            ev.lat = ev.latitude  = 51.5  + randRange(-0.1, 0.1);
            ev.lon = ev.longitude = -0.13 + randRange(-0.1, 0.1);
            ev.qualityScore = randRange(0.5, 1.0);
            ev.suburb       = kSuburbs[randInt(0, kSuburbs.size() - 1)];
            ev.outcome      = "unknown";
            events.append(ev);
        }

        QElapsedTimer t;
        t.start();
        for (const auto& ev : events)
            QVERIFY(db.insertEvent(ev));
        const qint64 elapsed = t.elapsed();

        const double rate = (elapsed > 0) ? 5000.0 / (elapsed / 1000.0) : 99999.0;
        qDebug("DBInsert 5000 events: %lld ms  (%.0f inserts/sec)", elapsed, rate);
        QVERIFY2(elapsed < 5000,
            qPrintable(QStringLiteral("DB insert took %1 ms, limit 5000 ms").arg(elapsed)));
        QCOMPARE(db.eventCount(), 5000);

        // Spot-check one arbitrary event for correct round-trip
        const auto retrieved = db.eventById("ev_42");
        QCOMPARE(retrieved.eventId, QString("ev_42"));
        QCOMPARE(retrieved.crimeType, events[42].crimeType);

        // QBENCHMARK: time a single insert into the already-populated table
        int insertSeq = 0;
        QBENCHMARK {
            CrimeEvent bench;
            bench.eventId = bench.id = QStringLiteral("bench_ins_%1").arg(++insertSeq);
            bench.crimeType = "theft";
            const QDateTime dt = base.addSecs(1000 + insertSeq);
            bench.occurredAt = dt;
            bench.timestamp  = dt;
            bench.lat = bench.latitude  = 51.5;
            bench.lon = bench.longitude = -0.13;
            bench.qualityScore = 0.8;
            bench.suburb  = "Westminster";
            bench.outcome = "unknown";
            db.insertEvent(bench);
        }
    }

    // ── 8. Database query by crime type — after 5,000 inserts ─────────────────
    // Design target: ≤ 100ms   Safety margin: ≤ 200ms
    void benchDBQuery_byType() {
        AppConfig cfg;
        cfg.databasePath = ":memory:";
        Database db(cfg);
        QVERIFY(db.open());

        const QDateTime base = baseTime();

        // Setup: insert 5,000 events (not timed)
        for (int i = 0; i < 5000; ++i) {
            CrimeEvent ev;
            ev.eventId      = ev.id = QStringLiteral("ev_%1").arg(i);
            ev.crimeType    = kCrimeTypes[randInt(0, kCrimeTypes.size() - 1)];
            const QDateTime dt = base.addSecs(randInt(0, 365 * 86400));
            ev.occurredAt   = dt;
            ev.timestamp    = dt;
            ev.lat = ev.latitude  = 51.5  + randRange(-0.1, 0.1);
            ev.lon = ev.longitude = -0.13 + randRange(-0.1, 0.1);
            ev.qualityScore = randRange(0.5, 1.0);
            ev.suburb       = kSuburbs[randInt(0, kSuburbs.size() - 1)];
            ev.outcome      = "unknown";
            db.insertEvent(ev);
        }
        QCOMPARE(db.eventCount(), 5000);

        // Time the filtered query
        QElapsedTimer t;
        t.start();
        const QVector<CrimeEvent> results = db.queryEvents(
            "burglary", QDateTime{}, QDateTime{},
            -90.0, 90.0, -180.0, 180.0, 5000);
        const qint64 elapsed = t.elapsed();

        qDebug("DBQuery crimeType='burglary' from 5k events: %lld ms, %d rows",
               elapsed, results.size());
        QVERIFY2(elapsed < 200,
            qPrintable(QStringLiteral("DB query took %1 ms, limit 200 ms").arg(elapsed)));

        // Correctness: every returned event must be the queried type
        QVERIFY2(results.size() > 0, "No burglary events found (unexpected with 5k inserts)");
        for (const auto& ev : results)
            QCOMPARE(ev.crimeType, QString("burglary"));

        QBENCHMARK {
            const auto r = db.queryEvents("theft", QDateTime{}, QDateTime{},
                                          -90.0, 90.0, -180.0, 180.0, 5000);
            Q_UNUSED(r);
        }
    }

    // ── 9. MOExtract — 500 narratives ─────────────────────────────────────────
    // Design target: ≤ 1000ms   Safety margin: ≤ 2000ms
    void benchMOExtract_500() {
        QVector<QString> narratives;
        narratives.reserve(500);
        for (int i = 0; i < 500; ++i)
            narratives.append(randomNarrative());

        MOExtractor extractor;
        QElapsedTimer t;
        t.start();
        int nonEmptyCount = 0;
        for (const auto& text : narratives) {
            const MOFeatures feat = extractor.extract(text);
            if (feat.entryMethod.has_value() || feat.targetType.has_value() ||
                feat.timeOfDay.has_value() || feat.soloOrGroup.has_value())
                ++nonEmptyCount;
        }
        const qint64 elapsed = t.elapsed();

        qDebug("MOExtract 500 narratives: %lld ms  (%d/%d non-empty)",
               elapsed, nonEmptyCount, 500);
        QVERIFY2(elapsed < 2000,
            qPrintable(QStringLiteral("MO extract took %1 ms, limit 2000 ms").arg(elapsed)));

        // Correctness: realistic narratives must trigger at least some feature extraction
        QVERIFY2(nonEmptyCount > 100,
            qPrintable(QStringLiteral("Only %1 non-empty feature sets from 500 narratives")
                        .arg(nonEmptyCount)));

        QBENCHMARK {
            const MOFeatures feat = extractor.extract(narratives[0]);
            Q_UNUSED(feat);
        }
    }

    // ── 10. CrimeClassify — 500 narratives ────────────────────────────────────
    // Design target: ≤ 500ms   Safety margin: ≤ 1000ms
    void benchCrimeClassify_500() {
        QVector<QString> narratives;
        narratives.reserve(500);
        for (int i = 0; i < 500; ++i)
            narratives.append(randomNarrative());

        CrimeClassifier classifier;
        QElapsedTimer t;
        t.start();
        int confidentCount = 0;
        for (const auto& text : narratives) {
            const auto [ct, conf] = classifier.classify(text);
            if (conf > 0.3) ++confidentCount;
        }
        const qint64 elapsed = t.elapsed();

        qDebug("CrimeClassify 500 narratives: %lld ms  (%d confident)",
               elapsed, confidentCount);
        QVERIFY2(elapsed < 1000,
            qPrintable(QStringLiteral("Crime classify took %1 ms, limit 1000 ms").arg(elapsed)));

        // Correctness: template narratives include clear crime-type keywords
        QVERIFY2(confidentCount > 200,
            qPrintable(QStringLiteral("Only %1 confident classifications from 500 narratives")
                        .arg(confidentCount)));

        QBENCHMARK {
            const auto [ct, conf] = classifier.classify(narratives[0]);
            Q_UNUSED(ct);
            Q_UNUSED(conf);
        }
    }

    // ── 11. BenchmarkMetrics::fullReport — 10,000 samples ─────────────────────
    // Design target: ≤ 500ms   Safety margin: ≤ 1000ms
    void benchFullReport_10000() {
        // Generate correlated synthetic data: P(actual=1) = 0.3 + 0.5 * pred
        // This ensures AUC > 0.5 (non-trivial outcome correlation)
        QVector<double> yTrue, yPred;
        yTrue.reserve(10000);
        yPred.reserve(10000);
        for (int i = 0; i < 10000; ++i) {
            const double pred    = randRange(0.05, 0.95);
            const double pActual = 0.3 + 0.5 * pred;
            const double actual  = (randRange(0.0, 1.0) < pActual) ? 1.0 : 0.0;
            yPred.append(pred);
            yTrue.append(actual);
        }

        QElapsedTimer t;
        t.start();
        const BenchmarkReport report = BenchmarkMetrics::fullReport(yTrue, yPred);
        const qint64 elapsed = t.elapsed();

        qDebug("BenchmarkMetrics::fullReport 10k: %lld ms"
               "  AUC-ROC=%.4f  PAI5%%=%.4f  RMSE=%.4f  Brier=%.4f",
               elapsed, report.aucRoc, report.pai5pct, report.rmse, report.brierScore);
        QVERIFY2(elapsed < 1000,
            qPrintable(QStringLiteral("fullReport took %1 ms, limit 1000 ms").arg(elapsed)));

        // Correctness: correlated data must produce informative metrics
        QCOMPARE(report.nSamples, 10000);
        QVERIFY2(report.aucRoc > 0.5,
            qPrintable(QStringLiteral("AUC-ROC = %1 (expected > 0.5 for correlated data)")
                        .arg(report.aucRoc)));
        QVERIFY(report.aucPr       > 0.0);
        QVERIFY(report.rmse        > 0.0 && report.rmse        < 1.0);
        QVERIFY(report.mae         > 0.0 && report.mae         < 1.0);
        QVERIFY(report.brierScore  > 0.0 && report.brierScore  < 1.0);
        QVERIFY(report.pai5pct     >= 0.0);
        QVERIFY(report.ser         >= 0.0);

        QBENCHMARK {
            BenchmarkMetrics::fullReport(yTrue, yPred);
        }
    }
};

// ─────────────────────────────────────────────────────────────────────────────
QTEST_MAIN(TestPerformanceSuite)
#include "test_performance_suite.moc"
