// test_ingest_deep.cpp
// Iteration 7 deep audit tests for UKPoliceSource, WeatherSource, and
// DataQualityScorer. All offline/mocked — no live network calls.

#include <QTest>
#include <QCoreApplication>
#include <QSignalSpy>
#include <QJsonObject>
#include <QJsonArray>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <cmath>

#include "core/CrimeEvent.h"
#include "ingest/DataQualityScorer.h"
#include "ingest/UKPoliceSource.h"
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

class IngestDeepTest : public QObject
{
    Q_OBJECT

private:
    static QJsonObject ukPoliceRecord()
    {
        QJsonObject raw;
        raw[QStringLiteral("id")]       = QStringLiteral("38b01fe3abc123");
        raw[QStringLiteral("month")]    = QStringLiteral("2024-03");
        raw[QStringLiteral("category")] = QStringLiteral("burglary");

        QJsonObject location;
        location[QStringLiteral("latitude")]  = QStringLiteral("51.5074");
        location[QStringLiteral("longitude")] = QStringLiteral("-0.1278");
        QJsonObject street;
        street[QStringLiteral("name")] = QStringLiteral("On or near Oxford Street");
        location[QStringLiteral("street")] = street;
        raw[QStringLiteral("location")] = location;

        QJsonObject outcome;
        outcome[QStringLiteral("category")] = QStringLiteral("Under investigation");
        raw[QStringLiteral("outcome_status")] = outcome;

        return raw;
    }

    static CrimeEvent fullQualityEvent()
    {
        CrimeEvent ev;
        ev.eventId     = QStringLiteral("E_FULL");
        ev.source      = QStringLiteral("uk_police_v1");
        ev.crimeType   = QStringLiteral("burglary");
        ev.occurredAt  = QDateTime(QDate(2024, 3, 15), QTime(14, 30, 0), QTimeZone::utc());
        ev.lat         = 51.5074;
        ev.lon         = -0.1278;
        ev.locationRaw = QStringLiteral("On or near High Street");
        return ev;
    }

private slots:

    // ── UKPoliceSource ───────────────────────────────────────────────────────

    void testUKPoliceSourceConstruction()
    {
        UKPoliceSource src(51.5, -0.1, 1.0);
        QCOMPARE(src.sourceId(), QStringLiteral("uk_police_v1"));
        QVERIFY(!src.displayName().isEmpty());
    }

    void testUKPoliceAvailableCategories()
    {
        UKPoliceSource src(51.5, -0.1);
        const QStringList cats = src.availableCategories();
        QVERIFY2(!cats.isEmpty(), "availableCategories must return a non-empty list");
        QVERIFY(cats.contains(QStringLiteral("burglary")));
        QVERIFY(cats.contains(QStringLiteral("violent-crime")));
    }

    void testUKPoliceSourceMalformedJsonNocrash()
    {
        UKPoliceSource src(51.5, -0.1);
        const CrimeEvent ev = src.parseRecord(QJsonObject{});
        Q_UNUSED(ev);
        QVERIFY(true);
    }

    void testUKPoliceSourceEmptyArrayResponse()
    {
        UKPoliceSource src(51.5, -0.1);

        QSignalSpy completeSpy(&src, &DataSource::fetchComplete);
        auto* reply = new FakeNetworkReply(QByteArray("[]"));
        src.onReplyFinished(reply);
        QCoreApplication::processEvents();

        QCOMPARE(completeSpy.size(), 1);
        QCOMPARE(completeSpy.at(0).at(0).toInt(), 0);
    }

    void testUKPoliceSourceEventParsing()
    {
        UKPoliceSource src(51.5, -0.1);
        const CrimeEvent ev = src.parseRecord(ukPoliceRecord());

        QCOMPARE(ev.source, QStringLiteral("uk_police_v1"));
        QCOMPARE(ev.crimeType, QStringLiteral("burglary"));
        QVERIFY(ev.eventId.startsWith(QStringLiteral("uk_")));
        QVERIFY(ev.occurredAt.has_value());
        QCOMPARE(ev.occurredAt->date(), QDate(2024, 3, 1));
        QVERIFY(ev.lat.has_value());
        QVERIFY(ev.lon.has_value());
        QVERIFY(std::abs(*ev.lat - 51.5074) < 1e-4);
        QVERIFY(std::abs(*ev.lon - (-0.1278)) < 1e-4);
        QVERIFY(ev.locationRaw.has_value());
        QCOMPARE(ev.outcome, QStringLiteral("Under investigation"));
    }

    void testUKPoliceSourceDateParams()
    {
        const QUrl url = UKPoliceSource::buildFetchUrl(51.5, -0.1, QStringLiteral("2024-03"));
        const QString urlStr = url.toString();
        QVERIFY2(urlStr.contains(QStringLiteral("2024")),
                 qPrintable(QStringLiteral("URL should contain year 2024: %1").arg(urlStr)));
        QVERIFY2(urlStr.contains(QStringLiteral("03")),
                 qPrintable(QStringLiteral("URL should contain month 03: %1").arg(urlStr)));
    }

