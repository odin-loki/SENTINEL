// test_weather_source_deep4.cpp — Deep audit iteration 20: WeatherSource
// UTC offset edges, discomfort piecewise boundaries, threshold flags, cache overwrite.

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

#ifdef __GNUC__
#  pragma GCC diagnostic push
#  pragma GCC diagnostic ignored "-Wkeyword-macro"
#endif
#define private public
#include "ingest/WeatherSource.h"
#undef private
#ifdef __GNUC__
#  pragma GCC diagnostic pop
#endif

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

class WeatherSourceDeep4Test : public QObject
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

    // ── UTC offset conversion ─────────────────────────────────────────────────

    void testNegativeUtcOffsetStoresUtcHour()
    {
        WeatherSource ws;
        // UTC-5: local 07:00 → UTC 12:00
        const QByteArray json = makeOpenMeteoJson(
            {QStringLiteral("2024-03-10T07:00")}, {18.0}, {}, {}, {}, {}, {}, -18000);
        ws.parseResponse(json);

        const auto data = ws.dataAt(
            QDateTime(QDate(2024, 3, 10), QTime(12, 0), QTimeZone::utc()));
        QVERIFY2(data.has_value(), "negative offset must map local 07:00 to UTC 12:00");
        QVERIFY(std::abs(data->temperatureC - 18.0) < 0.01);
    }

    void testDataAtTruncatesSubHourComponents()
    {
        WeatherSource ws;
        ws.parseResponse(makeOpenMeteoJson({QStringLiteral("2024-04-20T14:00")}, {21.0}));

        const auto exact = ws.dataAt(
            QDateTime(QDate(2024, 4, 20), QTime(14, 0), QTimeZone::utc()));
        const auto withMinutes = ws.dataAt(
            QDateTime(QDate(2024, 4, 20), QTime(14, 37, 22), QTimeZone::utc()));

        QVERIFY(exact.has_value());
        QVERIFY(withMinutes.has_value());
        QCOMPARE(exact->temperatureC, withMinutes->temperatureC);
    }

    // ── Derived flag thresholds (boundary behaviour) ────────────────────────

    void testPrecipExactlyPointOneNotRaining()
    {
        WeatherSource ws;
        ws.parseResponse(makeOpenMeteoJson(
            {QStringLiteral("2024-05-01T10:00")}, {15.0}, {0.1}));
        const auto data = ws.dataAt(
            QDateTime(QDate(2024, 5, 1), QTime(10, 0), QTimeZone::utc()));
        QVERIFY(data.has_value());
        QVERIFY2(!data->isRaining,
                 "precipitation == 0.1 mm must NOT set isRaining (strict > 0.1)");
    }

    void testVisibilityExactly1000NotLowVisibility()
    {
        WeatherSource ws;
        ws.parseResponse(makeOpenMeteoJson(
            {QStringLiteral("2024-05-02T08:00")}, {12.0}, {}, {}, {1000.0}));
        const auto data = ws.dataAt(
            QDateTime(QDate(2024, 5, 2), QTime(8, 0), QTimeZone::utc()));
        QVERIFY(data.has_value());
        QVERIFY2(!data->isLowVisibility,
                 "visibility == 1000 m must NOT set isLowVisibility (strict < 1000)");
    }

    void testWindExactly80NotExtreme()
    {
        WeatherSource ws;
        ws.parseResponse(makeOpenMeteoJson(
            {QStringLiteral("2024-05-03T16:00")}, {10.0}, {}, {80.0}));
        const auto data = ws.dataAt(
            QDateTime(QDate(2024, 5, 3), QTime(16, 0), QTimeZone::utc()));
        QVERIFY(data.has_value());
        QVERIFY2(!data->isExtremeWind,
                 "windspeed == 80 km/h must NOT set isExtremeWind (strict > 80)");
    }

    void testIsDayZeroSetsNightFlag()
    {
        WeatherSource ws;
        ws.parseResponse(makeOpenMeteoJson(
            {QStringLiteral("2024-05-04T02:00")}, {8.0}, {}, {}, {}, {0}));
        const auto data = ws.dataAt(
            QDateTime(QDate(2024, 5, 4), QTime(2, 0), QTimeZone::utc()));
        QVERIFY(data.has_value());
        QVERIFY2(!data->isDay, "is_day=0 must set isDay false");
    }

    // ── Discomfort piecewise boundaries ───────────────────────────────────────

    void testDiscomfortPiecewiseBoundaries()
    {
        QCOMPARE(WeatherSource::discomfortIndex(9.9), 0.1);
        QVERIFY(std::abs(WeatherSource::discomfortIndex(10.0) - 0.3) < 1e-9);
        QVERIFY(std::abs(WeatherSource::discomfortIndex(20.0) - 0.5) < 1e-9);
        QVERIFY(std::abs(WeatherSource::discomfortIndex(30.0) - 0.8) < 1e-9);
        QCOMPARE(WeatherSource::discomfortIndex(40.0), 1.0);
        QCOMPARE(WeatherSource::discomfortIndex(45.0), 1.0);
    }

    // ── Cache / parse count mismatch on duplicate hour keys ───────────────────

    void testDuplicateHourKeyOverwritesButInflatesCount()
    {
        // BUG (documented): parseResponse increments cached for each row even when
        // QMap::insert overwrites an existing hour key — reported count > cache size.
        WeatherSource ws;
        const QByteArray json = makeOpenMeteoJson(
            {QStringLiteral("2024-06-01T12:00"), QStringLiteral("2024-06-01T12:00")},
            {10.0, 99.0});
        const int reported = ws.parseResponse(json);
        QCOMPARE(reported, 1);
        QCOMPARE(ws.cachedHourCount(), 1);

        const auto data = ws.dataAt(
            QDateTime(QDate(2024, 6, 1), QTime(12, 0), QTimeZone::utc()));
        QVERIFY(data.has_value());
        QVERIFY(std::abs(data->temperatureC - 99.0) < 0.01);
    }

    // ── Invalid JSON shapes ───────────────────────────────────────────────────

    void testJsonArrayRootReturnsError()
    {
        WeatherSource ws;
        const int n = ws.parseResponse(QJsonDocument(QJsonArray{1, 2, 3}).toJson());
        QVERIFY2(n < 0, "JSON array root must return parse error");
        QVERIFY(!ws.lastFetchedAt().isValid());
    }

    void testOnReplyFinishedValidJsonUpdatesLastFetched()
    {
        WeatherSource ws;
        QSignalSpy completeSpy(&ws, &WeatherSource::fetchComplete);

        ws.onReplyFinished(new FakeNetworkReply(
            makeOpenMeteoJson({QStringLiteral("2024-07-01T09:00")}, {19.0})));
        QCoreApplication::processEvents();

        QCOMPARE(completeSpy.size(), 1);
        QVERIFY(ws.lastFetchedAt().isValid());
    }
};

QTEST_GUILESS_MAIN(WeatherSourceDeep4Test)
#include "test_weather_source_deep4.moc"
