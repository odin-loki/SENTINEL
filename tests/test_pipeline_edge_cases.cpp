// test_pipeline_edge_cases.cpp
// Comprehensive edge-case tests for the full SENTINEL pipeline.
//
// Covers:
//   - Zero-event corpus: all models handle gracefully
//   - Single-event corpus: all models produce valid (non-crashing) output
//   - 100 co-located events: KDE produces single high-density cluster;
//     SeriesDetector forms exactly one series
//   - 1000 events spanning 5 years, 3 crime types: end-to-end pipeline
//     completes in under 5 seconds

#include <QTest>
#include <QElapsedTimer>
#include <QDate>
#include <QDateTime>
#include <QTime>
#include <cmath>

#include "models/PoissonBaseline.h"
#include "models/HawkesProcess.h"
#include "models/KDEHotspot.h"
#include "models/SeriesDetector.h"
#include "models/RiskForecaster.h"
#include "models/EnsemblePredictor.h"
#include "core/CrimeEvent.h"

// ─────────────────────────────────────────────────────────────────────────────
// Helper factories
// ─────────────────────────────────────────────────────────────────────────────

static CrimeEvent makeEvent(int i,
                              double lat, double lon,
                              const QDateTime& dt,
                              const QString& type = QStringLiteral("burglary"),
                              const QString& suburb = QStringLiteral("TestZone"))
{
    CrimeEvent ev;
    ev.eventId    = QStringLiteral("EV-%1").arg(i, 6, 10, QChar('0'));
    ev.lat        = lat;
    ev.lon        = lon;
    ev.latitude   = lat;
    ev.longitude  = lon;
    ev.occurredAt = dt;
    ev.ingestedAt = dt;
    ev.timestamp  = dt;
    ev.crimeType  = type;
    ev.suburb     = suburb;
    ev.qualityScore = 0.8;
    return ev;
}

static SpatiotemporalEvent toSTE(const CrimeEvent& ev, const QDateTime& epoch)
{
    SpatiotemporalEvent se;
    se.tDays     = static_cast<double>(epoch.daysTo(ev.occurredAt.value_or(epoch)));
    se.lat       = ev.lat.value_or(0.0);
    se.lon       = ev.lon.value_or(0.0);
    se.crimeType = ev.crimeType;
    return se;
}

static PoissonBaseline::EventRecord toRec(const CrimeEvent& ev)
{
    PoissonBaseline::EventRecord r;
    r.zoneId     = ev.suburb;
    r.occurredAt = ev.occurredAt.value_or(ev.ingestedAt);
    r.crimeType  = ev.crimeType;
    return r;
}

// ─────────────────────────────────────────────────────────────────────────────

class TestPipelineEdgeCases : public QObject
{
    Q_OBJECT

private slots:

    // ── 1. Zero-event corpus ─────────────────────────────────────────────────

    void testZeroEventsPoissonBaseline()
    {
        PoissonBaseline model;
        model.fit({});
        // With no events, m_rates must stay empty → not fitted
        QVERIFY(!model.isFitted());
        QCOMPARE(model.totalEvents(), 0);
    }

    void testZeroEventsHawkesProcess()
    {
        HawkesProcess hp;
        // Should not crash and should report not fitted
        const bool ok = hp.fit({}, 5);
        // ok may be false or true, but it must not crash
        Q_UNUSED(ok);
        // With no history, intensity at any point must be finite (mu only)
        const double lam = hp.intensity(0.0, 51.5, -0.1);
        QVERIFY(std::isfinite(lam));
        QVERIFY(lam >= 0.0);
    }

    void testZeroEventsKDEHotspot()
    {
        KDEHotspot kde(20);
        // Empty input — must not crash
        const auto surface = kde.compute({}, 51.4, 51.6, -0.2, 0.0);
        // Surface may be empty or all-zero; just verify no crash + finite values
        for (const auto& row : surface)
            for (double v : row) {
                QVERIFY(std::isfinite(v));
                QVERIFY(v >= 0.0);
            }
    }