    void testUKPoliceSourceCategoryFilter()
    {
        const QUrl url = UKPoliceSource::buildFetchUrl(
            51.5, -0.1, QStringLiteral("2024-03"), QStringLiteral("burglary"));
        const QString urlStr = url.toString();
        QVERIFY2(urlStr.contains(QStringLiteral("burglary")),
                 qPrintable(QStringLiteral("URL should contain category burglary: %1").arg(urlStr)));
    }

    // ── WeatherSource ────────────────────────────────────────────────────────

    void testWeatherSourceConstruction()
    {
        WeatherSource ws;
        QVERIFY(ws.parent() == nullptr);
    }

    void testWeatherCacheSizeZeroInitially()
    {
        WeatherSource ws;
        QCOMPARE(ws.cachedHourCount(), 0);
    }

    void testWeatherForecastForTimeEmpty()
    {
        WeatherSource ws;
        const auto result = ws.dataAt(QDateTime::currentDateTimeUtc());
        QVERIFY(!result.has_value());
    }

    void testWeatherSourceMalformedJsonNocrash()
    {
        WeatherSource ws;

        QSignalSpy errorSpy(&ws, &WeatherSource::fetchError);
        auto* reply = new FakeNetworkReply(QByteArray("{not valid json"));
        ws.onReplyFinished(reply);
        QCoreApplication::processEvents();

        QCOMPARE(ws.cachedHourCount(), 0);
        QVERIFY(errorSpy.size() >= 1);
    }

    // ── DataQualityScorer ────────────────────────────────────────────────────

    void testQualityScoreFullEvent()
    {
        DataQualityScorer scorer = DataQualityScorer::withDefaults();
        const QualityReport report = scorer.score(fullQualityEvent());
        QVERIFY(qAbs(report.completeness - 1.0) < 1e-9);
    }

    void testQualityScoreNoLatLon()
    {
        DataQualityScorer scorer;
        CrimeEvent ev = fullQualityEvent();
        ev.lat.reset();
        ev.lon.reset();
        ev.latitude  = 0.0;
        ev.longitude = 0.0;

        const QualityReport report = scorer.score(ev);
        QCOMPARE(report.spatialPrecision, QStringLiteral("unknown"));
    }

    void testQualityScoreLatLonOnly()
    {
        DataQualityScorer scorer;
        CrimeEvent ev;
        ev.eventId   = QStringLiteral("E_COORDS");
        ev.source    = QStringLiteral("uk_police_v1");
        ev.crimeType = QStringLiteral("burglary");
        ev.occurredAt = QDateTime(QDate(2024, 3, 15), QTime(10, 0, 0), QTimeZone::utc());
        ev.lat = 51.5074;
        ev.lon = -0.1278;

        const QualityReport report = scorer.score(ev);
        QVERIFY2(report.spatialPrecision == QStringLiteral("exact") ||
                 report.spatialPrecision == QStringLiteral("block"),
                 qPrintable(QStringLiteral("Expected exact/block, got %1")
                                .arg(report.spatialPrecision)));
    }

    void testQualityScoreCompositeRange()
    {
        DataQualityScorer scorer = DataQualityScorer::withDefaults();

        CrimeEvent minimal;
        minimal.eventId = QStringLiteral("E_MIN");

        const QVector<CrimeEvent> cases = { fullQualityEvent(), minimal };
        for (const CrimeEvent& ev : cases) {
            const QualityReport report = scorer.score(ev);
            QVERIFY2(report.compositeScore >= 0.0 && report.compositeScore <= 1.0,
                     qPrintable(QStringLiteral("compositeScore %1 must be in [0,1]")
                                    .arg(report.compositeScore)));
        }
    }

    void testQualityScoreQuarantineThreshold()
    {
        DataQualityScorer scorer;
        CrimeEvent ev;
        ev.eventId = QStringLiteral("E_BAD");
        const QualityReport report = scorer.score(ev);
        if (report.compositeScore < 0.3) {
            QVERIFY(report.quarantined);
        }
        QVERIFY(report.compositeScore < 0.3);
        QVERIFY(report.quarantined);
    }

    void testQualityScoreNotQuarantined()
    {
        DataQualityScorer scorer = DataQualityScorer::withDefaults();
        const QualityReport report = scorer.score(fullQualityEvent());
        QVERIFY(report.compositeScore >= 0.3);
        QVERIFY(!report.quarantined);
    }

    void testQualityScoreDefaultReliability()
    {
        const QMap<QString, double> defaults = DataQualityScorer::defaultReliabilityMap();
        QVERIFY(defaults.contains(QStringLiteral("uk_police")));
        QVERIFY(defaults.contains(QStringLiteral("weather")));
        QVERIFY(defaults.contains(QStringLiteral("csv")));
    }

    void testQualityScoreSourceReliabilityUsed()
    {
        DataQualityScorer scorer = DataQualityScorer::withDefaults();
        CrimeEvent ev = fullQualityEvent();
        ev.source = QStringLiteral("uk_police");
        const QualityReport report = scorer.score(ev);
        QCOMPARE(report.sourceReliability, 0.90);
    }
};

QTEST_GUILESS_MAIN(IngestDeepTest)
#include "test_ingest_deep.moc"
