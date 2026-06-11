#include "audit/ProvenanceLog.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutexLocker>
#include <algorithm>

void ProvenanceLog::record(const QString& eventId,
                            const QString& stage,
                            const QString& action,
                            const QString& detail,
                            const QString& dataHash)
{
    QMutexLocker lock(&m_mutex);
    ProvenanceEntry entry;
    entry.timestamp = QDateTime::currentDateTimeUtc();
    entry.eventId   = eventId;
    entry.stage     = stage;
    entry.action    = action;
    entry.detail    = detail;
    entry.dataHash  = dataHash;
    m_entries.append(entry);
}

void ProvenanceLog::addEntry(const QString& source,
                              const QString& model,
                              const QString& action,
                              const QString& detail,
                              const QDateTime& timestamp)
{
    QMutexLocker lock(&m_mutex);
    ProvenanceEntry entry;
    entry.timestamp = timestamp.isValid() ? timestamp : QDateTime::currentDateTimeUtc();
    entry.source    = source;
    entry.model     = model;
    entry.action    = action;
    entry.detail    = detail;
    m_entries.append(entry);
}

int ProvenanceLog::count() const
{
    QMutexLocker lock(&m_mutex);
    return m_entries.size();
}

int ProvenanceLog::size() const
{
    return count();
}

QVector<ProvenanceEntry> ProvenanceLog::getEntries() const
{
    QMutexLocker lock(&m_mutex);
    QVector<ProvenanceEntry> result;
    result.reserve(m_entries.size());
    for (int i = m_entries.size() - 1; i >= 0; --i)
        result.append(m_entries[i]);
    return result;
}

QVector<ProvenanceEntry> ProvenanceLog::chain(const QString& eventId) const
{
    QMutexLocker lock(&m_mutex);
    QVector<ProvenanceEntry> result;
    for (const auto& e : m_entries) {
        if (e.eventId == eventId)
            result.append(e);
    }
    std::sort(result.begin(), result.end(),
              [](const ProvenanceEntry& a, const ProvenanceEntry& b) {
                  return a.timestamp < b.timestamp;
              });
    return result;
}

QVector<ProvenanceEntry> ProvenanceLog::recent(int n) const
{
    QMutexLocker lock(&m_mutex);
    if (m_entries.isEmpty())
        return {};

    const int total = m_entries.size();
    const int start = qMax(0, total - n);
    QVector<ProvenanceEntry> result;
    result.reserve(total - start);

    for (int i = total - 1; i >= start; --i)
        result.append(m_entries[i]);

    return result;
}

QVector<ProvenanceEntry> ProvenanceLog::filterByStage(const QString& stage) const
{
    QMutexLocker lock(&m_mutex);
    QVector<ProvenanceEntry> result;
    for (const auto& e : m_entries) {
        if (e.stage == stage)
            result.append(e);
    }
    return result;
}

QVector<ProvenanceEntry> ProvenanceLog::filterByModel(const QString& model) const
{
    QMutexLocker lock(&m_mutex);
    QVector<ProvenanceEntry> result;
    for (const auto& e : m_entries) {
        if (e.model == model)
            result.append(e);
    }
    return result;
}

QVector<ProvenanceEntry> ProvenanceLog::filterBySource(const QString& source) const
{
    QMutexLocker lock(&m_mutex);
    QVector<ProvenanceEntry> result;
    for (const auto& e : m_entries) {
        if (e.source == source)
            result.append(e);
    }
    return result;
}

QVector<ProvenanceEntry> ProvenanceLog::filterByTimeRange(const QDateTime& from,
                                                           const QDateTime& to) const
{
    QMutexLocker lock(&m_mutex);
    QVector<ProvenanceEntry> result;
    for (const auto& e : m_entries) {
        if (e.timestamp >= from && e.timestamp <= to)
            result.append(e);
    }
    return result;
}

void ProvenanceLog::clear()
{
    QMutexLocker lock(&m_mutex);
    m_entries.clear();
}

QString ProvenanceLog::csvEscape(const QString& field)
{
    const bool needsQuotes = field.contains(QLatin1Char(','))
                          || field.contains(QLatin1Char('"'))
                          || field.contains(QLatin1Char('\n'))
                          || field.contains(QLatin1Char('\r'));
    if (!needsQuotes)
        return field;

    QString escaped = field;
    escaped.replace(QLatin1Char('"'), QStringLiteral("\"\""));
    return QLatin1Char('"') + escaped + QLatin1Char('"');
}