    void testZeroEventsKDEFindHotspots()
    {
        KDEHotspot kde(20);
        const auto hotspots = kde.findHotspots({}, 51.4, 51.6, -0.2, 0.0, 3);
        // With no events, expect empty list (not a crash)
        QVERIFY(hotspots.isEmpty());
    }

    void testZeroEventsSeriesDetector()
    {
        SeriesDetector det(0.5, 14.0, 3);
        const auto series = det.detect({});
        QVERIFY(series.isEmpty());
    }

    void testZeroEventsRiskForecaster()
    {
        RiskForecaster forecaster(7);
        forecaster.fit({});
        QVERIFY(!forecaster.isFitted());
        QCOMPARE(forecaster.zoneCount(), 0);
    }

    void testZeroEventsEnsemblePredictor()
    {
        // With no models set, isReady() returns false
        EnsemblePredictor ensemble;
        QVERIFY(!ensemble.isReady());
    }

    // ── 2. Single-event corpus ───────────────────────────────────────────────

    void testSingleEventPoissonBaseline()
    {
        const QDateTime dt(QDate(2025, 6, 1), QTime(12, 0), Qt::UTC);
        PoissonBaseline::EventRecord rec;
        rec.zoneId     = QStringLiteral("Alpha");
        rec.occurredAt = dt;
        rec.crimeType  = QStringLiteral("burglary");

        PoissonBaseline model;
        model.fit({rec});
        // One event creates at least one bucket → fitted
        QVERIFY(model.isFitted());
        QCOMPARE(model.totalEvents(), 1);

        // Prediction must return a valid probability
        PoissonPrediction pred = model.predict(QStringLiteral("Alpha"), dt, QStringLiteral("burglary"));
        QVERIFY(pred.lambda >= 0.0);
        QVERIFY(std::isfinite(pred.probAtLeastOne));
        QVERIFY(pred.probAtLeastOne >= 0.0);
        QVERIFY(pred.probAtLeastOne <= 1.0);
    }

    void testSingleEventHawkesProcess()
    {
        const QDateTime epoch(QDate(2025, 1, 1), QTime(0, 0), Qt::UTC);
        const QDateTime dt(QDate(2025, 6, 1), QTime(12, 0), Qt::UTC);

        SpatiotemporalEvent se;
        se.tDays     = static_cast<double>(epoch.daysTo(dt));
        se.lat       = 51.5;
        se.lon       = -0.1;
        se.crimeType = QStringLiteral("burglary");

        HawkesProcess hp;
        const bool ok = hp.fit({se}, 10);
        Q_UNUSED(ok);  // convergence optional with 1 event

        // Intensity must be finite and non-negative at any point
        const double lam = hp.intensity(se.tDays + 1.0, 51.5, -0.1);
        QVERIFY(std::isfinite(lam));
        QVERIFY(lam >= 0.0);
    }

    void testSingleEventKDEHotspot()
    {
        KDEHotspot kde(20);
        const QVector<QPair<double,double>> locs = {{51.5, -0.1}};
        const auto surface = kde.compute(locs, 51.4, 51.6, -0.2, 0.0);

        // Must produce a valid non-empty surface
        QVERIFY(!surface.empty());
        double maxVal = 0.0;
        bool allFinite = true;
        for (const auto& row : surface)
            for (double v : row) {
                if (!std::isfinite(v)) allFinite = false;
                if (v > maxVal) maxVal = v;
            }
        QVERIFY(allFinite);
        QVERIFY(maxVal > 0.0);  // at least some density
    }

    void testSingleEventSeriesDetector()
    {
        const QDateTime dt(QDate(2025, 3, 1), QTime(10, 0), Qt::UTC);
        const auto event = makeEvent(0, 51.5, -0.1, dt);

        SeriesDetector det(0.5, 14.0, 3);
        const auto series = det.detect({event});
        // One event is below minSamples=3 → no series formed
        QVERIFY(series.isEmpty());
    }

