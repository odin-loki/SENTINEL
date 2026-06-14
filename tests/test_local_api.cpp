// test_local_api.cpp — Headless tests for LocalApiServer read-only endpoints

#include <QTest>
#include <QCoreApplication>
#include <QDateTime>
#include <QEventLoop>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>

#include "api/LocalApiServer.h"
#include "core/AppConfig.h"
#include "core/CrimeEvent.h"
#include "core/Database.h"

namespace {

QJsonObject httpGetJson(quint16 port, const QString& path)
{
    QNetworkAccessManager nam;
    const QUrl url(QStringLiteral("http://127.0.0.1:%1%2").arg(port).arg(path));
    QNetworkReply* reply = nam.get(QNetworkRequest(url));

    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QTimer::singleShot(5000, &loop, &QEventLoop::quit);
    loop.exec();

    QJsonObject result;
    if (reply->error() == QNetworkReply::NoError) {
        const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        if (doc.isObject())
            result = doc.object();
    }
    reply->deleteLater();
    return result;
}

CrimeEvent makeEvent(const QString& id, const QDateTime& when, const QString& crimeType)
{
    CrimeEvent ev;
    ev.eventId = id;
    ev.id = id;
    ev.source = QStringLiteral("test");
    ev.ingestedAt = QDateTime::currentDateTimeUtc();
    ev.crimeType = crimeType;
    ev.suburb = QStringLiteral("Testville");
    ev.lat = 51.5;
    ev.lon = -0.12;
    ev.latitude = 51.5;
    ev.longitude = -0.12;
    ev.occurredAt = when;
    ev.timestamp = when;
    ev.outcome = QStringLiteral("unknown");
    ev.qualityScore = 0.9;
    return ev;
}

} // namespace

class TestLocalApi : public QObject
{
    Q_OBJECT

private slots:
    void testHealthEndpoint();
    void testEventsSinceFilter();
    void testLeadsEndpoint();
    void testDisabledByDefaultInConfig();
};

void TestLocalApi::testHealthEndpoint()
{
    AppConfig cfg;
    cfg.databasePath = QStringLiteral(":memory:");
    auto db = std::make_shared<Database>(cfg);
    QVERIFY(db->open());

    LocalApiServer api(db, 0);
    QVERIFY(api.start());
    QVERIFY(api.isRunning());
    QVERIFY(api.port() > 0);

    const QJsonObject body = httpGetJson(api.port(), QStringLiteral("/api/v1/health"));
    QCOMPARE(body.value(QStringLiteral("status")).toString(), QStringLiteral("ok"));
    QVERIFY(body.contains(QStringLiteral("version")));
}

void TestLocalApi::testEventsSinceFilter()
{
    AppConfig cfg;
    cfg.databasePath = QStringLiteral(":memory:");
    auto db = std::make_shared<Database>(cfg);
    QVERIFY(db->open());

    const QDateTime oldTime = QDateTime::fromString(QStringLiteral("2024-01-01T10:00:00"),
                                                      Qt::ISODate);
    const QDateTime newTime = QDateTime::fromString(QStringLiteral("2024-06-01T10:00:00"),
                                                      Qt::ISODate);
    QVERIFY((db->insertEvent(makeEvent(QStringLiteral("ev-old"), oldTime, QStringLiteral("theft")))));
    QVERIFY((db->insertEvent(makeEvent(QStringLiteral("ev-new"), newTime, QStringLiteral("burglary")))));

    LocalApiServer api(db, 0);
    QVERIFY(api.start());

    const QJsonObject allBody = httpGetJson(api.port(), QStringLiteral("/api/v1/events"));
    QCOMPARE(allBody.value(QStringLiteral("count")).toInt(), 2);

    const QJsonObject filteredBody = httpGetJson(
        api.port(),
        QStringLiteral("/api/v1/events?since=2024-03-01T00:00:00"));
    QCOMPARE(filteredBody.value(QStringLiteral("count")).toInt(), 1);
}

void TestLocalApi::testLeadsEndpoint()
{
    AppConfig cfg;
    cfg.databasePath = QStringLiteral(":memory:");
    auto db = std::make_shared<Database>(cfg);
    QVERIFY(db->open());

    InvestigativeLead lead;
    lead.rank = 1;
    lead.category = QStringLiteral("series");
    lead.headline = QStringLiteral("Possible series link");
    lead.detail = QStringLiteral("Two burglaries within 300 m");
    lead.confidence = 0.72;
    lead.confidenceMethod = QStringLiteral("dbscan");
    lead.generatedAt = QDateTime::currentDateTimeUtc();
    QVERIFY(db->insertLead(lead, QStringLiteral("ev-1")));

    LocalApiServer api(db, 0);
    QVERIFY(api.start());

    const QJsonObject body = httpGetJson(api.port(), QStringLiteral("/api/v1/leads"));
    QCOMPARE(body.value(QStringLiteral("count")).toInt(), 1);
    QVERIFY(body.value(QStringLiteral("leads")).isArray());
}

void TestLocalApi::testDisabledByDefaultInConfig()
{
    AppConfig cfg;
    QVERIFY(!cfg.enableLocalApi);
    QCOMPARE(cfg.localApiPort, 8765);
}

QTEST_GUILESS_MAIN(TestLocalApi)
#include "test_local_api.moc"
