// test_ingest_enrichment.cpp — IngestEnricher quality scoring + weather enrichment
// Build via: sentinel_test(test_ingest_enrichment test_ingest_enrichment.cpp)

#include <QTest>
#include <QCoreApplication>
#include <QFile>
#include <QDir>
#include <QDate>
#include <QTime>
#include <QDateTime>
#include <QTimeZone>
#include <QJsonObject>

#include "core/AppConfig.h"
#include "core/CrimeEvent.h"
#include "ingest/CsvImporter.h"
#include "ingest/DataQualityScorer.h"
#include "ingest/IngestEnricher.h"
#include "ingest/WeatherSource.h"

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

static QString resolveCsvPath()
{
    const QStringList candidates = {
        QCoreApplication::applicationDirPath()
            + QStringLiteral("/../tests/data/london_crimes_2024.csv"),
        QStringLiteral("tests/data/london_crimes_2024.csv"),
        QCoreApplication::applicationDirPath()
            + QStringLiteral("/data/london_crimes_2024.csv"),
        QDir(QCoreApplication::applicationDirPath())
              .absoluteFilePath(QStringLiteral("../../data/crimes/london_crimes_2024.csv")),
    };
    for (const QString& path : candidates) {
        if (QFile::exists(path))
            return path;
    }
    return {};
}

static QString resolveWeatherJsonPath()
{
    const QStringList candidates = {
        QCoreApplication::applicationDirPath()
            + QStringLiteral("/data/weather/london_2024_h1.json"),
        QDir(QCoreApplication::applicationDirPath())
            .absoluteFilePath(QStringLiteral("../../data/weather/london_2024_h1.json")),
        QDir(QCoreApplication::applicationDirPath())
            .absoluteFilePath(QStringLiteral("../data/weather/london_2024_h1.json")),
        QStringLiteral("data/weather/london_2024_h1.json"),
    };
    for (const QString& path : candidates) {
        if (QFile::exists(path))
            return path;
    }
    return {};
}

// ─────────────────────────────────────────────────────────────────────────────
// TestIngestQualityScoring
// ─────────────────────────────────────────────────────────────────────────────

class TestIngestQualityScoring : public QObject
{
    Q_OBJECT

private slots:

    void testCsvEventsReceiveNonDefaultQualityScore()
    {
        const QString csvPath = resolveCsvPath();
        if (csvPath.isEmpty())
            QSKIP("London CSV test data not found — skipping");

        QVector<CrimeEvent> events = CsvImporter::importFile(
            csvPath, QStringLiteral("london_2024"));
        QVERIFY2(events.size() >= 10,
                 qPrintable(QStringLiteral("Expected >= 10 events, got %1")
                                .arg(events.size())));

        AppConfig cfg;
        IngestEnricher enricher(cfg);
        const ImportSummary summary = enricher.prepare(events);

        QCOMPARE(summary.totalParsed, events.size());
        QVERIFY(summary.avgQuality > 0.0);

        int nonDefaultCount = 0;
        for (const CrimeEvent& ev : events) {
            QVERIFY2(qAbs(ev.qualityScore - 0.5) > 1e-6,
                     qPrintable(QStringLiteral("Event %1 still has default qualityScore 0.5")
                                    .arg(ev.eventId)));
            ++nonDefaultCount;
        }
        QCOMPARE(nonDefaultCount, events.size());
        QVERIFY(summary.passRate > 0.5);
    }

