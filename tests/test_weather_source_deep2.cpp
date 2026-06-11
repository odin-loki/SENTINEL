// test_weather_source_deep2.cpp
// Deep tests of WeatherSource JSON parsing, cache population, and
// lastFetchedAt() tracking — no network access required.
#include <QTest>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QTimeZone>
#include "ingest/WeatherSource.h"
#include <cmath>

class WeatherSourceDeep2Test : public QObject
{
    Q_OBJECT

private:
    // Build a minimal Open-Meteo-format JSON response body
    static QByteArray makeOpenMeteoJson(
        const QStringList&   times,
        const QList<double>& temps,
        const QList<double>& precip,
        const QList<double>& wind,
        const QList<double>& vis,
        const QList<int>&    isDayList,
        const QList<int>&    wcode,
        int utcOffsetSec = 0)
    {
        QJsonArray timesArr, tempsArr, precipArr, windArr, visArr, isDayArr, wcodeArr;
        for (const QString& t : times)    timesArr.append(t);
        for (double v : temps)            tempsArr.append(v);
        for (double v : precip)           precipArr.append(v);
        for (double v : wind)             windArr.append(v);
        for (double v : vis)              visArr.append(v);
        for (int v : isDayList)           isDayArr.append(v);
        for (int v : wcode)               wcodeArr.append(v);

        QJsonObject hourly;
        hourly[QStringLiteral("time")]           = timesArr;
        hourly[QStringLiteral("temperature_2m")] = tempsArr;
        hourly[QStringLiteral("precipitation")]  = precipArr;
        hourly[QStringLiteral("windspeed_10m")]  = windArr;
        hourly[QStringLiteral("visibility")]     = visArr;
        hourly[QStringLiteral("is_day")]         = isDayArr;
        hourly[QStringLiteral("weathercode")]    = wcodeArr;

        QJsonObject root;
        root[QStringLiteral("latitude")]           = 51.5;
        root[QStringLiteral("longitude")]          = -0.12;
        root[QStringLiteral("timezone")]           = QStringLiteral("Europe/London");
        root[QStringLiteral("utc_offset_seconds")] = utcOffsetSec;
        root[QStringLiteral("hourly")]             = hourly;

        return QJsonDocument(root).toJson();
    }

private slots:

    // ── 1. parseResponse: record count matches the number of time entries ──────
    void testParseCountMatches()
    {
        WeatherSource ws;
        const QByteArray json = makeOpenMeteoJson(
            {QStringLiteral("2024-06-01T12:00")},
            {22.5}, {0.0}, {10.0}, {9000.0}, {1}, {0});
        const int n = ws.parseResponse(json);
        QCOMPARE(n, 1);
        QCOMPARE(ws.cachedHourCount(), 1);
    }

    // ── 2. parseResponse: correct temperature stored in cache ─────────────────
    void testParseCorrectTemperature()
    {
        WeatherSource ws;
        const QByteArray json = makeOpenMeteoJson(
            {QStringLiteral("2024-06-01T14:00")},
            {25.3}, {0.0}, {5.0}, {9000.0}, {1}, {0});
        ws.parseResponse(json);
        const QDateTime dt(QDate(2024, 6, 1), QTime(14, 0), QTimeZone::utc());
        const auto data = ws.dataAt(dt);
        QVERIFY(data.has_value());
        QVERIFY2(std::abs(data->temperatureC - 25.3) < 0.01,
                 qPrintable(QStringLiteral("Expected 25.3, got %1").arg(data->temperatureC)));
    }

    // ── 3. parseResponse: precipitation and wind stored correctly ─────────────
    void testParsePrecipAndWind()
    {
        WeatherSource ws;
        const QByteArray json = makeOpenMeteoJson(
            {QStringLiteral("2024-06-02T06:00")},
            {12.0}, {3.5}, {45.0}, {8000.0}, {1}, {61});
        ws.parseResponse(json);
        const QDateTime dt(QDate(2024, 6, 2), QTime(6, 0), QTimeZone::utc());
        const auto data = ws.dataAt(dt);
        QVERIFY(data.has_value());
        QVERIFY(std::abs(data->precipitationMm - 3.5) < 0.001);
        QVERIFY(std::abs(data->windspeedKmh - 45.0) < 0.001);
        QVERIFY(data->isRaining);
    }

