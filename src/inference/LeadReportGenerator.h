#pragma once
#include <QVector>
#include <QString>
#include <QDateTime>
#include <QStringList>
#include "core/CrimeEvent.h"

struct LeadReport {
    QString  caseId;
    QDateTime generatedAt;
    QString  markdownText;            // full formatted report in Markdown
    QString  plainText;               // plain text version
    int      totalLeads          = 0;
    int      highConfidenceLeads = 0; // confidence >= 0.7
    double   topConfidence       = 0.0;
    QVector<InvestigativeLead> leads; // individual leads, used by HTML export
};

class LeadReportGenerator {
public:
    // Generate a full lead report for a case
    static LeadReport generate(const QString& caseId,
                                const QVector<InvestigativeLead>& leads);

    // Save report to file (returns false on error)
    static bool saveToFile(const LeadReport& report,
                           const QString& filePath,
                           bool useMarkdown = true);

    // Format a single lead as Markdown
    static QString formatLead(const InvestigativeLead& lead);

    // Build a provenance chain string ("step1 → step2 → step3")
    static QString formatProvenance(const QStringList& provenance);

    // Generate a styled dark-theme HTML document for the report
    static QString generateHtml(const LeadReport& report);
};
