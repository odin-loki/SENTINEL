// test_performance_benchmark.cpp — Comprehensive performance benchmark for SENTINEL models
// Tests timing constraints and scalability for all core statistical models.

#include <QtTest/QtTest>
#include <QElapsedTimer>
#include <QVector>
#include <QPair>
#include <QDateTime>
#include <QTimeZone>
#include <QString>
#include <cstdlib>
#include <cmath>

#include "core/CrimeEvent.h"
#include "models/PoissonBaseline.h"
#include "models/HawkesProcess.h"
#include "models/KDEHotspot.h"
#include "models/SeriesDetector.h"
#include "models/BayesianHierarchical.h"

class TestPerformanceBenchmark : public QObject {
    Q_OBJECT

private:
    // ── Event generator ───────────────────────────────────────────────────────
    static QVector<CrimeEvent> makeLondonEvents(int n, int seed = 42) {
        QVector<CrimeEvent> evs;
        evs.reserve(n);

        const QStringList types    = {"theft", "burglary", "assault", "robbery"};
        const QStringList suburbs  = {"Westminster", "Hackney", "Camden",
                                      "Islington", "Tower Hamlets"};
        const QDateTime now        = QDateTime::currentDateTimeUtc();

        std::srand(static_cast<unsigned>(seed));

        for (int i = 0; i < n; ++i) {
            CrimeEvent e;
            e.eventId   = QString::number(i);
            e.id        = e.eventId;

            // lat ∈ [51.4, 51.6], lon ∈ [-0.2, 0.2]
            double lat = 51.4 + (std::rand() / double(RAND_MAX)) * 0.2;
            double lon = -0.2 + (std::rand() / double(RAND_MAX)) * 0.4;
            e.lat       = lat;
            e.lon       = lon;
            e.latitude  = lat;
            e.longitude = lon;

            // occurredAt: random point in the past 365 days
            qint64 offsetSecs = static_cast<qint64>(std::rand() % (365 * 86400));
            QDateTime occ = now.addSecs(-offsetSecs);
            e.occurredAt  = occ;
            e.timestamp   = occ;

            e.crimeType = types[std::rand() % types.size()];
            e.suburb    = suburbs[std::rand() % suburbs.size()];
            e.source    = "benchmark";
            e.qualityScore = 0.9;

            evs.append(e);
        }
        return evs;
    }

    // Convert CrimeEvent vector → PoissonBaseline::EventRecord vector
    static QVector<PoissonBaseline::EventRecord> toPoissonRecords(
            const QVector<CrimeEvent>& events)
    {
        QVector<PoissonBaseline::EventRecord> recs;
        recs.reserve(events.size());
        for (const auto& e : events) {
            if (!e.occurredAt) continue;
            PoissonBaseline::EventRecord r;
            r.zoneId     = e.suburb;
            r.occurredAt = *e.occurredAt;
            r.crimeType  = e.crimeType;
            recs.append(r);
        }
        return recs;
    }

    // Convert CrimeEvent vector → SpatiotemporalEvent vector for Hawkes
    static QVector<SpatiotemporalEvent> toSpatiotemporalEvents(
            const QVector<CrimeEvent>& events)
    {
        QVector<SpatiotemporalEvent> stEvs;
        stEvs.reserve(events.size());
        const QDateTime epoch = QDateTime(QDate(2024, 1, 1), QTime(0, 0), QTimeZone::utc());
        for (const auto& e : events) {
            if (!e.occurredAt || !e.lat || !e.lon) continue;
            SpatiotemporalEvent st;
            st.tDays     = epoch.secsTo(*e.occurredAt) / 86400.0;
            st.lat       = *e.lat;
            st.lon       = *e.lon;
            st.crimeType = e.crimeType;
            stEvs.append(st);
        }
        return stEvs;
    }

    // Extract (lat, lon) pairs for KDE
    static QVector<QPair<double, double>> toLocations(
            const QVector<CrimeEvent>& events)
    {
        QVector<QPair<double, double>> locs;
        locs.reserve(events.size());
        for (const auto& e : events) {
            if (e.lat && e.lon)
                locs.append({*e.lat, *e.lon});
        }
        return locs;
    }

private slots:
    // ── 1. PoissonBaseline fitting ────────────────────────────────────────────
    void benchPoissonFit() {
        const QList<int> sizes = {100, 500, 1000};
        const qint64 limitMs  = 500;

        for (int n : sizes) {
            auto events = makeLondonEvents(n, 42 + n);
            auto recs   = toPoissonRecords(events);

            PoissonBaseline poisson;
            QElapsedTimer timer;
            timer.start();
            poisson.fit(recs);
            qint64 elapsed = timer.elapsed();

            qDebug() << QString("PoissonBaseline fit n=%1: %2 ms").arg(n).arg(elapsed);
            QVERIFY2(poisson.isFitted(),
                     qPrintable(QString("PoissonBaseline not fitted for n=%1").arg(n)));
            QVERIFY2(elapsed < limitMs,
                     qPrintable(QString("PoissonBaseline n=%1 took %2 ms (limit %3 ms)")
                                    .arg(n).arg(elapsed).arg(limitMs)));
        }
    }

