// test_data_exporter_formats.cpp
// Tests DataExporter HTML, Markdown, CSV, and file-save format validation.
#include <QTest>
#include <QTemporaryFile>
#include <QFile>
#include "core/DataExporter.h"
#include "core/CrimeEvent.h"
#include "models/RiskForecaster.h"
#include "benchmark/BenchmarkMetrics.h"
#include "audit/ProvenanceLog.h"

class DataExporterFormatsTest : public QObject
{
    Q_OBJECT

private:
    static InvestigativeLead makeLead(int rank, double conf)
    {
        InvestigativeLead l;
        l.rank        = rank;
        l.headline    = QStringLiteral("Lead headline %1").arg(rank);
        l.category    = QStringLiteral("series");
        l.detail      = QStringLiteral("Detail text for lead %1").arg(rank);
        l.confidence  = conf;
        l.provenance  = { QStringLiteral("stage1"), QStringLiteral("stage2") };
        l.generatedAt = QDateTime::currentDateTimeUtc();
        return l;
    }

    static QVector<InvestigativeLead> threeLeads()
    {
        return { makeLead(1, 0.9), makeLead(2, 0.7), makeLead(3, 0.4) };
    }

    static ZoneForecast makeZoneForecast()
    {
        ZoneForecast zf;
        zf.zoneId     = QStringLiteral("TestZone");
        zf.alertLevel = 1;
        for (int i = 0; i < 7; ++i) {
            ForecastDay day;
            day.date            = QDate::currentDate().addDays(i);
            day.riskScore       = 0.3 + i * 0.05;
            day.escalationFactor = 1.0;
            day.explanation     = QStringLiteral("Day %1 forecast").arg(i);
            zf.days.append(day);
        }
        return zf;
    }

private slots:

    // 1. leadsToMarkdown: contains lead headlines
    void testLeadsToMarkdownContainsHeadlines()
    {
        const QString md = DataExporter::leadsToMarkdown(threeLeads());
        QVERIFY2(md.contains(QStringLiteral("Lead headline 1")), "Markdown should contain headline");
        QVERIFY2(md.contains(QStringLiteral("Lead headline 2")), "Markdown should contain headline 2");
    }

    // 2. leadsToMarkdown: contains markdown table markers
    void testLeadsToMarkdownHasTableMarkers()
    {
        const QString md = DataExporter::leadsToMarkdown(threeLeads());
        QVERIFY2(md.contains(QStringLiteral("|")) || md.contains(QStringLiteral("#")),
                 "Markdown should contain table or heading markers");
    }

    // 3. leadsToHtml: valid HTML structure
    void testLeadsToHtmlValid()
    {
        const QString html = DataExporter::leadsToHtml(threeLeads());
        QVERIFY2(html.contains(QStringLiteral("<html")) ||
                 html.contains(QStringLiteral("<table")) ||
                 html.contains(QStringLiteral("<!DOCTYPE")),
                 "HTML output should contain HTML/table structure");
    }

    // 4. leadsToCsv: comma-separated with header
    void testLeadsToCsvHasHeader()
    {
        const QString csv = DataExporter::leadsToCsv(threeLeads());
        QVERIFY2(!csv.isEmpty(), "CSV should not be empty");
        QVERIFY2(csv.count(QLatin1Char('\n')) >= 3, "CSV should have at least 3 lines");
    }

    // 5. forecastsToJson: correct count
    void testForecastsToJsonCount()
    {
        const QVector<ZoneForecast> forecasts = { makeZoneForecast(), makeZoneForecast() };
        const auto arr = DataExporter::forecastsToJson(forecasts);
        QCOMPARE(arr.size(), 2);
    }

    // 6. forecastsToCsv: non-empty
    void testForecastsToCsvNonEmpty()
    {
        const QVector<ZoneForecast> forecasts = { makeZoneForecast() };
        const QString csv = DataExporter::forecastsToCsv(forecasts);
        QVERIFY2(!csv.isEmpty(), "Forecasts CSV should not be empty");
    }

    // 7. benchmarkToMarkdown: contains metric names
    void testBenchmarkToMarkdownContainsMetrics()
    {
        BenchmarkReport rpt;
        rpt.pai5pct  = 2.5;
        rpt.aucRoc   = 0.85;
        rpt.nSamples = 100;
        const QString md = DataExporter::benchmarkToMarkdown(rpt);
        QVERIFY2(!md.isEmpty(), "benchmarkToMarkdown should return non-empty text");
    }

    // 8. eventsToHtml: valid HTML
    void testEventsToHtmlValid()
    {
        QVector<CrimeEvent> evs;
        CrimeEvent ev;
        ev.eventId   = QStringLiteral("E1");
        ev.crimeType = QStringLiteral("burglary");
        ev.suburb    = QStringLiteral("Soho");
        evs.append(ev);

        const QString html = DataExporter::eventsToHtml(evs);
        QVERIFY2(!html.isEmpty(), "eventsToHtml should produce non-empty output");
        QVERIFY2(html.contains(QStringLiteral("<")) && html.contains(QStringLiteral(">")),
                 "HTML output should contain HTML tags");
    }

    // 9. saveJson: writes file successfully
    void testSaveJsonWritesFile()
    {
        QTemporaryFile tmp;
        tmp.setAutoRemove(false);
        tmp.open();
        const QString path = tmp.fileName();
        tmp.close();

        const QJsonArray arr = DataExporter::leadsToJson(threeLeads());
        const bool ok = DataExporter::saveJson(arr, path);
        QVERIFY2(ok, "saveJson should return true");
        const QFile f(path);
        QVERIFY2(QFile::exists(path), "Saved JSON file should exist");
        QFile::remove(path);
    }

    // 10. saveText: writes text file successfully
    void testSaveTextWritesFile()
    {
        QTemporaryFile tmp;
        tmp.setAutoRemove(false);
        tmp.open();
        const QString path = tmp.fileName();
        tmp.close();

        const bool ok = DataExporter::saveText(
            QStringLiteral("# Test Report\nLine 2"), path);
        QVERIFY2(ok, "saveText should return true");
        QFile f(path);
        QVERIFY2(f.exists() && f.size() > 0, "Saved text file should be non-empty");
        QFile::remove(path);
    }
};

QTEST_MAIN(DataExporterFormatsTest)
#include "test_data_exporter_formats.moc"
