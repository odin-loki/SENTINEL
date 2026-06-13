// test_data_exporter_deep6.cpp — Iteration 22 deep audit: HTML/JSON export edge cases,
// CSV escaping, benchmark metric completeness, and array save roundtrip.
#include <QTest>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonDocument>
#include <QTemporaryFile>
#include <QFile>
#include <cmath>

#include "core/DataExporter.h"
#include "core/CrimeEvent.h"
#include "models/RiskForecaster.h"
#include "benchmark/BenchmarkMetrics.h"

class TestDataExporterDeep6 : public QObject
{
    Q_OBJECT

    static InvestigativeLead makeLead(int rank, const QString& headline,
                                      const QString& detail, double confidence)
    {
        InvestigativeLead l;
        l.rank             = rank;
        l.category         = QStringLiteral("geo");
        l.headline         = headline;
        l.detail           = detail;
        l.confidence       = confidence;
        l.confidenceMethod = QStringLiteral("Bayesian");
        l.generatedAt      = QDateTime(QDate(2024, 8, 1), QTime(15, 0), Qt::UTC);
        return l;
    }

private slots:
    void testLeadsToHtmlEscapesDetailScript();
    void testEventsToJsonNullLatUsesZero();
    void testBenchmarkToJsonIncludesAllMetrics();
    void testForecastsToJsonAlertLabelUppercase();
    void testCsvEscapeQuotes();
    void testEmptyLeadsMarkdownHeaderOnly();
    void testSaveJsonArrayRoundTrip();
};

void TestDataExporterDeep6::testLeadsToHtmlEscapesDetailScript()
{
    const QString scriptDetail = QStringLiteral("<script>alert('detail')</script>");
    const QString scriptHeadline = QStringLiteral("<script>alert('headline')</script>");

    const QString html = DataExporter::leadsToHtml(
        { makeLead(1, scriptHeadline, scriptDetail, 0.75) },
        QStringLiteral("Deep6 Leads"));

    QVERIFY(html.startsWith(QStringLiteral("<!DOCTYPE html>")));
    QVERIFY(!html.contains(QStringLiteral("<script>alert('headline')</script>")));
    QVERIFY(html.contains(QStringLiteral("&lt;script&gt;")));
    // Current contract: detail is not rendered in leadsToHtml table output.
    QVERIFY(!html.contains(QStringLiteral("alert('detail')")));
}

void TestDataExporterDeep6::testEventsToJsonNullLatUsesZero()
{
    CrimeEvent withCoords;
    withCoords.eventId   = QStringLiteral("HAS-LAT");
    withCoords.crimeType = QStringLiteral("theft");
    withCoords.lat       = 51.5;
    withCoords.lon       = -0.12;

    CrimeEvent noCoords;
    noCoords.eventId   = QStringLiteral("NO-LAT");
    noCoords.crimeType = QStringLiteral("burglary");
    noCoords.lat.reset();
    noCoords.lon.reset();

    const QJsonObject hasObj = DataExporter::eventsToJson({ withCoords }).first().toObject();
    const QJsonObject noObj  = DataExporter::eventsToJson({ noCoords }).first().toObject();

    QCOMPARE(hasObj[QStringLiteral("lat")].toDouble(), 51.5);
    // Documented current behaviour: null optional lat serialises as 0.0, not JSON null.
    QCOMPARE(noObj[QStringLiteral("lat")].toDouble(), 0.0);
    QCOMPARE(noObj[QStringLiteral("lon")].toDouble(), 0.0);
    QVERIFY(!noObj[QStringLiteral("lat")].isNull());
}

