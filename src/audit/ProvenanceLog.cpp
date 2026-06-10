#include "audit/ProvenanceLog.h"
#include <algorithm>

void ProvenanceLog::record(const QString& eventId,
                            const QString& stage,
                            const QString& action,
                            const QString& detail,
                            const QString& dataHash)
{
    ProvenanceEntry entry;
    entry.timestamp = QDateTime::currentDateTimeUtc();
    entry.eventId   = eventId;
    entry.stage     = stage;
    entry.action    = action;
    entry.detail    = detail;
    entry.dataHash  = dataHash;
    m_entries.append(entry);
}

QVector<ProvenanceEntry> ProvenanceLog::chain(const QString& eventId) const
{
    QVector<ProvenanceEntry> result;
    for (const auto& e : m_entries) {
        if (e.eventId == eventId)
            result.append(e);
    }
    // Sort ascending by timestamp so the chain reads chronologically
    std::sort(result.begin(), result.end(),
              [](const ProvenanceEntry& a, const ProvenanceEntry& b) {
                  return a.timestamp < b.timestamp;
              });
    return result;
}

QVector<ProvenanceEntry> ProvenanceLog::recent(int n) const
{
    if (m_entries.isEmpty())
        return {};

    const int total = m_entries.size();
    const int start = qMax(0, total - n);
    QVector<ProvenanceEntry> result;
    result.reserve(total - start);

    // Copy last n entries, then reverse so newest is first
    for (int i = total - 1; i >= start; --i)
        result.append(m_entries[i]);

    return result;
}

QVector<ProvenanceEntry> ProvenanceLog::filterByStage(const QString& stage) const
{
    QVector<ProvenanceEntry> result;
    for (const auto& e : m_entries) {
        if (e.stage == stage)
            result.append(e);
    }
    return result;
}

void ProvenanceLog::clear()
{
    m_entries.clear();
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
