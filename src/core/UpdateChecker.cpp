#include "core/UpdateChecker.h"

#include <QEventLoop>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>

namespace {

QStringList versionParts(const QString& version)
{
    const QString stripped = version.startsWith(QLatin1Char('v'), Qt::CaseInsensitive)
        ? version.mid(1)
        : version;
    return stripped.split(QLatin1Char('.'), Qt::SkipEmptyParts);
}

} // namespace

QString UpdateChecker::normaliseVersion(const QString& version)
{
    QString v = version.trimmed();
    if (v.startsWith(QLatin1Char('v'), Qt::CaseInsensitive))
        v = v.mid(1);
    return v;
}

int UpdateChecker::compareVersions(const QString& remote, const QString& current)
{
    const QStringList remoteParts = versionParts(normaliseVersion(remote));
    const QStringList currentParts = versionParts(normaliseVersion(current));
    const int n = qMax(remoteParts.size(), currentParts.size());

    for (int i = 0; i < n; ++i) {
        const int r = i < remoteParts.size() ? remoteParts[i].toInt() : 0;
        const int c = i < currentParts.size() ? currentParts[i].toInt() : 0;
        if (r > c) return 1;
        if (r < c) return -1;
    }
    return 0;
}

std::optional<QString> UpdateChecker::parseLatestRelease(
    const QByteArray& json,
    const QString& currentVersion)
{
    const QJsonDocument doc = QJsonDocument::fromJson(json);
    if (!doc.isObject())
        return std::nullopt;

    const QJsonObject root = doc.object();
    const QString tagName = root.value(QStringLiteral("tag_name")).toString();
    const QString htmlUrl = root.value(QStringLiteral("html_url")).toString();
    if (tagName.isEmpty() || htmlUrl.isEmpty())
        return std::nullopt;

    if (compareVersions(tagName, currentVersion) > 0)
        return htmlUrl;
    return std::nullopt;
}

std::optional<QString> UpdateChecker::checkForUpdate(
    const QString& currentVersion,
    const QString& repo)
{
    QNetworkAccessManager nam;
    QEventLoop loop;

    const QUrl url(QStringLiteral("https://api.github.com/repos/%1/releases/latest").arg(repo));
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("SENTINEL"));
    req.setRawHeader("Accept", "application/vnd.github+json");
    req.setTransferTimeout(8000);

    QNetworkReply* reply = nam.get(req);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QTimer::singleShot(8000, &loop, &QEventLoop::quit);
    loop.exec();

    if (!reply->isFinished()) {
        reply->abort();
        reply->deleteLater();
        return std::nullopt;
    }

    if (reply->error() != QNetworkReply::NoError) {
        reply->deleteLater();
        return std::nullopt;
    }

    const auto result = parseLatestRelease(reply->readAll(), currentVersion);
    reply->deleteLater();
    return result;
}
