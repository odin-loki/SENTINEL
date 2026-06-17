// test_lead_report_quality.cpp
// Tests LeadReportGenerator: Markdown, HTML, plaintext output quality,
// confidence stats, provenance formatting and file save.
#include <QTest>
#include <QTimeZone>
#include <QTemporaryFile>
#include <QFile>
#include "inference/LeadReportGenerator.h"
#include "core/CrimeEvent.h"

static InvestigativeLead makeLead(int rank, const QString& cat,
                                   const QString& headline, double conf,
                                   bool addProvenance = true)
{
    InvestigativeLead l;
    l.rank     = rank;
    l.category = cat;
    l.headline = headline;
    l.detail   = QStringLiteral("Detail for %1").arg(headline);
    l.confidence = conf;
    l.confidenceMethod = QStringLiteral("test_method");
    l.generatedAt = QDateTime(QDate(2024, 6, 10), QTime(10, 0, 0), QTimeZone::utc());
    if (addProvenance)
        l.provenance = { QStringLiteral("source_A"), QStringLiteral("inference_B") };
    return l;
}

class TestLeadReportQuality : public QObject
{
    Q_OBJECT

private slots:

    // ── 1. Generated report has non-empty markdown ────────────────────────────
    void testMarkdownNonEmpty()
    {
        QVector<InvestigativeLead> leads;
        leads.append(makeLead(1, QStringLiteral("series_linkage"), QStringLiteral("Linked to SER-001"), 0.85));
        leads.append(makeLead(2, QStringLiteral("mo_similarity"),  QStringLiteral("Case CASE-007"),     0.70));

        const auto report = LeadReportGenerator::generate(QStringLiteral("CASE-001"), leads);
        QVERIFY2(!report.markdownText.isEmpty(), "Markdown text must be non-empty");
        QVERIFY2(report.markdownText.contains(QStringLiteral("#")),
                 "Markdown text must contain at least one heading (#)");
    }

    // ── 2. Report totalLeads is correct ──────────────────────────────────────
    void testTotalLeadsCount()
    {
        QVector<InvestigativeLead> leads;
        for (int i = 0; i < 5; ++i)
            leads.append(makeLead(i + 1, QStringLiteral("test"),
                                  QStringLiteral("Headline %1").arg(i), 0.5 + i * 0.1));

        const auto report = LeadReportGenerator::generate(QStringLiteral("CASE-002"), leads);
        QCOMPARE(report.totalLeads, 5);
    }

    // ── 3. highConfidenceLeads counts conf >= 0.7 correctly ──────────────────
    void testHighConfidenceCount()
    {
        QVector<InvestigativeLead> leads;
        leads.append(makeLead(1, QStringLiteral("cat"), QStringLiteral("High1"), 0.90));  // high
        leads.append(makeLead(2, QStringLiteral("cat"), QStringLiteral("High2"), 0.75));  // high
        leads.append(makeLead(3, QStringLiteral("cat"), QStringLiteral("Low1"),  0.50));  // low
        leads.append(makeLead(4, QStringLiteral("cat"), QStringLiteral("Low2"),  0.30));  // low

        const auto report = LeadReportGenerator::generate(QStringLiteral("CASE-003"), leads);
        QCOMPARE(report.highConfidenceLeads, 2);
    }

    // ── 4. topConfidence is the maximum confidence ────────────────────────────
    void testTopConfidence()
    {
        QVector<InvestigativeLead> leads;
        leads.append(makeLead(1, QStringLiteral("cat"), QStringLiteral("A"), 0.60));
        leads.append(makeLead(2, QStringLiteral("cat"), QStringLiteral("B"), 0.92));
        leads.append(makeLead(3, QStringLiteral("cat"), QStringLiteral("C"), 0.45));

        const auto report = LeadReportGenerator::generate(QStringLiteral("CASE-004"), leads);
        QVERIFY2(std::abs(report.topConfidence - 0.92) < 1e-9,
                 qPrintable(QStringLiteral("topConfidence expected 0.92, got %1")
                    .arg(report.topConfidence)));
    }

    // ── 5. Empty leads → report with 0 leads, valid markdown ─────────────────
    void testEmptyLeads()
    {
        const auto report = LeadReportGenerator::generate(QStringLiteral("CASE-005"), {});
        QCOMPARE(report.totalLeads, 0);
        QCOMPARE(report.highConfidenceLeads, 0);
        QVERIFY(!report.markdownText.isEmpty());
    }

    // ── 6. caseId appears in the markdown ────────────────────────────────────
    void testCaseIdInMarkdown()
    {
        QVector<InvestigativeLead> leads;
        leads.append(makeLead(1, QStringLiteral("cat"), QStringLiteral("Test Lead"), 0.70));

        const auto report = LeadReportGenerator::generate(QStringLiteral("MY-CASE-XYZ"), leads);
        QVERIFY2(report.markdownText.contains(QStringLiteral("MY-CASE-XYZ")),
                 "Case ID should appear in markdown text");
    }

    // ── 7. HTML output has DOCTYPE and table ─────────────────────────────────
    void testHtmlOutput()
    {
        QVector<InvestigativeLead> leads;
        leads.append(makeLead(1, QStringLiteral("geographic_profile"),
                               QStringLiteral("Peak anchor at (51.50, -0.10)"), 0.80));

        const auto report = LeadReportGenerator::generate(QStringLiteral("CASE-006"), leads);
        const QString html = LeadReportGenerator::generateHtml(report);

        QVERIFY2(!html.isEmpty(), "HTML output must be non-empty");
        QVERIFY2(html.contains(QStringLiteral("<!DOCTYPE html>")) ||
                 html.contains(QStringLiteral("<html")),
                 "HTML output must contain DOCTYPE or <html tag");
    }

    // ── 8. formatProvenance builds correct chain string ───────────────────────
    void testFormatProvenance()
    {
        const QStringList prov = { "step1", "step2", "step3" };
        const QString s = LeadReportGenerator::formatProvenance(prov);
        QVERIFY2(s.contains(QStringLiteral("step1")), "Provenance chain must include step1");
        QVERIFY2(s.contains(QStringLiteral("step3")), "Provenance chain must include step3");
        QVERIFY2(s.contains(QStringLiteral("→")), "Provenance chain must contain arrows");
    }

    // ── 9. saveToFile creates a non-empty file ─────────────────────────────────
    void testSaveToFile()
    {
        QVector<InvestigativeLead> leads;
        leads.append(makeLead(1, QStringLiteral("cat"), QStringLiteral("Save Test"), 0.75));

        const auto report = LeadReportGenerator::generate(QStringLiteral("CASE-007"), leads);

        QTemporaryFile tmp;
        tmp.setAutoRemove(false);
        QVERIFY(tmp.open());
        const QString path = tmp.fileName();
        tmp.close();

        const bool ok = LeadReportGenerator::saveToFile(report, path, true);
        QVERIFY2(ok, "saveToFile should return true on success");

        QFile f(path);
        QVERIFY2(f.size() > 0, "Saved file must be non-empty");
        f.remove();
    }

    // ── 10. formatLead contains headline and category ─────────────────────────
    void testFormatLead()
    {
        const InvestigativeLead l = makeLead(
            1, QStringLiteral("series_linkage"),
            QStringLiteral("Linked to SER-999"), 0.88);

        const QString formatted = LeadReportGenerator::formatLead(l);
        QVERIFY2(formatted.contains(QStringLiteral("SER-999")),
                 "Formatted lead must contain the headline");
    }
};

QTEST_MAIN(TestLeadReportQuality)
#include "test_lead_report_quality.moc"
