// test_lead_report_deep8.cpp — Deep audit iteration 26: LeadReportGenerator
// high-confidence count, HTML export, provenance formatting, save roundtrip.
#include <QtTest/QtTest>
#include <QTemporaryFile>
#include <QFile>
#include "inference/LeadReportGenerator.h"
#include "core/CrimeEvent.h"

class LeadReportDeep8Test : public QObject
{
    Q_OBJECT

    static InvestigativeLead lead(double conf, int rank = 1)
    {
        InvestigativeLead l;
        l.rank       = rank;
        l.category   = QStringLiteral("series_linkage");
        l.headline   = QStringLiteral("Series match");
        l.detail     = QStringLiteral("Linked to S-1");
        l.confidence = conf;
        l.provenance = { QStringLiteral("SeriesDetector"), QStringLiteral("composite") };
        l.generatedAt = QDateTime::currentDateTimeUtc();
        return l;
    }

private slots:

    void testHighConfidenceCount()
    {
        const auto report = LeadReportGenerator::generate(
            QStringLiteral("CASE-8"),
            { lead(0.85), lead(0.55), lead(0.72) });
        QCOMPARE(report.totalLeads, 3);
        QCOMPARE(report.highConfidenceLeads, 2);
        QVERIFY(report.topConfidence >= 0.85 - 1e-6);
    }

    void testGenerateHtmlContainsCaseId()
    {
        const auto report = LeadReportGenerator::generate(
            QStringLiteral("HTML-8"), { lead(0.8) });
        const QString html = LeadReportGenerator::generateHtml(report);
        QVERIFY(html.contains(QStringLiteral("HTML-8")));
        QVERIFY(html.contains(QStringLiteral("<!DOCTYPE html>")));
    }

    void testFormatProvenanceUsesArrow()
    {
        const QString chain = LeadReportGenerator::formatProvenance(
            { QStringLiteral("A"), QStringLiteral("B") });
        QVERIFY(chain.contains(QStringLiteral("A")));
        QVERIFY(chain.contains(QStringLiteral("B")));
        QVERIFY(chain.contains(QChar(0x2192)));
    }

    void testSaveMarkdownRoundtrip()
    {
        const auto report = LeadReportGenerator::generate(
            QStringLiteral("SAVE-8"), { lead(0.9) });

        QTemporaryFile tmp;
        QVERIFY(tmp.open());
        QVERIFY(LeadReportGenerator::saveToFile(report, tmp.fileName(), true));

        QFile in(tmp.fileName());
        QVERIFY(in.open(QIODevice::ReadOnly));
        const QString content = QString::fromUtf8(in.readAll());
        QVERIFY(content.contains(QStringLiteral("SAVE-8")));
        QVERIFY(content.contains(QStringLiteral("Series match")));
    }

    void testEmptyLeadsReport()
    {
        const auto report = LeadReportGenerator::generate(
            QStringLiteral("EMPTY-8"), {});
        QCOMPARE(report.totalLeads, 0);
        QCOMPARE(report.highConfidenceLeads, 0);
        QCOMPARE(report.topConfidence, 0.0);
        QVERIFY(report.markdownText.contains(QStringLiteral("EMPTY-8")));
    }
};

QTEST_GUILESS_MAIN(LeadReportDeep8Test)
#include "test_lead_report_deep8.moc"
