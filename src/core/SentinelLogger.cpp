#include "core/SentinelLogger.h"
#include <QMutex>
#include <QMutexLocker>

Q_LOGGING_CATEGORY(lcIngest,    "sentinel.ingest")
Q_LOGGING_CATEGORY(lcNlp,       "sentinel.nlp")
Q_LOGGING_CATEGORY(lcModels,    "sentinel.models")
Q_LOGGING_CATEGORY(lcInference, "sentinel.inference")
Q_LOGGING_CATEGORY(lcDatabase,  "sentinel.database")
Q_LOGGING_CATEGORY(lcUI,        "sentinel.ui")

QtMessageHandler SentinelLogger::s_prevHandler = nullptr;
static bool     s_installed = false;
static QMutex   s_logMutex;

SentinelLogger::SentinelLogger() = default;

SentinelLogger& SentinelLogger::instance()
{
    static SentinelLogger inst;
    return inst;
}

void SentinelLogger::install()
{
    if (s_installed)
        return;
    s_prevHandler = qInstallMessageHandler(messageHandler);
    s_installed   = true;
}

void SentinelLogger::uninstall()
{
    if (!s_installed)
        return;
    qInstallMessageHandler(s_prevHandler);
    s_prevHandler = nullptr;
    s_installed   = false;
}

void SentinelLogger::setMaxEntries(int n)
{
    QMutexLocker lock(&s_logMutex);
    m_maxEntries = qMax(1, n);
    while (m_entries.size() > m_maxEntries)
        m_entries.removeFirst();
}

QVector<LogEntry> SentinelLogger::recent(int n) const
{
    QMutexLocker lock(&s_logMutex);
    if (n <= 0 || m_entries.isEmpty())
        return {};
    if (n >= m_entries.size())
        return m_entries;
    return m_entries.mid(m_entries.size() - n);
}

int SentinelLogger::count() const
{
    QMutexLocker lock(&s_logMutex);
    return m_entries.size();
}

namespace {

// QtMsgType enum order does not match severity; rank explicitly for filtering.
int msgSeverity(QtMsgType type)
{
    switch (type) {
    case QtDebugMsg:   return 0;
    case QtInfoMsg:    return 1;
    case QtWarningMsg: return 2;
    case QtCriticalMsg: return 3;
    case QtFatalMsg:   return 4;
    default:           return 0;
    }
}

} // namespace

QVector<LogEntry> SentinelLogger::filterByLevel(QtMsgType minLevel) const
{
    QMutexLocker lock(&s_logMutex);
    const int minRank = msgSeverity(minLevel);
    QVector<LogEntry> result;
    for (const auto& e : m_entries) {
        if (msgSeverity(e.level) >= minRank)
            result.append(e);
    }
    return result;
}

QVector<LogEntry> SentinelLogger::filterByCategory(const QString& category) const
{
    QMutexLocker lock(&s_logMutex);
    QVector<LogEntry> result;
    for (const auto& e : m_entries) {
        if (e.category.contains(category, Qt::CaseInsensitive))
            result.append(e);
    }
    return result;
}

void SentinelLogger::clear()
{
    QMutexLocker lock(&s_logMutex);
    m_entries.clear();
}

void SentinelLogger::messageHandler(QtMsgType type,
                                     const QMessageLogContext& ctx,
                                     const QString& msg)
{
    // Re-entrancy guard: prevents infinite recursion if signal slots log
    static thread_local bool s_inHandler = false;
    if (s_inHandler) {
        if (s_prevHandler)
            s_prevHandler(type, ctx, msg);
        return;
    }

    LogEntry entry;
    entry.timestamp = QDateTime::currentDateTimeUtc();
    entry.level     = type;
    entry.category  = ctx.category
                      ? QString::fromLatin1(ctx.category)
                      : QStringLiteral("default");
    entry.message   = msg;
    entry.file      = ctx.file ? QString::fromLatin1(ctx.file) : QString{};
    entry.line      = ctx.line;

    SentinelLogger& inst = instance();
    {
        QMutexLocker lock(&s_logMutex);
        inst.m_entries.append(entry);
        while (inst.m_entries.size() > inst.m_maxEntries)
            inst.m_entries.removeFirst();
    }

    s_inHandler = true;
    emit inst.newEntry(entry);
    s_inHandler = false;

    if (s_prevHandler)
        s_prevHandler(type, ctx, msg);
}
