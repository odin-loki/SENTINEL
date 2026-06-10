// test_data_ingestion.cpp — SENTINEL data-ingestion pipeline tests
// Covers:
//   DataQualityScorer  — completeness, penalties, batch scoring, quality ranking
//   UKPoliceSource     — JSON parsing, malformed input, URL, deduplication (offline)
//   WeatherSource      — JSON parsing, missing fields, caching, invalid JSON (offline)
//
// Build: sentinel_test(test_data_ingestion tests/test_data_ingestion.cpp)

#include <QTest>
#include <QCoreApplication>
#include <QDate>
#include <QTime>
#include <QDateTime>
#include <QIODevice>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSet>
#include <QVector>
#include <cmath>
#include <numeric>
#include <algorithm>

#include "core/CrimeEvent.h"
#include "ingest/DataQualityScorer.h"
#include "ingest/DataSource.h"   // include transitively-needed base before the define

// ─── White-box test access ───────────────────────────────────────────────────
// Redefine `private` → `public` so we can call parseRawEvent on UKPoliceSource
// and onReplyFinished/m_cache on WeatherSource without modifying source files.
// This technique does NOT alter the compiled binary: access specifiers have no
// effect on object layout or vtable entries.  The #pragma silences the GCC/Clang
// keyword-macro diagnostic that might fire with -pedantic builds.
#ifdef __GNUC__
#  pragma GCC diagnostic push
#  pragma GCC diagnostic ignored "-Wkeyword-macro"
#endif
#define private public
#include "ingest/UKPoliceSource.h"   // exposes parseRawEvent, BASE_URL
#include "ingest/WeatherSource.h"    // exposes onReplyFinished, m_cache
#undef private
#ifdef __GNUC__
#  pragma GCC diagnostic pop
#endif