    // ── 4. parseResponse: missing sub-arrays don't crash; defaults used ────────
    void testParseMissingArraysNoCrash()
    {
        WeatherSource ws;
        // Provide only the time array; all other arrays are absent
        QJsonArray timesArr;
        timesArr.append(QStringLiteral("2024-06-03T10:00"));
        QJsonObject hourly;
        hourly[QStringLiteral("time")] = timesArr;
        QJsonObject root;
        root[QStringLiteral("timezone")]           = QStringLiteral("UTC");
        root[QStringLiteral("utc_offset_seconds")] = 0;
        root[QStringLiteral("hourly")]             = hourly;
        const QByteArray json = QJsonDocument(root).toJson();

        const int n = ws.parseResponse(json);
        QVERIFY(n >= 0);
        QCOMPARE(ws.cachedHourCount(), n);
    }

    // ── 5. parseResponse: empty byte array returns error code, no crash ────────
    void testParseEmptyByteArrayNoCrash()
    {
        WeatherSource ws;
        const int n = ws.parseResponse(QByteArray{});
        QVERIFY2(n < 0, "Empty input should return an error code (< 0)");
        QCOMPARE(ws.cachedHourCount(), 0);
    }

    // ── 6. parseResponse: invalid JSON returns error code, no crash ───────────
    void testParseInvalidJsonNoCrash()
    {
        WeatherSource ws;
        const int n = ws.parseResponse(QByteArray("{ this is not : valid JSON }{{"));
        QVERIFY2(n < 0, "Invalid JSON should return an error code (< 0)");
        QCOMPARE(ws.cachedHourCount(), 0);
    }

    // ── 7. parseResponse: empty JSON object returns 0 (valid but no data) ──────
    void testParseEmptyObjectReturnsZero()
    {
        WeatherSource ws;
        const QByteArray json = QJsonDocument(QJsonObject{}).toJson();
        const int n = ws.parseResponse(json);
        QCOMPARE(n, 0);
        QCOMPARE(ws.cachedHourCount(), 0);
    }

    // ── 8. lastFetchedAt() is valid after a successful parse ──────────────────
    void testLastFetchedAtAfterParse()
    {
        WeatherSource ws;
        const QDateTime before = QDateTime::currentDateTimeUtc().addSecs(-2);
        const QByteArray json = makeOpenMeteoJson(
            {QStringLiteral("2024-06-01T09:00")},
            {18.0}, {0.0}, {15.0}, {9000.0}, {1}, {0});
        ws.parseResponse(json);
        const QDateTime after = QDateTime::currentDateTimeUtc().addSecs(2);
        QVERIFY2(ws.lastFetchedAt().isValid(), "lastFetchedAt() must be valid after successful parse");
        QVERIFY(ws.lastFetchedAt() >= before);
        QVERIFY(ws.lastFetchedAt() <= after);
    }

    // ── 9. lastFetchedAt() is invalid before any parse ────────────────────────
    void testLastFetchedAtInitiallyInvalid()
    {
        WeatherSource ws;
        QVERIFY2(!ws.lastFetchedAt().isValid(),
                 "lastFetchedAt() must be invalid before first parse");
    }

    // ── 10. lastFetchedAt() NOT updated on parse error ────────────────────────
    void testLastFetchedAtNotUpdatedOnError()
    {
        WeatherSource ws;
        ws.parseResponse(QByteArray("not json"));
        QVERIFY2(!ws.lastFetchedAt().isValid(),
                 "lastFetchedAt() must stay invalid after a parse error");
    }

