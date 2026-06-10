// test_missing_coverage.cpp — Edge-case tests for paths not covered elsewhere
#include <QTest>
#include <QCoreApplication>
#include <QTemporaryFile>
#include <QTextStream>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimeZone>
#include <cmath>

#include "ingest/CsvImporter.h"
#include "ingest/DataQualityScorer.h"
#include "models/PoissonBaseline.h"
#include "models/SeriesDetector.h"
#include "inference/LeadReportGenerator.h"
#include "core/DataExporter.h"
#include "core/CrimeEvent.h"

class TestMissingCoverage : public QObject
{
    Q_OBJECT

private:
    static QString writeTmpCsv(const QString& content)
    {
        auto* tmp = new QTemporaryFile();
        tmp->setAutoRemove(false);
        if (!tmp->open()) return {};
        QTextStream out(tmp);
        out.setEncoding(QStringConverter::Utf8);
        out << content;
        tmp->close();
        QString path = tmp->fileName();
        delete tmp;
        return path;
    }

    static InvestigativeLead makeLead(int rank, const QString& headline, double confidence)
    {
        InvestigativeLead l;
        l.rank             = rank;
        l.headline         = headline;
        l.detail           = QStringLiteral("Detail for ") + headline;
        l.confidence       = confidence;
        l.category         = QStringLiteral("test");
        l.confidenceMethod = QStringLiteral("test");
        return l;
    }

private slots:

    // ── 1. Progress callback receives monotonically non-decreasing done values ─

    void testCsvImporterProgressCallback()
    {
        QString rows;
        for (int i = 0; i < 600; ++i)
            rows += QString("E%1,2024-01-15,burglary,51.5,0.1\n").arg(i);
        QString path = writeTmpCsv("id,date,crime_type,lat,lon\n" + rows);

        QVector<int> doneValues;
        CsvImporter::importFile(path, "test", [&](int done, int) {
            doneValues.append(done);
        });

        QVERIFY2(doneValues.size() >= 2, "Progress callback must fire at least twice for 600 rows");
        for (int i = 1; i < doneValues.size(); ++i)
            QVERIFY2(doneValues[i] >= doneValues[i - 1],
                     "Progress 'done' values must be non-decreasing");
        // Final reported done should equal the number of imported events
        auto events = CsvImporter::importFile(path, "test");
        QVERIFY(doneValues.last() <= events.size() + 1);

        QFile::remove(path);
    }

    // ── 2. Full UK Police format import ───────────────────────────────────────

    void testCsvImporterUKPoliceFormat()
    {
        // UK Police open-data column layout
        QString content =
            "Crime ID,Month,Crime type,Latitude,Longitude,Last outcome category\n"
            "abc123def456,2024-01,Burglary,51.5074,-0.1278,"
            "Investigation complete; no suspect identified\n"
            "xyz789ghi012,2024-01,Vehicle crime,51.5100,-0.1300,"
            "Unable to prosecute suspect\n";
        QString path = writeTmpCsv(content);

        auto events = CsvImporter::importFile(path, "uk_police");
        QVERIFY2(events.size() >= 1, "At least one event must be imported from UK Police CSV");

        bool foundBurglary = false;
        for (const auto& ev : std::as_const(events))
            if (ev.crimeType.contains("burglary", Qt::CaseInsensitive)) {
                foundBurglary = true;
                QCOMPARE(ev.source, QStringLiteral("uk_police"));
                break;
            }
        QVERIFY2(foundBurglary, "Burglary event must be detected in UK Police import");

        QFile::remove(path);
    }

    // ── 3. Event with only required fields → low composite score ─────────────

    void testDataQualityScorerMissingAllOptionals()
    {
        CrimeEvent e;
        e.eventId = "MINIMAL001";
        e.id      = "MINIMAL001";
        // No lat/lon, no occurredAt, no narrative, no suburb, no crimeType

        DataQualityScorer scorer;
        auto report = scorer.score(e);

        QVERIFY2(report.compositeScore < 0.5,
                 "Event missing all optional fields must score below 0.5");
    }

    // ── 4. Event with all fields → high composite score, not quarantined ──────

    void testDataQualityScorerPerfectEvent()
    {
        CrimeEvent e;
        e.eventId    = "PERFECT001";
        e.id         = "PERFECT001";
        e.crimeType  = "burglary";
        e.suburb     = "TestZone";
        e.lat        = 51.5074;
        e.lon        = -0.1278;
        e.latitude   = 51.5074;
        e.longitude  = -0.1278;
        e.occurredAt = QDateTime(QDate(2024, 1, 15), QTime(22, 30, 0), QTimeZone::utc());
        e.narrative  = "Forced entry via rear window during the night";
        e.outcome    = "Under investigation";
        e.source     = "uk_police_v1";

        QMap<QString, double> relMap;
        relMap["uk_police_v1"] = 0.95;
        DataQualityScorer scorer(relMap);
        auto report = scorer.score(e);

        QVERIFY2(report.compositeScore >= 0.6,
                 "Fully-populated event must score >= 0.6");
        QVERIFY2(!report.quarantined,
                 "Fully-populated event must not be quarantined");
        QVERIFY2(report.compositeScore > 0.0 && report.compositeScore <= 1.0,
                 "Composite score must be in [0,1]");
    }

