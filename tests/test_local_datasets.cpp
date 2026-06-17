// test_local_datasets.cpp — integration tests against curated real-world datasets
// stored under sentinel/data/ (see data/README.md and data/manifest.json).
#include <QTest>
#include <QTimeZone>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QTextStream>
#include <QSet>
#include <algorithm>

#include "ingest/CsvImporter.h"
#include "ingest/DataQualityScorer.h"
#include "ingest/WeatherSource.h"
#include "inference/CoOffendingAnalyser.h"
#include "models/PoissonBaseline.h"
#include "models/KDEHotspot.h"
#include "inference/HintEngine.h"

#ifndef SENTINEL_DATA_DIR
#define SENTINEL_DATA_DIR "../../data"
#endif

class TestLocalDatasets : public QObject
{
    Q_OBJECT

private:
    static QString dataRoot()
    {
        return QDir(QStringLiteral(SENTINEL_DATA_DIR)).absolutePath();
    }

    static QString dataFile(const QString& rel)
    {
        return QDir(dataRoot()).filePath(rel);
    }

    static void requireFile(const QString& rel)
    {
        const QString path = dataFile(rel);
        if (!QFile::exists(path)) {
            QSKIP(qPrintable(QStringLiteral("Missing dataset: %1 (run data/scripts/fetch_datasets.ps1)")
                                 .arg(path)));
        }
    }

    static int countWithCoords(const QVector<CrimeEvent>& events)
    {
        int n = 0;
        for (const CrimeEvent& e : events) {
            if (e.lat.has_value() && e.lon.has_value())
                ++n;
        }
        return n;
    }

    static QVector<PersonIncidentRecord> loadPersonIncidentCsv(const QString& rel)
    {
        QVector<PersonIncidentRecord> out;
        const QString path = dataFile(rel);
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
            return out;

        QTextStream stream(&f);
        stream.setEncoding(QStringConverter::Utf8);
        const QString header = stream.readLine();
        const QStringList cols = header.split(QLatin1Char(','));
        const int personCol = cols.indexOf(QStringLiteral("person_id"));
        const int incidentCol = cols.indexOf(QStringLiteral("incident_id"));
        const int roleCol = cols.indexOf(QStringLiteral("role"));
        if (personCol < 0 || incidentCol < 0)
            return out;

        while (!stream.atEnd()) {
            const QString line = stream.readLine().trimmed();
            if (line.isEmpty())
                continue;
            const QStringList fields = line.split(QLatin1Char(','));
            if (fields.size() <= std::max(personCol, incidentCol))
                continue;
            PersonIncidentRecord r;
            r.personId = fields[personCol].trimmed();
            r.incidentId = fields[incidentCol].trimmed();
            r.role = roleCol >= 0 && roleCol < fields.size()
                         ? fields[roleCol].trimmed()
                         : QStringLiteral("participant");
            if (!r.personId.isEmpty() && !r.incidentId.isEmpty())
                out.append(r);
        }
        return out;
    }

private slots:
    void initTestCase()
    {
        QVERIFY2(QDir(dataRoot()).exists(),
                 qPrintable(QStringLiteral("Data root not found: %1").arg(dataRoot())));
    }

    void testManifestListsDatasets()
    {
        const QString manifestPath = dataFile(QStringLiteral("manifest.json"));
        if (!QFile::exists(manifestPath))
            QSKIP("manifest.json missing — run data/scripts/fetch_datasets.py");

        QFile f(manifestPath);
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
        QVERIFY(doc.isObject());
        const QJsonArray datasets = doc.object().value(QStringLiteral("datasets")).toArray();
        QVERIFY2(datasets.size() >= 5,
                 qPrintable(QStringLiteral("Expected >=5 datasets, got %1").arg(datasets.size())));
    }