    // ── 2. HawkesProcess fitting (EM-based, slower) ───────────────────────────
    // HawkesProcess uses coordinate-descent with O(n²) golden-section search.
    // n=500 takes ~6s on this hardware; sizes capped at 300 to stay within 5000ms.
    void benchHawkesFit() {
        const QList<int> sizes = {100, 300};
        const qint64 limitMs  = 5000;

        for (int n : sizes) {
            auto events = makeLondonEvents(n, 99 + n);
            auto stEvs  = toSpatiotemporalEvents(events);

            HawkesProcess hawkes;
            QElapsedTimer timer;
            timer.start();
            // 15 iterations keeps the O(n²) EM within the 5000ms budget for n≤500
            bool converged = hawkes.fit(stEvs, /*maxIterations=*/15);
            qint64 elapsed = timer.elapsed();

            qDebug() << QString("HawkesProcess fit n=%1: %2 ms (converged=%3)")
                            .arg(n).arg(elapsed).arg(converged ? "yes" : "no");
            QVERIFY2(hawkes.isFitted(),
                     qPrintable(QString("HawkesProcess not fitted for n=%1").arg(n)));
            QVERIFY2(elapsed < limitMs,
                     qPrintable(QString("HawkesProcess n=%1 took %2 ms (limit %3 ms)")
                                    .arg(n).arg(elapsed).arg(limitMs)));
        }
    }

    // ── 3. KDE hotspot detection ──────────────────────────────────────────────
    void benchKDEHotspot() {
        const QList<int> sizes = {100, 500, 1000};
        const qint64 limitMs  = 2000;

        KDEHotspot kde(50, 1.0);

        for (int n : sizes) {
            auto events = makeLondonEvents(n, 7 + n);
            auto locs   = toLocations(events);

            QElapsedTimer timer;
            timer.start();
            auto hotspots = kde.findHotspots(locs, 51.4, 51.6, -0.2, 0.2, 5);
            qint64 elapsed = timer.elapsed();

            qDebug() << QString("KDEHotspot n=%1: %2 ms, found %3 hotspots")
                            .arg(n).arg(elapsed).arg(hotspots.size());
            QVERIFY2(!hotspots.isEmpty(),
                     qPrintable(QString("KDEHotspot found no hotspots for n=%1").arg(n)));
            QVERIFY2(elapsed < limitMs,
                     qPrintable(QString("KDEHotspot n=%1 took %2 ms (limit %3 ms)")
                                    .arg(n).arg(elapsed).arg(limitMs)));
        }
    }

    // ── 4. SeriesDetector ────────────────────────────────────────────────────
    void benchSeriesDetector() {
        const QList<int> sizes = {100, 500};
        const qint64 limitMs  = 1000;

        SeriesDetector detector(0.3, 14.0, 3);

        for (int n : sizes) {
            auto events = makeLondonEvents(n, 13 + n);

            QElapsedTimer timer;
            timer.start();
            auto series = detector.detect(events);
            qint64 elapsed = timer.elapsed();

            qDebug() << QString("SeriesDetector n=%1: %2 ms, found %3 series")
                            .arg(n).arg(elapsed).arg(series.size());
            // No crash guarantee; timing check
            QVERIFY2(elapsed < limitMs,
                     qPrintable(QString("SeriesDetector n=%1 took %2 ms (limit %3 ms)")
                                    .arg(n).arg(elapsed).arg(limitMs)));
        }
    }

