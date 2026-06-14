// test_uk_police_source_deep5.cpp — Deep audit iteration 23: UKPoliceSource
// outcome mapping, date-range boundaries, empty response, sourceId/healthCheck,
// category normalisation, and lat/lon extraction.

#include <QTest>
#include <QCoreApplication>
#include <QSignalSpy>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QDate>
#include <QTimeZone>
#include <cmath>

#include "core/CrimeEvent.h"
#include "ingest/UKPoliceSource.h"

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

class UKPoliceSourceDeep5Test : public QObject
{
    Q_OBJECT

    static QJsonObject makeRecord(const QString& category,
                                  const QString& month = QStringLiteral("2024-03"),
                                  const QString& id    = QStringLiteral("rec-1"),
                                  const QString& lat   = QString(),
                                  const QString& lon   = QString(),
                                  const QString& outcomeCategory = QString())
    {
        QJsonObject obj;
        obj[QStringLiteral("id")]       = id;
        obj[QStringLiteral("category")] = category;
        obj[QStringLiteral("month")]    = month;

        QJsonObject loc;
        if (!lat.isEmpty()) loc[QStringLiteral("latitude")]  = lat;
        if (!lon.isEmpty()) loc[QStringLiteral("longitude")] = lon;
        obj[QStringLiteral("location")] = loc;

        if (!outcomeCategory.isEmpty()) {
            QJsonObject outcome;
            outcome[QStringLiteral("category")] = outcomeCategory;
            obj[QStringLiteral("outcome_status")] = outcome;
        }
        return obj;
    }

private slots:

    // ── outcome_status.category → ev.outcome ───────────────────────────────────

    void testOutcomeStatusCategoryMappedToOutcome()
    {
        UKPoliceSource src(51.5, -0.1);
        const CrimeEvent ev = src.parseRecord(
            makeRecord(QStringLiteral("burglary"),
                       QStringLiteral("2024-03"),
                       QStringLiteral("out-1"),
                       QString(),
                       QString(),
                       QStringLiteral("Investigation complete; no suspect identified")));

        QCOMPARE(ev.outcome,
                 QStringLiteral("Investigation complete; no suspect identified"));
    }

    // ── fetchSince date-range boundaries ───────────────────────────────────────

    void testFetchSinceCurrentMonthFirstDayEnqueuesOneRequest()
    {
        UKPoliceSource src(51.5, -0.1);
        const QDate today = QDate::currentDate();
        const QDateTime since = QDateTime(
            QDate(today.year(), today.month(), 1), QTime(0, 0), QTimeZone::utc());

        src.fetchSince(since);
        QCoreApplication::processEvents();

        QCOMPARE(src.m_totalRequests, 1);
        QVERIFY(!src.m_pendingUrls.isEmpty() || src.m_inFlightRequests > 0);
    }

    void testFetchSinceSpanningTwoMonthsEnqueuesTwoRequests()
    {
        UKPoliceSource src(51.5, -0.1);
        const QDate today = QDate::currentDate();
        const QDate prevMonth = today.addMonths(-1);
        const QDateTime since = QDateTime(
            QDate(prevMonth.year(), prevMonth.month(), 1), QTime(0, 0), QTimeZone::utc());

        src.fetchSince(since);
        QCoreApplication::processEvents();

        const int expected = (today.year() - prevMonth.year()) * 12
                           + (today.month() - prevMonth.month()) + 1;
        QCOMPARE(src.m_totalRequests, expected);
        QVERIFY(expected >= 2);
    }

    void testFetchSinceFutureDateEmitsCompleteZero()
    {
        UKPoliceSource src(51.5, -0.1);
        QSignalSpy completeSpy(&src, &DataSource::fetchComplete);

        const QDateTime future = QDateTime(
            QDate::currentDate().addYears(2), QTime(0, 0), QTimeZone::utc());
        src.fetchSince(future);
        QCoreApplication::processEvents();

        QCOMPARE(completeSpy.size(), 1);
        QCOMPARE(completeSpy.at(0).at(0).toInt(), 0);
        QCOMPARE(src.m_totalRequests, 0);
    }

    // ── empty JSON array handled gracefully ───────────────────────────────────

    void testEmptyArrayResponseCompletesWithZeroEvents()
    {
        UKPoliceSource src(51.5, -0.1);
        src.m_pendingUrls.clear();
        src.m_inFlightRequests = 1;
        src.m_totalRequests    = 1;
        src.m_fetchCount       = 0;

        QSignalSpy eventSpy(&src, &DataSource::eventFetched);
        QSignalSpy completeSpy(&src, &DataSource::fetchComplete);

        src.onReplyFinished(new FakeNetworkReply(QJsonDocument(QJsonArray{}).toJson()));
        QCoreApplication::processEvents();

        QCOMPARE(eventSpy.size(), 0);
        QCOMPARE(completeSpy.size(), 1);
        QCOMPARE(completeSpy.at(0).at(0).toInt(), 0);
    }

    // ── sourceId and healthCheck contract ─────────────────────────────────────

    void testSourceIdAndHealthCheckContract()
    {
        UKPoliceSource src(51.5, -0.1);
        QCOMPARE(src.sourceId(), QStringLiteral("uk_police_v1"));
        QCOMPARE(src.displayName(), QStringLiteral("UK Police Open Data"));
        const bool reachable = src.healthCheck();
        Q_UNUSED(reachable);
    }

    // ── crime category normalisation ──────────────────────────────────────────

    void testCrimeCategoryNormalisation()
    {
        UKPoliceSource src(51.5, -0.1);
        QCOMPARE(src.parseRecord(makeRecord(QStringLiteral("violent-crime"))).crimeType,
                 QStringLiteral("assault"));
        QCOMPARE(src.parseRecord(makeRecord(QStringLiteral("vehicle-crime"))).crimeType,
                 QStringLiteral("vehicle_crime"));
        QCOMPARE(src.parseRecord(makeRecord(QStringLiteral("bicycle-theft"))).crimeType,
                 QStringLiteral("bicycle-theft"));
    }

    // ── lat/lon extraction from location object ───────────────────────────────

    void testLocationLatLonExtractionFromLocationObject()
    {
        UKPoliceSource src(51.5, -0.1);
        const CrimeEvent valid = src.parseRecord(
            makeRecord(QStringLiteral("burglary"),
                       QStringLiteral("2024-04"),
                       QStringLiteral("geo-1"),
                       QStringLiteral("51.5074"),
                       QStringLiteral("-0.1278")));
        QVERIFY(valid.lat.has_value());
        QVERIFY(valid.lon.has_value());
        QVERIFY(std::abs(*valid.lat - 51.5074) < 1e-6);
        QVERIFY(std::abs(*valid.lon - (-0.1278)) < 1e-6);

        const CrimeEvent invalid = src.parseRecord(
            makeRecord(QStringLiteral("theft"),
                       QStringLiteral("2024-04"),
                       QStringLiteral("geo-2"),
                       QStringLiteral("not-a-number"),
                       QStringLiteral("also-bad")));
        QVERIFY(!invalid.lat.has_value());
        QVERIFY(!invalid.lon.has_value());
    }
};

QTEST_GUILESS_MAIN(UKPoliceSourceDeep5Test)
#include "test_uk_police_source_deep5.moc"