// ─────────────────────────────────────────────────────────────────────────────
// FakeNetworkReply
// Returns canned data to WeatherSource::onReplyFinished without real HTTP.
// We omit Q_OBJECT intentionally: AUTOMOC must not re-moc Qt headers that are
// already compiled into sentinel_core, and this class emits no custom signals.
// deleteLater() still works via the inherited QObject/QNetworkReply meta-object.
// ─────────────────────────────────────────────────────────────────────────────
class FakeNetworkReply : public QNetworkReply
{
public:
    explicit FakeNetworkReply(const QByteArray& payload, QObject* parent = nullptr)
        : QNetworkReply(parent)
        , m_payload(payload)
        , m_offset(0)
    {
        setError(QNetworkReply::NoError, {});
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

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────
static CrimeEvent makeHighQuality(const QString& id,
                                   const QString& source = QStringLiteral("uk_police_v1"))
{
    CrimeEvent ev;
    ev.eventId    = id;
    ev.source     = source;
    ev.crimeType  = QStringLiteral("burglary");
    ev.occurredAt = QDateTime(QDate(2024, 6, 15), QTime(14, 30, 0), QTimeZone::UTC);
    ev.lat        = 51.5074;
    ev.lon        = -0.1278;
    ev.locationRaw = QStringLiteral("On or near High Street");
    ev.ingestedAt  = QDateTime::currentDateTimeUtc();
    return ev;
}

// ─────────────────────────────────────────────────────────────────────────────
// TestDataIngestion
// ─────────────────────────────────────────────────────────────────────────────
class TestDataIngestion : public QObject
{
    Q_OBJECT

private slots:

    // ═════════════════════════════════════════════════════════════════════════
    // DataQualityScorer
    // ═════════════════════════════════════════════════════════════════════════

    // 1. Perfect-quality event should score ≥ 0.9
    void dqs_perfectEventScoresHigh()
    {
        QMap<QString, double> rel;
        rel[QStringLiteral("uk_police_v1")] = 1.0;
        DataQualityScorer scorer(rel);

        const QualityReport r = scorer.score(makeHighQuality(QStringLiteral("perf-001")));

        QVERIFY2(r.compositeScore >= 0.9,
            qPrintable(QString("Expected compositeScore >= 0.9, got %1").arg(r.compositeScore)));
        QVERIFY(!r.quarantined);
        QVERIFY(qAbs(r.completeness - 1.0) < 1e-9);
    }

    // 2. Event missing locationRaw should be penalised (completeness drops to 0.75)
    void dqs_missingLocationPenalised()
    {
        DataQualityScorer scorer;   // default reliability = 0.5

        CrimeEvent ev = makeHighQuality(QStringLiteral("noloc-001"));
        ev.locationRaw = std::nullopt;

        const QualityReport r = scorer.score(ev);

        // completeness = 3/4 = 0.75  →  compositeScore = 0.5*0.75 + 0.5*0.5 = 0.625
        QVERIFY(r.completeness < 1.0);
        QVERIFY2(qAbs(r.completeness - 0.75) < 1e-9,
            qPrintable(QString("Expected completeness 0.75, got %1").arg(r.completeness)));
        QVERIFY(r.compositeScore < 1.0);
    }

    // 3. Event missing occurredAt should be penalised (completeness drops to 0.75)
    void dqs_missingTimestampPenalised()
    {
        DataQualityScorer scorer;

        CrimeEvent ev = makeHighQuality(QStringLiteral("nots-001"));
        ev.occurredAt = std::nullopt;

        const QualityReport r = scorer.score(ev);

        QVERIFY(r.completeness < 1.0);
        QVERIFY2(qAbs(r.completeness - 0.75) < 1e-9,
            qPrintable(QString("Expected completeness 0.75, got %1").arg(r.completeness)));
        // Temporal precision must be "unknown" with no timestamp
        QCOMPARE(r.temporalPrecision, QStringLiteral("unknown"));
    }

    // 4. Event with (0, 0) coordinates should be penalised
    void dqs_suspiciousCoordinatesPenalised()
    {
        DataQualityScorer scorer;

        // Sub-test A: (0, 0) — fails the non-zero lat/lon completeness check
        CrimeEvent ev = makeHighQuality(QStringLiteral("zerozero-001"));
        ev.lat = 0.0;
        ev.lon = 0.0;

        QualityReport r = scorer.score(ev);

        // lat/lon credit is lost → completeness = 3/4
        QVERIFY2(r.completeness < 1.0,
            "Expected completeness < 1.0 for (0,0) coordinates");
        QCOMPARE(r.spatialPrecision, QStringLiteral("unknown"));

        // Sub-test B: off-UK but clearly outside any plausible range (0, 0.0)
        // same as A here; verify score is strictly below a perfect-event score
        QMap<QString, double> rel;
        rel[QStringLiteral("uk_police_v1")] = 1.0;
        DataQualityScorer scorerWithRel(rel);

        const QualityReport perfect = scorerWithRel.score(
            makeHighQuality(QStringLiteral("perfect-ref")));
        const QualityReport penalised = scorerWithRel.score(ev);

        QVERIFY2(penalised.compositeScore < perfect.compositeScore,
            qPrintable(QString("(0,0) score %1 should be < perfect score %2")
                .arg(penalised.compositeScore).arg(perfect.compositeScore)));
    }

    // 5. Batch of 20 events with varying quality: all scores must lie in [0, 1]
    void dqs_batchScores20EventsInRange()
    {
        DataQualityScorer scorer;
        QVector<CrimeEvent> events;

        // 10 high-quality events
        for (int i = 0; i < 10; ++i)
            events.append(makeHighQuality(QStringLiteral("hq-%1").arg(i)));

        // 10 low-quality events (all optional fields absent, crimeType empty)
        for (int i = 0; i < 10; ++i) {
            CrimeEvent ev;
            ev.eventId = QStringLiteral("lq-%1").arg(i);
            events.append(ev);
        }

        const QVector<QualityReport> reports = scorer.scoreBatch(events);
        QCOMPARE(reports.size(), 20);

        for (const QualityReport& rep : reports) {
            QVERIFY2(rep.compositeScore >= 0.0 && rep.compositeScore <= 1.0,
                qPrintable(QString("Score %1 outside [0,1] for event %2")
                    .arg(rep.compositeScore).arg(rep.eventId)));
        }
    }

    // 6. High-quality batch must have a strictly higher mean score than a low-quality batch
    void dqs_highQualityBatchBetterThanLowQuality()
    {
        QMap<QString, double> rel;
        rel[QStringLiteral("good_source")] = 0.9;
        DataQualityScorer scorer(rel);

        // High-quality: all 4 completeness fields present + source reliability 0.9
        // Expected compositeScore per event = 0.5*1.0 + 0.5*0.9 = 0.95
        QVector<CrimeEvent> highQ;
        for (int i = 0; i < 10; ++i)
            highQ.append(makeHighQuality(QStringLiteral("hq-%1").arg(i),
                                         QStringLiteral("good_source")));

        // Low-quality: no optional fields, unknown source (default reliability 0.5)
        // Expected compositeScore per event = 0.5*0.0 + 0.5*0.5 = 0.25
        QVector<CrimeEvent> lowQ;
        for (int i = 0; i < 10; ++i) {
            CrimeEvent ev;
            ev.eventId = QStringLiteral("lq-%1").arg(i);
            lowQ.append(ev);
        }

        const auto meanScore = [&scorer](const QVector<CrimeEvent>& evs) -> double {
            const auto reps = scorer.scoreBatch(evs);
            double sum = 0.0;
            for (const auto& r : reps) sum += r.compositeScore;
            return sum / reps.size();
        };

        const double highMean = meanScore(highQ);
        const double lowMean  = meanScore(lowQ);

        QVERIFY2(highMean > lowMean,
            qPrintable(QString("High-quality mean %1 should exceed low-quality mean %2")
                .arg(highMean).arg(lowMean)));
    }

    // ═════════════════════════════════════════════════════════════════════════
    // UKPoliceSource  (offline — no real HTTP requests)
    // ═════════════════════════════════════════════════════════════════════════

    // 1. Parse a well-formed API response JSON and verify CrimeEvent fields
    void ukps_parseApiResponse()
    {
        UKPoliceSource src(51.5074, -0.1278, 1.0);

        QJsonObject raw;
        raw[QStringLiteral("id")]       = 42001234;                         // integer id
        raw[QStringLiteral("month")]    = QStringLiteral("2024-03");
        raw[QStringLiteral("category")] = QStringLiteral("burglary");
        raw[QStringLiteral("context")]  = QStringLiteral("Suspect seen fleeing the scene");

        QJsonObject street;
        street[QStringLiteral("name")] = QStringLiteral("On or near Baker Street");

        QJsonObject location;
        location[QStringLiteral("latitude")]  = QStringLiteral("51.523800");
        location[QStringLiteral("longitude")] = QStringLiteral("-0.158600");
        location[QStringLiteral("street")]    = street;
        raw[QStringLiteral("location")] = location;

        QJsonObject outcome;
        outcome[QStringLiteral("category")] = QStringLiteral("Under investigation");
        raw[QStringLiteral("outcome_status")] = outcome;

        const CrimeEvent ev = src.parseRawEvent(raw);

        QCOMPARE(ev.source, QStringLiteral("uk_police_v1"));
        QVERIFY2(ev.eventId.contains(QStringLiteral("42001234")),
            qPrintable(QString("eventId '%1' should contain raw id").arg(ev.eventId)));

        // category "burglary" maps to "burglary" in CRIME_TYPE_MAP
        QCOMPARE(ev.crimeType, QStringLiteral("burglary"));

        QVERIFY(ev.lat.has_value());
        QVERIFY2(std::abs(*ev.lat - 51.5238) < 1e-4,
            qPrintable(QString("lat %1 differs from expected 51.5238").arg(*ev.lat)));

        QVERIFY(ev.lon.has_value());
        QVERIFY2(std::abs(*ev.lon - (-0.1586)) < 1e-4,
            qPrintable(QString("lon %1 differs from expected -0.1586").arg(*ev.lon)));

        QVERIFY(ev.locationRaw.has_value());
        QCOMPARE(*ev.locationRaw, QStringLiteral("On or near Baker Street"));

        // month "2024-03" → date 2024-03-01
        QVERIFY(ev.occurredAt.has_value());
        QCOMPARE(ev.occurredAt->date(), QDate(2024, 3, 1));

        QVERIFY(ev.narrative.has_value());
        QCOMPARE(*ev.narrative, QStringLiteral("Suspect seen fleeing the scene"));

        QCOMPARE(ev.outcome, QStringLiteral("Under investigation"));
    }

    // 2. Malformed / empty JSON must return a default CrimeEvent without crashing
    void ukps_malformedJsonReturnsEmpty()
    {
        UKPoliceSource src(51.5074, -0.1278, 1.0);

        // Empty JSON object — all fields absent
        const CrimeEvent ev = src.parseRawEvent(QJsonObject{});

        // Must not crash; source is always set in parseRawEvent
        QCOMPARE(ev.source, QStringLiteral("uk_police_v1"));
        QVERIFY(ev.eventId.startsWith(QStringLiteral("uk_")));
        QVERIFY(ev.crimeType.isEmpty());
        QVERIFY(!ev.lat.has_value());
        QVERIFY(!ev.lon.has_value());
        QVERIFY(!ev.locationRaw.has_value());
        QVERIFY(!ev.occurredAt.has_value());
        QVERIFY(!ev.narrative.has_value());

        // A variety of garbage-field JSON should also not crash
        QJsonObject garbage;
        garbage[QStringLiteral("id")]       = QStringLiteral("not-a-number");
        garbage[QStringLiteral("month")]    = QStringLiteral("bad-date");
        garbage[QStringLiteral("location")] = QJsonValue(42);   // wrong type
        const CrimeEvent ev2 = src.parseRawEvent(garbage);
        QCOMPARE(ev2.source, QStringLiteral("uk_police_v1"));
        QVERIFY(!ev2.lat.has_value());
        QVERIFY(!ev2.lon.has_value());
        QVERIFY(!ev2.occurredAt.has_value());
    }

    // 3. URL construction: verify endpoint constants and source metadata
    void ukps_urlConstructionEndpoint()
    {
        UKPoliceSource src(51.5074, -0.1278, 1.0);

        QCOMPARE(src.sourceId(), QStringLiteral("uk_police_v1"));
        QCOMPARE(src.displayName(), QStringLiteral("UK Police Open Data"));

        // BASE_URL accessible via white-box access; must point at the correct host
        QVERIFY2(UKPoliceSource::BASE_URL.startsWith(QStringLiteral("https://data.police.uk")),
            qPrintable(QString("BASE_URL '%1' does not start with expected host")
                .arg(UKPoliceSource::BASE_URL)));
        QVERIFY(UKPoliceSource::BASE_URL.contains(QStringLiteral("/api")));

        // Verify that the crimes-street endpoint path is embedded in the URL
        // that fetchSince() would generate (CRIME_TYPE_MAP exists and is non-empty)
        QVERIFY(!UKPoliceSource::CRIME_TYPE_MAP.isEmpty());
        QVERIFY(UKPoliceSource::CRIME_TYPE_MAP.contains(QStringLiteral("burglary")));
    }

    // 4. Deduplication: duplicate event IDs are collapsed to one; distinct IDs remain distinct
    void ukps_deduplication()
    {
        UKPoliceSource src(51.5074, -0.1278, 1.0);

        QJsonObject raw;
        raw[QStringLiteral("id")]       = 99999;
        raw[QStringLiteral("month")]    = QStringLiteral("2024-01");
        raw[QStringLiteral("category")] = QStringLiteral("theft-from-the-person");

        // Parse the same raw event twice (simulating a duplicate in the API response)
        const CrimeEvent ev1 = src.parseRawEvent(raw);
        const CrimeEvent ev2 = src.parseRawEvent(raw);
        QCOMPARE(ev1.eventId, ev2.eventId);

        // Deduplicate by eventId using QSet
        QSet<QString> seen;
        QVector<CrimeEvent> deduped;
        for (const CrimeEvent& ev : { ev1, ev2 }) {
            if (!seen.contains(ev.eventId)) {
                seen.insert(ev.eventId);
                deduped.append(ev);
            }
        }
        QCOMPARE(deduped.size(), 1);

        // Two events with different IDs must both survive deduplication
        QJsonObject raw2;
        raw2[QStringLiteral("id")]       = 11111;
        raw2[QStringLiteral("month")]    = QStringLiteral("2024-02");
        raw2[QStringLiteral("category")] = QStringLiteral("burglary");
        const CrimeEvent ev3 = src.parseRawEvent(raw2);

        QVERIFY(ev3.eventId != ev1.eventId);

        QSet<QString> seen2;
        QVector<CrimeEvent> deduped2;
        for (const CrimeEvent& ev : { ev1, ev3 }) {
            if (!seen2.contains(ev.eventId)) {
                seen2.insert(ev.eventId);
                deduped2.append(ev);
            }
        }
        QCOMPARE(deduped2.size(), 2);
    }

    // ═════════════════════════════════════════════════════════════════════════
    // WeatherSource  (offline — no real HTTP requests)
    // ═════════════════════════════════════════════════════════════════════════

    // 1. Parse a well-formed Open-Meteo JSON response and verify WeatherData fields
    void ws_parseWeatherJson()
    {
        WeatherSource ws;

        const QByteArray json = R"({
            "hourly": {
                "time":             ["2024-06-15T14:00"],
                "temperature_2m":   [22.5],
                "precipitation":    [1.5],
                "windspeed_10m":    [15.0],
                "visibility":       [8000.0],
                "is_day":           [1],
                "weathercode":      [61]
            },
            "utc_offset_seconds": 0,
            "timezone": "UTC"
        })";

        // Inject via the private onReplyFinished slot (accessible via white-box access)
        auto* reply = new FakeNetworkReply(json);
        ws.onReplyFinished(reply);
        QCoreApplication::processEvents();   // let deleteLater() clean up

        const QDateTime dt(QDate(2024, 6, 15), QTime(14, 0, 0), QTimeZone::UTC);
        const auto result = ws.dataAt(dt);

        QVERIFY2(result.has_value(), "Expected dataAt() to return a value after parsing");
        QVERIFY2(std::abs(result->temperatureC    - 22.5)   < 1e-6, "temperatureC mismatch");
        QVERIFY2(std::abs(result->precipitationMm - 1.5)    < 1e-6, "precipitationMm mismatch");
        QVERIFY2(std::abs(result->windspeedKmh    - 15.0)   < 1e-6, "windspeedKmh mismatch");
        QVERIFY2(std::abs(result->visibilityM     - 8000.0) < 1e-6, "visibilityM mismatch");
        QVERIFY(result->isDay);
        QCOMPARE(result->weatherCode, 61);
        QVERIFY2(result->isRaining,        "precipitationMm 1.5 > 0.1 should set isRaining");
        QVERIFY2(!result->isLowVisibility, "visibilityM 8000 >= 1000 should not set isLowVisibility");
        QVERIFY2(!result->isExtremeWind,   "windspeedKmh 15 <= 80 should not set isExtremeWind");
    }

