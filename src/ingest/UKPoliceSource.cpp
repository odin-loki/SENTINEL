#include "ingest/UKPoliceSource.h"
#include "core/SentinelLogger.h"
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QUrlQuery>
#include <QDateTime>
#include <QDate>
#include <QEventLoop>
#include <QTimer>
#include <QTimeZone>

const QString UKPoliceSource::BASE_URL = QStringLiteral("https://data.police.uk/api");

const QMap<QString, QString> UKPoliceSource::CRIME_TYPE_MAP = {
    { "burglary",                   "burglary"         },
    { "robbery",                    "robbery"          },
    { "violent-crime",              "assault"          },
    { "vehicle-crime",              "vehicle_crime"    },
    { "drugs",                      "drug_offence"     },
    { "anti-social-behaviour",      "antisocial"       },
    { "theft-from-the-person",      "theft"            },
    { "other-theft",                "theft"            },
    { "shoplifting",                "theft"            },
    { "criminal-damage-arson",      "criminal_damage"  },
    { "public-order",               "public_order"     },
    { "possession-of-weapons",      "weapons"          },
};

UKPoliceSource::UKPoliceSource(double lat, double lon, double radiusKm, QObject* parent)
    : DataSource(parent)
    , m_nam(new QNetworkAccessManager(this))
    , m_rateLimitTimer(new QTimer(this))
    , m_lat(lat)
    , m_lon(lon)
    , m_radiusKm(radiusKm)
{
    m_rateLimitTimer->setInterval(1000);
    m_rateLimitTimer->setSingleShot(false);
    connect(m_rateLimitTimer, &QTimer::timeout, this, &UKPoliceSource::processNextRequest);
    connect(m_nam, &QNetworkAccessManager::finished, this, &UKPoliceSource::onReplyFinished);
}

void UKPoliceSource::setLocation(double lat, double lon, double radiusKm)
{
    m_lat = lat;
    m_lon = lon;
    m_radiusKm = radiusKm;
}

void UKPoliceSource::fetchSince(const QDateTime& since)
{
    qCInfo(lcIngest) << "Fetching UK Police data for" << m_lat << m_lon;
    m_pendingUrls.clear();
    m_fetchCount       = 0;
    m_inFlightRequests = 0;

    const QDate startDate = since.date();
    const QDate endDate   = QDate::currentDate();

    QDate cursor = QDate(startDate.year(), startDate.month(), 1);
    while (cursor <= endDate) {
        m_pendingUrls.enqueue(
            buildFetchUrl(m_lat, m_lon, cursor.toString(QStringLiteral("yyyy-MM"))));
        cursor = cursor.addMonths(1);
    }

    m_totalRequests = m_pendingUrls.size();
    if (m_totalRequests == 0) {
        emit fetchComplete(0);
        return;
    }

    m_rateLimitTimer->start();
    processNextRequest();
}

void UKPoliceSource::processNextRequest()
{
    if (m_pendingUrls.isEmpty()) {
        m_rateLimitTimer->stop();
        return;
    }
    const QUrl url = m_pendingUrls.dequeue();
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("SENTINEL/1.0"));
    ++m_inFlightRequests;
    m_nam->get(req);
}

void UKPoliceSource::onReplyFinished(QNetworkReply* reply)
{
    reply->deleteLater();
    if (m_inFlightRequests > 0) --m_inFlightRequests;

    if (reply->error() != QNetworkReply::NoError) {
        emit fetchError(reply->errorString());
        if (m_pendingUrls.isEmpty() && m_inFlightRequests == 0) {
            m_rateLimitTimer->stop();
            emit fetchComplete(m_fetchCount);
        }
        return;
    }

    const QByteArray body = reply->readAll();
    const QJsonDocument doc = QJsonDocument::fromJson(body);
    if (!doc.isArray()) {
        if (m_pendingUrls.isEmpty() && m_inFlightRequests == 0) {
            m_rateLimitTimer->stop();
            emit fetchComplete(m_fetchCount);
        }
        return;
    }

    const QJsonArray arr = doc.array();
    for (const QJsonValue& val : arr) {
        if (!val.isObject()) continue;
        const CrimeEvent ev = parseRawEvent(val.toObject());
        ++m_fetchCount;
        emit eventFetched(ev);
    }

    const int done = m_totalRequests - static_cast<int>(m_pendingUrls.size());
    emit progress(done, m_totalRequests);

    if (m_pendingUrls.isEmpty() && m_inFlightRequests == 0) {
        m_rateLimitTimer->stop();
        qCInfo(lcIngest) << "Fetched" << m_fetchCount << "events from UK Police API";
        emit fetchComplete(m_fetchCount);
    }
}

