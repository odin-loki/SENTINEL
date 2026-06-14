// test_real_data_evaluation.cpp
// Quantitative evaluation of SENTINEL pipeline stages on curated real-world data.
// See docs/REAL_DATA_EVALUATION.md for the written report.
#include <QTest>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QSet>
#include <QMap>
#include <cmath>

#include "ingest/CsvImporter.h"
#include "ingest/DataQualityScorer.h"
#include "ingest/WeatherSource.h"
#include "inference/CoOffendingAnalyser.h"
#include "models/PoissonBaseline.h"
#include "models/KDEHotspot.h"
#include "models/SeriesDetector.h"
#include "benchmark/BenchmarkMetrics.h"
#include "nlp/CrimeClassifier.h"

#ifndef SENTINEL_DATA_DIR
#define SENTINEL_DATA_DIR "../../data"
#endif

namespace {

struct ImportStats {
    int    totalRows     = 0;
    int    withCoords    = 0;
    int    withDates     = 0;
    int    withType      = 0;
    int    quarantined   = 0;
    double passRate      = 0.0;
    double avgQuality    = 0.0;
    int    distinctTypes = 0;
};

struct ImportStats analyseImport(const QString& path, const QString& sourceTag, int maxRows = -1)
{
    ImportStats s;
    QVector<CrimeEvent> events = CsvImporter::importFile(path, sourceTag);
    if (maxRows > 0 && events.size() > maxRows)
        events.resize(maxRows);

    s.totalRows = events.size();
    if (s.totalRows == 0)
        return s;

    const DataQualityScorer scorer = DataQualityScorer::withDefaults();
    const QVector<QualityReport> reports = scorer.scoreBatch(events);

    QSet<QString> types;
    double qualitySum = 0.0;
    for (int i = 0; i < events.size(); ++i) {
        const CrimeEvent& e = events[i];
        if (e.lat && e.lon) ++s.withCoords;
        if (e.occurredAt)   ++s.withDates;
        if (!e.crimeType.isEmpty()) {
            ++s.withType;
            types.insert(e.crimeType);
        }
        qualitySum += reports[i].compositeScore;
        if (reports[i].quarantined) ++s.quarantined;
    }

    s.distinctTypes = types.size();
    s.avgQuality    = qualitySum / s.totalRows;
    s.passRate      = DataQualityScorer::passRate(reports);
    return s;
}

QString dataFile(const QString& rel)
{
    return QDir(QDir(QStringLiteral(SENTINEL_DATA_DIR)).absolutePath()).filePath(rel);
}

void requireDataFile(const QString& rel)
{
    if (!QFile::exists(dataFile(rel)))
        QSKIP(qPrintable(QStringLiteral("Missing %1 — run data/scripts/fetch_datasets.ps1").arg(rel)));
}

// Build grid-cell PAI inputs: train KDE on trainLocs, evaluate on testLocs.
struct GridPaiResult {
    double pai5  = 0.0;
    double pai10 = 0.0;
    double auc   = 0.0;
    int    cells = 0;
    int    testCrimes = 0;
};

GridPaiResult evaluateKdePai(const QVector<QPair<double, double>>& trainLocs,
                              const QVector<QPair<double, double>>& testLocs,
                              int gridN = 40)
{
    GridPaiResult r;
    r.testCrimes = testLocs.size();
    if (trainLocs.size() < 50 || testLocs.size() < 20)
        return r;

    double latMin = 90, latMax = -90, lonMin = 180, lonMax = -180;
    auto expand = [&](const QVector<QPair<double, double>>& locs) {
        for (const auto& p : locs) {
            latMin = std::min(latMin, p.first);
            latMax = std::max(latMax, p.first);
            lonMin = std::min(lonMin, p.second);
            lonMax = std::max(lonMax, p.second);
        }
    };
    expand(trainLocs);
    expand(testLocs);

    KDEHotspot kde(gridN);
    const auto surface = kde.compute(trainLocs, latMin, latMax, lonMin, lonMax);

    const double latStep = (latMax - latMin) / gridN;
    const double lonStep = (lonMax - lonMin) / gridN;

    QVector<double> yTrue, yPred;
    yTrue.reserve(gridN * gridN);
    yPred.reserve(gridN * gridN);

    for (int row = 0; row < gridN; ++row) {
        for (int col = 0; col < gridN; ++col) {
            const double cellLat = latMin + (row + 0.5) * latStep;
            const double cellLon = lonMin + (col + 0.5) * lonStep;
            const double halfLat = latStep * 0.5;
            const double halfLon = lonStep * 0.5;

            int crimesInCell = 0;
            for (const auto& p : testLocs) {
                if (std::abs(p.first - cellLat) <= halfLat
                    && std::abs(p.second - cellLon) <= halfLon)
                    ++crimesInCell;
            }

            yTrue.append(crimesInCell > 0 ? 1.0 : 0.0);
            yPred.append(surface[static_cast<size_t>(row)][static_cast<size_t>(col)]);
        }
    }

    r.cells = yTrue.size();
    r.pai5  = BenchmarkMetrics::pai(yTrue, yPred, 0.05);
    r.pai10 = BenchmarkMetrics::pai(yTrue, yPred, 0.10);
    r.auc   = BenchmarkMetrics::aucRoc(yTrue, yPred);
    return r;
}

QVector<CrimeEvent> loadUkEvents(int maxRows = 8000)
{
    QVector<CrimeEvent> events = CsvImporter::importFile(
        dataFile(QStringLiteral("crimes/uk_metropolitan_street_2024.csv")),
        QStringLiteral("uk_police"));
    if (maxRows > 0 && events.size() > maxRows)
        events.resize(maxRows);
    return events;
}

} // namespace

