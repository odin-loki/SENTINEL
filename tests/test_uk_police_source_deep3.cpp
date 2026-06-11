// Deep audit iteration 16 — UKPoliceSource parse, inFlight guard, empty response
#include <QTest>
#include <QCoreApplication>
#include <QSignalSpy>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QNetworkRequest>
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

class UKPoliceSourceDeep3Test : public QObject
{
    Q_OBJECT

private:
    UKPoliceSource* m_src = nullptr;

    static QJsonObject makeRecord(const QString& category,
                                  const QString& month = QStringLiteral("2024-03"),
                                  const QString& lat   = QString(),
                                  const QString& lon   = QString())
    {
        QJsonObject obj;
        obj[QStringLiteral("id")]       = QStringLiteral("rec-1");
        obj[QStringLiteral("category")] = category;
        obj[QStringLiteral("month")]    = month;

        QJsonObject loc;
        if (!lat.isEmpty()) loc[QStringLiteral("latitude")]  = lat;
        if (!lon.isEmpty()) loc[QStringLiteral("longitude")] = lon;
        obj[QStringLiteral("location")] = loc;
        return obj;
    }

private slots:

    void initTestCase()
    {
        m_src = new UKPoliceSource(51.5, -0.1, 1.0, this);
    }

    void cleanupTestCase()
    {
        delete m_src;
        m_src = nullptr;
    }

    // ── 1. parseRecord: context field populates narrative ───────────────────
    void testParseContextFieldToNarrative()
    {
        QJsonObject obj = makeRecord(QStringLiteral("burglary"));
        obj[QStringLiteral("context")] = QStringLiteral("Near railway station");

        const CrimeEvent ev = m_src->parseRecord(obj);
        QVERIFY(ev.narrative.has_value());
        QCOMPARE(ev.narrative.value(), QStringLiteral("Near railway station"));
    }

    // ── 2. parseRecord: non-numeric lat/lon strings leave optional unset ────
    void testParseNonNumericLatLonUnset()
    {
        QJsonObject obj = makeRecord(QStringLiteral("theft"),
                                     QStringLiteral("2024-04"),
                                     QStringLiteral("invalid"),
                                     QStringLiteral("also-bad"));

        const CrimeEvent ev = m_src->parseRecord(obj);
        QVERIFY(!ev.lat.has_value());
        QVERIFY(!ev.lon.has_value());
    }

    // ── 3. parseRecord: vehicle-crime maps to vehicle_crime ─────────────────
    void testParseVehicleCrimeMapping()
    {
        const CrimeEvent ev = m_src->parseRecord(
            makeRecord(QStringLiteral("vehicle-crime")));
        QCOMPARE(ev.crimeType, QStringLiteral("vehicle_crime"));
    }

    // ── 4. parseRecord: invalid month leaves occurredAt unset ───────────────
    void testParseInvalidMonthNoOccurredAt()
    {
        const CrimeEvent ev = m_src->parseRecord(
            makeRecord(QStringLiteral("burglary"), QStringLiteral("not-a-month")));
        QVERIFY(!ev.occurredAt.has_value());
    }

    // ── 5. Empty JSON array response emits fetchComplete(0) ─────────────────
    void testEmptyArrayResponseEmitsFetchCompleteZero()
    {
        UKPoliceSource src(51.5, -0.1);
        src.m_pendingUrls.clear();
        src.m_inFlightRequests = 1;
        src.m_totalRequests    = 1;

        QSignalSpy completeSpy(&src, &DataSource::fetchComplete);
        QSignalSpy eventSpy(&src, &DataSource::eventFetched);

        src.onReplyFinished(new FakeNetworkReply(QByteArray("[]")));
        QCoreApplication::processEvents();

        QCOMPARE(completeSpy.size(), 1);
        QCOMPARE(completeSpy.at(0).at(0).toInt(), 0);
        QCOMPARE(eventSpy.size(), 0);
    }

    // ── 6. inFlight guard: fetchComplete deferred until all replies finish ──
    void testInFlightGuardDefersFetchComplete()
    {
        UKPoliceSource src(51.5, -0.1);
        src.m_pendingUrls.clear();
        src.m_inFlightRequests = 2;
        src.m_totalRequests    = 2;

        QSignalSpy completeSpy(&src, &DataSource::fetchComplete);

        src.onReplyFinished(new FakeNetworkReply(QByteArray("[]")));
        QCoreApplication::processEvents();
        QCOMPARE(completeSpy.size(), 0);

        src.onReplyFinished(new FakeNetworkReply(QByteArray("[]")));
        QCoreApplication::processEvents();
        QCOMPARE(completeSpy.size(), 1);
    }

    // ── 7. Non-array JSON body produces no events, no crash ─────────────────
    void testNonArrayJsonResponseNoEvents()
    {
        UKPoliceSource src(51.5, -0.1);
        src.m_pendingUrls.clear();
        src.m_inFlightRequests = 1;
        src.m_totalRequests    = 1;

        QSignalSpy eventSpy(&src, &DataSource::eventFetched);
        QSignalSpy completeSpy(&src, &DataSource::fetchComplete);

        const QByteArray body = QJsonDocument(QJsonObject{
            { QStringLiteral("error"), QStringLiteral("not found") }
        }).toJson();

        src.onReplyFinished(new FakeNetworkReply(body));
        QCoreApplication::processEvents();

        QCOMPARE(eventSpy.size(), 0);
        QCOMPARE(completeSpy.size(), 1);
        QCOMPARE(completeSpy.at(0).at(0).toInt(), 0);
    }

    // ── 8. Populated array emits one eventFetched per record ────────────────
    void testPopulatedArrayEmitsEvents()
    {
        UKPoliceSource src(51.5, -0.1);
        src.m_pendingUrls.clear();
        src.m_inFlightRequests = 1;
        src.m_totalRequests    = 1;

        QJsonArray arr;
        arr.append(makeRecord(QStringLiteral("burglary")));
        arr.append(makeRecord(QStringLiteral("robbery"), QStringLiteral("2024-05"),
                              QStringLiteral("51.5"), QStringLiteral("-0.1")));

        QSignalSpy eventSpy(&src, &DataSource::eventFetched);
        src.onReplyFinished(new FakeNetworkReply(QJsonDocument(arr).toJson()));
        QCoreApplication::processEvents();

        QCOMPARE(eventSpy.size(), 2);
        QCOMPARE(eventSpy.at(0).at(0).value<CrimeEvent>().crimeType,
                 QStringLiteral("burglary"));
        QCOMPARE(eventSpy.at(1).at(0).value<CrimeEvent>().crimeType,
                 QStringLiteral("robbery"));
    }
};

QTEST_GUILESS_MAIN(UKPoliceSourceDeep3Test)
#include "test_uk_police_source_deep3.moc"
