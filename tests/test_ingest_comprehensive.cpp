// test_ingest_comprehensive.cpp
// Comprehensive offline tests for the SENTINEL data ingest layer:
// WeatherSource, UKPoliceSource, CsvImporter, DataQualityScorer.
// No live network calls — all API data is mocked as JSON/CSV strings.

#include <QTest>
#include <QTimeZone>
#include <QCoreApplication>
#include <QSignalSpy>
#include <QFile>
#include <QJsonObject>
#include <QJsonArray>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTemporaryFile>
#include <QTextStream>
#include <cmath>

#include "core/CrimeEvent.h"
#include "ingest/DataQualityScorer.h"
#include "ingest/CsvImporter.h"
#include "ingest/UKPoliceSource.h"

#include "ingest/WeatherSource.h"

// ─── FakeNetworkReply — feeds canned JSON into WeatherSource::onReplyFinished ─
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
        if (err == QNetworkReply::NoError) {
            setAttribute(QNetworkRequest::HttpStatusCodeAttribute, 200);
        }
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

class IngestComprehensiveTest : public QObject
{
    Q_OBJECT

private:
    static QString writeTempCsv(const QByteArray& content)
    {
        auto* f = new QTemporaryFile(QStringLiteral("XXXXXX.csv"));
        f->setAutoRemove(false);
        f->open();
        f->write(content);
        f->close();
        const QString path = f->fileName();
        delete f;
        return path;
    }

    static QJsonObject makeUkPoliceRecord(const QString& category,
                                          double lat = 51.5074,
                                          double lon = -0.1278,
                                          bool includeLocation = true)
    {
        QJsonObject raw;
        raw[QStringLiteral("id")]       = 12345;
        raw[QStringLiteral("month")]    = QStringLiteral("2024-06");
        raw[QStringLiteral("category")] = category;

        if (includeLocation) {
            QJsonObject location;
            location[QStringLiteral("latitude")]  = QString::number(lat, 'f', 6);
            location[QStringLiteral("longitude")] = QString::number(lon, 'f', 6);
            QJsonObject street;
            street[QStringLiteral("name")] = QStringLiteral("On or near Baker Street");
            location[QStringLiteral("street")] = street;
            raw[QStringLiteral("location")] = location;
        }

        return raw;
    }

    static CrimeEvent completeEvent()
    {
        CrimeEvent ev;
        ev.eventId     = QStringLiteral("E_COMPLETE");
        ev.source      = QStringLiteral("uk_police_v1");
        ev.crimeType   = QStringLiteral("burglary");
        ev.occurredAt  = QDateTime(QDate(2024, 6, 15), QTime(14, 30, 0), QTimeZone::utc());
        ev.lat         = 51.5074;
        ev.lon         = -0.1278;
        ev.locationRaw = QStringLiteral("On or near High Street");
        return ev;
    }

    static CrimeEvent minimalEvent()
    {
        CrimeEvent ev;
        ev.eventId = QStringLiteral("E_MINIMAL");
        ev.source  = QStringLiteral("unknown_source");
        return ev;
    }

private slots:

    // ── WeatherSource ────────────────────────────────────────────────────────

    // WeatherSource has no healthCheck() API; verify core offline behaviour is stable.
    void testWeatherSourceHealthCheck()
    {
        WeatherSource ws;
        QVERIFY(std::isfinite(WeatherSource::discomfortIndex(20.0)));
        QCOMPARE(ws.cachedHourCount(), 0);
        QVERIFY(!ws.dataAt(QDateTime::currentDateTimeUtc()).has_value());
    }

    void testWeatherSourceParsesValidJson()
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

        auto* reply = new FakeNetworkReply(json);
        ws.onReplyFinished(reply);
        QCoreApplication::processEvents();

        const QDateTime dt(QDate(2024, 6, 15), QTime(14, 0, 0), QTimeZone::utc());
        const auto result = ws.dataAt(dt);

