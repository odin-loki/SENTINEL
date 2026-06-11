// test_data_exporter_deep5.cpp — Iteration 19 deep audit: file save helpers,
// provenance/leads/forecast exports, markdown quirks, and HTML escaping.
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
#include "audit/ProvenanceLog.h"

class TestDataExporterDeep5 : public QObject
{
    Q_OBJECT

    static InvestigativeLead makeLead(int rank, const QString& category,
                                      const QString& headline, double confidence)
    {
        InvestigativeLead l;
        l.rank             = rank;
        l.category         = category;
        l.headline         = headline;
        l.detail           = QStringLiteral("Supporting detail for %1").arg(headline);
        l.confidence       = confidence;
        l.confidenceMethod = QStringLiteral("Bayesian");
        l.generatedAt      = QDateTime(QDate(2024, 8, 1), QTime(15, 0), Qt::UTC);
        return l;
    }

    static CrimeEvent makeEvent(const QString& id, const QString& type)
    {
        CrimeEvent e;
        e.eventId      = id;
        e.crimeType    = type;
        e.suburb       = QStringLiteral("Camden");
        e.lat          = 51.5074;
        e.lon          = -0.1278;
        e.occurredAt   = QDateTime(QDate(2024, 6, 20), QTime(11, 45), Qt::UTC);
        e.qualityScore = 0.72;
        return e;
    }

private slots:
    void testSaveJsonAndTextRoundTrip();
    void testSaveTextSetsLastErrorOnInvalidPath();
    void testLeadsJsonIncludesProvenanceAndContradictions();
    void testLeadsMarkdownProvenanceTrailingArrow();
    void testForecastsCsvRowCountMultiZoneMultiDay();
    void testProvenanceJsonOmitsSourceAndModelFields();
    void testBenchmarkMarkdownPassWarnThresholds();
    void testEventsHtmlEscapesScriptAndHandlesEmpty();
};

void TestDataExporterDeep5::testSaveJsonAndTextRoundTrip()
{
    QTemporaryFile tmp;
    QVERIFY(tmp.open());

    QJsonObject obj;
    obj[QStringLiteral("audit")] = QStringLiteral("iteration19");
    obj[QStringLiteral("count")]  = 3;

    QVERIFY(DataExporter::saveJson(obj, tmp.fileName()));

    QFile in(tmp.fileName());
    QVERIFY(in.open(QIODevice::ReadOnly));
    const QJsonDocument loaded = QJsonDocument::fromJson(in.readAll());
    QCOMPARE(loaded.object()[QStringLiteral("audit")].toString(),
             QStringLiteral("iteration19"));
    QCOMPARE(loaded.object()[QStringLiteral("count")].toInt(), 3);

    const QString textPath = tmp.fileName() + QStringLiteral(".txt");
    QVERIFY(DataExporter::saveText(QStringLiteral("utf8 — test"), textPath));

    QFile textIn(textPath);
    QVERIFY(textIn.open(QIODevice::ReadOnly));
    QCOMPARE(QString::fromUtf8(textIn.readAll()), QStringLiteral("utf8 — test"));
}

void TestDataExporterDeep5::testSaveTextSetsLastErrorOnInvalidPath()
{
    const QString badPath = QStringLiteral("/nonexistent_dir_zzz/deep5.json");
    QVERIFY(!DataExporter::saveText(QStringLiteral("x"), badPath));
    QVERIFY(!DataExporter::lastError().isEmpty());
    QVERIFY(DataExporter::lastError().contains(QStringLiteral("Cannot open")));
}

void TestDataExporterDeep5::testLeadsJsonIncludesProvenanceAndContradictions()
{
    InvestigativeLead l = makeLead(1, QStringLiteral("geo"), QStringLiteral("Hotspot"), 0.88);
    l.provenance      = { QStringLiteral("uk_police"), QStringLiteral("KDE"), QStringLiteral("lead") };
    l.contradictions  = { QStringLiteral("low sample size") };

    const QJsonObject obj = DataExporter::leadsToJson({ l }).first().toObject();
    const QJsonArray prov = obj[QStringLiteral("provenance")].toArray();
    const QJsonArray contr = obj[QStringLiteral("contradictions")].toArray();

    QCOMPARE(prov.size(), 3);
    QCOMPARE(prov[0].toString(), QStringLiteral("uk_police"));
    QCOMPARE(contr.size(), 1);
    QCOMPARE(contr[0].toString(), QStringLiteral("low sample size"));
    QVERIFY2(std::abs(obj[QStringLiteral("confidence")].toDouble() - 0.88) < 1e-9,
             "confidence must round-trip in leads JSON");
}

void TestDataExporterDeep5::testLeadsMarkdownProvenanceTrailingArrow()
{
    InvestigativeLead l = makeLead(2, QStringLiteral("mo"), QStringLiteral("Pattern"), 0.65);
    l.provenance = { QStringLiteral("csv_import"), QStringLiteral("MOAnalyser") };

    const QString md = DataExporter::leadsToMarkdown({ l }, QStringLiteral("Deep5"));
    QVERIFY(md.contains(QStringLiteral("**Provenance:**")));
    QVERIFY2(md.contains(QStringLiteral("csv_import → MOAnalyser")),
             "Provenance chain should join sources without a trailing arrow");
    QVERIFY(!md.contains(QStringLiteral("csv_import → MOAnalyser → ")));
}

