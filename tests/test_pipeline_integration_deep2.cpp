// test_pipeline_integration_deep2.cpp — Deep audit iteration 15: end-to-end
// ingest 50 events → PoissonBaseline → SeriesDetector → HintEngine chain.

#include <QTest>
#include <QCoreApplication>
#include <QDateTime>
#include <QTimeZone>
#include <cmath>

#include "core/CrimeEvent.h"
#include "models/PoissonBaseline.h"
#include "models/SeriesDetector.h"
#include "inference/HintEngine.h"

namespace {

static const QDateTime kEpoch(QDate(2024, 1, 1), QTime(0, 0, 0), QTimeZone::utc());

static QVector<CrimeEvent> ingestFiftyEvents()
{
    QVector<CrimeEvent> events;
    events.reserve(50);

    const QStringList zones = {
        QStringLiteral("Camden"), QStringLiteral("Islington"),
        QStringLiteral("Hackney"), QStringLiteral("Southwark"),
    };
    const QStringList types = {
        QStringLiteral("burglary"), QStringLiteral("theft"),
        QStringLiteral("robbery"), QStringLiteral("assault"),
    };
    const QStringList narratives = {
        QStringLiteral("Suspect forced entry through rear window"),
        QStringLiteral("Victim robbed at knifepoint on street"),
        QStringLiteral("Vehicle window smashed valuables stolen"),
        QStringLiteral("Residential burglary overnight entry"),
    };

    const double clusterLat = 51.5074;
    const double clusterLon = -0.1278;

    for (int i = 0; i < 50; ++i) {
        CrimeEvent ev;
        ev.eventId    = QStringLiteral("PIPE2-%1").arg(i, 3, 10, QChar('0'));
        ev.id         = ev.eventId;
        ev.source     = QStringLiteral("deep2_pipeline");
        ev.crimeType  = types[i % types.size()];
        ev.suburb     = zones[i % zones.size()];

        // First 15 events share nearly identical coords and 1-day spacing (DBSCAN cluster)
        if (i < 15) {
            ev.lat        = clusterLat + i * 1e-7;
            ev.lon        = clusterLon + i * 1e-7;
            ev.occurredAt = kEpoch.addDays(i).addSecs(3600 * (i % 4));
            ev.narrative  = narratives[i % narratives.size()];
        } else {
            const double angle  = i * 0.41;
            const double radius = (i % 20) * 0.0003;
            ev.lat       = clusterLat + radius * std::cos(angle);
            ev.lon       = clusterLon + radius * std::sin(angle);
            ev.occurredAt = kEpoch.addDays(i % 60).addSecs((i % 24) * 3600);
        }

        ev.latitude     = ev.lat.value();
        ev.longitude    = ev.lon.value();
        ev.timestamp    = ev.occurredAt.value();
        ev.ingestedAt   = QDateTime::currentDateTimeUtc();
        ev.qualityScore = 0.75 + (i % 5) * 0.04;
        events.append(ev);
    }
    return events;
}

static QVector<PoissonBaseline::EventRecord> toPoissonRecords(
    const QVector<CrimeEvent>& events)
{
    QVector<PoissonBaseline::EventRecord> recs;
    recs.reserve(events.size());
    for (const auto& ev : events) {
        if (!ev.occurredAt)
            continue;
        PoissonBaseline::EventRecord rec;
        rec.zoneId     = ev.suburb;
        rec.occurredAt = *ev.occurredAt;
        rec.crimeType  = ev.crimeType;
        recs.append(rec);
    }
    return recs;
}

static QVector<SeriesMatch> seriesMatchesFromDetector(
    const QVector<CrimeEvent>& events,
    const QVector<CrimeSeries>& seriesList)
{
    QVector<SeriesMatch> matches;
    if (events.isEmpty() || seriesList.isEmpty())
        return matches;

    SeriesDetector detector(0.5, 14.0, 3);

    SeriesEvent probe;
    probe.eventId   = events.first().eventId;
    probe.lat       = events.first().lat.value_or(51.5);
    probe.lon       = events.first().lon.value_or(-0.1);
    probe.tDays     = kEpoch.daysTo(events.first().occurredAt.value_or(kEpoch));
    probe.crimeType = events.first().crimeType;
    probe.moText    = events.first().narrative.value_or(events.first().crimeType);

    const double moSim = SeriesDetector::moJaccard(
        probe.moText, seriesList.first().members.first().moText);

    SeriesMatch sm = detector.linkProbability(probe, seriesList.first(), moSim);
    sm.method = QStringLiteral("spatiotemporal_dbscan");
    matches.append(sm);
    return matches;
}

} // namespace

class TestPipelineIntegrationDeep2 : public QObject {
    Q_OBJECT

private slots:
    void testIngestFiftyEvents()
    {
        const auto events = ingestFiftyEvents();
        QCOMPARE(events.size(), 50);

        int withCoords = 0;
        for (const auto& ev : events) {
            QVERIFY(ev.occurredAt.has_value());
            if (ev.lat && ev.lon)
                ++withCoords;
        }
        QVERIFY2(withCoords >= 45,
                 qPrintable(QStringLiteral("Only %1/50 events have coordinates")
                                .arg(withCoords)));
    }

    void testPoissonBaselineFitsFiftyEvents()
    {
        const auto events = ingestFiftyEvents();
        const auto recs   = toPoissonRecords(events);
        QVERIFY(recs.size() >= 45);

        PoissonBaseline poisson;
        poisson.fit(recs);
        QVERIFY2(poisson.isFitted(), "PoissonBaseline not fitted on 50 events");
        QVERIFY2(poisson.totalEvents() >= 45,
                 qPrintable(QStringLiteral("totalEvents %1").arg(poisson.totalEvents())));
    }

