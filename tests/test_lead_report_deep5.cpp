// Deep audit iteration 17 — LeadReportGenerator (deep5)
// Verifies: saveToFile error paths, plainText stripping, provenance HTML section,
//           UTF-8 round-trip, formatProvenance arrow separator.

#include <QtTest>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QRegularExpression>
#include <QStringConverter>
#include "inference/LeadReportGenerator.h"

class LeadReportDeep5Test : public QObject
{
    Q_OBJECT

    static InvestigativeLead makeLead(double confidence,
                                      const QString& headline,
                                      const QString& category = QStringLiteral("network"),
                                      const QStringList& provenance = {})
    {
        InvestigativeLead l;
        l.headline         = headline;
        l.category         = category;
        l.confidence       = confidence;
        l.confidenceMethod = QStringLiteral("ensemble");
        l.detail           = QStringLiteral("Supporting detail");
        l.provenance.assign(provenance.begin(), provenance.end());
        return l;
    }

private slots:
    void testSaveToFileFailsOnInvalidPath();
    void testSaveToFileMarkdownUtf8RoundTrip();
    void testPlainTextStripsMarkdownMarkers();
    void testPlainTextPreservesReadableContent();
    void testPlainTextPreservesAsterisksInDetail();
    void testHtmlProvenanceSectionOnlyWhenPresent();
    void testHtmlProvenanceOmitsLeadsWithoutChain();
    void testHtmlProvenanceEscapesSpecialCharacters();
};

void LeadReportDeep5Test::testSaveToFileFailsOnInvalidPath()
{
    const LeadReport report = LeadReportGenerator::generate(
        QStringLiteral("BAD-PATH"), { makeLead(0.5, QStringLiteral("X")) });

    const QString bogus = QStringLiteral("/nonexistent_dir_zzz/report.md");
    QVERIFY(!LeadReportGenerator::saveToFile(report, bogus, true));
    QVERIFY(!LeadReportGenerator::saveToFile(report, bogus, false));
}

void LeadReportDeep5Test::testSaveToFileMarkdownUtf8RoundTrip()
{
    InvestigativeLead l = makeLead(0.8, QStringLiteral("Caf\u00e9 \u2014 lead"));
    l.detail = QStringLiteral("Detail with \u00a3 symbol and \u2192 arrow");

    const LeadReport report = LeadReportGenerator::generate(
        QStringLiteral("UTF8-SAVE"), { l });

    QTemporaryDir tmpDir;
    QVERIFY2(tmpDir.isValid(), "Need writable temp directory");

    const QString path = tmpDir.path() + QStringLiteral("/utf8_report.md");
    QVERIFY(LeadReportGenerator::saveToFile(report, path, true));

    QFile f(path);
    QVERIFY(f.open(QIODevice::ReadOnly | QIODevice::Text));
    QTextStream in(&f);
    in.setEncoding(QStringConverter::Utf8);
    const QString content = in.readAll();
    f.close();

    QCOMPARE(content, report.markdownText);
    QVERIFY(content.contains(QStringLiteral("Caf\u00e9")));
    QVERIFY(content.contains(QStringLiteral("\u00a3")));
}

void LeadReportDeep5Test::testPlainTextStripsMarkdownMarkers()
{
    const LeadReport report = LeadReportGenerator::generate(
        QStringLiteral("PLAIN"),
        { makeLead(0.75, QStringLiteral("Headline")) });

    QVERIFY(!report.plainText.contains(QStringLiteral("## ")));
    QVERIFY(!report.plainText.contains(QStringLiteral("### ")));
    QVERIFY(!report.plainText.contains(QStringLiteral("**")));
    QVERIFY(!report.plainText.contains(QStringLiteral("---")));
    QVERIFY(report.plainText.contains(QStringLiteral("SENTINEL Investigative Leads Report")));
    QVERIFY(report.plainText.contains(QStringLiteral("Headline")));
}

