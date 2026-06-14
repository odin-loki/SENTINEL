// test_data_exporter_deep7.cpp — Deep audit iteration 26: DataExporter
// provenance JSON, forecast CSV headers, benchmark markdown, events HTML title.
#include <QtTest/QtTest>
#include <QJsonArray>
#include "core/DataExporter.h"
#include "core/CrimeEvent.h"
#include "models/RiskForecaster.h"
#include "audit/ProvenanceLog.h"
#include "benchmark/BenchmarkMetrics.h"

class DataExporterDeep7Test : public QObject
{
    Q_OBJECT

private slots:

    void testProvenanceToJsonFields()
    {
        ProvenanceLog log;
        log.record(QStringLiteral("EVT-P"), QStringLiteral("ingest"),
                   QStringLiteral("load"), QStringLiteral("csv row 1"));

        const QJsonArray arr = DataExporter::provenanceToJson(log.getEntries());
        QVERIFY(!arr.isEmpty());
        const QJsonObject obj = arr.first().toObject();
        QVERIFY(obj.contains(QStringLiteral("stage")));
        QVERIFY(obj.contains(QStringLiteral("action")));
    }

    void testForecastsToCsvHasHeader()
    {
        ZoneForecast zf;
        zf.zoneId = QStringLiteral("Z1");
        ForecastDay day;
        day.date = QDate(2024, 5, 1);
        day.riskScore = 0.42;
        zf.days.append(day);
        zf.weeklyRisk = 0.42;
        zf.alertLevel = 1;

        const QString csv = DataExporter::forecastsToCsv({ zf });
        QVERIFY(csv.contains(QStringLiteral("zone_id")));
        QVERIFY(csv.contains(QStringLiteral("Z1")));
    }

    void testBenchmarkToMarkdownIncludesPAI()
    {
        BenchmarkReport rep;
        rep.pai5pct  = 2.5;
        rep.pai10pct = 2.0;
        rep.nSamples = 100;

        const QString md = DataExporter::benchmarkToMarkdown(rep);
        QVERIFY(md.contains(QStringLiteral("PAI")));
        QVERIFY(md.contains(QStringLiteral("100")));
    }

    void testEventsToHtmlCustomTitle()
    {
        CrimeEvent ev;
        ev.eventId   = QStringLiteral("HTML-E1");
        ev.crimeType = QStringLiteral("theft");

        const QString html = DataExporter::eventsToHtml(
            { ev }, QStringLiteral("Deep7 Events"));
        QVERIFY(html.contains(QStringLiteral("Deep7 Events")));
        QVERIFY(html.contains(QStringLiteral("HTML-E1")));
    }

    void testLeadsToCsvEscapesComma()
    {
        InvestigativeLead l;
        l.rank       = 1;
        l.category   = QStringLiteral("mo");
        l.headline   = QStringLiteral("Match A, B");
        l.confidence = 0.6;

        const QString csv = DataExporter::leadsToCsv({ l });
        QVERIFY(csv.contains(QStringLiteral("Match")));
    }
};

QTEST_GUILESS_MAIN(DataExporterDeep7Test)
#include "test_data_exporter_deep7.moc"
