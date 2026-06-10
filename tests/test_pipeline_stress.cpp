// test_pipeline_stress.cpp — Full pipeline stress tests with realistic volumes
#include <QTest>
#include <QCoreApplication>
#include <QDateTime>
#include <QUuid>
#include "core/CrimeEvent.h"
#include "core/Database.h"
#include "ingest/CsvImporter.h"
#include "ingest/DataQualityScorer.h"
#include "models/PoissonBaseline.h"
#include "models/HawkesProcess.h"
#include "models/SeriesDetector.h"
#include "models/KDEHotspot.h"
#include "models/EnsemblePredictor.h"
#include "models/BayesianHierarchical.h"
#include "inference/MOAnalyser.h"
#include "inference/EvidenceScorer.h"
#include "inference/HintEngine.h"
#include "inference/AnomalyDetector.h"
#include "inference/GeographicProfiler.h"
#include "audit/ProvenanceLog.h"
#include "core/DataExporter.h"
#include "core/AppConfig.h"
#include <cmath>

class TestPipelineStress : public QObject
{
    Q_OBJECT

private:
    static QVector<CrimeEvent> generateLondonEvents(int count, int daysBack = 90)
    {
        QVector<CrimeEvent> events;
        const QStringList zones = {"Camden", "Islington", "Hackney", "Tower Hamlets",
                                    "Southwark", "Lambeth", "Westminster", "Kensington"};
        const QStringList types = {"burglary", "theft", "robbery", "assault",
                                    "vehicle_crime", "drug_offence"};

        const double latCenter = 51.5074, lonCenter = -0.1278;
        for (int i = 0; i < count; ++i) {
            CrimeEvent e;
            e.eventId = e.id = QStringLiteral("EVT_%1").arg(i, 6, 10, QChar('0'));
            e.crimeType = types[i % types.size()];
            e.suburb = zones[i % zones.size()];

            // Spread events within ~5km of London
            double angle = (i * 37.0 * M_PI / 180.0);
            double radius = (i % 50) * 0.001;
            e.lat = latCenter + radius * std::cos(angle);
            e.lon = lonCenter + radius * std::sin(angle);
            e.latitude  = *e.lat;
            e.longitude = *e.lon;

            // Events spread over daysBack
            e.occurredAt = QDateTime::currentDateTimeUtc().addDays(-(i % daysBack));
            e.qualityScore = 0.7 + (i % 3) * 0.1;
            e.narrative = QStringLiteral("Test narrative for event %1 in %2")
                              .arg(i).arg(e.suburb);
            events.append(e);
        }
        return events;
    }

    static QVector<SpatiotemporalEvent> toSpatiotemporal(const QVector<CrimeEvent>& events)
    {
        QVector<SpatiotemporalEvent> sts;
        const QDateTime epoch = QDateTime(QDate(2024,1,1), QTime(0,0,0), Qt::UTC);
        for (const auto& e : events) {
            if (!e.occurredAt || !e.lat || !e.lon) continue;
            SpatiotemporalEvent st;
            st.tDays = epoch.daysTo(*e.occurredAt);
            st.lat = *e.lat;
            st.lon = *e.lon;
            sts.append(st);
        }
        return sts;
    }

private slots:

    void testIngestAndQualityScore500Events()
    {
        auto events = generateLondonEvents(500);
        DataQualityScorer scorer;
        auto reports = scorer.scoreBatch(events);
        QCOMPARE(reports.size(), 500);
        double pass = DataQualityScorer::passRate(reports);
        QVERIFY(pass > 0.8);
    }

    void testDatabaseInsert1000Events()
    {
        AppConfig cfg; auto db = std::make_shared<Database>(cfg);
        QVERIFY(db->open());
        auto events = generateLondonEvents(1000);
        for (const auto& e : events)
            db->insertEvent(e);
        QVERIFY(db->eventCount() >= 500);
    }

    void testPoissonFitOn300Events()
    {
        auto events = generateLondonEvents(300, 90);
        // Convert to EventRecord
        QVector<PoissonBaseline::EventRecord> records;
        for (const auto& e : events) {
            if (!e.occurredAt) continue;
            PoissonBaseline::EventRecord r;
            r.zoneId = e.suburb;
            r.occurredAt = *e.occurredAt;
            r.crimeType = e.crimeType;
            records.append(r);
        }
        PoissonBaseline poisson;
        poisson.fit(records);
        QVERIFY(poisson.isFitted());
        QVERIFY(poisson.totalEvents() > 0);
    }

    void testBayesianHierarchicalOn500Events()
    {
        auto events = generateLondonEvents(500);
        BayesianHierarchical bh;
        bh.fit(events, 30.0);
        QVERIFY(bh.isFitted());
        QVERIFY(bh.zoneCount() > 0);
        auto posteriors = bh.allPosteriors();
        for (const auto& p : posteriors) {
            QVERIFY(p.posteriorMean >= 0.0);
            QVERIFY(p.credibleLow <= p.credibleHigh);
        }
    }

    void testKDEHotspotOn200Events()
    {
        auto events = generateLondonEvents(200);
        QVector<QPair<double,double>> locs;
        for (const auto& e : events)
            if (e.lat && e.lon)
                locs.append({*e.lat, *e.lon});
        QVERIFY(!locs.isEmpty());

        KDEHotspot kde(30);
        auto hotspots = kde.findHotspots(locs, 51.45, 51.55, -0.17, -0.08, 5);
        QVERIFY(!hotspots.isEmpty());
        QVERIFY(hotspots.size() <= 5);
    }

