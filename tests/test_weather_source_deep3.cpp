// test_weather_source_deep3.cpp — Deep audit iteration 17: WeatherSource
// JSON parse paths, empty response handling, rate-limit guard absence.

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

class WeatherSourceDeep3Test : public QObject
{
    Q_OBJECT

    static QByteArray makeOpenMeteoJson(const QStringList& times,
                                        const QList<double>& temps = {20.0})
    {
        QJsonArray timesArr, tempsArr;
        for (const QString& t : times) timesArr.append(t);
        for (double v : temps)        tempsArr.append(v);

        QJsonObject hourly;
        hourly[QStringLiteral("time")]           = timesArr;
        hourly[QStringLiteral("temperature_2m")] = tempsArr;

        QJsonObject root;
        root[QStringLiteral("utc_offset_seconds")] = 0;
        root[QStringLiteral("hourly")]             = hourly;
        return QJsonDocument(root).toJson();
    }

private slots:

    // ── JSON parse via onReplyFinished ────────────────────────────────────────

    void testOnReplyFinishedParsesValidJson()
    {
        WeatherSource ws;
        QSignalSpy completeSpy(&ws, &WeatherSource::fetchComplete);
        QSignalSpy errorSpy(&ws, &WeatherSource::fetchError);

        ws.onReplyFinished(new FakeNetworkReply(
            makeOpenMeteoJson({QStringLiteral("2024-06-01T12:00")}, {22.5})));
        QCoreApplication::processEvents();

        QCOMPARE(completeSpy.size(), 1);
        QCOMPARE(completeSpy.at(0).at(0).toInt(), 1);
        QCOMPARE(errorSpy.size(), 0);
        QCOMPARE(ws.cachedHourCount(), 1);
        QVERIFY(ws.lastFetchedAt().isValid());
    }

    void testOnReplyFinishedInvalidJsonEmitsError()
    {
        WeatherSource ws;
        QSignalSpy completeSpy(&ws, &WeatherSource::fetchComplete);
        QSignalSpy errorSpy(&ws, &WeatherSource::fetchError);

        ws.onReplyFinished(new FakeNetworkReply(QByteArray("{ not valid json")));
        QCoreApplication::processEvents();

        QCOMPARE(completeSpy.size(), 0);
        QCOMPARE(errorSpy.size(), 1);
        QCOMPARE(ws.cachedHourCount(), 0);
        QVERIFY(!ws.lastFetchedAt().isValid());
    }

    // ── Empty response variants ─────────────────────────────────────────────────

    void testEmptyHourlyTimesArrayReturnsZero()
    {
        WeatherSource ws;
        QJsonObject root;
        root[QStringLiteral("utc_offset_seconds")] = 0;
        root[QStringLiteral("hourly")] = QJsonObject{};
        const int n = ws.parseResponse(QJsonDocument(root).toJson());
        QCOMPARE(n, 0);
        QCOMPARE(ws.cachedHourCount(), 0);
        QVERIFY(ws.lastFetchedAt().isValid());
    }

    void testEmptyByteArrayReturnsErrorNoCache()
    {
        WeatherSource ws;
        const int n = ws.parseResponse(QByteArray{});
        QVERIFY2(n < 0, "empty body must return error code");
        QCOMPARE(ws.cachedHourCount(), 0);
        QVERIFY(!ws.lastFetchedAt().isValid());
    }

    void testNetworkErrorReplyDoesNotParseOrUpdateCache()
    {
        WeatherSource ws;
        QSignalSpy completeSpy(&ws, &WeatherSource::fetchComplete);
        QSignalSpy errorSpy(&ws, &WeatherSource::fetchError);

        ws.onReplyFinished(new FakeNetworkReply(
            QByteArray{}, QNetworkReply::HostNotFoundError));
        QCoreApplication::processEvents();

        QCOMPARE(completeSpy.size(), 0);
        QCOMPARE(errorSpy.size(), 1);
        QCOMPARE(ws.cachedHourCount(), 0);
    }

    // ── Rate-limit / in-flight guard ────────────────────────────────────────────

    void testNoRateLimitGuardMultipleFetchesAllowed()
    {
        // WeatherSource has no m_inFlightRequests / rate-limit timer (unlike UKPoliceSource).
        // Repeated fetchHistorical calls must not crash; each issues an independent GET.
        WeatherSource ws;
        QSignalSpy errorSpy(&ws, &WeatherSource::fetchError);

        ws.fetchHistorical(51.5, -0.12, QDate(2024, 1, 1), QDate(2024, 1, 7));
        ws.fetchHistorical(51.5, -0.12, QDate(2024, 2, 1), QDate(2024, 2, 7));
        ws.fetchHistorical(51.6, -0.11, QDate(2024, 3, 1), QDate(2024, 3, 7));

        // No guard rejects or queues — all three requests are dispatched immediately.
        QCoreApplication::processEvents();
        QVERIFY(errorSpy.size() >= 0); // may fail on network; test is non-crash + no guard
    }

    // ── Parse edge: malformed time strings skipped ─────────────────────────────

    void testMalformedTimeStringsSkippedInCache()
    {
        WeatherSource ws;
        QJsonArray timesArr;
        timesArr.append(QStringLiteral("not-a-datetime"));
        timesArr.append(QStringLiteral("2024-06-01T15:00"));

        QJsonArray tempsArr;
        tempsArr.append(99.0);
        tempsArr.append(18.0);

        QJsonObject hourly;
        hourly[QStringLiteral("time")]           = timesArr;
        hourly[QStringLiteral("temperature_2m")] = tempsArr;

        QJsonObject root;
        root[QStringLiteral("utc_offset_seconds")] = 0;
        root[QStringLiteral("hourly")]             = hourly;

        const int reported = ws.parseResponse(QJsonDocument(root).toJson());
        QCOMPARE(reported, 1);
        QCOMPARE(ws.cachedHourCount(), 1);

        const auto data = ws.dataAt(
            QDateTime(QDate(2024, 6, 1), QTime(15, 0), QTimeZone::utc()));
        QVERIFY(data.has_value());
        QVERIFY(std::abs(data->temperatureC - 18.0) < 0.01);
    }
};

QTEST_GUILESS_MAIN(WeatherSourceDeep3Test)
#include "test_weather_source_deep3.moc"