CrimeEvent UKPoliceSource::parseRawEvent(const QJsonObject& raw) const
{
    CrimeEvent ev;
    ev.source        = sourceId();
    ev.sourceVersion = QStringLiteral("1.0");
    ev.ingestedAt    = QDateTime::currentDateTimeUtc();

    // Event ID
    const QString rawId = raw.value(QStringLiteral("id")).toVariant().toString();
    ev.eventId = QStringLiteral("uk_") + rawId;

    // Occurred date: "month" field is "YYYY-MM"
    const QString month = raw.value(QStringLiteral("month")).toString();
    if (!month.isEmpty()) {
        const QDate d = QDate::fromString(month, QStringLiteral("yyyy-MM"));
        if (d.isValid()) {
            ev.occurredAt = QDateTime(d, QTime(0, 0), QTimeZone::utc());
        }
    }

    // Crime type
    const QString rawCategory = raw.value(QStringLiteral("category")).toString();
    if (!rawCategory.isEmpty()) {
        ev.crimeType = CRIME_TYPE_MAP.value(rawCategory, rawCategory);
    }

    // Location
    const QJsonObject loc = raw.value(QStringLiteral("location")).toObject();
    if (!loc.isEmpty()) {
        bool latOk = false, lonOk = false;
        const double lat = loc.value(QStringLiteral("latitude")).toString().toDouble(&latOk);
        const double lon = loc.value(QStringLiteral("longitude")).toString().toDouble(&lonOk);
        if (latOk) ev.lat = lat;
        if (lonOk) ev.lon = lon;

        const QJsonObject street = loc.value(QStringLiteral("street")).toObject();
        const QString streetName = street.value(QStringLiteral("name")).toString();
        if (!streetName.isEmpty()) {
            ev.locationRaw = streetName;
        }
    }

    // Outcome
    const QJsonObject outcomeStatus = raw.value(QStringLiteral("outcome_status")).toObject();
    if (!outcomeStatus.isEmpty()) {
        const QString category = outcomeStatus.value(QStringLiteral("category")).toString();
        if (!category.isEmpty()) {
            ev.outcome = category;
        }
    }

    // Context as narrative
    const QString context = raw.value(QStringLiteral("context")).toString();
    if (!context.isEmpty()) {
        ev.narrative = context;
    }

    return ev;
}

QVector<CrimeEvent> UKPoliceSource::fetchSync(const QDateTime& since, int timeoutMs)
{
    QVector<CrimeEvent> collected;
    QEventLoop loop;

    // Capture events as they are emitted
    QMetaObject::Connection connEvent = connect(
        this, &DataSource::eventFetched,
        [&collected](const CrimeEvent& ev) { collected.append(ev); });

    QMetaObject::Connection connDone = connect(
        this, &DataSource::fetchComplete,
        [&loop](int) { loop.quit(); });

    QMetaObject::Connection connErr = connect(
        this, &DataSource::fetchError,
        [&loop](const QString&) { loop.quit(); });

    // Safety timeout
    QTimer timer;
    timer.setSingleShot(true);
    connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    timer.start(timeoutMs);

    fetchSince(since);
    loop.exec();

    disconnect(connEvent);
    disconnect(connDone);
    disconnect(connErr);

    return collected;
}

QStringList UKPoliceSource::availableCategories() const
{
    return CRIME_TYPE_MAP.keys();
}

QUrl UKPoliceSource::buildFetchUrl(double lat, double lon, const QString& yyyyMm,
                                   const QString& category)
{
    QUrl url(BASE_URL + QStringLiteral("/crimes-street/") + category);
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("lat"),  QString::number(lat, 'f', 6));
    query.addQueryItem(QStringLiteral("lng"),  QString::number(lon, 'f', 6));
    query.addQueryItem(QStringLiteral("date"), yyyyMm);
    url.setQuery(query);
    return url;
}

bool UKPoliceSource::healthCheck()
{
    QUrl url(BASE_URL + QStringLiteral("/crimes-no-location-categories"));
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("SENTINEL/1.0"));

    QEventLoop loop;
    QNetworkReply* reply = m_nam->get(req);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    const int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    reply->deleteLater();
    return (statusCode == 200);
}