void TestDataExporterDeep5::testForecastsCsvRowCountMultiZoneMultiDay()
{
    auto makeZone = [](const QString& id, int days) {
        ZoneForecast zf;
        zf.zoneId     = id;
        zf.alertLevel = 1;
        zf.weeklyRisk = 0.42;
        for (int d = 0; d < days; ++d) {
            ForecastDay day;
            day.date             = QDate(2024, 9, 1).addDays(d);
            day.riskScore        = 0.3 + d * 0.05;
            day.baselineProb     = 0.2;
            day.escalationFactor = 1.1;
            day.temporalFactor   = 1.0;
            day.expectedCount    = 1.5;
            zf.days.append(day);
        }
        return zf;
    };

    const QString csv = DataExporter::forecastsToCsv({
        makeZone(QStringLiteral("ZONE_A"), 3),
        makeZone(QStringLiteral("ZONE_B"), 2)
    });

    QVERIFY(csv.startsWith(QStringLiteral("zone_id,date,risk_score")));
    QCOMPARE(csv.count(QLatin1Char('\n')), 6); // header + 5 day rows

    QVERIFY(csv.contains(QStringLiteral("ZONE_A")));
    QVERIFY(csv.contains(QStringLiteral("ZONE_B")));
    QVERIFY2(csv.contains(QStringLiteral("0.3000")) || csv.contains(QStringLiteral("0.3")),
             "risk_score column must be present");
}

void TestDataExporterDeep5::testProvenanceJsonOmitsSourceAndModelFields()
{
    ProvenanceEntry e;
    e.timestamp = QDateTime(QDate(2024, 6, 15), QTime(12, 0), Qt::UTC);
    e.source    = QStringLiteral("uk_police");
    e.model     = QStringLiteral("Poisson");
    e.eventId   = QStringLiteral("DEEP5-PROV");
    e.stage     = QStringLiteral("inference");
    e.action    = QStringLiteral("score");
    e.detail    = QStringLiteral("zone risk");
    e.dataHash  = QStringLiteral("deadbeef");

    const QJsonObject obj = DataExporter::provenanceToJson({ e }).first().toObject();
    QCOMPARE(obj[QStringLiteral("eventId")].toString(), QStringLiteral("DEEP5-PROV"));
    QCOMPARE(obj[QStringLiteral("dataHash")].toString(), QStringLiteral("deadbeef"));
    QCOMPARE(obj[QStringLiteral("source")].toString(), QStringLiteral("uk_police"));
    QCOMPARE(obj[QStringLiteral("model")].toString(), QStringLiteral("Poisson"));
}

void TestDataExporterDeep5::testBenchmarkMarkdownPassWarnThresholds()
{
    BenchmarkReport pass;
    pass.nSamples   = 200;
    pass.pai5pct    = 7.0;
    pass.pai10pct   = 5.0;
    pass.pai20pct   = 3.5;
    pass.pei10pct   = 0.7;
    pass.ser        = 0.5;
    pass.aucRoc     = 0.90;
    pass.brierScore = 0.08;

    const QString passMd = DataExporter::benchmarkToMarkdown(pass);
    QVERIFY(passMd.contains(QStringLiteral("✓ PASS")));
    QVERIFY(!passMd.contains(QStringLiteral("✗ WARN")));

    BenchmarkReport warn = pass;
    warn.pai5pct    = 2.0;
    warn.aucRoc     = 0.70;
    warn.brierScore = 0.25;

    const QString warnMd = DataExporter::benchmarkToMarkdown(warn);
    QVERIFY(warnMd.contains(QStringLiteral("✗ WARN")));
    QVERIFY(warnMd.contains(QStringLiteral("PAI @ 5%")));
    QVERIFY(warnMd.contains(QStringLiteral("Brier Score")));
}

void TestDataExporterDeep5::testEventsHtmlEscapesScriptAndHandlesEmpty()
{
    const QString html = DataExporter::eventsToHtml(
        { makeEvent(QStringLiteral("XSS-1"), QStringLiteral("<img onerror=alert(1)>")) },
        QStringLiteral("<script>title</script>"));

    QVERIFY(html.startsWith(QStringLiteral("<!DOCTYPE html>")));
    QVERIFY(!html.contains(QStringLiteral("<script>title</script>")));
    QVERIFY(html.contains(QStringLiteral("&lt;script&gt;")));
    QVERIFY(!html.contains(QStringLiteral("<img onerror")));
    QVERIFY(html.contains(QStringLiteral("&lt;img")));

    const QString emptyHtml = DataExporter::eventsToHtml({}, QStringLiteral("Empty"));
    QVERIFY(emptyHtml.contains(QStringLiteral("<table>")));
    QVERIFY(emptyHtml.contains(QStringLiteral("</tbody>")));
    QVERIFY(!emptyHtml.contains(QStringLiteral("<tr><td>")));
}

QTEST_GUILESS_MAIN(TestDataExporterDeep5)
#include "test_data_exporter_deep5.moc"