    void testUkPoliceStreetCsvImport()
    {
        const QString rel = QStringLiteral("crimes/uk_metropolitan_street_2024.csv");
        requireFile(rel);

        const QVector<CrimeEvent> events =
            CsvImporter::importFile(dataFile(rel), QStringLiteral("uk_police"));
        QVERIFY2(events.size() >= 5000,
                 qPrintable(QStringLiteral("UK import too small: %1 rows").arg(events.size())));

        const int withCoords = countWithCoords(events);
        QVERIFY2(withCoords >= static_cast<int>(events.size() * 0.8),
                 qPrintable(QStringLiteral("UK coord coverage low: %1/%2")
                                .arg(withCoords)
                                .arg(events.size())));

        QSet<QString> types;
        for (const CrimeEvent& e : events)
            types.insert(e.crimeType);
        QVERIFY(types.size() >= 8);
    }

    void testCincinnatiPdiCsvImport()
    {
        const QString rel = QStringLiteral("crimes/cincinnati_pdi_crimes_sample.csv");
        requireFile(rel);

        const QVector<CrimeEvent> events =
            CsvImporter::importFile(dataFile(rel), QStringLiteral("cincinnati"));
        QVERIFY2(events.size() >= 10000,
                 qPrintable(QStringLiteral("Cincinnati sample too small: %1").arg(events.size())));
        QVERIFY(countWithCoords(events) >= 8000);

        QSet<QString> types;
        for (const CrimeEvent& e : events)
            types.insert(e.crimeType);
        QVERIFY(types.size() >= 5);
    }

    void testSfpdIncidentsCsvImport()
    {
        const QString rel = QStringLiteral("crimes/sfpd_incidents_sample.csv");
        requireFile(rel);

        const QVector<CrimeEvent> events =
            CsvImporter::importFile(dataFile(rel), QStringLiteral("sfpd"));
        QVERIFY2(events.size() >= 10000,
                 qPrintable(QStringLiteral("SFPD sample too small: %1").arg(events.size())));
        QVERIFY(countWithCoords(events) >= 8000);
    }

    void testLondonCrimesNarrativeCsvImport()
    {
        const QString rel = QStringLiteral("crimes/london_crimes_2024.csv");
        requireFile(rel);

        const QVector<CrimeEvent> events =
            CsvImporter::importFile(dataFile(rel), QStringLiteral("london_sim"));
        QVERIFY(events.size() >= 50);

        int withNarrative = 0;
        for (const CrimeEvent& e : events) {
            if ((e.narrative && !e.narrative->isEmpty()) || !e.crimeType.isEmpty())
                ++withNarrative;
        }
        QVERIFY(withNarrative >= 40);
    }

    void testWeatherLondonH1Json()
    {
        const QString rel = QStringLiteral("weather/london_2024_h1.json");
        requireFile(rel);

        QFile f(dataFile(rel));
        QVERIFY(f.open(QIODevice::ReadOnly));
        WeatherSource ws;
        const int cached = ws.parseResponse(f.readAll());
        QVERIFY2(cached >= 4000,
                 qPrintable(QStringLiteral("Expected ~4344 hourly records, got %1").arg(cached)));

        const QDateTime sample(QDate(2024, 3, 15), QTime(12, 0), QTimeZone::utc());
        const auto wd = ws.dataAt(sample);
        QVERIFY(wd.has_value());
        QVERIFY(wd->temperatureC > -30.0 && wd->temperatureC < 45.0);
    }

    void testMorenoCoOffendingGraph()
    {
        const QString rel = QStringLiteral("co_offending/moreno_person_crime.csv");
        requireFile(rel);

        const QVector<PersonIncidentRecord> recs = loadPersonIncidentCsv(rel);
        QVERIFY2(recs.size() >= 500,
                 qPrintable(QStringLiteral("Moreno records too few: %1").arg(recs.size())));

        CoOffendingAnalyser analyser;
        analyser.buildGraph(recs);
        analyser.analyse();
        QVERIFY(analyser.isBuilt());
        QVERIFY(analyser.nodes().size() >= 100);

        const QVector<NetworkNode> ranked = analyser.nodes();
        bool hasEdges = false;
        for (const NetworkNode& n : ranked) {
            if (!n.neighbours.isEmpty()) {
                hasEdges = true;
                break;
            }
        }
        QVERIFY(hasEdges);
    }

