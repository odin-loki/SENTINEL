// test_uk_police_source_deep4.cpp — Deep audit iteration 20: UKPoliceSource
// fetchSince scheduling, error paths, progress signal, parse edge cases.

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
#ifdef __GNUC__
#  pragma GCC diagnostic push
#  pragma GCC diagnostic ignored "-Wkeyword-macro"
#endif
#define private public
#include "ingest/UKPoliceSource.h"
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

class UKPoliceSourceDeep4Test : public QObject
{
    Q_OBJECT

    static QJsonObject makeRecord(const QString& category,
                                  const QString& month = QStringLiteral("2024-03"),
                                  const QString& id    = QStringLiteral("rec-1"),
                                  const QString& lat   = QString(),
                                  const QString& lon   = QString())
    {
        QJsonObject obj;
        obj[QStringLiteral("id")]       = id;
        obj[QStringLiteral("category")] = category;
        obj[QStringLiteral("month")]    = month;

        QJsonObject loc;
        if (!lat.isEmpty()) loc[QStringLiteral("latitude")]  = lat;
        if (!lon.isEmpty()) loc[QStringLiteral("longitude")] = lon;
        obj[QStringLiteral("location")] = loc;
        return obj;
    }

private slots:

    // ── fetchSince: future start date ─────────────────────────────────────────

    void testFetchSinceFutureDateEmitsCompleteZero()
    {
        UKPoliceSource src(51.5, -0.1);
        QSignalSpy completeSpy(&src, &DataSource::fetchComplete);
        QSignalSpy errorSpy(&src, &DataSource::fetchError);

        const QDateTime future = QDateTime(
            QDate::currentDate().addYears(2), QTime(0, 0), QTimeZone::utc());
        src.fetchSince(future);
        QCoreApplication::processEvents();

        QCOMPARE(completeSpy.size(), 1);
        QCOMPARE(completeSpy.at(0).at(0).toInt(), 0);
        QCOMPARE(errorSpy.size(), 0);
        QVERIFY(src.m_pendingUrls.isEmpty());
    }

    // ── radiusKm stored but not passed to API URL ─────────────────────────────

    void testRadiusKmUnusedInBuildFetchUrl()
    {
        // BUG (documented): m_radiusKm is stored via constructor/setLocation but
        // buildFetchUrl never includes a radius query parameter.
        UKPoliceSource src(51.5, -0.1, 2.5);
        const QUrl url = UKPoliceSource::buildFetchUrl(51.5, -0.1, QStringLiteral("2024-06"));
        const QString urlStr = url.toString().toLower();
        QVERIFY2(!urlStr.contains(QStringLiteral("radius")),
                 "radius parameter absent from UK Police API URL");
        QVERIFY2(!urlStr.contains(QStringLiteral("distance")),
                 "distance parameter absent from UK Police API URL");
        Q_UNUSED(src);
    }

    void testSetLocationUpdatesInternalCoordinates()
    {
        UKPoliceSource src(51.0, 0.0, 1.0);
        src.setLocation(52.2, -1.5, 3.0);
        QCOMPARE(src.m_lat, 52.2);
        QCOMPARE(src.m_lon, -1.5);
        QCOMPARE(src.m_radiusKm, 3.0);
    }

    // ── onReplyFinished: network error path ───────────────────────────────────

    void testNetworkErrorEmitsFetchErrorAndComplete()
    {
        UKPoliceSource src(51.5, -0.1);
        src.m_pendingUrls.clear();
        src.m_inFlightRequests = 1;
        src.m_totalRequests    = 1;
        src.m_fetchCount       = 0;

        QSignalSpy errorSpy(&src, &DataSource::fetchError);
        QSignalSpy completeSpy(&src, &DataSource::fetchComplete);

        src.onReplyFinished(new FakeNetworkReply(
            QByteArray{}, QNetworkReply::ConnectionRefusedError));
        QCoreApplication::processEvents();

        QCOMPARE(errorSpy.size(), 1);
        QCOMPARE(completeSpy.size(), 1);
        QCOMPARE(completeSpy.at(0).at(0).toInt(), 0);
    }

    // ── progress signal on successful multi-request batch ───────────────────────

    void testProgressEmittedOnSuccessfulReply()
    {
        UKPoliceSource src(51.5, -0.1);
        src.m_pendingUrls.clear();
        src.m_inFlightRequests = 1;
        src.m_totalRequests    = 3;
        src.m_fetchCount       = 0;

        QSignalSpy progressSpy(&src, &DataSource::progress);

        QJsonArray arr;
        arr.append(makeRecord(QStringLiteral("burglary")));
        src.onReplyFinished(new FakeNetworkReply(QJsonDocument(arr).toJson()));
        QCoreApplication::processEvents();

        QCOMPARE(progressSpy.size(), 1);
        QCOMPARE(progressSpy.at(0).at(0).toInt(), 3); // done = total - pending(0) after dequeue? 
        // pending was cleared before call; done = m_totalRequests - pending.size() = 3 - 0 = 3
        QCOMPARE(progressSpy.at(0).at(1).toInt(), 3);
    }

    // ── parseRecord edge cases ────────────────────────────────────────────────

    void testEmptyCategoryLeavesCrimeTypeEmpty()
    {
        UKPoliceSource src(51.5, -0.1);
        QJsonObject obj = makeRecord(QString());
        obj.remove(QStringLiteral("category"));

        const CrimeEvent ev = src.parseRecord(obj);
        QVERIFY(ev.crimeType.isEmpty());
    }

    void testQualityScoreAlwaysHalf()
    {
        UKPoliceSource src(51.5, -0.1);
        const CrimeEvent ev = src.parseRecord(makeRecord(QStringLiteral("robbery")));
        QCOMPARE(ev.qualityScore, 0.5);
    }

    void testNonObjectArrayElementsSkipped()
    {
        UKPoliceSource src(51.5, -0.1);
        src.m_pendingUrls.clear();
        src.m_inFlightRequests = 1;
        src.m_totalRequests    = 1;

        QJsonArray arr;
        arr.append(QStringLiteral("not-an-object"));
        arr.append(42);
        arr.append(makeRecord(QStringLiteral("theft-from-the-person")));

        QSignalSpy eventSpy(&src, &DataSource::eventFetched);
        src.onReplyFinished(new FakeNetworkReply(QJsonDocument(arr).toJson()));
        QCoreApplication::processEvents();

        QCOMPARE(eventSpy.size(), 1);
        QCOMPARE(eventSpy.at(0).at(0).value<CrimeEvent>().crimeType,
                 QStringLiteral("theft"));
    }

    void testTheftFromPersonMapsToTheft()
    {
        UKPoliceSource src(51.5, -0.1);
        const CrimeEvent ev = src.parseRecord(
            makeRecord(QStringLiteral("theft-from-the-person")));
        QCOMPARE(ev.crimeType, QStringLiteral("theft"));
    }
};

QTEST_GUILESS_MAIN(UKPoliceSourceDeep4Test)
#include "test_uk_police_source_deep4.moc"
