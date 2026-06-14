// test_weather_source_deep5.cpp — Deep audit iteration 23: WeatherSource
// discomfort bounds, cache hits, API-unavailable defaults, hourly temperature
// parsing, and fetch URL lat/lon construction.

#include <QTest>
#include <QCoreApplication>
#include <QSignalSpy>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimeZone>
#include <cmath>

#include "ingest/WeatherSource.h"

class FakeNetworkReply : public QNetworkReply
{
public:
    explicit FakeNetworkReply(const QByteArray& payload,
                              QNetworkReply::NetworkError err = QNetworkReply::NoError,
                              QObject* parent = nullptr)
        : QNetworkReply(parent)
        , m_payload(payload)
        , m_offset(0)
    {
        setError(err, err == QNetworkReply::NoError ? QString() : QStringLiteral("mock error"));
        if (err == QNetworkReply::NoError)
            setAttribute(QNetworkRequest::HttpStatusCodeAttribute, 200);
        open(QIODevice::ReadOnly);
    }

    void abort() override {}

    qint64 bytesAvailable() const override
    {
        return QIODevice::bytesAvailable() + (m_payload.size() - m_offset);
    }

    bool isSequential() const override { return true; }

protected:
    qint64 readData(char* buf, qint64 maxLen) override
    {
        const qint64 n = qMin(maxLen, m_payload.size() - m_offset);
        if (n <= 0) return 0;
        memcpy(buf, m_payload.constData() + m_offset, static_cast<size_t>(n));
        m_offset += n;
        return n;
    }

private:
    QByteArray m_payload;
    qint64     m_offset;
};

class RecordingNam : public QNetworkAccessManager
{
public:
    explicit RecordingNam(QObject* parent = nullptr)
        : QNetworkAccessManager(parent)
    {}
    QList<QUrl> requestedUrls;

protected:
    QNetworkReply* createRequest(Operation op,
                                 const QNetworkRequest& req,
                                 QIODevice* outgoingData) override
    {
        Q_UNUSED(op);
        Q_UNUSED(outgoingData);
        requestedUrls.append(req.url());
        return new FakeNetworkReply(QByteArrayLiteral("{}"));
    }
};

class WeatherSourceDeep5Test : public QObject
{
    Q_OBJECT

    static QByteArray makeOpenMeteoJson(const QStringList& times,
                                        const QList<double>& temps,
                                        const QList<double>& precip = {},
                                        const QList<double>& wind = {},
                                        const QList<double>& vis = {},
                                        const QList<int>& isDay = {},
                                        const QList<int>& wcode = {},
                                        int utcOffsetSec = 0)
    {
        QJsonArray timesArr, tempsArr, precipArr, windArr, visArr, isDayArr, wcodeArr;
        for (const QString& t : times) timesArr.append(t);
        for (double v : temps)        tempsArr.append(v);
        for (double v : precip)       precipArr.append(v);
        for (double v : wind)         windArr.append(v);
        for (double v : vis)          visArr.append(v);
        for (int v : isDay)           isDayArr.append(v);
        for (int v : wcode)           wcodeArr.append(v);

        QJsonObject hourly;
        hourly[QStringLiteral("time")]           = timesArr;
        hourly[QStringLiteral("temperature_2m")] = tempsArr;
        if (!precip.isEmpty()) hourly[QStringLiteral("precipitation")]  = precipArr;
        if (!wind.isEmpty())   hourly[QStringLiteral("windspeed_10m")]  = windArr;
        if (!vis.isEmpty())    hourly[QStringLiteral("visibility")]     = visArr;
        if (!isDay.isEmpty())  hourly[QStringLiteral("is_day")]         = isDayArr;
        if (!wcode.isEmpty())  hourly[QStringLiteral("weathercode")]    = wcodeArr;

        QJsonObject root;
        root[QStringLiteral("utc_offset_seconds")] = utcOffsetSec;
        root[QStringLiteral("hourly")]             = hourly;
        return QJsonDocument(root).toJson();
    }

private slots:

    // ── discomfort index documented range [0.1, 1.0] ─────────────────────────

    void testDiscomfortIndexWithinDocumentedRange()
    {
        const QList<double> samples = { -5.0, 0.0, 9.9, 10.0, 15.0, 20.0,
                                      25.0, 30.0, 35.0, 40.0, 50.0 };
        for (double temp : samples) {
            const double d = WeatherSource::discomfortIndex(temp);
            QVERIFY2(d >= 0.1 && d <= 1.0,
                     qPrintable(QStringLiteral("temp %1 → discomfort %2 out of range")
                                    .arg(temp).arg(d)));
        }
        QCOMPARE(WeatherSource::discomfortIndex(9.9), 0.1);
        QCOMPARE(WeatherSource::discomfortIndex(40.0), 1.0);
        QCOMPARE(WeatherSource::discomfortIndex(50.0), 1.0);
    }

    // ── cache hit returns same data without re-fetch ──────────────────────────