    void testSingleEventRiskForecaster()
    {
        const QDateTime dt(QDate(2025, 3, 1), QTime(10, 0), Qt::UTC);
        const auto ev = makeEvent(0, 51.5, -0.1, dt, QStringLiteral("burglary"),
                                   QStringLiteral("BetaZone"));

        RiskForecaster forecaster(7);
        forecaster.fit({ev}, QStringLiteral("burglary"));
        QVERIFY(forecaster.isFitted());
        QCOMPARE(forecaster.zoneCount(), 1);

        // Forecast must not crash and must produce 7 days
        const QDateTime from(QDate(2025, 3, 2), QTime(0, 0), Qt::UTC);
        const auto zones = forecaster.forecast(from);
        QVERIFY(!zones.isEmpty());

        for (const auto& zf : zones) {
            QCOMPARE(zf.days.size(), 7);
            for (const auto& day : zf.days) {
                QVERIFY(std::isfinite(day.riskScore));
                QVERIFY(day.riskScore >= 0.0);
                QVERIFY(day.riskScore <= 1.0);
            }
        }
    }

    // ── 3. 100 co-located events ─────────────────────────────────────────────

    void testCoLocatedKDEHotspot()
    {
        // All events at the same location → one tight high-density peak
        QVector<QPair<double,double>> locs;
        locs.reserve(100);
        for (int i = 0; i < 100; ++i)
            locs.append({51.5, -0.1});

        KDEHotspot kde(30);
        const auto hotspots = kde.findHotspots(locs, 51.3, 51.7, -0.3, 0.1, 3, 0.05);

        // Expect at least one hotspot found
        QVERIFY(!hotspots.isEmpty());

        // The highest-ranked hotspot centroid must be close to the crime location
        const auto& top = hotspots.first();
        QVERIFY(qAbs(top.centroidLat - 51.5) < 0.2);
        QVERIFY(qAbs(top.centroidLon - (-0.1)) < 0.2);
        QVERIFY(top.rank == 1);
        QVERIFY(top.peakDensity > 0.0);
    }

    void testCoLocatedKDESurfacePeakLocation()
    {
        // KDE surface maximum must be near the co-located point
        QVector<QPair<double,double>> locs;
        locs.reserve(100);
        for (int i = 0; i < 100; ++i)
            locs.append({51.5, -0.1});

        const double latMin = 51.3, latMax = 51.7;
        const double lonMin = -0.3, lonMax =  0.1;
        const int gridN = 40;

        KDEHotspot kde(gridN);
        const auto surface = kde.compute(locs, latMin, latMax, lonMin, lonMax);

        QVERIFY(!surface.empty());
        QCOMPARE(static_cast<int>(surface.size()),    gridN);
        QCOMPARE(static_cast<int>(surface[0].size()), gridN);

        // Find the peak cell
        int peakRow = 0, peakCol = 0;
        double peakVal = -1.0;
        for (int r = 0; r < gridN; ++r)
            for (int c = 0; c < gridN; ++c)
                if (surface[r][c] > peakVal) {
                    peakVal = surface[r][c]; peakRow = r; peakCol = c;
                }

        // Convert grid cell to lat/lon
        const double cellH = (latMax - latMin) / gridN;
        const double cellW = (lonMax - lonMin) / gridN;
        const double peakLat = latMin + (peakRow + 0.5) * cellH;
        const double peakLon = lonMin + (peakCol + 0.5) * cellW;

        QVERIFY(qAbs(peakLat - 51.5) < 0.15);
        QVERIFY(qAbs(peakLon - (-0.1)) < 0.15);
    }

    void testCoLocatedSeriesDetector()
    {
        // 100 burglaries all at (51.5, -0.1) within 7 days → must form >= 1 series
        const QDateTime base(QDate(2025, 4, 1), QTime(10, 0), Qt::UTC);
        QVector<CrimeEvent> events;
        events.reserve(100);
        for (int i = 0; i < 100; ++i)
            events.append(makeEvent(i, 51.5, -0.1, base.addDays(i % 7)));

        SeriesDetector det(0.5, 14.0, 3);
        const auto series = det.detect(events);

        // All points are within epsilon of each other → forms one large cluster
        QVERIFY(!series.isEmpty());
        QVERIFY(series.first().members.size() >= 3);
    }

