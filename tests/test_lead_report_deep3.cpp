// Deep audit iteration 12 — LeadReportGenerator (deep3)
// Verifies: ranks assigned before report.leads stored, HTML escaping,
//           sort by confidence descending.

#include <QtTest>
#include <QDir>
#include <QFile>
#include "inference/LeadReportGenerator.h"

class LeadReportDeep3Test : public QObject
{
    Q_OBJECT

    static InvestigativeLead makeLead(double confidence,
                                      const QString& headline,
                                      const QString& category = QStringLiteral("network"))
    {
        InvestigativeLead l;
        l.headline         = headline;
        l.category         = category;
        l.confidence       = confidence;
        l.confidenceMethod = QStringLiteral("ensemble");
        l.detail           = QStringLiteral("Supporting detail");
        return l;
    }

private slots:

    // ── Ranks assigned BEFORE report.leads stored (regression for rank=0 bug) ─

    void testRanksAssignedBeforeLeadsStored()
    {
        QVector<InvestigativeLead> input;
        input << makeLead(0.85, QStringLiteral("High"))
              << makeLead(0.55, QStringLiteral("Mid"))
              << makeLead(0.25, QStringLiteral("Low"));

        // Input leads have rank=0 (default).
        for (const auto& l : input)
            QCOMPARE(l.rank, 0);

        const LeadReport report = LeadReportGenerator::generate(
            QStringLiteral("RANK-ORDER"), input);

        QCOMPARE(report.leads.size(), 3);

        // Stored leads must have sequential ranks starting at 1.
        for (int i = 0; i < report.leads.size(); ++i) {
            QVERIFY2(report.leads[i].rank >= 1,
                     qPrintable(QStringLiteral("Lead %1 rank must be >= 1, got %2")
                         .arg(i).arg(report.leads[i].rank)));
            QCOMPARE(report.leads[i].rank, i + 1);
        }

        // HTML export reads report.leads — must not contain rank 0.
        const QString html = LeadReportGenerator::generateHtml(report);
        QVERIFY2(!html.contains(QStringLiteral("<td>0</td>")),
                 "HTML must never show rank 0 after fix");
        QVERIFY(html.contains(QStringLiteral("<td>1</td>")));
        QVERIFY(html.contains(QStringLiteral("<td>2</td>")));
        QVERIFY(html.contains(QStringLiteral("<td>3</td>")));
    }

    void testMarkdownUsesAssignedRanks()
    {
        QVector<InvestigativeLead> input;
        input << makeLead(0.9, QStringLiteral("Alpha"))
              << makeLead(0.6, QStringLiteral("Beta"));

        const LeadReport report = LeadReportGenerator::generate(
            QStringLiteral("MD-RANK"), input);

        QCOMPARE(report.leads[0].rank, 1);
        QCOMPARE(report.leads[1].rank, 2);
        QVERIFY(report.markdownText.contains(QStringLiteral("### #1")));
        QVERIFY(report.markdownText.contains(QStringLiteral("### #2")));
        QVERIFY(!report.markdownText.contains(QStringLiteral("### #0")));
    }

    // ── Sort by confidence descending ───────────────────────────────────────

    void testLeadsSortedByConfidenceDescending()
    {
        QVector<InvestigativeLead> input;
        input << makeLead(0.4, QStringLiteral("D"))
              << makeLead(0.95, QStringLiteral("A"))
              << makeLead(0.7, QStringLiteral("B"))
              << makeLead(0.55, QStringLiteral("C"));

        const LeadReport report = LeadReportGenerator::generate(
            QStringLiteral("SORT-TEST"), input);

        for (int i = 1; i < report.leads.size(); ++i) {
            QVERIFY2(report.leads[i - 1].confidence >= report.leads[i].confidence,
                     qPrintable(QStringLiteral("Lead %1 conf %2 should >= lead %3 conf %4")
                         .arg(i - 1).arg(report.leads[i - 1].confidence)
                         .arg(i).arg(report.leads[i].confidence)));
        }

        QCOMPARE(report.leads[0].headline, QStringLiteral("A"));
        QCOMPARE(report.leads[1].headline, QStringLiteral("B"));
        QCOMPARE(report.leads[2].headline, QStringLiteral("C"));
        QCOMPARE(report.leads[3].headline, QStringLiteral("D"));
        QVERIFY(qFuzzyCompare(report.topConfidence, 0.95));
    }

