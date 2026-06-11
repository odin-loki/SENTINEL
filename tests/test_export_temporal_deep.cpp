// test_export_temporal_deep.cpp — deep audit tests for DataExporter,
// LeadReportGenerator, and TemporalFeatures (iteration 7).
#include <QTest>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <cmath>
#include "core/DataExporter.h"
#include "core/CrimeEvent.h"
#include "inference/LeadReportGenerator.h"
#include "models/TemporalFeatures.h"

namespace {

static InvestigativeLead makeLead(const QString& headline = QStringLiteral("Default headline"),
                                  double confidence = 0.75)
{
    InvestigativeLead l;
    l.rank             = 1;
    l.category         = QStringLiteral("series");
    l.headline         = headline;
    l.detail           = QStringLiteral("Detail text");
    l.confidence       = confidence;
    l.confidenceMethod = QStringLiteral("ensemble");
    l.generatedAt      = QDateTime(QDate(2024, 6, 1), QTime(12, 0, 0), QTimeZone::utc());
    l.provenance       = { QStringLiteral("HintEngine"), QStringLiteral("MOAnalyser") };
    l.contradictions   = { QStringLiteral("weak_signal") };
    return l;
}

// Parse full CSV text into records (handles quoted fields with embedded newlines).
static QVector<QStringList> parseCsvRecords(const QString& csv)
{
    QVector<QStringList> records;
    QStringList currentRow;
    QString currentField;
    bool inQuotes = false;

    for (int i = 0; i < csv.size(); ++i) {
        const QChar ch = csv[i];
        if (inQuotes) {
            if (ch == QLatin1Char('"')) {
                if (i + 1 < csv.size() && csv[i + 1] == QLatin1Char('"')) {
                    currentField += QLatin1Char('"');
                    ++i;
                } else {
                    inQuotes = false;
                }
            } else {
                currentField += ch;
            }
        } else if (ch == QLatin1Char('"')) {
            inQuotes = true;
        } else if (ch == QLatin1Char(',')) {
            currentRow.append(currentField);
            currentField.clear();
        } else if (ch == QLatin1Char('\n')) {
            currentRow.append(currentField);
            currentField.clear();
            if (!currentRow.isEmpty() || !records.isEmpty()) {
                records.append(currentRow);
                currentRow.clear();
            }
        } else if (ch == QLatin1Char('\r')) {
            // ignore CR; LF delimits records
        } else {
            currentField += ch;
        }
    }

    if (!currentField.isEmpty() || !currentRow.isEmpty()) {
        currentRow.append(currentField);
        records.append(currentRow);
    }
    return records;
}

static QString csvHeadlineField(const QString& csv, bool* ok = nullptr)
{
    const QVector<QStringList> records = parseCsvRecords(csv);
    if (records.size() < 2 || records[1].size() < 3) {
        if (ok) *ok = false;
        return {};
    }
    if (ok) *ok = true;
    return records[1][2];
}

static QDateTime makeUTC(int year, int month, int day, int hour, int minute = 0)
{
    return QDateTime(QDate(year, month, day), QTime(hour, minute, 0), QTimeZone::utc());
}

} // namespace

class ExportTemporalDeepTest : public QObject
{
    Q_OBJECT

private slots:

    // ── DataExporter: escapeCsv via leadsToCsv ───────────────────────────────

    void testEscapeCsvNoSpecialChars()
    {
        const QString csv = DataExporter::leadsToCsv({ makeLead(QStringLiteral("PlainText")) });
        bool ok = false;
        const QString headline = csvHeadlineField(csv, &ok);
        QVERIFY(ok);
        QCOMPARE(headline, QStringLiteral("PlainText"));
        QVERIFY2(!headline.startsWith(QLatin1Char('"')),
                 "Plain strings should not be quoted");
    }

    void testEscapeCsvWithComma()
    {
        const QString csv = DataExporter::leadsToCsv({ makeLead(QStringLiteral("a,b")) });
        bool ok = false;
        const QString headline = csvHeadlineField(csv, &ok);
        QVERIFY(ok);
        QCOMPARE(headline, QStringLiteral("a,b"));
        QVERIFY2(csv.contains(QStringLiteral("\"a,b\"")),
                 "Comma-containing fields must be quoted in CSV");
    }

    void testEscapeCsvWithQuotes()
    {
        const QString csv = DataExporter::leadsToCsv({ makeLead(QStringLiteral("say \"hello\"")) });
        bool ok = false;
        const QString headline = csvHeadlineField(csv, &ok);
        QVERIFY(ok);
        QCOMPARE(headline, QStringLiteral("say \"hello\""));
        QVERIFY2(csv.contains(QStringLiteral("\"say \"\"hello\"\"\"")),
                 "Embedded double quotes must be doubled inside quoted fields");
    }

    void testEscapeCsvWithNewline()
    {
        const QString csv = DataExporter::leadsToCsv({ makeLead(QStringLiteral("line1\nline2")) });
        bool ok = false;
        const QString headline = csvHeadlineField(csv, &ok);
        QVERIFY(ok);
        QCOMPARE(headline, QStringLiteral("line1\nline2"));
        QVERIFY2(csv.contains(QLatin1Char('"')), "Newline-containing fields must be quoted");
    }