class TestRealDataEvaluation : public QObject
{
    Q_OBJECT

private slots:
    void testInventoryMatchesManifest()
    {
        requireDataFile(QStringLiteral("manifest.json"));
        QFile f(dataFile(QStringLiteral("manifest.json")));
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QJsonArray datasets =
            QJsonDocument::fromJson(f.readAll()).object().value(QStringLiteral("datasets")).toArray();
        QVERIFY2(datasets.size() >= 7,
                 qPrintable(QStringLiteral("Expected 7 datasets, got %1").arg(datasets.size())));

        for (const QJsonValue& v : datasets) {
            const QJsonObject d = v.toObject();
            const QString path  = d.value(QStringLiteral("path")).toString();
            QVERIFY2(QFile::exists(dataFile(path)),
                     qPrintable(QStringLiteral("Manifest entry missing on disk: %1").arg(path)));
        }
    }

    void testUkPoliceImportQuality()
    {
        requireDataFile(QStringLiteral("crimes/uk_metropolitan_street_2024.csv"));
        const ImportStats s = analyseImport(
            dataFile(QStringLiteral("crimes/uk_metropolitan_street_2024.csv")),
            QStringLiteral("uk_police"),
            8000);

        fprintf(stderr, "EVAL: uk_rows=%d coords=%d dates=%d types=%d passRate=%.3f avgQ=%.3f\n",
                s.totalRows, s.withCoords, s.withDates, s.distinctTypes, s.passRate, s.avgQuality);

        QCOMPARE(s.totalRows, 8000);
        QVERIFY2(s.withCoords >= 7500,
                 qPrintable(QStringLiteral("UK coord coverage %1/8000").arg(s.withCoords)));
        QVERIFY2(s.withDates >= 7500,
                 qPrintable(QStringLiteral("UK date coverage %1/8000").arg(s.withDates)));
        QVERIFY(s.distinctTypes >= 8);
        QVERIFY2(s.passRate >= 0.85,
                 qPrintable(QStringLiteral("UK pass rate %1").arg(s.passRate)));
        QVERIFY(s.avgQuality >= 0.6);
    }

    void testUsCityImportQuality()
    {
        requireDataFile(QStringLiteral("crimes/cincinnati_pdi_crimes_sample.csv"));
        requireDataFile(QStringLiteral("crimes/sfpd_incidents_sample.csv"));

        const ImportStats cincy = analyseImport(
            dataFile(QStringLiteral("crimes/cincinnati_pdi_crimes_sample.csv")),
            QStringLiteral("cincinnati"),
            5000);
        const ImportStats sfpd = analyseImport(
            dataFile(QStringLiteral("crimes/sfpd_incidents_sample.csv")),
            QStringLiteral("sfpd"),
            5000);

        fprintf(stderr, "EVAL: cincy_coords=%d cincy_pass=%.3f cincy_types=%d\n",
                cincy.withCoords, cincy.passRate, cincy.distinctTypes);
        fprintf(stderr, "EVAL: sfpd_coords=%d sfpd_pass=%.3f sfpd_types=%d\n",
                sfpd.withCoords, sfpd.passRate, sfpd.distinctTypes);

        QVERIFY(cincy.withCoords >= 4000);
        QVERIFY(sfpd.withCoords >= 4500);
        QVERIFY(cincy.distinctTypes >= 5);
        QVERIFY(sfpd.distinctTypes >= 3);
        QVERIFY(cincy.passRate >= 0.55);
        QVERIFY(sfpd.passRate >= 0.55);
    }

