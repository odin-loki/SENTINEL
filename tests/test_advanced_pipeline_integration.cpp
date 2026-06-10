// test_advanced_pipeline_integration.cpp
// Advanced pipeline integration tests for the SENTINEL crime analytics system.
// Covers: large dataset processing, lead generation pipeline,
// export round-trip, risk forecasting with history, concurrent scoring.

#include <QTest>
#include <QCoreApplication>
#include <QDateTime>
#include <QDate>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonObject>
#include <QVector>
#include <QPair>
#include <QString>
#include <QtConcurrent/QtConcurrent>
#include <algorithm>
#include <cmath>
#include <future>
#include <vector>

#include "core/CrimeEvent.h"
#include "core/DataExporter.h"
#include "ingest/DataQualityScorer.h"
#include "models/KDEHotspot.h"
#include "models/RiskForecaster.h"
#include "inference/HintEngine.h"

// ─────────────────────────────────────────────────────────────────────────────
// Shared helpers
// ─────────────────────────────────────────────────────────────────────────────

namespace {

static CrimeEvent makeEvent(int idx, double lat, double lon,
                             const QDateTime& dt,
                             const QString& type   = QStringLiteral("burglary"),
                             const QString& suburb = QStringLiteral("London"))
{
    CrimeEvent ev;
    ev.eventId    = QString("ADV-EVT-%1").arg(idx, 6, 10, QChar('0'));
    ev.id         = ev.eventId;
    ev.source     = QStringLiteral("test_source");
    ev.ingestedAt = dt;
    ev.occurredAt = dt;
    ev.reportedAt = dt;
    ev.lat        = lat;
    ev.lon        = lon;
    ev.latitude   = lat;
    ev.longitude  = lon;
    ev.crimeType  = type;
    ev.suburb     = suburb;
    ev.outcome    = QStringLiteral("unresolved");
    ev.qualityScore = 0.75;
    return ev;
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// Test class
// ─────────────────────────────────────────────────────────────────────────────

class AdvancedPipelineIntegrationTest : public QObject {
    Q_OBJECT

private slots:

    // ─────────────────────────────────────────────────────────────────────
    // 1. Large dataset: 500 events → quality scoring + KDE hotspots
    // ─────────────────────────────────────────────────────────────────────
    void testLargeDatasetPipeline()
    {
        const int N = 500;
        QDateTime base(QDate(2024, 1, 1), QTime(0, 0, 0), Qt::UTC);

        QVector<CrimeEvent> events;
        events.reserve(N);
        for (int i = 0; i < N; ++i) {
            // London area: lat 51.5 ± 0.2, lon -0.1 ± 0.3
            double lat = 51.5 + (((i * 7) % 41) - 20) * 0.01;   // ~± 0.20
            double lon = -0.1 + (((i * 11) % 61) - 30) * 0.01;  // ~± 0.30
            QDateTime dt = base.addSecs(static_cast<qint64>(i) * 86400LL * 365 / N);
            events.append(makeEvent(i, lat, lon, dt));
        }

        QElapsedTimer timer;
        timer.start();

        // ── Data quality scoring ──────────────────────────────────────────
        DataQualityScorer scorer;
        QVector<QualityReport> reports = scorer.scoreBatch(events);
        QCOMPARE(reports.size(), N);
        for (const auto& r : reports) {
            QVERIFY2(r.compositeScore >= 0.0 && r.compositeScore <= 1.0,
                     "Quality score must be in [0,1]");
        }

        // ── KDE hotspot detection ─────────────────────────────────────────
        QVector<QPair<double,double>> locs;
        locs.reserve(N);
        for (const auto& ev : events)
            locs.append({ev.lat.value_or(51.5), ev.lon.value_or(-0.1)});

        KDEHotspot kde(40, 1.0);
        QVector<HotspotRegion> hotspots =
            kde.findHotspots(locs, 51.28, 51.72, -0.41, 0.21, 5);

        QVERIFY2(!hotspots.isEmpty(),
                 "KDE should detect at least 1 hotspot from 500 events");
        QVERIFY2(hotspots[0].peakDensity > 0.0,
                 "Top hotspot peak density must be positive");
        QVERIFY2(hotspots[0].rank == 1, "Top hotspot rank must be 1");

        qint64 elapsed = timer.elapsed();
        QVERIFY2(elapsed < 10000,
                 qPrintable(QString("Large dataset pipeline took %1 ms (> 10s limit)")
                            .arg(elapsed)));
    }

    // ─────────────────────────────────────────────────────────────────────
    // 2. Lead generation pipeline: 20 events → HintEngine leads
    // ─────────────────────────────────────────────────────────────────────
    void testLeadGenerationPipeline()
    {
        const int N = 20;
        QDateTime base(QDate(2024, 3, 1), QTime(10, 0, 0), Qt::UTC);

        QVector<CrimeEvent> events;
        events.reserve(N);
        for (int i = 0; i < N; ++i) {
            double lat = 51.505 + (i % 5) * 0.002;
            double lon = -0.095 + (i % 7) * 0.002;
            events.append(makeEvent(i, lat, lon, base.addDays(i)));
        }

        HintEngine engine;
        QVector<InvestigativeLead> allLeads;

        for (int i = 0; i < N; ++i) {
            HintEngineInput input;
            input.event = events[i];

            // Provide a series match so leads are generated
            SeriesMatch sm;
            sm.seriesId             = QStringLiteral("SERIES-ADV-001");
            sm.memberCount          = 5;
            sm.linkProbability      = 0.75;
            sm.spatialDistanceM     = 120.0;
            sm.temporalDistanceDays = 3.0;
            sm.moSimilarity         = 0.65;
            sm.compositeScore       = 0.75;
            sm.method               = QStringLiteral("near_repeat");
            input.seriesMatches.append(sm);
            input.dataQuality = 0.8;

            QVector<InvestigativeLead> leads = engine.generate(input);
            allLeads.append(leads);
        }

        QVERIFY2(!allLeads.isEmpty(),
                 "HintEngine should produce leads for 20 events with series context");

        for (const auto& lead : allLeads) {
            QVERIFY2(!lead.detail.isEmpty(),
                     qPrintable(QString("Lead rank=%1 must have non-empty description")
                                .arg(lead.rank)));
            QVERIFY2(lead.confidence > 0.0,
                     qPrintable(QString("Lead rank=%1 confidence=%2 must be > 0")
                                .arg(lead.rank).arg(lead.confidence)));
        }
    }

    // ─────────────────────────────────────────────────────────────────────
    // 3. Export round-trip: 10 events → JSON → verify all event IDs present
    // ─────────────────────────────────────────────────────────────────────
    void testExportRoundtrip()
    {
        const int N = 10;
        QDateTime base(QDate(2024, 6, 1), QTime(8, 0, 0), Qt::UTC);

        QVector<CrimeEvent> events;
        events.reserve(N);
        QStringList expectedIds;
        for (int i = 0; i < N; ++i) {
            QString id = QString("EXPORT-EVT-%1").arg(i, 3, 10, QChar('0'));
            CrimeEvent ev = makeEvent(i, 51.5 + i * 0.005, -0.1 + i * 0.005,
                                       base.addDays(i));
            ev.eventId = id;
            ev.id      = id;
            events.append(ev);
            expectedIds.append(id);
        }

        QJsonArray jsonArr = DataExporter::eventsToJson(events);
        QCOMPARE(jsonArr.size(), N);

        // Collect all eventId values from the JSON
        QStringList foundIds;
        for (int i = 0; i < jsonArr.size(); ++i) {
            QJsonObject obj = jsonArr[i].toObject();
            QVERIFY2(obj.contains(QStringLiteral("eventId")),
                     qPrintable(QString("JSON element %1 missing 'eventId' key").arg(i)));
            foundIds.append(obj[QStringLiteral("eventId")].toString());
        }

        // Every expected ID must appear exactly once
        for (const QString& id : expectedIds) {
            QVERIFY2(foundIds.contains(id),
                     qPrintable(QString("Event ID '%1' missing from JSON export").arg(id)));
        }
    }

    // ─────────────────────────────────────────────────────────────────────
    // 4. Risk forecast with 90-day history across 7 zones
    //    Verify 7 ZoneForecasts produced, each with 7 valid-probability days
    // ─────────────────────────────────────────────────────────────────────
    void testRiskForecastWithHistory()
    {
        const int numZones  = 7;
        const int numDays   = 90;
        const int horizonDays = 7;

        QDateTime historyStart(QDate(2024, 1, 1), QTime(0, 0, 0), Qt::UTC);
        QVector<CrimeEvent> events;

        for (int z = 0; z < numZones; ++z) {
            QString zone = QString("zone_%1").arg(z + 1);
            for (int d = 0; d < numDays; ++d) {
                int count = 1 + (d % 5);  // 1–5 events per day
                QDateTime dt = historyStart.addDays(d);
                for (int e = 0; e < count; ++e) {
                    CrimeEvent ev;
                    ev.eventId    = QString("RF-%1-%2-%3").arg(z).arg(d).arg(e);
                    ev.id         = ev.eventId;
                    ev.suburb     = zone;
                    ev.crimeType  = QStringLiteral("burglary");
                    ev.occurredAt = dt;
                    ev.timestamp  = dt;   // RiskForecaster::fit() reads e.timestamp
                    ev.ingestedAt = dt;
                    ev.lat        = 51.5 + z * 0.01;
                    ev.lon        = -0.1 + z * 0.01;
                    ev.latitude   = ev.lat.value();
                    ev.longitude  = ev.lon.value();
                    ev.qualityScore = 0.8;
                    events.append(ev);
                }
            }
        }

        RiskForecaster forecaster(horizonDays);
        forecaster.fit(events, QStringLiteral("burglary"));

        QVERIFY2(forecaster.isFitted(), "RiskForecaster should be fitted after loading history");
        QCOMPARE(forecaster.zoneCount(), numZones);

        QDateTime forecastFrom = historyStart.addDays(numDays + 1);
        QVector<ZoneForecast> forecasts = forecaster.forecast(forecastFrom);

        QCOMPARE(forecasts.size(), numZones);

        for (const auto& zf : forecasts) {
            QVERIFY2(!zf.zoneId.isEmpty(), "ZoneForecast zoneId must not be empty");
            QCOMPARE(zf.days.size(), horizonDays);
            for (const auto& day : zf.days) {
                QVERIFY2(day.riskScore    >= 0.0 && day.riskScore    <= 1.0,
                         qPrintable(QString("riskScore %1 out of [0,1]").arg(day.riskScore)));
                QVERIFY2(day.baselineProb >= 0.0 && day.baselineProb <= 1.0,
                         qPrintable(QString("baselineProb %1 out of [0,1]").arg(day.baselineProb)));
            }
            QVERIFY2(zf.weeklyRisk >= 0.0, "weeklyRisk must be non-negative");
        }
    }

    // ─────────────────────────────────────────────────────────────────────
    // 5. Concurrent quality scoring: 200 events via QtConcurrent::mapped
    //    Verify all scores are in [0, 1]
    // ─────────────────────────────────────────────────────────────────────
    void testConcurrentPipelineStages()
    {
        const int N = 200;
        QDateTime base(QDate(2024, 1, 1), QTime(0, 0, 0), Qt::UTC);

        QVector<CrimeEvent> events;
        events.reserve(N);
        for (int i = 0; i < N; ++i) {
            CrimeEvent ev;
            ev.eventId      = QString("CONC-%1").arg(i, 4, 10, QChar('0'));
            ev.id           = ev.eventId;
            ev.crimeType    = QStringLiteral("burglary");
            ev.lat          = 51.5 + (i % 11) * 0.01;
            ev.lon          = -0.1 + (i % 7)  * 0.01;
            ev.latitude     = ev.lat.value();
            ev.longitude    = ev.lon.value();
            ev.occurredAt   = base.addDays(i % 365);
            ev.ingestedAt   = base.addDays(i % 365);
            ev.suburb       = QStringLiteral("London");
            ev.source       = QStringLiteral("test_source");
            ev.qualityScore = 0.5;
            events.append(ev);
        }

        // DataQualityScorer is read-only after construction → safe for concurrent use
        DataQualityScorer scorer;

        // Run scoring concurrently across all events
        auto scoreFunc = [&scorer](const CrimeEvent& ev) -> double {
            return scorer.score(ev).compositeScore;
        };

        QFuture<double> future = QtConcurrent::mapped(events, scoreFunc);
        future.waitForFinished();

        QList<double> scores = future.results();
        QCOMPARE(scores.size(), N);

        for (int i = 0; i < N; ++i) {
            QVERIFY2(scores[i] >= 0.0 && scores[i] <= 1.0,
                     qPrintable(QString("Event %1 concurrent score %2 out of [0,1]")
                                .arg(i).arg(scores[i])));
        }
    }
};

QTEST_MAIN(AdvancedPipelineIntegrationTest)
#include "test_advanced_pipeline_integration.moc"