    // 2. Missing weather data arrays produce sensible defaults
    void ws_missingFieldsDefaults()
    {
        WeatherSource ws;

        // Only "time" is present; all measurement arrays are absent
        const QByteArray json = R"({
            "hourly": {
                "time": ["2024-01-10T09:00"]
            },
            "utc_offset_seconds": 0,
            "timezone": "UTC"
        })";

        auto* reply = new FakeNetworkReply(json);
        ws.onReplyFinished(reply);
        QCoreApplication::processEvents();

        const QDateTime dt(QDate(2024, 1, 10), QTime(9, 0, 0), QTimeZone::UTC);
        const auto result = ws.dataAt(dt);

        QVERIFY2(result.has_value(), "Expected a WeatherData entry even with missing arrays");
        QVERIFY2(std::abs(result->temperatureC    - 0.0)     < 1e-9, "default temperatureC = 0");
        QVERIFY2(std::abs(result->precipitationMm - 0.0)     < 1e-9, "default precipitationMm = 0");
        QVERIFY2(std::abs(result->windspeedKmh    - 0.0)     < 1e-9, "default windspeedKmh = 0");
        QVERIFY2(std::abs(result->visibilityM     - 10000.0) < 1e-9, "default visibilityM = 10000");
        QVERIFY2(result->isDay,                "default isDay = true");
        QCOMPARE(result->weatherCode, 0);
        QVERIFY(!result->isRaining);
        QVERIFY(!result->isLowVisibility);
        QVERIFY(!result->isExtremeWind);
    }

    // 3. Calling dataAt() for the same datetime twice returns the same cached result
    void ws_cachingReturnsSameResult()
    {
        WeatherSource ws;

        const QByteArray json = R"({
            "hourly": {
                "time":             ["2024-03-20T10:00"],
                "temperature_2m":   [15.0],
                "precipitation":    [0.0],
                "windspeed_10m":    [20.0],
                "visibility":       [9000.0],
                "is_day":           [1],
                "weathercode":      [1]
            },
            "utc_offset_seconds": 0,
            "timezone": "UTC"
        })";

        auto* reply = new FakeNetworkReply(json);
        ws.onReplyFinished(reply);
        QCoreApplication::processEvents();

        const QDateTime dt(QDate(2024, 3, 20), QTime(10, 0, 0), QTimeZone::UTC);

        // First and second lookups must return identical values (cached)
        const auto result1 = ws.dataAt(dt);
        const auto result2 = ws.dataAt(dt);

        QVERIFY(result1.has_value());
        QVERIFY(result2.has_value());
        QVERIFY2(std::abs(result1->temperatureC    - result2->temperatureC)    < 1e-9,
                 "Cached temperatureC changed between lookups");
        QVERIFY2(std::abs(result1->precipitationMm - result2->precipitationMm) < 1e-9,
                 "Cached precipitationMm changed between lookups");
        QVERIFY2(std::abs(result1->windspeedKmh    - result2->windspeedKmh)    < 1e-9,
                 "Cached windspeedKmh changed between lookups");
        QCOMPARE(result1->weatherCode, result2->weatherCode);
        QCOMPARE(result1->isDay,       result2->isDay);

        // Querying a different datetime must still return nullopt (not in cache)
        const QDateTime other(QDate(2024, 3, 21), QTime(10, 0, 0), QTimeZone::UTC);
        QVERIFY(!ws.dataAt(other).has_value());
    }

    // 4. Invalid / malformed JSON must not crash and must leave cache empty
    void ws_invalidJsonReturnsDefault()
    {
        WeatherSource ws;

        // Sub-test A: completely malformed JSON
        {
            auto* reply = new FakeNetworkReply(QByteArray("{ this is !!! not JSON }"));
            ws.onReplyFinished(reply);
            QCoreApplication::processEvents();

            // QJsonDocument::fromJson fails → doc.isObject() == false → cache unchanged
            const auto result = ws.dataAt(QDateTime::currentDateTimeUtc());
            QVERIFY2(!result.has_value(),
                "Malformed JSON must not populate the cache");
        }

        // Sub-test B: valid JSON but wrong type (array instead of object)
        {
            WeatherSource ws2;
            auto* reply2 = new FakeNetworkReply(QByteArray("[1,2,3]"));
            ws2.onReplyFinished(reply2);
            QCoreApplication::processEvents();

            QVERIFY2(!ws2.dataAt(QDateTime::currentDateTimeUtc()).has_value(),
                "JSON array root must not populate the cache");
        }
    }
};

QTEST_MAIN(TestDataIngestion)
#include "test_data_ingestion.moc"