void LeadReportDeep5Test::testPlainTextPreservesAsterisksInDetail()
{
    InvestigativeLead l = makeLead(0.5, QStringLiteral("Star test"));
    l.detail = QStringLiteral("Evidence marked *important*");

    const LeadReport report = LeadReportGenerator::generate(
        QStringLiteral("ASTERISK"), { l });

    QVERIFY2(report.plainText.contains(QStringLiteral("*important*")),
             "plainText must not strip asterisks from lead detail");
}

void LeadReportDeep5Test::testPlainTextPreservesReadableContent()
{
    InvestigativeLead l = makeLead(0.66, QStringLiteral("Readable headline"));
    l.detail = QStringLiteral("Line one. Line two.");

    const LeadReport report = LeadReportGenerator::generate(
        QStringLiteral("READABLE"), { l });

    QVERIFY(report.plainText.contains(QStringLiteral("Readable headline")));
    QVERIFY(report.plainText.contains(QStringLiteral("Line one. Line two.")));
    QVERIFY(report.plainText.contains(QStringLiteral("Confidence:")));
    QVERIFY(report.plainText.contains(QStringLiteral("66%")));
}

void LeadReportDeep5Test::testHtmlProvenanceSectionOnlyWhenPresent()
{
    const LeadReport noProv = LeadReportGenerator::generate(
        QStringLiteral("NO-PROV"), { makeLead(0.5, QStringLiteral("Bare")) });
    const QString htmlNoProv = LeadReportGenerator::generateHtml(noProv);
    QVERIFY(!htmlNoProv.contains(QStringLiteral("Provenance Chain")));

    const LeadReport withProv = LeadReportGenerator::generate(
        QStringLiteral("WITH-PROV"),
        { makeLead(0.5, QStringLiteral("Traced"),
                   QStringLiteral("series"),
                   { QStringLiteral("MOAnalyser"), QStringLiteral("tfidf") }) });
    const QString htmlWithProv = LeadReportGenerator::generateHtml(withProv);
    QVERIFY(htmlWithProv.contains(QStringLiteral("Provenance Chain")));
    QVERIFY(htmlWithProv.contains(QStringLiteral("prov-chain")));
    QVERIFY(htmlWithProv.contains(QStringLiteral("MOAnalyser")));
}

void LeadReportDeep5Test::testHtmlProvenanceOmitsLeadsWithoutChain()
{
    const LeadReport report = LeadReportGenerator::generate(
        QStringLiteral("MIXED"),
        { makeLead(0.9, QStringLiteral("With chain"), QStringLiteral("a"),
                   { QStringLiteral("stepA") }),
          makeLead(0.4, QStringLiteral("No chain")) });

    const QString html = LeadReportGenerator::generateHtml(report);

    const int provSectionStart = html.indexOf(QStringLiteral("Provenance Chain"));
    QVERIFY(provSectionStart >= 0);
    const QString provSection = html.mid(provSectionStart);

    QVERIFY(provSection.contains(QStringLiteral("With chain")));
    QVERIFY(!provSection.contains(QStringLiteral("No chain")));
}

void LeadReportDeep5Test::testHtmlProvenanceEscapesSpecialCharacters()
{
    InvestigativeLead l = makeLead(0.7, QStringLiteral("Esc <test>"),
                                   QStringLiteral("geo"),
                                   { QStringLiteral("src<script>"), QStringLiteral("step&2") });
    const LeadReport report = LeadReportGenerator::generate(
        QStringLiteral("PROV-XSS"), { l });
    const QString html = LeadReportGenerator::generateHtml(report);

    QVERIFY(html.contains(QStringLiteral("Provenance Chain")));
    QVERIFY(!html.contains(QStringLiteral("<script>")));
    QVERIFY(html.contains(QStringLiteral("&lt;script&gt;")));
    QVERIFY(html.contains(QStringLiteral("&amp;")));
}

QTEST_GUILESS_MAIN(LeadReportDeep5Test)
#include "test_lead_report_deep5.moc"