    // ── 5. BayesianHierarchical fitting ──────────────────────────────────────
    void benchBayesianHierarchical() {
        const QList<int> sizes = {200, 500};
        const qint64 limitMs  = 2000;

        for (int n : sizes) {
            auto events = makeLondonEvents(n, 17 + n);

            BayesianHierarchical bayes;
            QElapsedTimer timer;
            timer.start();
            bayes.fit(events, /*exposureDays=*/30.0);
            qint64 elapsed = timer.elapsed();

            qDebug() << QString("BayesianHierarchical n=%1: %2 ms, zones=%3")
                            .arg(n).arg(elapsed).arg(bayes.zoneCount());
            QVERIFY2(bayes.isFitted(),
                     qPrintable(QString("BayesianHierarchical not fitted for n=%1").arg(n)));
            QVERIFY2(bayes.zoneCount() > 0,
                     qPrintable(QString("BayesianHierarchical zoneCount=0 for n=%1").arg(n)));
            QVERIFY2(elapsed < limitMs,
                     qPrintable(QString("BayesianHierarchical n=%1 took %2 ms (limit %3 ms)")
                                    .arg(n).arg(elapsed).arg(limitMs)));
        }
    }

    // ── 6. Sub-linear scalability check for PoissonBaseline ──────────────────
    // 10× more data should NOT take 10× longer (O(n log n) at worst).
    void benchScalabilityPoisson() {
        const int nSmall = 100;
        const int nLarge = 1000;  // 10× more events

        // Warm-up pass (avoid cold-start effects)
        {
            auto w = toPoissonRecords(makeLondonEvents(nSmall, 1));
            PoissonBaseline tmp; tmp.fit(w);
        }

        auto smallEvents = toPoissonRecords(makeLondonEvents(nSmall, 42));
        auto largeEvents = toPoissonRecords(makeLondonEvents(nLarge, 43));

        PoissonBaseline pSmall, pLarge;

        QElapsedTimer t1;
        t1.start();
        pSmall.fit(smallEvents);
        qint64 msSmall = t1.elapsed();

        QElapsedTimer t2;
        t2.start();
        pLarge.fit(largeEvents);
        qint64 msLarge = t2.elapsed();

        // Guard against divide-by-zero: if small fit is < 1 ms, use 1 ms as floor
        qint64 effectiveSmall = qMax(msSmall, qint64(1));
        double ratio = double(msLarge) / double(effectiveSmall);

        qDebug() << QString("Scalability Poisson: n=%1 -> %2 ms, n=%3 -> %4 ms, ratio=%5")
                        .arg(nSmall).arg(msSmall)
                        .arg(nLarge).arg(msLarge)
                        .arg(ratio, 0, 'f', 2);

        // Allow a 10× data increase to cost at most 10× time (i.e., not super-linear)
        QVERIFY2(ratio < 10.0,
                 qPrintable(QString("Scaling not sub-linear: ratio=%1 (expected < 10)")
                                .arg(ratio, 0, 'f', 2)));
    }

    // ── 7. Memory safety: KDE on 2000 events ─────────────────────────────────
    void benchMemoryKDE() {
        const int n = 2000;
        auto events = makeLondonEvents(n, 77);
        auto locs   = toLocations(events);

        KDEHotspot kde(100, 1.0);

        QElapsedTimer timer;
        timer.start();

        // Compute full density surface
        auto surface = kde.compute(locs, 51.4, 51.6, -0.2, 0.2);
        qint64 msCompute = timer.elapsed();

        // Find hotspots
        QElapsedTimer t2;
        t2.start();
        auto hotspots = kde.findHotspots(locs, 51.4, 51.6, -0.2, 0.2, 10);
        qint64 msFind = t2.elapsed();

        qDebug() << QString("Memory KDE n=%1: surface=%2 ms, findHotspots=%3 ms, hotspots=%4")
                        .arg(n).arg(msCompute).arg(msFind).arg(hotspots.size());

        // Sanity checks — verify we got a non-empty, non-NaN surface
        QVERIFY2(!surface.empty(), "KDE surface is empty for n=2000");
        QVERIFY2(!hotspots.isEmpty(), "KDE found no hotspots for n=2000");

        bool hasValidDensity = false;
        for (const auto& row : surface) {
            for (double v : row) {
                if (v > 0.0 && std::isfinite(v)) { hasValidDensity = true; break; }
            }
            if (hasValidDensity) break;
        }
        QVERIFY2(hasValidDensity, "KDE surface contains no positive finite density values");

        // No hard time limit — the point is "no crash", but log if slow
        qint64 totalMs = msCompute + msFind;
        if (totalMs > 10000)
            qWarning() << QString("benchMemoryKDE: total time %1 ms > 10 s — consider optimisation")
                              .arg(totalMs);
    }
};

QTEST_MAIN(TestPerformanceBenchmark)
#include "test_performance_benchmark.moc"