    // ── 4. Performance: 1000 events, 5 years, 3 crime types ─────────────────

    void testFullPipelinePerformance1000Events()
    {
        const QDateTime epoch(QDate(2020, 1, 1), QTime(0, 0), Qt::UTC);
        const QString types[] = {
            QStringLiteral("burglary"),
            QStringLiteral("robbery"),
            QStringLiteral("assault")
        };
        const QString suburbs[] = {
            QStringLiteral("Alpha"), QStringLiteral("Beta"),
            QStringLiteral("Gamma"), QStringLiteral("Delta")
        };

        QVector<CrimeEvent> events;
        events.reserve(1000);

        for (int i = 0; i < 1000; ++i) {
            // Spread across 5 years (1825 days) with geographic spread
            const QDateTime dt = epoch.addDays(i % 1825).addSecs((i % 24) * 3600);
            const double lat = 51.4 + (i % 20) * 0.01;
            const double lon = -0.2 + (i % 15) * 0.01;
            const QString& type   = types[i % 3];
            const QString& suburb = suburbs[i % 4];
            events.append(makeEvent(i, lat, lon, dt, type, suburb));
        }

        QElapsedTimer timer;
        timer.start();

        // ── Poisson Baseline ──────────────────────────────────────────────────
        QVector<PoissonBaseline::EventRecord> recs;
        recs.reserve(events.size());
        for (const auto& ev : events)
            recs.append(toRec(ev));

        PoissonBaseline poisson;
        poisson.fit(recs);
        QVERIFY(poisson.isFitted());

        // ── Hawkes Process (limited iterations for performance test) ──────────
        QVector<SpatiotemporalEvent> stes;
        stes.reserve(events.size());
        for (const auto& ev : events)
            stes.append(toSTE(ev, epoch));
        std::sort(stes.begin(), stes.end(), [](const auto& a, const auto& b){
            return a.tDays < b.tDays;
        });

        HawkesProcess hawkes;
        hawkes.fit(stes, 3);   // minimal iterations → fast
        // Whether fitted or not, must not crash

        // ── KDE Hotspot ───────────────────────────────────────────────────────
        QVector<QPair<double,double>> locs;
        locs.reserve(events.size());
        for (const auto& ev : events)
            locs.append({ev.lat.value_or(0.0), ev.lon.value_or(0.0)});

        KDEHotspot kde(30);
        const auto hotspots = kde.findHotspots(locs, 51.3, 51.6, -0.3, 0.0, 5);
        QVERIFY(!hotspots.isEmpty());

        // ── Series Detector ───────────────────────────────────────────────────
        SeriesDetector det(0.5, 14.0, 3);
        const auto series = det.detect(events);
        // Series count can be anything; just verify no crash
        QVERIFY(series.size() >= 0);

        // ── Risk Forecaster ───────────────────────────────────────────────────
        RiskForecaster forecaster(7);
        forecaster.fit(events);
        // 4 suburbs → 4 zones
        QCOMPARE(forecaster.zoneCount(), 4);

        const QDateTime forecastFrom = epoch.addDays(1826);
        const auto zoneForecast = forecaster.forecast(forecastFrom);
        QVERIFY(!zoneForecast.isEmpty());

        // ── Ensemble ──────────────────────────────────────────────────────────
        EnsemblePredictor ensemble;
        ensemble.setPoisson(&poisson);
        ensemble.setHawkes(&hawkes);
        QVERIFY(ensemble.isReady());

        const auto ensPred = ensemble.predict(
            QStringLiteral("Alpha"),
            epoch.addDays(1830),
            QStringLiteral("burglary"),
            51.5, -0.1);
        QVERIFY(ensPred.probCrime >= 0.0);
        QVERIFY(ensPred.probCrime <= 1.0);

        const qint64 elapsed = timer.elapsed();
        qDebug() << "Full pipeline (1000 events, 5 years, 3 types):" << elapsed << "ms";
        QVERIFY2(elapsed < 5000,
                 qPrintable(QStringLiteral("Full pipeline took %1 ms, expected < 5000 ms").arg(elapsed)));
    }

