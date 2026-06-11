// Deep audit iteration 14 — LeadReportGenerator (deep4)
// Verifies: rank assignment order, HTML escaping, confidence sort,
//           Markdown/HTML export consistency.

#include <QtTest>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QRegularExpression>
#include <QStringConverter>
#include "inference/LeadReportGenerator.h"

class LeadReportDeep4Test : public QObject
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

    void testRanksAssignedBeforeLeadsStored()
    {
        QVector<InvestigativeLead> input;
        input << makeLead(0.85, QStringLiteral("High"))
              << makeLead(0.55, QStringLiteral("Mid"))
              << makeLead(0.25, QStringLiteral("Low"));

        for (const auto& l : input)
            QCOMPARE(l.rank, 0);

        const LeadReport report = LeadReportGenerator::generate(
            QStringLiteral("RANK-ORDER"), input);

        for (int i = 0; i < report.leads.size(); ++i)
            QCOMPARE(report.leads[i].rank, i + 1);

        const QString html = LeadReportGenerator::generateHtml(report);
        QVERIFY(!html.contains(QStringLiteral("<td>0</td>")));
    }

    void testMarkdownUsesAssignedRanks()
    {
        const LeadReport report = LeadReportGenerator::generate(
            QStringLiteral("MD-RANK"),
            { makeLead(0.9, QStringLiteral("Alpha")),
              makeLead(0.6, QStringLiteral("Beta")) });

        QVERIFY(report.markdownText.contains(QStringLiteral("### #1")));
        QVERIFY(report.markdownText.contains(QStringLiteral("### #2")));
        QVERIFY(!report.markdownText.contains(QStringLiteral("### #0")));
    }

    void testLeadsSortedByConfidenceDescending()
    {
        const LeadReport report = LeadReportGenerator::generate(
            QStringLiteral("SORT-TEST"),
            { makeLead(0.4, QStringLiteral("D")),
              makeLead(0.95, QStringLiteral("A")),
              makeLead(0.7, QStringLiteral("B")),
              makeLead(0.55, QStringLiteral("C")) });

        for (int i = 1; i < report.leads.size(); ++i)
            QVERIFY(report.leads[i - 1].confidence >= report.leads[i].confidence);

        QCOMPARE(report.leads[0].headline, QStringLiteral("A"));
        QVERIFY(qFuzzyCompare(report.topConfidence, 0.95));
    }

    void testHtmlTableFollowsConfidenceOrder()
    {
        const LeadReport report = LeadReportGenerator::generate(
            QStringLiteral("HTML-SORT"),
            { makeLead(0.3, QStringLiteral("Third")),
              makeLead(0.9, QStringLiteral("First")),
              makeLead(0.6, QStringLiteral("Second")) });

        const QString html = LeadReportGenerator::generateHtml(report);
        const int posFirst  = html.indexOf(QStringLiteral("First"));
        const int posSecond = html.indexOf(QStringLiteral("Second"));
        const int posThird  = html.indexOf(QStringLiteral("Third"));
        QVERIFY(posFirst >= 0 && posSecond > posFirst && posThird > posSecond);
    }

    void testHtmlEscapesCaseIdInTitleAndBody()
    {
        const LeadReport report = LeadReportGenerator::generate(
            QStringLiteral("CASE<>&\"XSS"), { makeLead(0.5, QStringLiteral("H")) });
        const QString html = LeadReportGenerator::generateHtml(report);

        QVERIFY2(!html.contains(QStringLiteral("<title>SENTINEL Report \u2014 CASE<>&")),
                 "Title must HTML-escape case ID");
        QVERIFY(html.contains(report.caseId.toHtmlEscaped()));
        QVERIFY(html.contains(QStringLiteral("&lt;")));
    }

    void testHtmlEscapesLeadFields()
    {
        InvestigativeLead l;
        l.headline         = QStringLiteral("<img onerror=alert(1)>");
        l.category         = QStringLiteral("geo & <script>");
        l.confidence       = 0.75;
        l.confidenceMethod = QStringLiteral("bayes <test>");
        l.detail           = QStringLiteral("Detail \"quoted\" & <tag>");

        const LeadReport report = LeadReportGenerator::generate(
            QStringLiteral("XSS-FIELDS"), { l });
        const QString html = LeadReportGenerator::generateHtml(report);

        QVERIFY(!html.contains(QStringLiteral("<script>")));
        QVERIFY(!html.contains(QStringLiteral("<img onerror")));
        QVERIFY(html.contains(QStringLiteral("&lt;script&gt;")));
        QVERIFY(html.contains(QStringLiteral("&amp;")));
    }

    void testHtmlEscapesProvenanceChain()
    {
        InvestigativeLead l = makeLead(0.8, QStringLiteral("Prov test"));
        l.provenance = { QStringLiteral("step<1>"), QStringLiteral("step&2") };

        const LeadReport report = LeadReportGenerator::generate(
            QStringLiteral("PROV-ESC"), { l });
        const QString html = LeadReportGenerator::generateHtml(report);

        QVERIFY(html.contains(QStringLiteral("&lt;1&gt;")));
        QVERIFY(!html.contains(QStringLiteral("step<1>")));
    }

    void testMarkdownHtmlConfidencePercentMatch()
    {
        InvestigativeLead l = makeLead(0.755, QStringLiteral("Round test"));
        const LeadReport report = LeadReportGenerator::generate(
            QStringLiteral("ROUND"), { l });

        QVERIFY(report.markdownText.contains(QStringLiteral("**Confidence:** 76%")));
        const QString html = LeadReportGenerator::generateHtml(report);
        QVERIFY(html.contains(QStringLiteral("76%")));
    }

    void testHtmlHighConfidenceBadgeMatchesSummaryThreshold()
    {
        const LeadReport report = LeadReportGenerator::generate(
            QStringLiteral("BADGE"),
            { makeLead(0.72, QStringLiteral("Borderline high")),
              makeLead(0.45, QStringLiteral("Low")) });

        QCOMPARE(report.highConfidenceLeads, 1);
        const QString html = LeadReportGenerator::generateHtml(report);
        QVERIFY(html.contains(QStringLiteral("HIGH CONFIDENCE")));
    }

    void testMarkdownHtmlLeadCountConsistent()
    {
        const LeadReport report = LeadReportGenerator::generate(
            QStringLiteral("COUNT"),
            { makeLead(0.9, QStringLiteral("One")),
              makeLead(0.8, QStringLiteral("Two")),
              makeLead(0.7, QStringLiteral("Three")) });

        QCOMPARE(report.totalLeads, 3);
        const QString html = LeadReportGenerator::generateHtml(report);
        const int rowCount = html.count(QStringLiteral("<tr>")) - 1; // minus header row
        QCOMPARE(rowCount, report.totalLeads);

        int mdHeadings = 0;
        QRegularExpression re(QStringLiteral(R"(### #\d+)"));
        auto it = re.globalMatch(report.markdownText);
        while (it.hasNext()) { it.next(); ++mdHeadings; }
        QCOMPARE(mdHeadings, report.totalLeads);
    }

    void testSaveToFileRoundTrip()
    {
        const LeadReport report = LeadReportGenerator::generate(
            QStringLiteral("SAVE"), { makeLead(0.8, QStringLiteral("Saved")) });

        QTemporaryDir tmpDir;
        QVERIFY2(tmpDir.isValid(), "Need a writable temp directory");

        const QString mdPath = tmpDir.path() + QStringLiteral("/report.md");
        const QString ptPath = tmpDir.path() + QStringLiteral("/report.txt");
        QVERIFY(LeadReportGenerator::saveToFile(report, mdPath, true));
        QVERIFY(LeadReportGenerator::saveToFile(report, ptPath, false));

        QFile mdFile(mdPath);
        QVERIFY(mdFile.open(QIODevice::ReadOnly | QIODevice::Text));
        QTextStream mdIn(&mdFile);
        mdIn.setEncoding(QStringConverter::Utf8);
        const QString mdContent = mdIn.readAll();
        mdFile.close();
        QCOMPARE(mdContent, report.markdownText);

        QFile ptFile(ptPath);
        QVERIFY(ptFile.open(QIODevice::ReadOnly | QIODevice::Text));
        QTextStream ptIn(&ptFile);
        ptIn.setEncoding(QStringConverter::Utf8);
        const QString ptContent = ptIn.readAll();
        ptFile.close();
        QCOMPARE(ptContent, report.plainText);
    }
};

QTEST_GUILESS_MAIN(LeadReportDeep4Test)
#include "test_lead_report_deep4.moc"