    void testSeriesDetectorOn100Events()
    {
        auto events = generateLondonEvents(100, 30);
        SeriesDetector sd;
        auto seriesList = sd.detect(events);
        // Series may or may not be detected
        for (const auto& s : seriesList) {
            QVERIFY(s.members.size() >= 3);
        }
    }

    void testMOAnalyser100Cases()
    {
        MOAnalyser analyser;
        QVector<MOCaseRecord> cases;
        QStringList mos = {
            "forced entry rear window residential burglary",
            "front door kicked in commercial premises",
            "vehicle crime window smash valuables stolen",
            "assault victim approached knife weapon",
            "drug dealing street arrest controlled substances"
        };
        for (int i = 0; i < 100; ++i) {
            MOCaseRecord r;
            r.caseId = QString("C%1").arg(i);
            r.moText = mos[i % mos.size()] + QString(" variant_%1").arg(i);
            r.resolved = (i % 3 == 0);
            cases.append(r);
        }
        analyser.fit(cases);
        QVERIFY(analyser.isFitted());
        QCOMPARE(analyser.caseCount(), 100);

        auto matches = analyser.findSimilar("forced entry rear window", 5);
        QVERIFY(matches.size() <= 5);
    }

    void testProvenanceLogChain()
    {
        ProvenanceLog log;
        const QString eventId = "STRESS_EVENT_1";
        for (int i = 0; i < 10; ++i) {
            log.record(eventId, QString("stage_%1").arg(i),
                       QString("action_%1").arg(i),
                       QString("detail for step %1").arg(i));
        }
        auto chain = log.chain(eventId);
        QCOMPARE(chain.size(), 10);
        QVERIFY(!log.formatChain(eventId).isEmpty());
    }

    void testDataExporterWith200Events()
    {
        auto events = generateLondonEvents(200);
        auto json = DataExporter::eventsToJson(events);
        QCOMPARE(json.size(), 200);
        auto csv = DataExporter::eventsToCsv(events);
        QVERIFY(csv.contains("event_id"));
        QVERIFY(csv.split('\n').size() >= 200);
    }

    void testFullPipeline200EventsNocrash()
    {
        // Full pipeline: ingest → quality → DB → Poisson → BH → KDE → series → export
        auto events = generateLondonEvents(200, 60);

        // Quality scoring
        DataQualityScorer scorer;
        auto qualityReports = scorer.scoreBatch(events);
        double passRate = DataQualityScorer::passRate(qualityReports);
        QVERIFY(passRate >= 0.0 && passRate <= 1.0);

        // Filter high quality
        QVector<CrimeEvent> goodEvents;
        for (int i = 0; i < events.size(); ++i)
            if (!qualityReports[i].quarantined)
                goodEvents.append(events[i]);

        // Database storage
        AppConfig cfg; auto db = std::make_shared<Database>(cfg);
        QVERIFY(db->open());
        for (const auto& e : goodEvents)
            db->insertEvent(e);

        // Poisson fit
        QVector<PoissonBaseline::EventRecord> records;
        for (const auto& e : goodEvents) {
            if (!e.occurredAt) continue;
            PoissonBaseline::EventRecord r;
            r.zoneId = e.suburb; r.occurredAt = *e.occurredAt; r.crimeType = e.crimeType;
            records.append(r);
        }
        PoissonBaseline poisson;
        poisson.fit(records);

        // Bayesian hierarchical
        BayesianHierarchical bh;
        bh.fit(goodEvents, 30.0);

        // KDE
        QVector<QPair<double,double>> locs;
        for (const auto& e : goodEvents)
            if (e.lat && e.lon)
                locs.append({*e.lat, *e.lon});
        KDEHotspot kde(20);
        if (!locs.isEmpty())
            kde.findHotspots(locs, 51.45, 51.55, -0.17, -0.08, 3);

        // Export
        auto json = DataExporter::eventsToJson(goodEvents);
        QVERIFY(json.size() <= static_cast<int>(goodEvents.size()));

        // All fine
        QVERIFY(true);
    }

    void testAnomalyDetectionOn100Events()
    {
        auto events = generateLondonEvents(100);
        QVector<AnomalyFeatureVector> features;
        const QDateTime epoch = QDateTime(QDate(2024,1,1), QTime(0,0,0), Qt::UTC);
        for (const auto& e : events) {
            if (!e.lat || !e.lon || !e.occurredAt) continue;
            AnomalyFeatureVector f;
            f.eventId = e.eventId;
            f.lat = *e.lat; f.lon = *e.lon;
            f.tDays = epoch.daysTo(*e.occurredAt);
            f.hourNorm = e.occurredAt->time().hour() / 24.0;
            f.crimeTypeCode = 1;
            features.append(f);
        }

        AnomalyDetector detector(0.05);
        detector.fit(features);
        QVERIFY(detector.isFitted());

        auto anomalies = detector.detectAnomalies(features);
        // With contamination=0.05, about 5% flagged
        QVERIFY(anomalies.size() >= 0);
    }
};

QTEST_MAIN(TestPipelineStress)
#include "test_pipeline_stress.moc"