    // ── 11. isRaining flag set when precipitation > 0.1 mm ───────────────────
    void testIsRainingFlagSet()
    {
        WeatherSource ws;
        const QByteArray json = makeOpenMeteoJson(
            {QStringLiteral("2024-06-04T08:00")},
            {15.0}, {0.5}, {20.0}, {8000.0}, {1}, {80});
        ws.parseResponse(json);
        const auto data = ws.dataAt(
            QDateTime(QDate(2024, 6, 4), QTime(8, 0), QTimeZone::utc()));
        QVERIFY(data.has_value());
        QVERIFY2(data->isRaining, "precipitation 0.5 mm should set isRaining");
    }

    // ── 12. isExtremeWind flag set when windspeed > 80 km/h ──────────────────
    void testIsExtremeWindFlagSet()
    {
        WeatherSource ws;
        const QByteArray json = makeOpenMeteoJson(
            {QStringLiteral("2024-06-05T12:00")},
            {10.0}, {0.0}, {90.0}, {5000.0}, {1}, {95});
        ws.parseResponse(json);
        const auto data = ws.dataAt(
            QDateTime(QDate(2024, 6, 5), QTime(12, 0), QTimeZone::utc()));
        QVERIFY(data.has_value());
        QVERIFY2(data->isExtremeWind, "90 km/h wind should set isExtremeWind");
    }

    // ── 13. isLowVisibility flag set when visibility < 1000 m ────────────────
    void testIsLowVisibilityFlagSet()
    {
        WeatherSource ws;
        const QByteArray json = makeOpenMeteoJson(
            {QStringLiteral("2024-06-06T07:00")},
            {12.0}, {0.0}, {10.0}, {300.0}, {1}, {45});
        ws.parseResponse(json);
        const auto data = ws.dataAt(
            QDateTime(QDate(2024, 6, 6), QTime(7, 0), QTimeZone::utc()));
        QVERIFY(data.has_value());
        QVERIFY2(data->isLowVisibility, "300 m visibility should set isLowVisibility");
    }

    // ── 14. Multiple records: all inserted into cache ─────────────────────────
    void testMultipleRecordsCached()
    {
        WeatherSource ws;
        const QByteArray json = makeOpenMeteoJson(
            {QStringLiteral("2024-07-01T00:00"),
             QStringLiteral("2024-07-01T01:00"),
             QStringLiteral("2024-07-01T02:00")},
            {20.0, 19.5, 19.0},
            {0.0,  0.0,  0.0},
            {5.0,  5.0,  5.0},
            {9000.0, 9000.0, 9000.0},
            {0, 0, 0},
            {0, 0, 0});
        const int n = ws.parseResponse(json);
        QCOMPARE(n, 3);
        QCOMPARE(ws.cachedHourCount(), 3);
    }

    // ── 15. discomfortIndex at ≥40°C must be >= discomfort at 39°C ───────────
    void testDiscomfortExtremeHeatMonotone()
    {
        const double d39 = WeatherSource::discomfortIndex(39.0);
        const double d40 = WeatherSource::discomfortIndex(40.0);
        QVERIFY2(d40 >= d39,
                 qPrintable(QStringLiteral("discomfort at 40°C (%1) should be >= 39°C (%2)")
                            .arg(d40).arg(d39)));
    }

    // ── 16. dataAt: key lookup works across UTC offset ────────────────────────
    void testDataAtUtcOffset()
    {
        WeatherSource ws;
        // Simulate UTC+2 (utcOffsetSec = 7200): local 14:00 → UTC 12:00
        const QByteArray json = makeOpenMeteoJson(
            {QStringLiteral("2024-08-15T14:00")},
            {30.0}, {0.0}, {5.0}, {9000.0}, {1}, {0},
            7200 /* UTC+2 */);
        ws.parseResponse(json);
        // Expect data stored at UTC 12:00
        const QDateTime utcKey(QDate(2024, 8, 15), QTime(12, 0), QTimeZone::utc());
        const auto data = ws.dataAt(utcKey);
        QVERIFY2(data.has_value(), "Data should be stored at UTC 12:00 when local is 14:00 UTC+2");
        QVERIFY(std::abs(data->temperatureC - 30.0) < 0.01);
    }
};

QTEST_GUILESS_MAIN(WeatherSourceDeep2Test)
#include "test_weather_source_deep2.moc"