QString ProvenanceLog::exportToJson() const
{
    QMutexLocker lock(&m_mutex);
    QJsonArray arr;
    for (const auto& e : m_entries) {
        QJsonObject obj;
        obj.insert(QStringLiteral("timestamp"), e.timestamp.toString(Qt::ISODate));
        if (!e.eventId.isEmpty())
            obj.insert(QStringLiteral("eventId"), e.eventId);
        if (!e.stage.isEmpty())
            obj.insert(QStringLiteral("stage"), e.stage);
        if (!e.source.isEmpty())
            obj.insert(QStringLiteral("source"), e.source);
        if (!e.model.isEmpty())
            obj.insert(QStringLiteral("model"), e.model);
        obj.insert(QStringLiteral("action"), e.action);
        obj.insert(QStringLiteral("detail"), e.detail);
        if (!e.dataHash.isEmpty())
            obj.insert(QStringLiteral("dataHash"), e.dataHash);
        arr.append(obj);
    }
    return QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact));
}

QString ProvenanceLog::exportToCsv() const
{
    QMutexLocker lock(&m_mutex);
    QString out = QStringLiteral("timestamp,eventId,stage,source,model,action,detail,dataHash\n");
    for (const auto& e : m_entries) {
        out += csvEscape(e.timestamp.toString(Qt::ISODate))
            + QLatin1Char(',')
            + csvEscape(e.eventId)
            + QLatin1Char(',')
            + csvEscape(e.stage)
            + QLatin1Char(',')
            + csvEscape(e.source)
            + QLatin1Char(',')
            + csvEscape(e.model)
            + QLatin1Char(',')
            + csvEscape(e.action)
            + QLatin1Char(',')
            + csvEscape(e.detail)
            + QLatin1Char(',')
            + csvEscape(e.dataHash)
            + QLatin1Char('\n');
    }
    return out;
}

QString ProvenanceLog::formatHtml(const QString& eventId) const
{
    const QVector<ProvenanceEntry> entries = chain(eventId);
    if (entries.isEmpty())
        return {};

    QString html;
    html.reserve(entries.size() * 300 + 800);

    html += QStringLiteral(
        "<!DOCTYPE html>"
        "<html><head><meta charset=\"utf-8\"><style>"
        "body{background:#0d1117;color:#c9d1d9;font-family:sans-serif;margin:16px}"
        "h2{color:#e94560;margin-bottom:12px}"
        "table{border-collapse:collapse;width:100%}"
        "th{background:#161b22;color:#e94560;padding:8px 12px;text-align:left;"
        "border-bottom:2px solid #e94560}"
        "td{padding:8px 12px;border-bottom:1px solid #21262d;vertical-align:top}"
        "tr:hover td{background:#161b22}"
        ".hash{font-family:monospace;color:#8b949e;font-size:0.9em}"
        "</style></head><body>"
        "<h2>Provenance Chain &#8212; Event: ");
    html += eventId.toHtmlEscaped();
    html += QStringLiteral(
        "</h2>"
        "<table>"
        "<tr><th>Timestamp</th><th>Stage</th><th>Action</th>"
        "<th>Detail</th><th>Hash</th></tr>");

    for (const auto& e : entries) {
        html += QStringLiteral("<tr>");
        html += QStringLiteral("<td>") + e.timestamp.toString(Qt::ISODate).toHtmlEscaped() + QStringLiteral("</td>");
        html += QStringLiteral("<td>") + e.stage.toHtmlEscaped()  + QStringLiteral("</td>");
        html += QStringLiteral("<td>") + e.action.toHtmlEscaped() + QStringLiteral("</td>");
        html += QStringLiteral("<td>") + e.detail.toHtmlEscaped() + QStringLiteral("</td>");
        const QString hash = e.dataHash.isEmpty() ? QStringLiteral("&#8212;") : e.dataHash.toHtmlEscaped();
        html += QStringLiteral("<td class=\"hash\">") + hash + QStringLiteral("</td>");
        html += QStringLiteral("</tr>");
    }

    html += QStringLiteral("</table></body></html>");
    return html;
}

QString ProvenanceLog::formatChain(const QString& eventId) const
{
    const QVector<ProvenanceEntry> entries = chain(eventId);
    QString out;
    out.reserve(entries.size() * 120);

    for (const auto& e : entries) {
        out += QStringLiteral("[%1] [%2] [%3] %4: %5 (hash:%6)\n")
                   .arg(e.timestamp.toString(Qt::ISODate))
                   .arg(e.eventId)
                   .arg(e.stage)
                   .arg(e.action)
                   .arg(e.detail)
                   .arg(e.dataHash.isEmpty() ? QStringLiteral("-") : e.dataHash);
    }
    return out;
}
