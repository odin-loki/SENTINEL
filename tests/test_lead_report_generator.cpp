// test_lead_report_generator.cpp
// Tests for LeadReportGenerator: markdown/HTML generation, provenance
// formatting, single-lead format, and file saving.
#include <QTest>
#include <QTemporaryFile>
#include <QFile>
#include "inference/LeadReportGenerator.h"
#include "core/CrimeEvent.h"

static InvestigativeLead makeLead(int rank, const QString& category,
                                   double confidence, const QString& detail)
{
    InvestigativeLead l;
    l.rank       = rank;
    l.category   = category;
    l.confidence = confidence;
    l.headline   = QStringLiteral("Lead #%1").arg(rank);
    l.detail     = detail;
    l.provenance = { QStringLiteral("HintEngine"), QStringLiteral("MOAnalyser") };
    return l;
}

class LeadReportGeneratorTest : public QObject
{
    Q_OBJECT

private:
    QVector<InvestigativeLead> sampleLeads() const
    {
        return {
            makeLead(1, QStringLiteral("series"),  0.91, QStringLiteral("Strong series link")),
            makeLead(2, QStringLiteral("mo"),      0.76, QStringLiteral("Similar MO to case C42")),
            makeLead(3, QStringLiteral("network"), 0.55, QStringLiteral("Co-offending link")),
        };
    }

private slots:

    // 1. generate: returns non-empty markdownText
    void testMarkdownNonEmpty()
    {
        const auto rep = LeadReportGenerator::generate(QStringLiteral("CASE-001"), sampleLeads());
        QVERIFY2(!rep.markdownText.isEmpty(), "markdownText must be non-empty");
    }

    // 2. generate: totalLeads matches input count
    void testTotalLeadsCount()
    {
        const auto rep = LeadReportGenerator::generate(QStringLiteral("CASE-002"), sampleLeads());
        QCOMPARE(rep.totalLeads, 3);
    }

    // 3. generate: highConfidenceLeads count is correct (>= 0.7)
    void testHighConfidenceCount()
    {
        const auto rep = LeadReportGenerator::generate(QStringLiteral("CASE-003"), sampleLeads());
        QCOMPARE(rep.highConfidenceLeads, 2); // 0.91 and 0.76 >= 0.7
    }

    // 4. generate: topConfidence is max of input leads
    void testTopConfidence()
    {
        const auto rep = LeadReportGenerator::generate(QStringLiteral("CASE-004"), sampleLeads());
        QVERIFY2(std::abs(rep.topConfidence - 0.91) < 1e-6,
                 qPrintable(QStringLiteral("topConfidence %1 expected 0.91").arg(rep.topConfidence)));
    }

    // 5. generate: caseId in report
    void testCaseIdInReport()
    {
        const auto rep = LeadReportGenerator::generate(QStringLiteral("CASE-XYZ"), sampleLeads());
        QVERIFY2(rep.markdownText.contains(QStringLiteral("CASE-XYZ")),
                 "Report markdown should contain the caseId");
    }

    // 6. generateHtml: non-empty
    void testHtmlNonEmpty()
    {
        const auto rep  = LeadReportGenerator::generate(QStringLiteral("CASE-005"), sampleLeads());
        const auto html = LeadReportGenerator::generateHtml(rep);
        QVERIFY2(!html.isEmpty(), "HTML output must be non-empty");
    }

    // 7. generateHtml: contains <html> tag
    void testHtmlHasHtmlTag()
    {
        const auto rep  = LeadReportGenerator::generate(QStringLiteral("CASE-006"), sampleLeads());
        const auto html = LeadReportGenerator::generateHtml(rep);
        QVERIFY2(html.contains(QStringLiteral("<html"), Qt::CaseInsensitive),
                 "HTML output should contain <html> tag");
    }

    // 8. formatLead: non-empty for valid lead
    void testFormatLeadNonEmpty()
    {
        const auto formatted = LeadReportGenerator::formatLead(
            makeLead(1, QStringLiteral("series"), 0.85, QStringLiteral("Test summary")));
        QVERIFY2(!formatted.isEmpty(), "formatLead must return non-empty string");
    }

    // 9. formatProvenance: chain joined correctly
    void testFormatProvenance()
    {
        const QStringList chain = { QStringLiteral("A"), QStringLiteral("B"), QStringLiteral("C") };
        const QString result = LeadReportGenerator::formatProvenance(chain);
        QVERIFY2(result.contains(QStringLiteral("A")), "Provenance must contain first step");
        QVERIFY2(result.contains(QStringLiteral("C")), "Provenance must contain last step");
    }

    // 10. saveToFile: saves to temp file successfully
    void testSaveToFile()
    {
        const auto rep = LeadReportGenerator::generate(QStringLiteral("CASE-007"), sampleLeads());
        QTemporaryFile tmp;
        QVERIFY(tmp.open());
        const QString path = tmp.fileName();
        tmp.close();

        const bool ok = LeadReportGenerator::saveToFile(rep, path, true);
        QVERIFY2(ok, "saveToFile should return true on success");

        QFile check(path);
        QVERIFY2(check.exists() && check.size() > 0,
                 "Saved file must exist and be non-empty");
    }
};

QTEST_MAIN(LeadReportGeneratorTest)
#include "test_lead_report_generator.moc"