    void testCacheHitReturnsIdenticalDataWithoutRefetch()
    {
        WeatherSource ws;
        ws.parseResponse(makeOpenMeteoJson(
            {QStringLiteral("2024-09-10T11:00")}, {17.5}));

        const QDateTime key(QDate(2024, 9, 10), QTime(11, 0), QTimeZone::utc());
        const auto first  = ws.dataAt(key);
        const auto second = ws.dataAt(key);

        QVERIFY(first.has_value());
        QVERIFY(second.has_value());
        QCOMPARE(first->temperatureC, second->temperatureC);
        QCOMPARE(first->tempDiscomfort, second->tempDiscomfort);
        QCOMPARE(ws.cachedHourCount(), 1);
    }

    // ── default values when API fields missing / unavailable ──────────────────

    void testSparseHourlyArraysUseDocumentedDefaults()
    {
        WeatherSource ws;
        QJsonObject hourly;
        hourly[QStringLiteral("time")]           = QJsonArray{QStringLiteral("2024-10-05T06:00")};
        hourly[QStringLiteral("temperature_2m")] = QJsonArray{};

        QJsonObject root;
        root[QStringLiteral("utc_offset_seconds")] = 0;
        root[QStringLiteral("hourly")]               = hourly;

        ws.parseResponse(QJsonDocument(root).toJson());
        const auto data = ws.dataAt(
            QDateTime(QDate(2024, 10, 5), QTime(6, 0), QTimeZone::utc()));
        QVERIFY(data.has_value());
        QCOMPARE(data->temperatureC, 0.0);
        QCOMPARE(data->visibilityM, 10000.0);
        QVERIFY(data->isDay);
        QCOMPARE(data->weatherCode, 0);
    }

    void testNetworkErrorLeavesCacheEmptyAndNoTimestamp()
    {
        WeatherSource ws;
        QSignalSpy errorSpy(&ws, &WeatherSource::fetchError);

        ws.onReplyFinished(new FakeNetworkReply(
            QByteArray{}, QNetworkReply::HostNotFoundError));
        QCoreApplication::processEvents();

        QCOMPARE(errorSpy.size(), 1);
        QCOMPARE(ws.cachedHourCount(), 0);
        QVERIFY(!ws.lastFetchedAt().isValid());
        QVERIFY(!ws.dataAt(QDateTime(QDate(2024, 1, 1), QTime(12, 0), QTimeZone::utc())).has_value());
    }

    // ── hourly temperature parsing ────────────────────────────────────────────

    void testMultipleHourlyTemperaturesParsed()
    {
        WeatherSource ws;
        const int inserted = ws.parseResponse(makeOpenMeteoJson(
            {QStringLiteral("2024-11-01T08:00"),
             QStringLiteral("2024-11-01T09:00"),
             QStringLiteral("2024-11-01T10:00")},
            {6.0, 8.5, 11.2}));

        QCOMPARE(inserted, 3);
        QCOMPARE(ws.cachedHourCount(), 3);

        const auto h8  = ws.dataAt(QDateTime(QDate(2024, 11, 1), QTime(8, 0), QTimeZone::utc()));
        const auto h9  = ws.dataAt(QDateTime(QDate(2024, 11, 1), QTime(9, 0), QTimeZone::utc()));
        const auto h10 = ws.dataAt(QDateTime(QDate(2024, 11, 1), QTime(10, 0), QTimeZone::utc()));

        QVERIFY(h8.has_value() && h9.has_value() && h10.has_value());
        QVERIFY(std::abs(h8->temperatureC  - 6.0)  < 0.01);
        QVERIFY(std::abs(h9->temperatureC  - 8.5)  < 0.01);
        QVERIFY(std::abs(h10->temperatureC - 11.2) < 0.01);
    }

    // ── fetchHistorical URL includes lat/lon ──────────────────────────────────

    void testFetchHistoricalUrlIncludesLatLon()
    {
        WeatherSource ws;
        auto* nam = new RecordingNam(&ws);
        ws.setNetworkManagerForTesting(nam);

        ws.fetchHistorical(51.507400, -0.127800,
                           QDate(2024, 3, 1), QDate(2024, 3, 7));
        QCoreApplication::processEvents();

        QVERIFY(!nam->requestedUrls.isEmpty());
        const QString url = nam->requestedUrls.first().toString();
        QVERIFY2(url.contains(QStringLiteral("latitude=51.5074")),
                 qPrintable(QStringLiteral("URL missing latitude: %1").arg(url)));
        QVERIFY2(url.contains(QStringLiteral("longitude=-0.1278")),
                 qPrintable(QStringLiteral("URL missing longitude: %1").arg(url)));
        QVERIFY(url.contains(QStringLiteral("start_date=2024-03-01")));
        QVERIFY(url.contains(QStringLiteral("end_date=2024-03-07")));
    }
};

QTEST_GUILESS_MAIN(WeatherSourceDeep5Test)
#include "test_weather_source_deep5.moc"
