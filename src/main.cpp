#include <QApplication>
#include <QFile>
#include <QDir>
#include <QStandardPaths>
#include "ui/MainWindow.h"
#include "core/AppConfig.h"
#include "core/Database.h"
#include "core/SentinelLogger.h"

#ifndef SENTINEL_VERSION
#define SENTINEL_VERSION "1.0.0"
#endif

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName("Sentinel");
    app.setApplicationVersion(QStringLiteral(SENTINEL_VERSION));
    app.setOrganizationName("SENTINEL");

    SentinelLogger::instance().install();

    // Ensure AppData dir exists
    const QString appData = QStandardPaths::writableLocation(
        QStandardPaths::AppDataLocation);
    QDir().mkpath(appData);

    AppConfig cfg = AppConfig::load();

    // Open database
    auto db = std::make_shared<Database>(cfg);
    if (!db->open()) {
        qCritical() << "Failed to open database:" << db->lastError();
        return 1;
    }

    // Load and apply dark stylesheet
    QFile styleFile(QStringLiteral(":/styles/dark.qss"));
    if (styleFile.open(QIODevice::ReadOnly))
        app.setStyleSheet(QString::fromUtf8(styleFile.readAll()));

    MainWindow window(cfg, db);
    window.setWindowTitle(QStringLiteral("SENTINEL \u2014 Crime Analytics & Threat Assessment"));
    window.resize(1400, 900);
    window.show();

    return app.exec();
}
