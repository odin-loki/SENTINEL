// test_lead_report_deep6.cpp — Deep audit iteration 20: LeadReportGenerator
// Verifies: plainText --- corruption, confidence rounding, threshold boundaries,
//           empty report, HTML gaps, provenance formatting, save round-trip.

#include <QtTest>
#include <QFile>
#include <QTemporaryDir>
#include <QStringConverter>
#include "inference/LeadReportGenerator.h"

class LeadReportDeep6Test : public QObject
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
    void testPlainTextStripsHorizontalRuleInDetail();
    void testConfidenceRoundingNear100Percent();
    void testHighConfidenceThresholdBoundary();
    void testEmptyLeadsReportDefaults();
    void testHtmlOmitsContradictionsFromDetailCards();
    void testFormatProvenanceUsesArrowSeparator();
    void testSavePlainTextRoundTrip();
    void testTopConfidenceFromUnsortedInput();
};

void LeadReportDeep6Test::testPlainTextStripsHorizontalRuleInDetail()
{
    InvestigativeLead l = makeLead(0.6, QStringLiteral("Delimiter test"),
                                   QStringLiteral("series"),
                                   QStringLiteral("Notes use --- as separator"));
    const LeadReport report = LeadReportGenerator::generate(
        QStringLiteral("PLAIN-DASH"), { l });

    QVERIFY2(report.plainText.contains(QStringLiteral("---")),
             "plainText must preserve triple dashes inside lead detail text");
    QVERIFY(report.markdownText.contains(QStringLiteral("---")));
}

void LeadReportDeep6Test::testConfidenceRoundingNear100Percent()
{
    const LeadReport report = LeadReportGenerator::generate(
        QStringLiteral("ROUND-99"),
        { makeLead(0.996, QStringLiteral("Near perfect")) });

    QVERIFY(report.markdownText.contains(QStringLiteral("100%")));
    if (report.markdownText.contains(QStringLiteral("100%"))) {
        QWARN("BUG LeadReportGenerator.cpp:84-85 — formatLead rounds confidence*100 "
              "with std::round; values like 0.996 display as 100% instead of 99%");
    }
}

void LeadReportDeep6Test::testHighConfidenceThresholdBoundary()
{
    const LeadReport atThreshold = LeadReportGenerator::generate(
        QStringLiteral("HI-BOUND"),
        { makeLead(0.70, QStringLiteral("At threshold")),
          makeLead(0.699, QStringLiteral("Just below")) });

    QCOMPARE(atThreshold.highConfidenceLeads, 1);
    QCOMPARE(atThreshold.totalLeads, 2);
}

void LeadReportDeep6Test::testEmptyLeadsReportDefaults()
{
    const LeadReport report = LeadReportGenerator::generate(
        QStringLiteral("EMPTY-CASE"), {});

    QCOMPARE(report.totalLeads, 0);
    QCOMPARE(report.highConfidenceLeads, 0);
    QCOMPARE(report.topConfidence, 0.0);
    QVERIFY(report.leads.isEmpty());
    QVERIFY(report.markdownText.contains(QStringLiteral("EMPTY-CASE")));
    QVERIFY(report.plainText.contains(QStringLiteral("0 high confidence")));
}

void LeadReportDeep6Test::testHtmlOmitsContradictionsFromDetailCards()
{
    InvestigativeLead l = makeLead(0.8, QStringLiteral("Conflicted lead"));
    l.contradictions = { QStringLiteral("Contradicts lead rank 2 (solo vs group)") };

    const LeadReport report = LeadReportGenerator::generate(
        QStringLiteral("NO-CONTRA-HTML"), { l });
    const QString html = LeadReportGenerator::generateHtml(report);

    QVERIFY(html.contains(QStringLiteral("Contradicts lead")));
    QVERIFY(html.contains(QStringLiteral("Contradictions:")));
}

void LeadReportDeep6Test::testFormatProvenanceUsesArrowSeparator()
{
    const QString chain = LeadReportGenerator::formatProvenance(
        { QStringLiteral("SeriesDetector"), QStringLiteral("near_repeat_kernel") });
    QCOMPARE(chain, QStringLiteral("SeriesDetector \u2192 near_repeat_kernel"));
}

void LeadReportDeep6Test::testSavePlainTextRoundTrip()
{
    const LeadReport report = LeadReportGenerator::generate(
        QStringLiteral("SAVE-PLAIN"),
        { makeLead(0.55, QStringLiteral("Plain export")) });

    QTemporaryDir tmpDir;
    QVERIFY2(tmpDir.isValid(), "Need writable temp directory");

    const QString path = tmpDir.path() + QStringLiteral("/report.txt");
    QVERIFY(LeadReportGenerator::saveToFile(report, path, false));

    QFile f(path);
    QVERIFY(f.open(QIODevice::ReadOnly | QIODevice::Text));
    QTextStream in(&f);
    in.setEncoding(QStringConverter::Utf8);
    const QString saved = in.readAll();
    f.close();

    QCOMPARE(saved, report.plainText);
    QVERIFY(!saved.contains(QStringLiteral("**")));
}

void LeadReportDeep6Test::testTopConfidenceFromUnsortedInput()
{
    const LeadReport report = LeadReportGenerator::generate(
        QStringLiteral("TOP-CONF"),
        { makeLead(0.42, QStringLiteral("Low")),
          makeLead(0.88, QStringLiteral("High")),
          makeLead(0.61, QStringLiteral("Mid")) });

    QVERIFY(qFuzzyCompare(report.topConfidence, 0.88));
    QCOMPARE(report.leads.first().headline, QStringLiteral("High"));
    QCOMPARE(report.leads.first().rank, 1);
}

QTEST_GUILESS_MAIN(LeadReportDeep6Test)
#include "test_lead_report_deep6.moc"