    void testLeadsToJsonAllFields()
    {
        const InvestigativeLead lead = makeLead(QStringLiteral("JSON headline"), 0.82);
        const QJsonArray arr = DataExporter::leadsToJson({ lead });
        QCOMPARE(arr.size(), 1);

        const QJsonObject obj = arr[0].toObject();
        QVERIFY(obj.contains(QStringLiteral("rank")));
        QVERIFY(obj.contains(QStringLiteral("category")));
        QVERIFY(obj.contains(QStringLiteral("headline")));
        QVERIFY(obj.contains(QStringLiteral("detail")));
        QVERIFY(obj.contains(QStringLiteral("confidence")));
        QVERIFY(obj.contains(QStringLiteral("confidenceMethod")));
        QVERIFY(obj.contains(QStringLiteral("provenance")));
        QVERIFY(obj.contains(QStringLiteral("contradictions")));

        QCOMPARE(obj[QStringLiteral("rank")].toInt(), 1);
        QCOMPARE(obj[QStringLiteral("headline")].toString(), QStringLiteral("JSON headline"));
        QVERIFY(obj[QStringLiteral("provenance")].toArray().size() >= 1);
        QVERIFY(obj[QStringLiteral("contradictions")].toArray().size() >= 1);
    }

    void testLeadsToHtmlEscapesHtml()
    {
        InvestigativeLead lead = makeLead(QStringLiteral("<script>alert(1)</script>"));
        const QString html = DataExporter::leadsToHtml({ lead });
        QVERIFY2(!html.contains(QStringLiteral("<script>")),
                 "Raw script tags must not appear in HTML");
        QVERIFY(html.contains(QStringLiteral("&lt;script&gt;")));
    }

    void testLeadsToHtmlConfidencePercent()
    {
        const QString html = DataExporter::leadsToHtml({ makeLead(QStringLiteral("Conf test"), 0.75) });
        QVERIFY2(html.contains(QStringLiteral("75.0%")),
                 "Confidence 0.75 should render as 75.0% in HTML");
    }

    void testLeadsToCsvRoundtrip()
    {
        InvestigativeLead lead = makeLead(QStringLiteral("Roundtrip, \"quoted\" headline"), 0.65);
        lead.category         = QStringLiteral("mo");
        lead.detail           = QStringLiteral("Multi\nline\ndetail");
        lead.confidenceMethod = QStringLiteral("bayesian");

        const QString csv = DataExporter::leadsToCsv({ lead });
        const QVector<QStringList> records = parseCsvRecords(csv);
        QCOMPARE(records.size(), 2);

        const QStringList& fields = records[1];
        QCOMPARE(fields.size(), 7);
        QCOMPARE(fields[0].toInt(), lead.rank);
        QCOMPARE(fields[1], lead.category);
        QCOMPARE(fields[2], lead.headline);
        QCOMPARE(fields[3], lead.detail);
        QVERIFY(std::abs(fields[4].toDouble() - lead.confidence) < 1e-4);
        QCOMPARE(fields[5], lead.confidenceMethod);
    }

    void testEventsToJsonAllFields()
    {
        CrimeEvent ev;
        ev.eventId   = QStringLiteral("EV-42");
        ev.crimeType = QStringLiteral("burglary");
        ev.lat       = 51.5074;
        ev.lon       = -0.1278;

        const QJsonArray arr = DataExporter::eventsToJson({ ev });
        QCOMPARE(arr.size(), 1);

        const QJsonObject obj = arr[0].toObject();
        QVERIFY(obj.contains(QStringLiteral("eventId")));
        QVERIFY(obj.contains(QStringLiteral("crimeType")));
        QVERIFY(obj.contains(QStringLiteral("lat")));
        QVERIFY(obj.contains(QStringLiteral("lon")));

        QCOMPARE(obj[QStringLiteral("eventId")].toString(), QStringLiteral("EV-42"));
        QCOMPARE(obj[QStringLiteral("crimeType")].toString(), QStringLiteral("burglary"));
        QVERIFY(std::abs(obj[QStringLiteral("lat")].toDouble() - 51.5074) < 1e-6);
        QVERIFY(std::abs(obj[QStringLiteral("lon")].toDouble() - (-0.1278)) < 1e-6);
    }

    // ── LeadReportGenerator ──────────────────────────────────────────────────

    void testMarkdownContainsRank()
    {
        const auto report = LeadReportGenerator::generate(
            QStringLiteral("CASE-RANK"),
            { makeLead(QStringLiteral("Ranked lead"), 0.88) });
        QVERIFY2(report.markdownText.contains(QStringLiteral("#1")),
                 "Markdown should contain display rank");
    }

    void testMarkdownContainsHeadline()
    {
        const auto report = LeadReportGenerator::generate(
            QStringLiteral("CASE-HL"),
            { makeLead(QStringLiteral("Unique Headline XYZ"), 0.77) });
        QVERIFY(report.markdownText.contains(QStringLiteral("Unique Headline XYZ")));
    }