    void testQualityReportsMatchEventScores()
    {
        const QString csvPath = resolveCsvPath();
        if (csvPath.isEmpty())
            QSKIP("London CSV test data not found — skipping");

        QVector<CrimeEvent> events = CsvImporter::importFile(
            csvPath, QStringLiteral("london_2024"));
        AppConfig cfg;
        IngestEnricher enricher(cfg);
        const ImportSummary summary = enricher.prepare(events);

        QCOMPARE(summary.reports.size(), events.size());
        for (int i = 0; i < events.size(); ++i) {
            QVERIFY(qAbs(events[i].qualityScore - summary.reports[i].compositeScore) < 1e-9);
        }
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// TestWeatherEnrichment
// ─────────────────────────────────────────────────────────────────────────────

class TestWeatherEnrichment : public QObject
{
    Q_OBJECT

private slots:

    void testWeatherSourceParsesLondonJson()
    {
        const QString jsonPath = resolveWeatherJsonPath();
        if (jsonPath.isEmpty())
            QSKIP("London weather JSON not found — skipping");

        QFile f(jsonPath);
        QVERIFY(f.open(QIODevice::ReadOnly));
        WeatherSource ws;
        const int hours = ws.parseResponse(f.readAll());
        QVERIFY2(hours > 0,
                 qPrintable(QStringLiteral("Expected cached hours > 0, got %1").arg(hours)));
    }

    void testIngestEnricherAttachesWeatherMeta()
    {
        const QString csvPath = resolveCsvPath();
        const QString jsonPath = resolveWeatherJsonPath();
        if (csvPath.isEmpty())
            QSKIP("London CSV test data not found — skipping");
        if (jsonPath.isEmpty())
            QSKIP("London weather JSON not found — skipping");

        QVector<CrimeEvent> events = CsvImporter::importFile(
            csvPath, QStringLiteral("london_2024"));
        QVERIFY(!events.isEmpty());

        AppConfig cfg;
        IngestEnricher enricher(cfg);
        const ImportSummary summary = enricher.prepare(events);
        Q_UNUSED(summary);

        int withWeather = 0;
        for (const CrimeEvent& ev : events) {
            if (!ev.meta.contains(QStringLiteral("weather")))
                continue;
            const QJsonObject w = ev.meta.value(QStringLiteral("weather")).toObject();
            QVERIFY(w.contains(QStringLiteral("temperatureC")));
            QVERIFY(w.contains(QStringLiteral("precipitationMm")));
            QVERIFY(w.contains(QStringLiteral("windspeedKmh")));
            ++withWeather;
        }

        QVERIFY2(withWeather > 0,
               "Expected at least one London event enriched with weather metadata");
    }

    void testWeatherMetaMatchesParseResponsePattern()
    {
        const QString jsonPath = resolveWeatherJsonPath();
        if (jsonPath.isEmpty())
            QSKIP("London weather JSON not found — skipping");

        QFile f(jsonPath);
        QVERIFY(f.open(QIODevice::ReadOnly));

        WeatherSource ws;
        QVERIFY(ws.parseResponse(f.readAll()) > 0);

        CrimeEvent ev;
        ev.eventId = QStringLiteral("weather-pattern-001");
        ev.lat = 51.5074;
        ev.lon = -0.1278;
        ev.occurredAt = QDateTime(QDate(2024, 1, 3), QTime(12, 0, 0), QTimeZone::utc());

        const auto wd = ws.dataAt(*ev.occurredAt);
        if (!wd.has_value())
            QSKIP("No weather hour matching test event timestamp — skipping");

        QJsonObject w;
        w[QStringLiteral("temperatureC")]     = wd->temperatureC;
        w[QStringLiteral("precipitationMm")]    = wd->precipitationMm;
        w[QStringLiteral("windspeedKmh")]       = wd->windspeedKmh;
        w[QStringLiteral("isRaining")]          = wd->isRaining;
        w[QStringLiteral("tempDiscomfort")]     = wd->tempDiscomfort;
        w[QStringLiteral("isDay")]              = wd->isDay;
        ev.meta[QStringLiteral("weather")] = w;

        const QJsonObject stored = ev.meta.value(QStringLiteral("weather")).toObject();
        QVERIFY(stored.contains(QStringLiteral("temperatureC")));
        QVERIFY(qAbs(stored.value(QStringLiteral("temperatureC")).toDouble()
                     - wd->temperatureC) < 1e-6);
    }
};

// ─────────────────────────────────────────────────────────────────────────────

static int runTest(QObject* obj, const char* logFile)
{
    QStringList args = { QStringLiteral("test"),
                         QStringLiteral("-o"),
                         QString(QStringLiteral("%1,txt")).arg(logFile) };
    return QTest::qExec(obj, args);
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    int r = 0;
    TestIngestQualityScoring t1; r |= runTest(&t1, "ingest_quality.txt");
    TestWeatherEnrichment  t2; r |= runTest(&t2, "ingest_weather.txt");
    return r;
}

#include "test_ingest_enrichment.moc"