    void testNlpOnLondonFixture()
    {
        requireDataFile(QStringLiteral("crimes/london_crimes_2024.csv"));
        const QVector<CrimeEvent> events = CsvImporter::importFile(
            dataFile(QStringLiteral("crimes/london_crimes_2024.csv")),
            QStringLiteral("london_fixture"));

        CrimeClassifier classifier;
        int typeLabelMatch = 0;
        int nonEmptyClass  = 0;

        for (const CrimeEvent& e : events) {
            if (e.crimeType.isEmpty()) continue;
            const auto cls = classifier.classify(e.crimeType);
            if (!cls.first.isEmpty()) ++nonEmptyClass;
            if (cls.first == e.crimeType || cls.second > 0.15)
                ++typeLabelMatch;
        }

        qInfo() << "NLP on london fixture: labelMatch=" << typeLabelMatch
                << "nonEmptyClass=" << nonEmptyClass << "of" << events.size();

        QVERIFY2(typeLabelMatch >= 50,
                 qPrintable(QStringLiteral("Only %1 crime-type labels matched classifier")
                                .arg(typeLabelMatch)));
        QVERIFY(nonEmptyClass >= 50);
    }

    void testUkKdeHotspotDetection()
    {
        requireDataFile(QStringLiteral("crimes/uk_metropolitan_street_2024.csv"));
        QVector<CrimeEvent> events = loadUkEvents(3000);

        QVector<QPair<double, double>> locs;
        double latMin = 90, latMax = -90, lonMin = 180, lonMax = -180;
        for (const CrimeEvent& e : events) {
            if (!e.lat || !e.lon) continue;
            locs.append({*e.lat, *e.lon});
            latMin = std::min(latMin, *e.lat);
            latMax = std::max(latMax, *e.lat);
            lonMin = std::min(lonMin, *e.lon);
            lonMax = std::max(lonMax, *e.lon);
        }

        KDEHotspot kde(50);
        const auto hotspots = kde.findHotspots(locs, latMin, latMax, lonMin, lonMax, 5);
        const auto surface  = kde.compute(locs, latMin, latMax, lonMin, lonMax);
        const double paiArea = kde.paiAreaFraction(surface, 0.5);

        qInfo() << "UK KDE: locs=" << locs.size()
                << "hotspots=" << hotspots.size()
                << "topPeakDensity=" << (hotspots.isEmpty() ? 0.0 : hotspots.first().peakDensity)
                << "paiArea50=" << paiArea;

        QVERIFY(locs.size() >= 2800);
        QVERIFY(hotspots.size() >= 3);
        QVERIFY(hotspots.first().peakDensity > 0.0);
        QVERIFY2(paiArea <= 0.5,
                 qPrintable(QStringLiteral("PAI area fraction %1 should be <= 0.5").arg(paiArea)));
    }

    void testUkTemporalTrainTestPai()
    {
        requireDataFile(QStringLiteral("crimes/uk_metropolitan_street_2024.csv"));
        QVector<CrimeEvent> events = loadUkEvents(0); // full file for month split

        QVector<QPair<double, double>> train, test;
        for (const CrimeEvent& e : events) {
            if (!e.lat || !e.lon || !e.occurredAt) continue;
            const int month = e.occurredAt->date().month();
            const auto loc = qMakePair(*e.lat, *e.lon);
            if (month <= 4)
                train.append(loc);
            else
                test.append(loc);
        }

        const GridPaiResult pai = evaluateKdePai(train, test);
        qInfo() << "UK temporal holdout (Jan-Apr train, May-Jun test):"
                << "train=" << train.size() << "test=" << test.size()
                << "PAI@5%=" << pai.pai5 << "PAI@10%=" << pai.pai10
                << "AUC=" << pai.auc;
        fprintf(stderr, "EVAL: uk_pai5=%.3f uk_pai10=%.3f uk_auc=%.3f train=%d test=%d\n",
                pai.pai5, pai.pai10, pai.auc, train.size(), test.size());

        QVERIFY(train.size() >= 5000);
        QVERIFY(test.size() >= 1500);
        // Real urban crime is heterogeneous; KDE should beat random (PAI > 1 at 5%).
        QVERIFY2(pai.pai5 > 1.0,
                 qPrintable(QStringLiteral("PAI@5%% = %1, expected > 1").arg(pai.pai5)));
        QVERIFY(pai.auc >= 0.55);
    }