    void testChicagoCoOffendingGraph()
    {
        const QString rel = QStringLiteral("co_offending/chicago_co_offending.csv");
        requireFile(rel);

        const QVector<PersonIncidentRecord> recs = loadPersonIncidentCsv(rel);
        QVERIFY2(recs.size() >= 100,
                 qPrintable(QStringLiteral("Chicago co-offend records: %1").arg(recs.size())));

        CoOffendingAnalyser analyser;
        analyser.buildGraph(recs);
        analyser.analyse();
        QVERIFY(analyser.isBuilt());

        QString incidentWithLeads;
        for (const PersonIncidentRecord& r : recs) {
            const auto leads = analyser.findLeads(r.incidentId, 3);
            if (!leads.isEmpty()) {
                incidentWithLeads = r.incidentId;
                break;
            }
        }
        QVERIFY2(!incidentWithLeads.isEmpty(), "Expected co-offending leads from Chicago arrests");
    }

    void testLocalDatasetMiniPipeline()
    {
        const QString ukRel = QStringLiteral("crimes/uk_metropolitan_street_2024.csv");
        requireFile(ukRel);

        QVector<CrimeEvent> events =
            CsvImporter::importFile(dataFile(ukRel), QStringLiteral("uk_police"));
        if (events.size() > 2000)
            events.resize(2000);

        const DataQualityScorer scorer = DataQualityScorer::withDefaults();
        const QVector<QualityReport> reports = scorer.scoreBatch(events);
        QVERIFY(reports.size() == events.size());
        QVERIFY2(DataQualityScorer::passRate(reports) >= 0.3,
                 qPrintable(QStringLiteral("Quality pass rate: %1")
                                .arg(DataQualityScorer::passRate(reports))));

        const int kept = static_cast<int>(std::min<qsizetype>(1500, events.size()));
        events.resize(kept);
        QVERIFY(kept >= 500);

        QVector<PoissonBaseline::EventRecord> poissonRecs;
        poissonRecs.reserve(events.size());
        for (const CrimeEvent& ev : events) {
            if (!ev.occurredAt)
                continue;
            PoissonBaseline::EventRecord rec;
            const QString zone = ev.locationRaw.value_or(QString());
            rec.zoneId = zone.isEmpty() ? QStringLiteral("london") : zone;
            rec.occurredAt = *ev.occurredAt;
            rec.crimeType = ev.crimeType;
            poissonRecs.append(rec);
        }
        QVERIFY2(poissonRecs.size() >= 400,
                 qPrintable(QStringLiteral("Poisson records: %1").arg(poissonRecs.size())));

        PoissonBaseline baseline;
        baseline.fit(poissonRecs);
        QVERIFY(baseline.isFitted());

        QVector<QPair<double, double>> locations;
        double latMin = 90.0, latMax = -90.0, lonMin = 180.0, lonMax = -180.0;
        for (const CrimeEvent& ev : events) {
            if (!ev.lat || !ev.lon)
                continue;
            locations.append({*ev.lat, *ev.lon});
            latMin = std::min(latMin, *ev.lat);
            latMax = std::max(latMax, *ev.lat);
            lonMin = std::min(lonMin, *ev.lon);
            lonMax = std::max(lonMax, *ev.lon);
        }
        QVERIFY(locations.size() >= 400);

        KDEHotspot kde;
        const auto hotspots =
            kde.findHotspots(locations, latMin, latMax, lonMin, lonMax, 5);
        QVERIFY(!hotspots.isEmpty());

        HintEngine hints;
        HintEngineInput input;
        input.event = events.first();
        input.dataQuality = reports.first().compositeScore;
        SeriesMatch sm;
        sm.seriesId = QStringLiteral("SER-LOCAL");
        sm.memberCount = 6;
        sm.linkProbability = 0.65;
        sm.compositeScore = 0.6;
        sm.method = QStringLiteral("near_repeat");
        input.seriesMatches.append(sm);
        const auto generated = hints.generate(input);
        QVERIFY(!generated.isEmpty());
    }
};

QTEST_MAIN(TestLocalDatasets)
#include "test_local_datasets.moc"
