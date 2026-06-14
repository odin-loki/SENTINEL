// test_lead_report_deep9.cpp — Deep audit iteration 29: LeadReportGenerator
// formatLead markdown, plain text body, empty report, formatLead rank.
#include <QtTest/QtTest>
#include "inference/LeadReportGenerator.h"
#include "core/CrimeEvent.h"

class LeadReportDeep9Test : public QObject
{
    Q_OBJECT

    static InvestigativeLead lead(int rank, double conf)
    {
        InvestigativeLead l;
        l.rank       = rank;
        l.category   = QStringLiteral("mo_similarity");
        l.headline   = QStringLiteral("MO match %1").arg(rank);
        l.detail     = QStringLiteral("Shared forced entry pattern");
        l.confidence = conf;
        l.provenance = { QStringLiteral("MOAnalyser") };
        l.generatedAt = QDateTime::currentDateTimeUtc();
        return l;
    }

private slots:

    void testFormatLeadIncludesRank()
    {
        const QString md = LeadReportGenerator::formatLead(lead(3, 0.77));
        QVERIFY(md.contains(QStringLiteral("3")));
        QVERIFY(md.contains(QStringLiteral("mo_similarity")));
    }

    void testPlainTextNonEmpty()
    {
        const auto report = LeadReportGenerator::generate(
            QStringLiteral("CASE-9"), { lead(1, 0.8) });
        QVERIFY(!report.plainText.isEmpty());
        QVERIFY(!report.markdownText.isEmpty());
    }

    void testEmptyLeadsReport()
    {
        const auto report = LeadReportGenerator::generate(QStringLiteral("EMPTY-9"), {});
        QCOMPARE(report.totalLeads, 0);
        QCOMPARE(report.highConfidenceLeads, 0);
        QCOMPARE(report.topConfidence, 0.0);
    }

    void testGeneratedAtValid()
    {
        const auto report = LeadReportGenerator::generate(
            QStringLiteral("TIME-9"), { lead(1, 0.6) });
        QVERIFY(report.generatedAt.isValid());
    }

    void testHtmlIncludesLeadHeadline()
    {
        const auto report = LeadReportGenerator::generate(
            QStringLiteral("HTML-9"), { lead(1, 0.85) });
        const QString html = LeadReportGenerator::generateHtml(report);
        QVERIFY(html.contains(QStringLiteral("MO match 1")));
    }
};

QTEST_GUILESS_MAIN(LeadReportDeep9Test)
#include "test_lead_report_deep9.moc"