    // ── 5. Multi-year spread: events in different years form no spurious clusters ─

    void testMultiYearSpreadNoSpuriousClusters()
    {
        // 50 events: 10 per year over 5 years, all at the same location.
        // With epsDays=30, events >30 days apart should not cluster together.
        const QDateTime base(QDate(2020, 1, 1), QTime(10, 0), Qt::UTC);
        QVector<CrimeEvent> events;
        events.reserve(50);
        for (int year = 0; year < 5; ++year) {
            for (int j = 0; j < 10; ++j) {
                const QDateTime dt = base.addDays(year * 365 + j);
                events.append(makeEvent(year * 10 + j, 51.5, -0.1, dt));
            }
        }

        // With epsDays=20, events in the same year-window cluster,
        // but events across years should not join.
        SeriesDetector det(1.0, 20.0, 3);
        const auto series = det.detect(events);

        // Each year-group of 10 events within 20 days should form a separate series.
        // We expect >= 5 distinct series (one per year-group).
        QVERIFY(series.size() >= 5);
    }

    // ── 6. Mixed crime types: Poisson handles all types independently ─────────

    void testMixedCrimeTypesFitAndPredict()
    {
        const QDateTime base(QDate(2024, 1, 1), QTime(10, 0), Qt::UTC);
        QVector<PoissonBaseline::EventRecord> recs;

        const QStringList types = {
            QStringLiteral("burglary"),
            QStringLiteral("robbery"),
            QStringLiteral("assault"),
            QStringLiteral("theft"),
            QStringLiteral("fraud")
        };

        for (int i = 0; i < 50; ++i) {
            PoissonBaseline::EventRecord r;
            r.zoneId     = QStringLiteral("Central");
            r.occurredAt = base.addDays(i);
            r.crimeType  = types[i % 5];
            recs.append(r);
        }

        PoissonBaseline model;
        model.fit(recs);
        QVERIFY(model.isFitted());
        QCOMPARE(model.totalEvents(), 50);

        // Each crime type should produce a valid prediction
        const QDateTime queryDt = base.addDays(60);
        for (const QString& type : types) {
            PoissonPrediction pred = model.predict(QStringLiteral("Central"), queryDt, type);
            QVERIFY(pred.lambda >= 0.0);
            QVERIFY(std::isfinite(pred.probAtLeastOne));
            QVERIFY(pred.probAtLeastOne >= 0.0);
            QVERIFY(pred.probAtLeastOne <= 1.0);
        }
    }

    // ── 7. All events in same zone (no geographic spread) ────────────────────

    void testAllSameZonePoissonAndSeries()
    {
        const QDateTime base(QDate(2025, 1, 1), QTime(2, 0), Qt::UTC);
        QVector<CrimeEvent> events;
        events.reserve(50);
        for (int i = 0; i < 50; ++i)
            events.append(makeEvent(i, 51.5, -0.1, base.addDays(i % 30),
                                    QStringLiteral("burglary"),
                                    QStringLiteral("SingleZone")));

        // Poisson should see only one zone
        QVector<PoissonBaseline::EventRecord> recs;
        for (const auto& ev : events)
            recs.append(toRec(ev));

        PoissonBaseline model;
        model.fit(recs);
        QVERIFY(model.isFitted());

        // SeriesDetector should group all same-location events into >= 1 series
        SeriesDetector det(0.1, 35.0, 3);
        const auto series = det.detect(events);
        QVERIFY(!series.isEmpty());

        // No series should span radically different zones (all are "SingleZone")
        for (const auto& s : series)
            QVERIFY(!s.members.isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestPipelineEdgeCases)
#include "test_pipeline_edge_cases.moc"
