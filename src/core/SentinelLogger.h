#pragma once
#include <QObject>
#include <QLoggingCategory>
#include <QVector>
#include <QString>
#include <QDateTime>
#include <functional>

Q_DECLARE_LOGGING_CATEGORY(lcIngest)
Q_DECLARE_LOGGING_CATEGORY(lcNlp)
Q_DECLARE_LOGGING_CATEGORY(lcModels)
Q_DECLARE_LOGGING_CATEGORY(lcInference)
Q_DECLARE_LOGGING_CATEGORY(lcDatabase)
Q_DECLARE_LOGGING_CATEGORY(lcUI)

struct LogEntry {
    QDateTime timestamp;
    QtMsgType level;          // QtDebugMsg, QtInfoMsg, QtWarningMsg, QtCriticalMsg
    QString   category;
    QString   message;
    QString   file;
    int       line = 0;
};

// Global logger that captures Qt log messages and re-emits them.
class SentinelLogger : public QObject {
    Q_OBJECT
public:
    static SentinelLogger& instance();

    // Install as Qt message handler — call once at startup.
    void install();
    void uninstall();

    // Keep last N messages in ring buffer for the console widget.
    void setMaxEntries(int n);
    QVector<LogEntry> recent(int n = 200) const;
    int  count() const;

    // Filter helpers for the DebugConsoleWidget
    QVector<LogEntry> filterByLevel(QtMsgType minLevel) const;
    QVector<LogEntry> filterByCategory(const QString& category) const;

    void clear();

signals:
    void newEntry(const LogEntry& entry);

private:
    SentinelLogger();
    static void messageHandler(QtMsgType type,
                                const QMessageLogContext& ctx,
                                const QString& msg);

    QVector<LogEntry> m_entries;
    int               m_maxEntries = 2000;
    static QtMessageHandler s_prevHandler;
};