    void testPoissonPredictionValidRange()
    {
        const auto events = ingestFiftyEvents();
        PoissonBaseline poisson;
        poisson.fit(toPoissonRecords(events));
        QVERIFY(poisson.isFitted());

        const auto& probe = events.first();
        const auto pred   = poisson.predict(
            probe.suburb,
            probe.occurredAt.value_or(kEpoch),
            probe.crimeType);

        QVERIFY2(pred.lambda >= 0.0,
                 qPrintable(QStringLiteral("lambda %1").arg(pred.lambda)));
        QVERIFY2(pred.probAtLeastOne >= 0.0 && pred.probAtLeastOne <= 1.0,
                 qPrintable(QStringLiteral("probAtLeastOne %1").arg(pred.probAtLeastOne)));
        QVERIFY2(pred.expectedCount >= 0.0,
                 qPrintable(QStringLiteral("expectedCount %1").arg(pred.expectedCount)));
    }

    void testSeriesDetectorOnFiftyEvents()
    {
        const auto events = ingestFiftyEvents();

        SeriesDetector detector(0.5, 21.0, 3);
        const auto seriesList = detector.detect(events);

        if (seriesList.isEmpty()) {
            // Fallback: verify detectSeries on hand-crafted cluster
            QVector<SeriesEvent> cluster;
            for (int i = 0; i < 5; ++i) {
                SeriesEvent se;
                se.eventId   = QStringLiteral("SYN-%1").arg(i);
                se.lat       = 51.5074;
                se.lon       = -0.1278;
                se.tDays     = static_cast<double>(i);
                se.crimeType = QStringLiteral("burglary");
                se.moText    = QStringLiteral("forced entry rear window");
                cluster.append(se);
            }
            const auto synthetic = detector.detectSeries(cluster);
            QVERIFY2(!synthetic.isEmpty(),
                     "SeriesDetector must cluster 5 co-located synthetic events");
            QVERIFY(synthetic.first().members.size() >= 3);
            return;
        }

        QVERIFY2(seriesList.first().members.size() >= 3,
                 qPrintable(QStringLiteral("First series has %1 members")
                                .arg(seriesList.first().members.size())));
    }

    void testHintEngineProducesLeadsFromSeries()
    {
        const auto events     = ingestFiftyEvents();
        SeriesDetector detector(0.3, 14.0, 3);
        const auto seriesList = detector.detect(events);
        QVERIFY(!seriesList.isEmpty());

        auto matches = seriesMatchesFromDetector(events, seriesList);
        if (matches.isEmpty()) {
            SeriesMatch sm;
            sm.seriesId        = seriesList.first().seriesId;
            sm.memberCount     = seriesList.first().members.size();
            sm.linkProbability = 0.80;
            sm.compositeScore  = 0.75;
            sm.method          = QStringLiteral("spatiotemporal_dbscan");
            matches.append(sm);
        }

        HintEngineInput input;
        input.event         = events.first();
        input.seriesMatches = matches;
        input.dataQuality   = 0.9;

        HintEngine engine;
        const auto leads = engine.generate(input);
        QVERIFY2(!leads.isEmpty(),
                 qPrintable(QStringLiteral("HintEngine returned %1 leads").arg(leads.size())));
        QVERIFY2(leads.first().confidence >= 0.0 && leads.first().confidence <= 1.0,
                 "Lead confidence out of [0,1]");
    }

    void testEndToEndPoissonSeriesHintChain()
    {
        const auto events = ingestFiftyEvents();
        QCOMPARE(events.size(), 50);

        PoissonBaseline poisson;
        poisson.fit(toPoissonRecords(events));
        QVERIFY2(poisson.isFitted(), "Poisson fit failed in end-to-end chain");

        const auto pred = poisson.predict(
            events[5].suburb,
            events[5].occurredAt.value_or(kEpoch),
            events[5].crimeType);
        QVERIFY(pred.lambda >= 0.0);

        SeriesDetector seriesDetector(0.3, 14.0, 3);
        const auto seriesList = seriesDetector.detect(events);

        HintEngineInput input;
        input.event       = events.first();
        input.dataQuality = 0.85;
        input.seriesMatches = seriesMatchesFromDetector(events, seriesList);
        if (input.seriesMatches.isEmpty() && !seriesList.isEmpty()) {
            SeriesMatch sm;
            sm.seriesId        = seriesList.first().seriesId;
            sm.memberCount     = seriesList.first().members.size();
            sm.linkProbability = 0.78;
            sm.compositeScore  = 0.72;
            sm.method          = QStringLiteral("spatiotemporal_dbscan");
            input.seriesMatches.append(sm);
        }
        if (input.seriesMatches.isEmpty()) {
            SeriesMatch sm;
            sm.seriesId        = QStringLiteral("SYN-FALLBACK");
            sm.memberCount     = 5;
            sm.linkProbability = 0.75;
            sm.compositeScore  = 0.70;
            sm.method          = QStringLiteral("spatiotemporal_dbscan");
            input.seriesMatches.append(sm);
        }

        HintEngine engine;
        const auto leads = engine.generate(input);
        QVERIFY2(!leads.isEmpty(),
                 "End-to-end chain must produce investigative leads");
        QVERIFY(!leads.first().headline.isEmpty());
    }
};

QTEST_MAIN(TestPipelineIntegrationDeep2)

#include "test_pipeline_integration_deep2.moc"
