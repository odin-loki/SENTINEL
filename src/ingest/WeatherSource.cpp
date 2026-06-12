#include "ingest/WeatherSource.h"
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QUrlQuery>
#include <QTimeZone>

WeatherSource::WeatherSource(QObject* parent)
    : QObject(parent)
    , m_nam(new QNetworkAccessManager(this))
{
    connect(m_nam, &QNetworkAccessManager::finished,
            this,  &WeatherSource::onReplyFinished);
}

void WeatherSource::fetchHistorical(double lat, double lon,
                                     const QDate& start, const QDate& end)
{
    QUrl url(QStringLiteral("https://api.open-meteo.com/v1/forecast"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("latitude"),   QString::number(lat, 'f', 6));
    query.addQueryItem(QStringLiteral("longitude"),  QString::number(lon, 'f', 6));
    query.addQueryItem(QStringLiteral("start_date"), start.toString(Qt::ISODate));
    query.addQueryItem(QStringLiteral("end_date"),   end.toString(Qt::ISODate));
    query.addQueryItem(QStringLiteral("hourly"),
        QStringLiteral("temperature_2m,precipitation,windspeed_10m,visibility,is_day,weathercode"));
    query.addQueryItem(QStringLiteral("timezone"), QStringLiteral("auto"));
    url.setQuery(query);

    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("SENTINEL/1.0"));
    m_nam->get(req);
}

int WeatherSource::parseResponse(const QByteArray& json)
{
    const QJsonDocument doc = QJsonDocument::fromJson(json);
    if (!doc.isObject())
        return -1;  // not a valid Open-Meteo JSON object

    const QJsonObject root   = doc.object();
    const QJsonObject hourly = root.value(QStringLiteral("hourly")).toObject();

    const QJsonArray times      = hourly.value(QStringLiteral("time")).toArray();
    const QJsonArray temps      = hourly.value(QStringLiteral("temperature_2m")).toArray();
    const QJsonArray precip     = hourly.value(QStringLiteral("precipitation")).toArray();
    const QJsonArray windspeed  = hourly.value(QStringLiteral("windspeed_10m")).toArray();
    const QJsonArray visibility = hourly.value(QStringLiteral("visibility")).toArray();
    const QJsonArray isDay      = hourly.value(QStringLiteral("is_day")).toArray();
    const QJsonArray wcode      = hourly.value(QStringLiteral("weathercode")).toArray();

    const int count = static_cast<int>(times.size());
    int cached = 0;

    // Open-Meteo returns times in local ISO8601 without offset;
    // use utc_offset_seconds to reconstruct UTC key.
    const int utcOffsetSec = root.value(QStringLiteral("utc_offset_seconds")).toInt(0);

    for (int i = 0; i < count; ++i) {
        const QString timeStr = times.at(i).toString();
        // Format: "2024-01-15T14:00"
        QDateTime dt = QDateTime::fromString(timeStr, QStringLiteral("yyyy-MM-ddTHH:mm"));
        if (!dt.isValid()) continue;
        dt.setTimeZone(QTimeZone::utc());
        dt = dt.addSecs(-utcOffsetSec); // convert local to UTC

        WeatherData wd;
        wd.temperatureC    = i < temps.size()      ? temps.at(i).toDouble()      : 0.0;
        wd.precipitationMm = i < precip.size()     ? precip.at(i).toDouble()     : 0.0;
        wd.windspeedKmh    = i < windspeed.size()  ? windspeed.at(i).toDouble()  : 0.0;
        wd.visibilityM     = i < visibility.size() ? visibility.at(i).toDouble() : 10000.0;
        wd.isDay           = i < isDay.size()      ? (isDay.at(i).toInt() == 1)  : true;
        wd.weatherCode     = i < wcode.size()      ? wcode.at(i).toInt()         : 0;

        wd.tempDiscomfort  = computeDiscomfort(wd.temperatureC);
        wd.isRaining       = (wd.precipitationMm > 0.1);
        wd.isLowVisibility = (wd.visibilityM < 1000.0);
        wd.isExtremeWind   = (wd.windspeedKmh > 80.0);

        // Key on the truncated UTC hour
        const QDateTime key = QDateTime(dt.date(), QTime(dt.time().hour(), 0, 0), QTimeZone::utc());
        const bool isNew = !m_cache.contains(key);
        m_cache.insert(key, wd);
        if (isNew)
            ++cached;
    }

    // Mark as successfully parsed (even if 0 records — API responded correctly)
    m_lastFetchedAt = QDateTime::currentDateTimeUtc();
    return cached;
}

void WeatherSource::onReplyFinished(QNetworkReply* reply)
{
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError) {
        emit fetchError(reply->errorString());
        return;
    }

    const QByteArray body = reply->readAll();
    const int count = parseResponse(body);
    if (count < 0) {
        emit fetchError(QStringLiteral("Invalid JSON response from Open-Meteo"));
        return;
    }

    emit fetchComplete(count);
}

std::optional<WeatherData> WeatherSource::dataAt(const QDateTime& dt) const
{
    const QDateTime key = QDateTime(dt.toUTC().date(),
                                    QTime(dt.toUTC().time().hour(), 0, 0),
                                    QTimeZone::utc());
    const auto it = m_cache.find(key);
    if (it == m_cache.end()) return std::nullopt;
    return it.value();
}

double WeatherSource::computeDiscomfort(double tempC)
{
    if (tempC < 10.0)
        return 0.1;
    if (tempC < 20.0)
        return 0.3 + (tempC - 10.0) * 0.02;
    if (tempC < 30.0)
        return 0.5 + (tempC - 20.0) * 0.03;
    if (tempC < 40.0)
        return 0.8 + (tempC - 30.0) * 0.02;
    return 1.0; // >= 40°C (extreme heat — maximum discomfort)
}