        QVERIFY(result.has_value());
        QCOMPARE(result->temperatureC, 22.5);
        QCOMPARE(result->precipitationMm, 1.5);
        QCOMPARE(result->windspeedKmh, 15.0);
        QCOMPARE(result->visibilityM, 8000.0);
        QVERIFY(result->isDay);
        QCOMPARE(result->weatherCode, 61);
        QVERIFY(result->isRaining);
        QVERIFY(!result->isLowVisibility);
        QVERIFY(!result->isExtremeWind);
        QVERIFY(std::isfinite(result->tempDiscomfort));
    }

    void testWeatherSourceEmptyResponse()
    {
        WeatherSource ws;

        QSignalSpy completeSpy(&ws, &WeatherSource::fetchComplete);
        auto* reply = new FakeNetworkReply(QByteArray("{}"));
        ws.onReplyFinished(reply);
        QCoreApplication::processEvents();

        QCOMPARE(ws.cachedHourCount(), 0);
        QVERIFY(!ws.dataAt(QDateTime::currentDateTimeUtc()).has_value());
        // Empty object has no hourly.time array — completes with 0 hours, no crash
        QCOMPARE(completeSpy.size(), 1);
        QCOMPARE(completeSpy.at(0).at(0).toInt(), 0);
    }

    // ── UKPoliceSource ───────────────────────────────────────────────────────

    void testUKPoliceSourceParsesRecord()
    {
        UKPoliceSource src(51.5, -0.1);
        const CrimeEvent ev = src.parseRecord(makeUkPoliceRecord(QStringLiteral("burglary")));

        QVERIFY(ev.eventId.startsWith(QStringLiteral("uk_")));
        QCOMPARE(ev.source, QStringLiteral("uk_police_v1"));
        QCOMPARE(ev.crimeType, QStringLiteral("burglary"));
        QVERIFY(ev.occurredAt.has_value());
        QCOMPARE(ev.occurredAt->date(), QDate(2024, 6, 1));
        QVERIFY(ev.lat.has_value());
        QVERIFY(ev.lon.has_value());
        QVERIFY(std::abs(*ev.lat - 51.5074) < 1e-4);
        QVERIFY(std::abs(*ev.lon - (-0.1278)) < 1e-4);
        QVERIFY(ev.locationRaw.has_value());
    }

    void testUKPoliceSourceMissingFields()
    {
        UKPoliceSource src(51.5, -0.1);

        QJsonObject raw;
        raw[QStringLiteral("category")] = QStringLiteral("robbery");
        raw[QStringLiteral("month")]    = QStringLiteral("2024-03");

        const CrimeEvent ev = src.parseRecord(raw);
        QCOMPARE(ev.crimeType, QStringLiteral("robbery"));
        QVERIFY(!ev.lat.has_value());
        QVERIFY(!ev.lon.has_value());
    }

    void testUKPoliceSourceCrimeTypeMapping()
    {
        UKPoliceSource src(51.5, -0.1);

        struct Mapping { const char* raw; const char* expected; } mappings[] = {
            { "burglary",      "burglary"       },
            { "robbery",       "robbery"        },
            { "violent-crime", "assault"        },
            { "vehicle-crime", "vehicle_crime"  },
            { "drugs",         "drug_offence"   },
        };

        for (const auto& m : mappings) {
            const CrimeEvent ev = src.parseRecord(
                makeUkPoliceRecord(QString::fromLatin1(m.raw)));
            QCOMPARE(ev.crimeType, QString::fromLatin1(m.expected));
        }
    }

    // ── CsvImporter ──────────────────────────────────────────────────────────

    void testCsvImporterDetectsChicagoColumns()
    {
        const QStringList headers = {
            QStringLiteral("case_number"),
            QStringLiteral("date"),
            QStringLiteral("primary_type"),
            QStringLiteral("description"),
            QStringLiteral("y_coord"),
            QStringLiteral("x_coord"),
        };
        const CsvColumnMap map = CsvImporter::detectColumns(headers);

        QVERIFY(map.idCol >= 0);
        QVERIFY(map.dateCol >= 0);
        QVERIFY(map.crimeTypeCol >= 0);
        QVERIFY(map.latCol >= 0);
        QVERIFY(map.lonCol >= 0);
    }

    void testCsvImporterDetectsNYPDColumns()
    {
        const QStringList headers = {
            QStringLiteral("INCIDENT_KEY"),
            QStringLiteral("OCCUR_DATE"),
            QStringLiteral("OFFENSE_DESCRIPTION"),
            QStringLiteral("LATITUDE"),
            QStringLiteral("LONGITUDE"),
        };
        const CsvColumnMap map = CsvImporter::detectColumns(headers);

        QVERIFY(map.idCol >= 0);
        QVERIFY(map.dateCol >= 0);
        QVERIFY(map.crimeTypeCol >= 0);
        QVERIFY(map.latCol >= 0);
        QVERIFY(map.lonCol >= 0);
    }

    void testCsvImporterHandlesBOM()
    {
        const QByteArray bom = QByteArray::fromHex("EFBBBF");
        const QByteArray csv =
            "id,date,crime_type,latitude,longitude\n"
            "E1,2024-01-15,burglary,51.5074,-0.1278\n";

        const QString path = writeTempCsv(bom + csv);
        const auto events = CsvImporter::importFile(path, QStringLiteral("test_bom"));
        QFile::remove(path);

        QCOMPARE(events.size(), 1);
        QCOMPARE(events[0].crimeType, QStringLiteral("burglary"));
        QVERIFY(events[0].lat.has_value());
    }

    void testCsvImporterQuotedFields()
    {
        const QString path = writeTempCsv(
            QByteArray("id,description,crime_type,latitude,longitude\n"
                       "E1,\"Theft at 123 Main St, Apt 4\",theft,51.5,-0.1\n"));

        const auto events = CsvImporter::importFile(path, QStringLiteral("test_quotes"));
        QFile::remove(path);

        QCOMPARE(events.size(), 1);
        QVERIFY(events[0].narrative.has_value());
        QVERIFY(events[0].narrative->contains(QStringLiteral("Apt 4")));
        QCOMPARE(events[0].crimeType, QStringLiteral("theft"));
    }

    // ── DataQualityScorer ────────────────────────────────────────────────────

    void testDataQualityScorerCompositeScore()
    {
        QMap<QString, double> rel;
        rel[QStringLiteral("uk_police_v1")] = 1.0;
        DataQualityScorer scorer(rel);

        const QualityReport report = scorer.score(completeEvent());

        // completeness=1.0, temporal=hour(1.0), spatial=exact(1.0), reliability=1.0
        const double expected =
            0.30 * 1.0 + 0.20 * 1.0 + 0.20 * 1.0 + 0.30 * 1.0;
        QVERIFY(qAbs(report.completeness - 1.0) < 1e-9);
        QCOMPARE(report.temporalPrecision, QStringLiteral("hour"));
        QCOMPARE(report.spatialPrecision, QStringLiteral("exact"));
        QVERIFY(qAbs(report.compositeScore - expected) < 1e-9);
        QVERIFY(!report.quarantined);
    }

    void testDataQualityScorerQuarantine()
    {
        DataQualityScorer scorer;
        const QualityReport report = scorer.score(minimalEvent());

        QVERIFY(report.compositeScore < 0.3);
        QVERIFY(report.quarantined);
        QCOMPARE(DataQualityScorer::passRate({ report }), 0.0);
    }

    void testDataQualityScorerSourceReliability()
    {
        QMap<QString, double> rel;
        rel[QStringLiteral("uk_police_v1")] = 0.95;
        rel[QStringLiteral("chicago_crimes")] = 0.85;
        rel[QStringLiteral("ny_pd")] = 0.80;

        DataQualityScorer scorer(rel);

        CrimeEvent uk = completeEvent();
        uk.source = QStringLiteral("uk_police_v1");
        QCOMPARE(scorer.score(uk).sourceReliability, 0.95);

        CrimeEvent chicago = completeEvent();
        chicago.source = QStringLiteral("chicago_crimes");
        QCOMPARE(scorer.score(chicago).sourceReliability, 0.85);

        CrimeEvent nypd = completeEvent();
        nypd.source = QStringLiteral("ny_pd");
        QCOMPARE(scorer.score(nypd).sourceReliability, 0.80);

        CrimeEvent unknown = completeEvent();
        unknown.source = QStringLiteral("mystery_feed");
        QCOMPARE(scorer.score(unknown).sourceReliability, 0.5);
    }

    void testDataQualityScorerAllFieldsPresent()
    {
        QMap<QString, double> rel;
        rel[QStringLiteral("uk_police_v1")] = 0.95;
        DataQualityScorer scorer(rel);

        const QualityReport report = scorer.score(completeEvent());
        QVERIFY(report.compositeScore > 0.85);
        QVERIFY(!report.quarantined);
        QVERIFY(qAbs(report.completeness - 1.0) < 1e-9);
    }

    void testDataQualityScorerMinimalFields()
    {
        DataQualityScorer scorer;
        const QualityReport complete = scorer.score(completeEvent());
        const QualityReport minimal  = scorer.score(minimalEvent());

        QVERIFY(minimal.compositeScore < complete.compositeScore);
        QVERIFY(minimal.quarantined);
        QVERIFY(qAbs(minimal.completeness - 0.0) < 1e-9);
        QCOMPARE(DataQualityScorer::passRate(QVector<QualityReport>{}), 0.0);
    }
};

QTEST_MAIN(IngestComprehensiveTest)
#include "test_ingest_comprehensive.moc"