    void testUkPoissonAndSeries()
    {
        requireDataFile(QStringLiteral("crimes/uk_metropolitan_street_2024.csv"));
        QVector<CrimeEvent> events = loadUkEvents(1500);

        QVector<PoissonBaseline::EventRecord> recs;
        for (const CrimeEvent& e : events) {
            if (!e.occurredAt) continue;
            PoissonBaseline::EventRecord r;
            r.zoneId     = e.locationRaw.value_or(QStringLiteral("london"));
            r.occurredAt = *e.occurredAt;
            r.crimeType  = e.crimeType;
            recs.append(r);
        }

        PoissonBaseline baseline;
        baseline.fit(recs);
        QVERIFY(baseline.isFitted());
        QVERIFY(baseline.totalEvents() >= 1200);

        SeriesDetector detector;
        const auto series = detector.detect(events);
        int multiMember = 0;
        for (const CrimeSeries& s : series)
            if (s.members.size() >= 3) ++multiMember;

        fprintf(stderr, "EVAL: poisson_events=%d series=%d series3plus=%d\n",
                baseline.totalEvents(), series.size(), multiMember);

        QVERIFY(series.size() >= 1);
    }

    void testCoOffendingNetworks()
    {
        requireDataFile(QStringLiteral("co_offending/moreno_person_crime.csv"));
        requireDataFile(QStringLiteral("co_offending/chicago_co_offending.csv"));

        auto loadPairs = [](const QString& rel) {
            QFile f(dataFile(rel));
            QVector<PersonIncidentRecord> out;
            if (!f.open(QIODevice::ReadOnly)) return out;
            QTextStream ts(&f);
            ts.readLine();
            while (!ts.atEnd()) {
                const auto parts = ts.readLine().split(QLatin1Char(','));
                if (parts.size() < 2) continue;
                PersonIncidentRecord r;
                r.personId   = parts[0].trimmed();
                r.incidentId = parts[1].trimmed();
                r.role       = parts.size() > 2 ? parts[2].trimmed() : QStringLiteral("participant");
                out.append(r);
            }
            return out;
        };

        const auto morenoRecs   = loadPairs(QStringLiteral("co_offending/moreno_person_crime.csv"));
        const auto chicagoRecs  = loadPairs(QStringLiteral("co_offending/chicago_co_offending.csv"));

        CoOffendingAnalyser moreno, chicago;
        moreno.buildGraph(morenoRecs);
        moreno.analyse();
        chicago.buildGraph(chicagoRecs);
        chicago.analyse();

        int chicagoLeads = 0;
        QSet<QString> seen;
        for (const PersonIncidentRecord& r : chicagoRecs) {
            if (seen.contains(r.incidentId)) continue;
            seen.insert(r.incidentId);
            if (!chicago.findLeads(r.incidentId, 3).isEmpty()) ++chicagoLeads;
        }

        fprintf(stderr, "EVAL: moreno_nodes=%d chicago_nodes=%d chicago_leads=%d\n",
                moreno.nodes().size(), chicago.nodes().size(), chicagoLeads);

        QVERIFY(moreno.nodes().size() >= 200);
        QVERIFY(chicago.nodes().size() >= 50);
        QVERIFY2(chicagoLeads >= 10,
                 qPrintable(QStringLiteral("Only %1 Chicago incidents produced leads").arg(chicagoLeads)));
    }

    void testWeatherLookupCoverage()
    {
        requireDataFile(QStringLiteral("weather/london_2024_h1.json"));
        requireDataFile(QStringLiteral("crimes/uk_metropolitan_street_2024.csv"));

        QFile wf(dataFile(QStringLiteral("weather/london_2024_h1.json")));
        QVERIFY(wf.open(QIODevice::ReadOnly));
        WeatherSource ws;
        const int cached = ws.parseResponse(wf.readAll());
        QVERIFY(cached >= 4000);

        const QVector<CrimeEvent> events = loadUkEvents(500);
        int lookups = 0;
        int hits    = 0;
        for (const CrimeEvent& e : events) {
            if (!e.occurredAt) continue;
            ++lookups;
            const QDateTime hour(e.occurredAt->date(),
                                 QTime(e.occurredAt->time().hour(), 0),
                                 QTimeZone::utc());
            if (ws.dataAt(hour).has_value()) ++hits;
        }

        const double hitRate = lookups > 0 ? static_cast<double>(hits) / lookups : 0.0;
        fprintf(stderr, "EVAL: weather_cached=%d lookups=%d hits=%d hitRate=%.3f\n",
                cached, lookups, hits, hitRate);

        QVERIFY(lookups >= 400);
        // Month-level UK dates map to midnight UTC; many still resolve to a cached hour bucket.
        QVERIFY2(hitRate >= 0.5,
                 qPrintable(QStringLiteral("Weather hit rate %1").arg(hitRate)));
    }
};

QTEST_MAIN(TestRealDataEvaluation)
#include "test_real_data_evaluation.moc"