    void testHtmlContainsConfBar()
    {
        const auto report = LeadReportGenerator::generate(
            QStringLiteral("CASE-BAR"),
            { makeLead(QStringLiteral("Bar lead"), 0.6) });
        const QString html = LeadReportGenerator::generateHtml(report);
        QVERIFY(html.contains(QStringLiteral("conf-bar")));
    }

    void testHtmlContainsDoctype()
    {
        const auto report = LeadReportGenerator::generate(
            QStringLiteral("CASE-DOC"),
            { makeLead(QStringLiteral("Doc lead"), 0.5) });
        const QString html = LeadReportGenerator::generateHtml(report);
        QVERIFY(html.contains(QStringLiteral("<!DOCTYPE")) ||
                html.contains(QStringLiteral("<html")));
    }

    void testEmptyLeadsMarkdown()
    {
        const auto report = LeadReportGenerator::generate(QStringLiteral("CASE-EMPTY"), {});
        QVERIFY2(!report.markdownText.isEmpty(), "Empty leads must still produce Markdown");
        QVERIFY(report.markdownText.contains(QStringLiteral("SENTINEL")));
        QCOMPARE(report.totalLeads, 0);
    }

    void testEmptyLeadsHtml()
    {
        const auto report = LeadReportGenerator::generate(QStringLiteral("CASE-EMPTY-H"), {});
        const QString html = LeadReportGenerator::generateHtml(report);
        QVERIFY2(!html.isEmpty(), "Empty leads must still produce HTML");
        QVERIFY(html.contains(QStringLiteral("<!DOCTYPE")) ||
                html.contains(QStringLiteral("<html")));
        QVERIFY(html.contains(QStringLiteral("CASE-EMPTY-H")));
    }

    // ── TemporalFeatures ─────────────────────────────────────────────────────

    void testSinCosHourMidnight()
    {
        const auto fv = TemporalFeatures::compute(makeUTC(2024, 6, 15, 0));
        QVERIFY(std::abs(fv.hourSin) < 1e-9);
        QVERIFY(std::abs(fv.hourCos - 1.0) < 1e-9);
    }

    void testSinCosHourNoon()
    {
        const auto fv = TemporalFeatures::compute(makeUTC(2024, 6, 15, 12));
        QVERIFY(std::abs(fv.hourSin) < 1e-9);
        QVERIFY(std::abs(fv.hourCos + 1.0) < 1e-9);
    }

    void testWeekendFlagSaturday()
    {
        // 2024-01-06 is Saturday (dowRaw = 5 in 0=Mon..6=Sun encoding)
        const auto fv = TemporalFeatures::compute(makeUTC(2024, 1, 6, 12));
        QVERIFY(fv.isWeekend);
        QCOMPARE(fv.dowRaw, 5);
    }

    void testNightFlagAtMidnight()
    {
        const auto fv = TemporalFeatures::compute(makeUTC(2024, 6, 15, 0));
        QVERIFY(fv.isNight);
    }

    void testNightFlagAtNoon()
    {
        const auto fv = TemporalFeatures::compute(makeUTC(2024, 6, 15, 12));
        QVERIFY(!fv.isNight);
    }

    void testPaydayProximityAtDay0()
    {
        // Jan 1 → dayOfYear = 1 → 1 % 14 = 1 → min(1, 13) = 1
        // Use a date with doy=1 for explicit boundary; Jan 1 is doy=1 not 0.
        // For doy where d=0: use day 14 of year → Jan 14 doy=14, 14%14=0 → min(0,14)=0
        const auto fv = TemporalFeatures::compute(makeUTC(2024, 1, 14, 12));
        QCOMPARE(fv.daysFromPayday, 0);
    }

    void testPaydayProximityAtDay7()
    {
        // doy=7 → Jan 7: 7 % 14 = 7 → min(7, 7) = 7
        const auto fv = TemporalFeatures::compute(makeUTC(2024, 1, 7, 12));
        QCOMPARE(fv.daysFromPayday, 7);
    }

    void testLunarPhaseRange()
    {
        for (int month = 1; month <= 12; ++month) {
            const auto fv = TemporalFeatures::compute(makeUTC(2024, month, 15, 12));
            QVERIFY2(fv.lunarPhase >= 0.0 && fv.lunarPhase <= 1.0,
                     qPrintable(QStringLiteral("lunarPhase %1 out of [0,1] for month %2")
                                    .arg(fv.lunarPhase)
                                    .arg(month)));
        }
    }

    void testCyclicEncodingMonth()
    {
        // month=7 (July) uses 0-indexed encoding: sin(2π * 6 / 12) = sin(π) ≈ 0
        const auto fv = TemporalFeatures::compute(makeUTC(2024, 7, 15, 12));
        const double expected = std::sin(2.0 * M_PI * 6.0 / 12.0);
        QVERIFY(std::abs(fv.monthSin - expected) < 1e-9);
    }
};

QTEST_MAIN(ExportTemporalDeepTest)
#include "test_export_temporal_deep.moc"
