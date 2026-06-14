#include "ingest/IngestEnricher.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonObject>
#include <cmath>

namespace {
QString resolveBundledRoot()
{
    const QByteArray envData = qgetenv("SENTINEL_DATA_DIR");
    if (!envData.isEmpty()) {
        const QString fromEnv = QString::fromUtf8(envData);
        if (QDir(fromEnv).exists())
            return QDir(fromEnv).absolutePath();
    }

    const QString appDir = QCoreApplication::applicationDirPath();
#if defined(Q_OS_WIN)
    const QString rel = appDir + QStringLiteral("/../share/sentinel/data");
#else
    const QString rel = appDir + QStringLiteral("/../share/sentinel/data");
    if (!QDir(rel).exists())
        return appDir + QStringLiteral("/../../share/sentinel/data");
#endif
    if (QDir(rel).exists())
        return QDir(rel).absolutePath();

    // Dev / test fallback: walk up from binary dir looking for data/weather bundle
    QDir dir(appDir);
    for (int depth = 0; depth < 10; ++depth) {
        const QString candidate = dir.absoluteFilePath(QStringLiteral("data"));
        if (QFile::exists(candidate + QStringLiteral("/weather/london_2024_h1.json")))
            return candidate;
        const QString testsData = dir.absoluteFilePath(QStringLiteral("tests/data"));
        if (QFile::exists(testsData + QStringLiteral("/weather/london_2024_h1.json")))
            return testsData;
        if (!dir.cdUp())
            break;
    }

    for (const QString& relDev : {QStringLiteral("/../data"), QStringLiteral("/../../data")}) {
        const QString dev = QDir(appDir + relDev).absolutePath();
        if (QDir(dev).exists())
            return dev;
    }
    return QDir(appDir).absolutePath();
}
} // namespace

IngestEnricher::IngestEnricher(const AppConfig& cfg)
    : m_cfg(cfg)
    , m_scorer(DataQualityScorer::withDefaults())
{}

QString IngestEnricher::bundledDataDir()
{
    return resolveBundledRoot();
}

QString IngestEnricher::defaultSampleCsv()
{
    return bundledDataDir() + QStringLiteral("/crimes/london_crimes_2024.csv");
}

ImportSummary IngestEnricher::prepare(QVector<CrimeEvent>& events)
{
    ImportSummary summary;
    summary.totalParsed = events.size();
    scoreEvents(events, summary);
    enrichWeather(events);
    return summary;
}

void IngestEnricher::scoreEvents(QVector<CrimeEvent>& events, ImportSummary& summary)
{
    summary.reports = m_scorer.scoreBatch(events);
    summary.passingCount = 0;
    double qualitySum = 0.0;

    for (int i = 0; i < events.size(); ++i) {
        auto& report = summary.reports[i];
        report.quarantined = report.compositeScore < m_cfg.qualityThreshold;
        events[i].qualityScore = report.compositeScore;
        qualitySum += report.compositeScore;
        if (!report.quarantined)
            ++summary.passingCount;
    }

    summary.quarantinedCount = summary.totalParsed - summary.passingCount;
    summary.passRate = summary.totalParsed > 0
        ? static_cast<double>(summary.passingCount) / summary.totalParsed
        : 0.0;
    summary.avgQuality = summary.totalParsed > 0
        ? qualitySum / summary.totalParsed
        : 0.0;
}

bool IngestEnricher::loadWeatherCache(WeatherSource& source, const QString& jsonPath)
{
    QFile f(jsonPath);
    if (!f.open(QIODevice::ReadOnly))
        return false;
    return source.parseResponse(f.readAll()) > 0;
}

void IngestEnricher::enrichWeather(QVector<CrimeEvent>& events)
{
    if (events.isEmpty())
        return;

    double minLat = 90, maxLat = -90, minLon = 180, maxLon = -180;
    QDate minDate = QDate::currentDate(), maxDate = QDate(2000, 1, 1);
    bool hasGeo = false;

    for (const auto& ev : events) {
        const double lat = ev.lat.value_or(ev.latitude);
        const double lon = ev.lon.value_or(ev.longitude);
        if (std::abs(lat) < 1e-6 && std::abs(lon) < 1e-6)
            continue;
        hasGeo = true;
        minLat = std::min(minLat, lat);
        maxLat = std::max(maxLat, lat);
        minLon = std::min(minLon, lon);
        maxLon = std::max(maxLon, lon);
        if (ev.occurredAt.has_value()) {
            const QDate d = ev.occurredAt->date();
            minDate = std::min(minDate, d);
            maxDate = std::max(maxDate, d);
        }
    }
    if (!hasGeo)
        return;

    WeatherSource weather;
    const QString weatherJson = bundledDataDir() + QStringLiteral("/weather/london_2024_h1.json");
    if (!loadWeatherCache(weather, weatherJson))
        return;

    // London bbox rough check — skip if events are far from UK sample weather
    const bool nearLondon = minLat > 50.0 && maxLat < 53.0 && minLon > -1.5 && maxLon < 1.5;
    if (!nearLondon)
        return;

    for (auto& ev : events) {
        if (!ev.occurredAt.has_value())
            continue;
        const auto wd = weather.dataAt(*ev.occurredAt);
        if (!wd.has_value())
            continue;
        QJsonObject w;
        w[QStringLiteral("temperatureC")]    = wd->temperatureC;
        w[QStringLiteral("precipitationMm")] = wd->precipitationMm;
        w[QStringLiteral("windspeedKmh")]    = wd->windspeedKmh;
        w[QStringLiteral("isRaining")]       = wd->isRaining;
        w[QStringLiteral("tempDiscomfort")]  = wd->tempDiscomfort;
        w[QStringLiteral("isDay")]           = wd->isDay;
        ev.meta[QStringLiteral("weather")] = w;
    }
}