    // ── 5. Negative binomial CI wider than Poisson CI for the same mean λ ─────

    void testPoissonBaselineNegBinomial()
    {
        // Poisson with lambda=5: 90 % CI
        double pLo = PoissonBaseline::poissonPPF(5.0, 0.05);
        double pHi = PoissonBaseline::poissonPPF(5.0, 0.95);
        double poissonWidth = pHi - pLo;

        // NegBin with mean=5, r=2: mean = r*p/(1-p)  →  p = 5/(r+5) = 5/7
        double r    = 2.0;
        double p_nb = 5.0 / (r + 5.0);
        double nbLo = PoissonBaseline::negBinPPF(r, p_nb, 0.05);
        double nbHi = PoissonBaseline::negBinPPF(r, p_nb, 0.95);
        double nbWidth = nbHi - nbLo;

        QVERIFY2(nbWidth > poissonWidth,
                 "NegBin CI must be wider than Poisson CI for same mean (overdispersion)");
    }

    // ── 6. poissonPMF with lambda=0: P(0)=1, P(k>0)=0 ────────────────────────

    void testPoissonBaselineZeroLambda()
    {
        double pmf0 = PoissonBaseline::poissonPMF(0.0, 0);
        double pmf1 = PoissonBaseline::poissonPMF(0.0, 1);
        double pmf5 = PoissonBaseline::poissonPMF(0.0, 5);

        QVERIFY2(std::abs(pmf0 - 1.0) < 1e-9,
                 "poissonPMF(0,0) must equal 1.0");
        QVERIFY2(pmf1 < 1e-9,
                 "poissonPMF(0,1) must be 0 — no event possible when lambda=0");
        QVERIFY2(pmf5 < 1e-9,
                 "poissonPMF(0,5) must be 0 — no event possible when lambda=0");
    }

    // ── 7. 2 events < minSamples=3 threshold → no series formed ──────────────

    void testSeriesDetectorMinEventThreshold()
    {
        SeriesDetector detector(1.0, 14.0, 3); // epsKm=1.0, epsDays=14, minSamples=3

        SeriesEvent e1, e2;
        e1.eventId   = "E001"; e1.lat = 51.5; e1.lon = -0.1; e1.tDays = 0.0;
        e1.crimeType = "burglary"; e1.moText = "forced entry";
        e2.eventId   = "E002"; e2.lat = 51.5001; e2.lon = -0.1001; e2.tDays = 1.0;
        e2.crimeType = "burglary"; e2.moText = "forced entry";

        auto series = detector.detectSeries({e1, e2});
        QCOMPARE(series.size(), 0);
    }

    // ── 8. generate() Markdown output contains lead confidence score ──────────

    void testLeadReportGeneratorMarkdown()
    {
        QVector<InvestigativeLead> leads = {
            makeLead(1, "Suspect seen near warehouse", 0.85),
        };
        LeadReport report = LeadReportGenerator::generate("CASE-MD", leads);

        QVERIFY2(!report.markdownText.isEmpty(),
                 "Markdown must not be empty");
        QVERIFY2(report.markdownText.contains("85"),
                 "Markdown must contain the confidence score (85)");
        QVERIFY2(report.markdownText.contains("CASE-MD"),
                 "Markdown must contain the case ID");
    }

    // ── 9. generateHtml() output contains <table tag ──────────────────────────

    void testLeadReportGeneratorHtml()
    {
        QVector<InvestigativeLead> leads = {
            makeLead(1, "Break-in pattern detected", 0.75),
        };
        LeadReport report = LeadReportGenerator::generate("CASE-HTML2", leads);
        QString html = LeadReportGenerator::generateHtml(report);

        QVERIFY2(!html.isEmpty(), "HTML must not be empty");
        QVERIFY2(html.contains("<table"),
                 "HTML must contain a <table> element");
    }

    // ── 10. leadsToJson() produces valid, round-trippable JSON ────────────────

    void testLeadReportGeneratorJson()
    {
        QVector<InvestigativeLead> leads = {
            makeLead(1, "Alpha lead", 0.88),
            makeLead(2, "Beta lead",  0.65),
        };

        QJsonArray arr = DataExporter::leadsToJson(leads);
        QCOMPARE(arr.size(), 2);

        // Verify the array serialises to non-empty JSON text
        QJsonDocument doc(arr);
        QByteArray jsonBytes = doc.toJson(QJsonDocument::Compact);
        QVERIFY2(!jsonBytes.isEmpty(), "JSON bytes must not be empty");
        QVERIFY2(jsonBytes.startsWith('['), "JSON output must be an array");

        // Round-trip: parse back and check structure
        QJsonDocument parsed = QJsonDocument::fromJson(jsonBytes);
        QVERIFY2(!parsed.isNull(), "Round-tripped JSON must parse successfully");
        QVERIFY2(parsed.isArray(), "Round-tripped JSON must be an array");
        QCOMPARE(parsed.array().size(), 2);

        // Each element must have at least a 'headline' field
        for (const QJsonValue& v : parsed.array()) {
            QVERIFY2(v.isObject(), "Each JSON lead must be an object");
            QVERIFY2(v.toObject().contains("headline"),
                     "Each JSON lead must have a 'headline' field");
        }
    }
};

QTEST_MAIN(TestMissingCoverage)
#include "test_missing_coverage.moc"