void TestDataExporterDeep6::testBenchmarkToJsonIncludesAllMetrics()
{
    BenchmarkReport report;
    report.nSamples   = 500;
    report.pai5pct    = 6.5;
    report.pai10pct   = 4.8;
    report.pai20pct   = 3.2;
    report.pei10pct   = 0.72;
    report.ser        = 0.55;
    report.aucRoc     = 0.91;
    report.aucPr      = 0.63;
    report.mae        = 0.11;
    report.rmse       = 0.19;
    report.brierScore = 0.07;

    const QJsonObject obj = DataExporter::benchmarkToJson(report);

    QCOMPARE(obj[QStringLiteral("nSamples")].toInt(), 500);
    QVERIFY2(qAbs(obj[QStringLiteral("pai05")].toDouble() - 6.5) < 1e-9, "pai05");
    QVERIFY2(qAbs(obj[QStringLiteral("pai10")].toDouble() - 4.8) < 1e-9, "pai10");
    QVERIFY2(qAbs(obj[QStringLiteral("pai20")].toDouble() - 3.2) < 1e-9, "pai20");
    QVERIFY2(qAbs(obj[QStringLiteral("pei10")].toDouble() - 0.72) < 1e-9, "pei10");
    QVERIFY2(qAbs(obj[QStringLiteral("ser")].toDouble() - 0.55) < 1e-9, "ser");
    QVERIFY2(qAbs(obj[QStringLiteral("aucRoc")].toDouble() - 0.91) < 1e-9, "aucRoc");
    QVERIFY2(qAbs(obj[QStringLiteral("aucPr")].toDouble() - 0.63) < 1e-9, "aucPr");
    QVERIFY2(qAbs(obj[QStringLiteral("mae")].toDouble() - 0.11) < 1e-9, "mae");
    QVERIFY2(qAbs(obj[QStringLiteral("rmse")].toDouble() - 0.19) < 1e-9, "rmse");
    QVERIFY2(qAbs(obj[QStringLiteral("brierScore")].toDouble() - 0.07) < 1e-9, "brierScore");
    QVERIFY(obj.contains(QStringLiteral("summary")));
    QVERIFY(!obj[QStringLiteral("summary")].toString().isEmpty());
}

void TestDataExporterDeep6::testForecastsToJsonAlertLabelUppercase()
{
    ZoneForecast elevated;
    elevated.zoneId     = QStringLiteral("ZONE_E");
    elevated.alertLevel = 1;

    ZoneForecast critical;
    critical.zoneId     = QStringLiteral("ZONE_C");
    critical.alertLevel = 3;

    const QJsonObject elevObj = DataExporter::forecastsToJson({ elevated }).first().toObject();
    const QJsonObject critObj = DataExporter::forecastsToJson({ critical }).first().toObject();

    const QString elevLabel = elevObj[QStringLiteral("alertLabel")].toString();
    const QString critLabel = critObj[QStringLiteral("alertLabel")].toString();

    QCOMPARE(elevLabel, QStringLiteral("ELEVATED"));
    QCOMPARE(critLabel, QStringLiteral("CRITICAL"));
    QVERIFY(elevLabel == elevLabel.toUpper());
    QVERIFY(critLabel == critLabel.toUpper());
}

void TestDataExporterDeep6::testCsvEscapeQuotes()
{
    InvestigativeLead l = makeLead(
        1,
        QStringLiteral("Head \"quoted\" line"),
        QStringLiteral("Detail with \"quotes\" and, comma"),
        0.5);

    const QString csv = DataExporter::leadsToCsv({ l });
    QVERIFY(csv.contains(QStringLiteral("\"Head \"\"quoted\"\" line\"")));
    QVERIFY(csv.contains(QStringLiteral("\"Detail with \"\"quotes\"\" and, comma\"")));
}

void TestDataExporterDeep6::testEmptyLeadsMarkdownHeaderOnly()
{
    const QString md = DataExporter::leadsToMarkdown({}, QStringLiteral("Empty Leads"));

    QVERIFY(md.startsWith(QStringLiteral("# Empty Leads")));
    QVERIFY(md.contains(QStringLiteral("| Rank | Category | Headline | Confidence | Method |")));
    QVERIFY(!md.contains(QStringLiteral("## Details")));
    QVERIFY(!md.contains(QStringLiteral("### ")));
}

void TestDataExporterDeep6::testSaveJsonArrayRoundTrip()
{
    QTemporaryFile tmp;
    QVERIFY(tmp.open());

    QJsonArray arr;
    QJsonObject item;
    item[QStringLiteral("id")] = QStringLiteral("deep6-array");
    item[QStringLiteral("value")] = 42;
    arr.append(item);

    QVERIFY(DataExporter::saveJson(arr, tmp.fileName()));

    QFile in(tmp.fileName());
    QVERIFY(in.open(QIODevice::ReadOnly));
    const QJsonDocument loaded = QJsonDocument::fromJson(in.readAll());
    QVERIFY(loaded.isArray());
    QCOMPARE(loaded.array().size(), 1);
    QCOMPARE(loaded.array().first().toObject()[QStringLiteral("id")].toString(),
             QStringLiteral("deep6-array"));
    QCOMPARE(loaded.array().first().toObject()[QStringLiteral("value")].toInt(), 42);
}

QTEST_GUILESS_MAIN(TestDataExporterDeep6)
#include "test_data_exporter_deep6.moc"
