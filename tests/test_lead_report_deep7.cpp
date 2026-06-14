// test_lead_report_deep7.cpp — Deep audit iteration 23: LeadReportGenerator
// Verifies: Markdown headline/provenance, HTML escaping, empty report validity,
// saveToFile disk write, rank ordering, and confidence formatting.

#include <QtTest>
#include <QFile>
#include <QTemporaryDir>
#include <QStringConverter>
#include "inference/LeadReportGenerator.h"

class LeadReportDeep7Test : public QObject
{
    Q_OBJECT

    static InvestigativeLead makeLead(double confidence,
                                      const QString& headline,
                                      const QString& category = QStringLiteral("network"),
                                      const QString& detail = QStringLiteral("Supporting detail"),
                                      const QStringList& provenance = {})
    {
        InvestigativeLead l;
        l.headline         = headline;
        l.category         = category;
        l.confidence       = confidence;
        l.confidenceMethod = QStringLiteral("ensemble");
        l.detail           = detail;
        l.provenance.assign(provenance.begin(), provenance.end());
        return l;
    }

private slots:
    void testGenerateMarkdownIncludesHeadlineAndProvenance();
    void testGenerateHtmlEscapesAngleBrackets();
    void testEmptyLeadsListProducesValidOutput();
    void testSaveToFileCreatesFileOnDisk();
    void testRankOrderingPreservedInOutput();
    void testConfidenceFormattedInReport();
    void testHtmlConfidenceBarMatchesRoundedPercent();
};

void LeadReportDeep7Test::testGenerateMarkdownIncludesHeadlineAndProvenance()
{
    InvestigativeLead lead = makeLead(
        0.82,
        QStringLiteral("Linked suspect vehicle"),
        QStringLiteral("series"),
        QStringLiteral("Plate match across three burglaries"),
        { QStringLiteral("SeriesDetector"), QStringLiteral("MOAnalyser") });

    const LeadReport report = LeadReportGenerator::generate(
        QStringLiteral("CASE-MD-23"), { lead });

    QVERIFY(report.markdownText.contains(QStringLiteral("Linked suspect vehicle")));
    QVERIFY(report.markdownText.contains(QStringLiteral("Provenance:")));
    QVERIFY(report.markdownText.contains(QStringLiteral("SeriesDetector")));
    QVERIFY(report.markdownText.contains(QStringLiteral("MOAnalyser")));
    QVERIFY(report.markdownText.contains(QStringLiteral("CASE-MD-23")));
}

void LeadReportDeep7Test::testGenerateHtmlEscapesAngleBrackets()
{
    InvestigativeLead lead = makeLead(
        0.65,
        QStringLiteral("Tag <script>alert(1)</script>"),
        QStringLiteral("network"),
        QStringLiteral("Detail with <b>bold</b> markup"));

    const LeadReport report = LeadReportGenerator::generate(
        QStringLiteral("HTML-ESC"), { lead });
    const QString html = LeadReportGenerator::generateHtml(report);

    QVERIFY(!html.contains(QStringLiteral("<script>alert")));
    QVERIFY(html.contains(QStringLiteral("&lt;script&gt;")));
    QVERIFY(html.contains(QStringLiteral("&lt;b&gt;bold&lt;/b&gt;")));
}

void LeadReportDeep7Test::testEmptyLeadsListProducesValidOutput()
{
    const LeadReport report = LeadReportGenerator::generate(
        QStringLiteral("EMPTY-23"), {});

    QCOMPARE(report.totalLeads, 0);
    QCOMPARE(report.highConfidenceLeads, 0);
    QCOMPARE(report.topConfidence, 0.0);
    QVERIFY(report.leads.isEmpty());

    QVERIFY(report.markdownText.contains(QStringLiteral("SENTINEL Investigative Leads Report")));
    QVERIFY(report.markdownText.contains(QStringLiteral("EMPTY-23")));
    QVERIFY(report.plainText.contains(QStringLiteral("0 high confidence")));

    const QString html = LeadReportGenerator::generateHtml(report);
    QVERIFY(html.contains(QStringLiteral("<!DOCTYPE html>")));
    QVERIFY(html.contains(QStringLiteral("EMPTY-23")));
}

void LeadReportDeep7Test::testSaveToFileCreatesFileOnDisk()
{
    const LeadReport report = LeadReportGenerator::generate(
        QStringLiteral("SAVE-23"),
        { makeLead(0.71, QStringLiteral("Disk write test")) });

    QTemporaryDir tmpDir;
    QVERIFY2(tmpDir.isValid(), "Need writable temp directory");

    const QString path = tmpDir.path() + QStringLiteral("/lead_report.md");
    QVERIFY(LeadReportGenerator::saveToFile(report, path, true));
    QVERIFY(QFile::exists(path));

    QFile f(path);
    QVERIFY(f.open(QIODevice::ReadOnly | QIODevice::Text));
    QTextStream in(&f);
    in.setEncoding(QStringConverter::Utf8);
    const QString saved = in.readAll();
    f.close();

    QCOMPARE(saved, report.markdownText);
    QVERIFY(saved.contains(QStringLiteral("Disk write test")));
}

void LeadReportDeep7Test::testRankOrderingPreservedInOutput()
{
    const LeadReport report = LeadReportGenerator::generate(
        QStringLiteral("RANK-23"),
        { makeLead(0.40, QStringLiteral("Low priority")),
          makeLead(0.91, QStringLiteral("Top priority")),
          makeLead(0.62, QStringLiteral("Mid priority")) });

    QCOMPARE(report.leads.size(), 3);
    QCOMPARE(report.leads.at(0).rank, 1);
    QCOMPARE(report.leads.at(0).headline, QStringLiteral("Top priority"));
    QCOMPARE(report.leads.at(1).rank, 2);
    QCOMPARE(report.leads.at(2).rank, 3);

    QVERIFY(report.markdownText.indexOf(QStringLiteral("Top priority"))
            < report.markdownText.indexOf(QStringLiteral("Mid priority")));
    QVERIFY(report.markdownText.indexOf(QStringLiteral("Mid priority"))
            < report.markdownText.indexOf(QStringLiteral("Low priority")));

    const QString html = LeadReportGenerator::generateHtml(report);
    QVERIFY(html.indexOf(QStringLiteral("Top priority"))
            < html.indexOf(QStringLiteral("Low priority")));
}

void LeadReportDeep7Test::testConfidenceFormattedInReport()
{
    const LeadReport report = LeadReportGenerator::generate(
        QStringLiteral("CONF-23"),
        { makeLead(0.745, QStringLiteral("Rounded confidence lead")) });

    QVERIFY(report.markdownText.contains(QStringLiteral("**Confidence:** 75%")));
    QVERIFY(report.plainText.contains(QStringLiteral("Confidence: 75%")));

    const QString html = LeadReportGenerator::generateHtml(report);
    QVERIFY(html.contains(QStringLiteral("75%")));
}

void LeadReportDeep7Test::testHtmlConfidenceBarMatchesRoundedPercent()
{
    const LeadReport report = LeadReportGenerator::generate(
        QStringLiteral("BAR-23"),
        { makeLead(0.555, QStringLiteral("Bar width check")) });

    const QString html = LeadReportGenerator::generateHtml(report);
    QVERIFY(html.contains(QStringLiteral("width:56%")));
    QVERIFY(html.contains(QStringLiteral(">56%</span>")));
}

QTEST_GUILESS_MAIN(LeadReportDeep7Test)
#include "test_lead_report_deep7.moc"
