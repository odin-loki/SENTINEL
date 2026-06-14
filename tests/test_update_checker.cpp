// test_update_checker.cpp — UpdateChecker unit tests (offline + optional live check)
// Build via: sentinel_test(test_update_checker test_update_checker.cpp)

#include <QTest>
#include <QCoreApplication>
#include <QEventLoop>
#include <QJsonObject>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>

#include "core/UpdateChecker.h"

namespace {

bool hasNetwork()
{
    QNetworkAccessManager nam;
    QEventLoop loop;

    QNetworkRequest req(QUrl(QStringLiteral("https://api.github.com")));
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("SENTINEL-test"));
    req.setTransferTimeout(4000);

    QNetworkReply* reply = nam.head(req);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QTimer::singleShot(4000, &loop, &QEventLoop::quit);
    loop.exec();

    const bool ok = reply->isFinished() && reply->error() == QNetworkReply::NoError;
    reply->deleteLater();
    return ok;
}

QByteArray makeReleaseJson(const QString& tag, const QString& url)
{
    QJsonObject root;
    root[QStringLiteral("tag_name")] = tag;
    root[QStringLiteral("html_url")]   = url;
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// TestUpdateChecker
// ─────────────────────────────────────────────────────────────────────────────

class TestUpdateChecker : public QObject
{
    Q_OBJECT

private slots:

    void testCompareVersionsOrdering()
    {
        QCOMPARE(UpdateChecker::compareVersions(QStringLiteral("1.1.0"),
                                                QStringLiteral("1.0.0")), 1);
        QCOMPARE(UpdateChecker::compareVersions(QStringLiteral("v2.0.0"),
                                                QStringLiteral("1.9.9")), 1);
        QCOMPARE(UpdateChecker::compareVersions(QStringLiteral("1.0.0"),
                                                QStringLiteral("1.0.0")), 0);
        QCOMPARE(UpdateChecker::compareVersions(QStringLiteral("0.9.0"),
                                                QStringLiteral("1.0.0")), -1);
    }

    void testParseLatestReleaseDetectsNewerVersion()
    {
        const QByteArray json = makeReleaseJson(
            QStringLiteral("v1.2.0"),
            QStringLiteral("https://github.com/odin-loki/SENTINEL/releases/tag/v1.2.0"));
        const auto url = UpdateChecker::parseLatestRelease(json, QStringLiteral("1.0.0"));
        QVERIFY(url.has_value());
        QCOMPARE(*url, QStringLiteral("https://github.com/odin-loki/SENTINEL/releases/tag/v1.2.0"));
    }

    void testParseLatestReleaseNoUpdateWhenCurrent()
    {
        const QByteArray json = makeReleaseJson(
            QStringLiteral("1.0.0"),
            QStringLiteral("https://github.com/example/releases/tag/1.0.0"));
        const auto url = UpdateChecker::parseLatestRelease(json, QStringLiteral("1.0.0"));
        QVERIFY(!url.has_value());
    }

    void testParseLatestReleaseInvalidJson()
    {
        const auto url = UpdateChecker::parseLatestRelease(
            QByteArrayLiteral("not-json"), QStringLiteral("1.0.0"));
        QVERIFY(!url.has_value());
    }

    void testCheckForUpdateLiveOrSkip()
    {
        if (!qEnvironmentVariableIsSet("SENTINEL_LIVE_NETWORK"))
            QSKIP("Live network check disabled (set SENTINEL_LIVE_NETWORK=1 to enable)");
        if (!hasNetwork())
            QSKIP("No network connectivity — skipping live GitHub API check");

        // Use an implausibly high version so a successful response still yields no update.
        const auto url = UpdateChecker::checkForUpdate(QStringLiteral("99.99.99"));
        QVERIFY(!url.has_value());
    }
};

int runUpdateCheckerTests(int argc, char** argv)
{
    TestUpdateChecker checker;
    return QTest::qExec(&checker, argc, argv);
}

#include "test_update_checker.moc"
