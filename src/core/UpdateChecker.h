#pragma once

#include <QByteArray>
#include <QString>
#include <optional>

// Checks GitHub Releases API for a version newer than the running build.
class UpdateChecker {
public:
    // Returns the release page URL when a newer tag exists; nullopt on error or no update.
    static std::optional<QString> checkForUpdate(
        const QString& currentVersion,
        const QString& repo = QStringLiteral("odin-loki/SENTINEL"));

    // Exposed for unit tests (no network).
    static std::optional<QString> parseLatestRelease(
        const QByteArray& json,
        const QString& currentVersion);

    static int compareVersions(const QString& remote, const QString& current);

private:
    static QString normaliseVersion(const QString& version);
};
