#include <QtTest>
#include "inference/LeadReportGenerator.h"
#include "core/CrimeEvent.h"

class TestLeadReportDeep2 : public QObject
{
    Q_OBJECT

    static InvestigativeLead makeLead(double confidence, const QString& headline)
    {
        InvestigativeLead l;
        l.headline         = headline;
        l.category         = QStringLiteral("geographic");
        l.confidence       = confidence;
        l.confidenceMethod = QStringLiteral("bayesian");
        l.detail           = QStringLiteral("detail text");
        return l;
    }

private slots:
    void testHtmlRanksAreSequential()
    {
        QVector<InvestigativeLead> leads;
        leads << makeLead(0.9, QStringLiteral("Lead A"))
              << makeLead(0.5, QStringLiteral("Lead B"))
              << makeLead(0.7, QStringLiteral("Lead C"));

        const LeadReport report = LeadReportGenerator::generate(QStringLiteral("CASE-001"), leads);

        // report.leads must have ranks 1, 2, 3 in descending-confidence order
        QCOMPARE(report.leads.size(), 3);
        QCOMPARE(report.leads[0].rank, 1);
        QCOMPARE(report.leads[1].rank, 2);
        QCOMPARE(report.leads[2].rank, 3);

        // Sorted by confidence descending: A(0.9), C(0.7), B(0.5)
        QCOMPARE(report.leads[0].headline, QStringLiteral("Lead A"));
        QCOMPARE(report.leads[1].headline, QStringLiteral("Lead C"));
        QCOMPARE(report.leads[2].headline, QStringLiteral("Lead B"));
    }

    void testHtmlContainsCorrectRanks()
    {
        QVector<InvestigativeLead> leads;
        leads << makeLead(0.8, QStringLiteral("First"))
              << makeLead(0.3, QStringLiteral("Second"));

        const LeadReport report = LeadReportGenerator::generate(QStringLiteral("CASE-002"), leads);
        const QString html = LeadReportGenerator::generateHtml(report);

        // HTML should contain "<td>1</td>" and "<td>2</td>" — not "<td>0</td>"
        QVERIFY2(html.contains(QStringLiteral("<td>1</td>")),
                 "HTML rank column must show 1 for top lead");
        QVERIFY2(html.contains(QStringLiteral("<td>2</td>")),
                 "HTML rank column must show 2 for second lead");
        QVERIFY2(!html.contains(QStringLiteral("<td>0</td>")),
                 "HTML must never show rank 0");
    }

    void testMarkdownRanks()
    {
        QVector<InvestigativeLead> leads;
        leads << makeLead(0.9, QStringLiteral("Alpha"))
              << makeLead(0.6, QStringLiteral("Beta"));

        const LeadReport report = LeadReportGenerator::generate(QStringLiteral("CASE-003"), leads);

        QVERIFY2(report.markdownText.contains(QStringLiteral("### #1")),
                 "Markdown must contain rank #1");
        QVERIFY2(report.markdownText.contains(QStringLiteral("### #2")),
                 "Markdown must contain rank #2");
    }

    void testEmptyLeads()
    {
        const LeadReport report = LeadReportGenerator::generate(
            QStringLiteral("CASE-EMPTY"), QVector<InvestigativeLead>{});

        QCOMPARE(report.totalLeads, 0);
        QCOMPARE(report.highConfidenceLeads, 0);
        QVERIFY(qFuzzyCompare(report.topConfidence, 0.0));
        QVERIFY(report.leads.isEmpty());
        QVERIFY(!report.markdownText.isEmpty());
    }

    void testHighConfidenceCount()
    {
        QVector<InvestigativeLead> leads;
        leads << makeLead(0.95, QStringLiteral("High1"))
              << makeLead(0.80, QStringLiteral("High2"))
              << makeLead(0.69, QStringLiteral("Medium"))
              << makeLead(0.30, QStringLiteral("Low"));

        const LeadReport report = LeadReportGenerator::generate(QStringLiteral("CASE-HC"), leads);
        QCOMPARE(report.highConfidenceLeads, 2);
        QVERIFY(qFuzzyCompare(report.topConfidence, 0.95));
    }

    void testTopConfidenceIsHighest()
    {
        QVector<InvestigativeLead> leads;
        leads << makeLead(0.4, QStringLiteral("Low"))
              << makeLead(0.99, QStringLiteral("Best"))
              << makeLead(0.6, QStringLiteral("Mid"));

        const LeadReport report = LeadReportGenerator::generate(QStringLiteral("CASE-TC"), leads);
        QVERIFY(qFuzzyCompare(report.topConfidence, 0.99));
    }

    void testHtmlXssEscaping()
    {
        QVector<InvestigativeLead> leads;
        InvestigativeLead l = makeLead(0.5, QStringLiteral("<script>alert('xss')</script>"));
        l.category          = QStringLiteral("<b>bold</b>");
        leads << l;

        const LeadReport report = LeadReportGenerator::generate(QStringLiteral("CASE-XSS"), leads);
        const QString html = LeadReportGenerator::generateHtml(report);

        QVERIFY2(!html.contains(QStringLiteral("<script>")),
                 "Raw <script> tags must not appear in HTML");
        QVERIFY2(html.contains(QStringLiteral("&lt;script&gt;")),
                 "Script tags must be HTML-escaped");
    }

    void testSaveToFileMarkdown()
    {
        QVector<InvestigativeLead> leads;
        leads << makeLead(0.8, QStringLiteral("Test Lead"));

        const LeadReport report = LeadReportGenerator::generate(QStringLiteral("SAVE-TEST"), leads);

        const QString tmpPath = QDir::tempPath() + QStringLiteral("/sentinel_test_lead.md");
        QVERIFY(LeadReportGenerator::saveToFile(report, tmpPath, true));

        QFile f(tmpPath);
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QString content = QString::fromUtf8(f.readAll());
        QVERIFY(content.contains(QStringLiteral("SAVE-TEST")));
        QVERIFY(content.contains(QStringLiteral("Test Lead")));
        f.close();
        QFile::remove(tmpPath);
    }

    void testProvenanceChain()
    {
        QVector<InvestigativeLead> leads;
        InvestigativeLead l = makeLead(0.7, QStringLiteral("With Provenance"));
        l.provenance = {"step1", "step2", "step3"};
        leads << l;

        const LeadReport report = LeadReportGenerator::generate(QStringLiteral("CASE-PROV"), leads);
        QVERIFY(report.markdownText.contains(QStringLiteral("step1")));
        QVERIFY(report.markdownText.contains(QStringLiteral("step2")));
    }
};

QTEST_GUILESS_MAIN(TestLeadReportDeep2)
#include "test_lead_report_deep2.moc"
