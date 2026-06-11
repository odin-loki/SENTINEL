#include "inference/LeadReportGenerator.h"
#include <QFile>
#include <QTextStream>
#include <QStringConverter>
#include <algorithm>
#include <cmath>

LeadReport LeadReportGenerator::generate(const QString& caseId,
                                          const QVector<InvestigativeLead>& leads)
{
    LeadReport report;
    report.caseId       = caseId;
    report.generatedAt  = QDateTime::currentDateTimeUtc();
    report.totalLeads   = leads.size();

    // Sort a local copy by confidence descending
    QVector<InvestigativeLead> sorted = leads;
    std::sort(sorted.begin(), sorted.end(),
              [](const InvestigativeLead& a, const InvestigativeLead& b) {
                  return a.confidence > b.confidence;
              });

    // Count high-confidence leads and find top confidence
    for (const auto& lead : sorted) {
        if (lead.confidence >= 0.7)
            ++report.highConfidenceLeads;
    }
    report.topConfidence = sorted.isEmpty() ? 0.0 : sorted.first().confidence;

    // Assign ranks and store for both Markdown and HTML export
    int displayRank = 1;
    for (InvestigativeLead& lead : sorted)
        lead.rank = displayRank++;
    report.leads = sorted;

    // Build Markdown
    QString md;
    md.reserve(2048);
    md += QStringLiteral("## SENTINEL Investigative Leads Report\n\n");
    md += QStringLiteral("**Case ID:** %1\n\n").arg(caseId);
    md += QStringLiteral("**Generated:** %1\n\n")
              .arg(report.generatedAt.toString(Qt::ISODate));
    md += QStringLiteral("**Total Leads:** %1 (%2 high confidence)\n\n")
              .arg(report.totalLeads)
              .arg(report.highConfidenceLeads);
    md += QStringLiteral("---\n\n");

    for (const InvestigativeLead& lead : sorted)
        md += formatLead(lead);

    report.markdownText = md;

    // Build plain text: strip common Markdown markers
    QString pt = md;
    pt.replace(QStringLiteral("## "), QString{});
    pt.replace(QStringLiteral("### "), QString{});
    pt.replace(QStringLiteral("**"), QString{});
    pt.replace(QStringLiteral("---"), QString{});
    report.plainText = pt;

    return report;
}

bool LeadReportGenerator::saveToFile(const LeadReport& report,
                                      const QString& filePath,
                                      bool useMarkdown)
{
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;

    QTextStream out(&file);
    out.setEncoding(QStringConverter::Utf8);
    out << (useMarkdown ? report.markdownText : report.plainText);
    out.flush();
    return true;
}

QString LeadReportGenerator::formatLead(const InvestigativeLead& lead)
{
    QString s;
    s += QStringLiteral("### #%1 — %2\n\n").arg(lead.rank).arg(lead.category);
    s += QStringLiteral("**%1**\n\n").arg(lead.headline);
    s += QStringLiteral("**Confidence:** %1%\n\n")
             .arg(static_cast<int>(std::round(lead.confidence * 100.0)));
    if (!lead.detail.isEmpty())
        s += QStringLiteral("%1\n\n").arg(lead.detail);
    if (!lead.provenance.empty()) {
        QStringList provList;
        for (const auto& p : lead.provenance)
            provList.append(p);
        s += QStringLiteral("*Provenance: %1*\n\n").arg(formatProvenance(provList));
    }
    return s;
}

QString LeadReportGenerator::formatProvenance(const QStringList& provenance)
{
    return provenance.join(QStringLiteral(" \u2192 "));
}