    void testHtmlTableFollowsConfidenceOrder()
    {
        QVector<InvestigativeLead> input;
        input << makeLead(0.3, QStringLiteral("Third"))
              << makeLead(0.9, QStringLiteral("First"))
              << makeLead(0.6, QStringLiteral("Second"));

        const LeadReport report = LeadReportGenerator::generate(
            QStringLiteral("HTML-SORT"), input);
        const QString html = LeadReportGenerator::generateHtml(report);

        const int posFirst  = html.indexOf(QStringLiteral("First"));
        const int posSecond = html.indexOf(QStringLiteral("Second"));
        const int posThird  = html.indexOf(QStringLiteral("Third"));

        QVERIFY(posFirst >= 0 && posSecond > posFirst && posThird > posSecond);
    }

    // ── HTML escaping ───────────────────────────────────────────────────────

    void testHtmlEscapesCaseIdAndLeadFields()
    {
        QVector<InvestigativeLead> input;
        InvestigativeLead l;
        l.headline         = QStringLiteral("<img onerror=alert(1)>");
        l.category         = QStringLiteral("geo & <script>");
        l.confidence       = 0.75;
        l.confidenceMethod = QStringLiteral("bayes <test>");
        l.detail           = QStringLiteral("Detail \"quoted\" & <tag>");
        l.provenance       = { QStringLiteral("src<script>"), QStringLiteral("rule&2") };
        input << l;

        const LeadReport report = LeadReportGenerator::generate(
            QStringLiteral("CASE<>&\"XSS"), input);
        const QString html = LeadReportGenerator::generateHtml(report);

        // Raw dangerous markup must not appear unescaped.
        QVERIFY2(!html.contains(QStringLiteral("<script>")),
                 "Unescaped <script> must not appear");
        QVERIFY2(!html.contains(QStringLiteral("<img onerror")),
                 "Unescaped attribute injection must not appear");

        // Escaped forms must appear.
        QVERIFY(html.contains(QStringLiteral("&lt;script&gt;")));
        QVERIFY(html.contains(QStringLiteral("&amp;")));
        QVERIFY(html.contains(report.caseId.toHtmlEscaped()));
    }

    void testHtmlEscapesProvenanceChain()
    {
        QVector<InvestigativeLead> input;
        InvestigativeLead l = makeLead(0.8, QStringLiteral("Prov test"));
        l.provenance = { QStringLiteral("step<1>"), QStringLiteral("step&2") };
        input << l;

        const LeadReport report = LeadReportGenerator::generate(
            QStringLiteral("PROV-ESC"), input);
        const QString html = LeadReportGenerator::generateHtml(report);

        QVERIFY(html.contains(QStringLiteral("&lt;1&gt;")));
        QVERIFY(html.contains(QStringLiteral("&amp;")));
        QVERIFY2(!html.contains(QStringLiteral("step<1>")),
                 "Provenance must be HTML-escaped");
    }

    void testFormatLeadDoesNotEscapeMarkdown()
    {
        // formatLead is Markdown — should preserve raw text (HTML export handles escaping).
        InvestigativeLead l = makeLead(0.5, QStringLiteral("**bold**"));
        l.category = QStringLiteral("<cat>");
        const QString md = LeadReportGenerator::formatLead(l);
        QVERIFY(md.contains(QStringLiteral("**bold**")));
        QVERIFY(md.contains(QStringLiteral("<cat>")));
    }
};

QTEST_GUILESS_MAIN(LeadReportDeep3Test)
#include "test_lead_report_deep3.moc"