// ---------------------------------------------------------------------------
// generateHtml() — dark-themed HTML export
// ---------------------------------------------------------------------------
QString LeadReportGenerator::generateHtml(const LeadReport& report)
{
    QString html;
    html.reserve(8192);
    QTextStream out(&html);

    const QString ts = report.generatedAt.isValid()
        ? report.generatedAt.toString(Qt::ISODate)
        : QDateTime::currentDateTimeUtc().toString(Qt::ISODate);

    // ── Document head ────────────────────────────────────────────────────────
    out << "<!DOCTYPE html>\n"
           "<html lang=\"en\">\n"
           "<head>\n"
           "<meta charset=\"UTF-8\">\n"
           "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">\n"
           "<title>SENTINEL Report \u2014 " << report.caseId.toHtmlEscaped() << "</title>\n"
           "<style>\n"
           "  :root { --bg:#0d1117; --surface:#161b22; --accent:#e94560; "
           "          --text:#c9d1d9; --muted:#8b949e; --border:#30363d; }\n"
           "  * { box-sizing:border-box; margin:0; padding:0; }\n"
           "  body { background:var(--bg); color:var(--text); "
           "         font-family:'Segoe UI',system-ui,sans-serif; padding:2rem; }\n"
           "  header { border-bottom:2px solid var(--accent); padding-bottom:1rem; margin-bottom:2rem; }\n"
           "  .logo { font-size:2rem; font-weight:700; color:var(--accent); letter-spacing:0.15em; }\n"
           "  .meta { color:var(--muted); font-size:0.85rem; margin-top:0.4rem; }\n"
           "  h2 { color:var(--accent); margin:1.5rem 0 0.75rem; font-size:1.1rem; text-transform:uppercase; }\n"
           "  table { width:100%; border-collapse:collapse; margin-bottom:2rem; }\n"
           "  th { background:var(--surface); color:var(--muted); text-align:left; "
           "       padding:0.6rem 0.8rem; border-bottom:1px solid var(--border); font-size:0.8rem; }\n"
           "  td { padding:0.6rem 0.8rem; border-bottom:1px solid var(--border); vertical-align:top; }\n"
           "  tr:hover td { background:var(--surface); }\n"
           "  .badge { display:inline-block; padding:0.15rem 0.5rem; border-radius:4px; "
           "           font-size:0.7rem; font-weight:700; letter-spacing:0.05em; }\n"
           "  .badge-high { background:var(--accent); color:#fff; }\n"
           "  .badge-med  { background:#d29922; color:#000; }\n"
           "  .badge-low  { background:#3a3a3a; color:var(--muted); }\n"
           "  .conf-bar-wrap { background:var(--border); border-radius:4px; height:6px; width:80px; overflow:hidden; }\n"
           "  .conf-bar { background:var(--accent); border-radius:4px; height:6px; }\n"
           "  .detail-card { background:var(--surface); border:1px solid var(--border); "
           "                 border-radius:6px; padding:1rem; margin-bottom:1rem; }\n"
           "  .detail-headline { font-weight:600; margin-bottom:0.4rem; }\n"
           "  .detail-text { color:var(--muted); font-size:0.9rem; }\n"
           "  .prov-chain { font-family:monospace; font-size:0.8rem; color:var(--muted); }\n"
           "  footer { margin-top:3rem; font-size:0.75rem; color:var(--muted); "
           "           border-top:1px solid var(--border); padding-top:0.75rem; }\n"
           "</style>\n"
           "</head>\n"
           "<body>\n";

    // ── Header ───────────────────────────────────────────────────────────────
    out << "<header>\n"
           "  <div class=\"logo\">&#x25A6; SENTINEL</div>\n"
           "  <div class=\"meta\">Investigative Leads Report</div>\n"
           "  <div class=\"meta\">Case ID: <strong>" << report.caseId.toHtmlEscaped() << "</strong>"
           " &nbsp;|&nbsp; Generated: " << ts << "</div>\n"
           "  <div class=\"meta\">"
        << report.totalLeads << " leads &nbsp;|&nbsp; "
        << report.highConfidenceLeads << " high confidence"
           "</div>\n"
           "</header>\n";

    // ── Summary table ─────────────────────────────────────────────────────────
    out << "<h2>Lead Summary</h2>\n"
           "<table>\n"
           "  <thead><tr>"
           "<th>#</th><th>Category</th><th>Headline</th>"
           "<th>Confidence</th><th>Method</th>"
           "</tr></thead>\n"
           "  <tbody>\n";

    for (const InvestigativeLead& lead : report.leads) {
        const int pct = static_cast<int>(std::round(lead.confidence * 100.0));
        const bool isHigh = lead.confidence >= 0.7;
        const bool isMed  = lead.confidence >= 0.5 && !isHigh;

        QString badgeClass = isHigh ? QStringLiteral("badge-high")
                           : (isMed ? QStringLiteral("badge-med") : QStringLiteral("badge-low"));
        QString badgeLabel = isHigh ? QStringLiteral("HIGH CONFIDENCE")
                           : (isMed ? QStringLiteral("MED")        : QStringLiteral("LOW"));

        out << "  <tr>"
               "<td>" << lead.rank << "</td>"
               "<td>" << lead.category.toHtmlEscaped() << "</td>"
               "<td>" << lead.headline.toHtmlEscaped()
            << " <span class=\"badge " << badgeClass << "\">" << badgeLabel << "</span></td>"
               "<td>"
               "<div class=\"conf-bar-wrap\">"
               "<div class=\"conf-bar\" style=\"width:" << pct << "%\"></div>"
               "</div>"
               "<span style=\"font-size:0.8rem;margin-left:4px\">" << pct << "%</span>"
               "</td>"
               "<td style=\"font-size:0.8rem;color:var(--muted)\">"
            << lead.confidenceMethod.toHtmlEscaped() << "</td>"
               "</tr>\n";
    }

    out << "  </tbody>\n</table>\n";

    // ── Detail cards ──────────────────────────────────────────────────────────
    if (!report.leads.isEmpty()) {
        out << "<h2>Lead Details</h2>\n";
        for (const InvestigativeLead& lead : report.leads) {
            out << "<div class=\"detail-card\">\n"
                   "  <div class=\"detail-headline\">#" << lead.rank
                << " &mdash; " << lead.category.toHtmlEscaped()
                << " &mdash; " << lead.headline.toHtmlEscaped() << "</div>\n";
            if (!lead.detail.isEmpty())
                out << "  <div class=\"detail-text\">" << lead.detail.toHtmlEscaped() << "</div>\n";
            out << "</div>\n";
        }
    }

    // ── Provenance appendix ───────────────────────────────────────────────────
    bool hasProvenance = false;
    for (const InvestigativeLead& lead : report.leads) {
        if (!lead.provenance.empty()) { hasProvenance = true; break; }
    }

    if (hasProvenance) {
        out << "<h2>Provenance Chain</h2>\n";
        for (const InvestigativeLead& lead : report.leads) {
            if (lead.provenance.empty()) continue;
            QStringList provList;
            for (const auto& p : lead.provenance)
                provList.append(p);
            out << "<div class=\"detail-card\">\n"
                   "  <div class=\"detail-headline\">#" << lead.rank
                << " " << lead.headline.toHtmlEscaped() << "</div>\n"
                   "  <div class=\"prov-chain\">"
                << formatProvenance(provList).toHtmlEscaped()
                << "</div>\n"
                   "</div>\n";
        }
    }

    // ── Footer ────────────────────────────────────────────────────────────────
    out << "<footer>Generated by SENTINEL Analytics &mdash; "
        << ts << " &mdash; RESTRICTED</footer>\n"
           "</body>\n</html>\n";

    return html;
}
