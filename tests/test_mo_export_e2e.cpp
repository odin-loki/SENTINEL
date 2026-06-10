// test_mo_export_e2e.cpp — Advanced E2E, MOAnalyser deep tests, DataExporter roundtrip
// Three test classes:
//   TestMOAnalyser        — TF-IDF / cosine similarity deep tests
//   TestExportRoundtrip   — DataExporter JSON/CSV/Markdown roundtrip
//   TestAdvancedEndToEnd  — Full pipeline: CSV import → DB → predict → leads → report

#include <QTest>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <cmath>
#include <algorithm>

#include "inference/MOAnalyser.h"
#include "core/CrimeEvent.h"
#include "core/DataExporter.h"
#include "core/AppConfig.h"
#include "core/Database.h"
#include "models/PoissonBaseline.h"
#include "models/EnsemblePredictor.h"
#include "inference/HintEngine.h"
#include "inference/LeadReportGenerator.h"
#include "benchmark/BenchmarkMetrics.h"
#include "ingest/CsvImporter.h"
#include "audit/ProvenanceLog.h"

// ─────────────────────────────────────────────────────────────────────────────
// Shared helpers
// ─────────────────────────────────────────────────────────────────────────────

namespace {

static MOCaseRecord makeMOCase(const QString& id, const QString& mo,
                                bool resolved = false,
                                const QString& outcome = {},
                                const QString& suspect = {})
{
    MOCaseRecord r;
    r.caseId         = id;
    r.moText         = mo;
    r.resolved       = resolved;
    r.outcome        = outcome;
    r.suspectProfile = suspect;
    return r;
}

static CrimeEvent makeEvent(const QString& id, double lat, double lon,
                             const QString& crimeType = QStringLiteral("burglary"),
                             double quality = 0.9)
{
    CrimeEvent e;
    e.eventId      = e.id = id;
    e.crimeType    = crimeType;
    e.lat          = lat;
    e.lon          = lon;
    e.latitude     = lat;
    e.longitude    = lon;
    e.suburb       = QStringLiteral("TestSuburb");
    e.qualityScore = quality;
    e.occurredAt   = QDateTime(QDate(2025, 1, 15), QTime(10, 0, 0), Qt::UTC);
    e.ingestedAt   = QDateTime(QDate(2025, 1, 15), QTime(10, 0, 0), Qt::UTC);
    return e;
}

static InvestigativeLead makeLead(int rank, const QString& cat,
                                   const QString& headline, double conf)
{
    InvestigativeLead l;
    l.rank             = rank;
    l.category         = cat;
    l.headline         = headline;
    l.confidence       = conf;
    l.detail           = QStringLiteral("Detail for %1").arg(headline);
    l.confidenceMethod = QStringLiteral("test_method");
    l.generatedAt      = QDateTime(QDate(2025, 1, 1), QTime(0, 0, 0), Qt::UTC);
    l.provenance       = {"source_A", "model_B"};
    return l;
}

} // namespace

// ═════════════════════════════════════════════════════════════════════════════
// TASK 1 — MOAnalyser deep tests
// ═════════════════════════════════════════════════════════════════════════════

class TestMOAnalyser : public QObject {
    Q_OBJECT
private slots:
    void testFitAndFindSimilar();
    void testCosineSimilarityEdgeCases();
    void testTFIDFWeighting();
    void testMOAnalyserUnfitted();
};

// ─────────────────────────────────────────────────────────────────────────────
// testFitAndFindSimilar
//
// Build 20 case records: 3 "similar" that share most tokens with the query,
// and 17 "dissimilar" that have completely different vocabularies.
// After fit(), findSimilar(query, 3, 0.0) must return exactly the 3 similar
// cases in the top-3 positions with scores in [0, 1].
// ─────────────────────────────────────────────────────────────────────────────
void TestMOAnalyser::testFitAndFindSimilar()
{
    const QString queryText =
        QStringLiteral("forced entry residential burglary night window");

    // 3 similar records — share most query tokens
    QVector<MOCaseRecord> cases = {
        makeMOCase("SIMILAR-0", "forced entry residential burglary night window glass"),
        makeMOCase("SIMILAR-1", "forced entry residential burglary night window rear door"),
        makeMOCase("SIMILAR-2", "burglary night window forced residential entry solo"),
    };

    // 17 dissimilar records — completely different vocabulary, zero overlap
    const QStringList dissimTexts = {
        "alpha beta gamma delta epsilon",
        "zeta eta theta iota kappa",
        "lambda mu nu xi omicron",
        "pi rho sigma tau upsilon",
        "phi chi psi omega vega",
        "quasar pulsar nebula corona",
        "xenon krypton helium neon argon",
        "cobalt nickel manganese chromium",
        "polygon hexagon octagon decagon",
        "sodium potassium calcium lithium",
        "photon electron proton neutron",
        "antelope gazelle impala kudu",
        "tangerine kumquat papaya guava",
        "tundra steppe savanna taiga",
        "cello viola oboe bassoon",
        "turquoise vermilion cerulean chartreuse",
        "parabola hyperbola ellipse cycloid",
    };
    for (int i = 0; i < dissimTexts.size(); ++i)
        cases.append(makeMOCase(QString("DISSIM-%1").arg(i), dissimTexts[i]));

    QCOMPARE(cases.size(), 20);

    MOAnalyser moAnalyser;
    moAnalyser.fit(cases);
    QVERIFY(moAnalyser.isFitted());
    QCOMPARE(moAnalyser.caseCount(), 20);

    // Request top-3 with no similarity floor (minSimilarity=0.0) so dissimilar
    // records (cosine=0) are still included as candidates; the 3 similar ones
    // must float to the top.
    const auto results = moAnalyser.findSimilar(queryText, 3, 0.0);
    QVERIFY2(results.size() == 3,
             qPrintable(QString("Expected 3 results, got %1").arg(results.size())));

    // Collect returned caseIds
    QStringList returnedIds;
    for (const auto& m : results)
        returnedIds.append(m.caseId);

    QVERIFY2(returnedIds.contains("SIMILAR-0"),
             "SIMILAR-0 not in top-3 results");
    QVERIFY2(returnedIds.contains("SIMILAR-1"),
             "SIMILAR-1 not in top-3 results");
    QVERIFY2(returnedIds.contains("SIMILAR-2"),
             "SIMILAR-2 not in top-3 results");

    // All scores must be in [0, 1]
    for (const auto& m : results) {
        QVERIFY2(m.similarityScore >= 0.0,
                 qPrintable(QString("Score %1 < 0 for %2")
                     .arg(m.similarityScore).arg(m.caseId)));
        QVERIFY2(m.similarityScore <= 1.0,
                 qPrintable(QString("Score %1 > 1 for %2")
                     .arg(m.similarityScore).arg(m.caseId)));
    }

    // Similar cases must score higher than dissimilar ones
    const double minSimilarScore = [&](){
        double mn = 1.0;
        for (const auto& m : results) mn = std::min(mn, m.similarityScore);
        return mn;
    }();
    QVERIFY2(minSimilarScore > 0.0,
             "All top-3 similar records should have similarity > 0");
}

// ─────────────────────────────────────────────────────────────────────────────
// testCosineSimilarityEdgeCases
// ─────────────────────────────────────────────────────────────────────────────
void TestMOAnalyser::testCosineSimilarityEdgeCases()
{
    // ── Case 1: Two identical MO profiles → similarity must be 1.0 ──────────
    {
        const QString text = QStringLiteral("forced entry residential burglary");
        MOAnalyser ma;
        ma.fit({ makeMOCase("ID-A", text), makeMOCase("ID-B", text) });

        const auto results = ma.findSimilar(text, 10, 0.0);
        QVERIFY2(!results.isEmpty(), "Identical query should return matches");
        for (const auto& m : results) {
            QVERIFY2(std::abs(m.similarityScore - 1.0) < 1e-9,
                     qPrintable(QString("Identical profile similarity=%1 (expected 1.0)")
                         .arg(m.similarityScore)));
        }
    }

    // ── Case 2: Completely different MO profiles → similarity < 0.3 ─────────
    {
        MOAnalyser ma;
        ma.fit({
            makeMOCase("SAME", "forced entry residential burglary"),
            makeMOCase("DIFF", "vehicle theft carjacking daytime parking"),
        });

        // Use minSimilarity=0.0 so DIFF is included; its score must be < 0.3
        const auto results = ma.findSimilar(
            QStringLiteral("forced entry residential burglary"), 10, 0.0);

        bool foundDiff = false;
        for (const auto& m : results) {
            if (m.caseId == QLatin1String("DIFF")) {
                foundDiff = true;
                QVERIFY2(m.similarityScore < 0.3,
                         qPrintable(QString("Expected DIFF similarity < 0.3, got %1")
                             .arg(m.similarityScore)));
            }
        }
        // DIFF may have similarity=0 and thus appears in results only if minSim=0
        // Either it's absent (filtered by default threshold) or present with score<0.3
        (void)foundDiff; // acceptable either way
    }

    // ── Case 3: Empty features → no crash, graceful empty result ────────────
    {
        MOAnalyser ma;
        ma.fit({ makeMOCase("EMPTY", "") });
        // Should not crash; score = 0 for empty vocab
        const auto r1 = ma.findSimilar(QStringLiteral("any text here"), 5, 0.0);
        // r1 may be empty or contain one zero-score entry — either is fine
        for (const auto& m : r1)
            QVERIFY(m.similarityScore >= 0.0 && m.similarityScore <= 1.0);

        // Query with empty string should also not crash
        const auto r2 = ma.findSimilar(QString(), 5, 0.0);
        for (const auto& m : r2)
            QVERIFY(m.similarityScore >= 0.0 && m.similarityScore <= 1.0);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// testTFIDFWeighting
//
// "snorkelmask" appears in only 1 of 11 records → high IDF → high weight.
// Querying that rare term should promote the rare record to the top result.
// "burglary" appears in all 11 records → lower IDF → weaker discrimination.
// ─────────────────────────────────────────────────────────────────────────────
void TestMOAnalyser::testTFIDFWeighting()
{
    // 10 records with only common terms
    QVector<MOCaseRecord> cases;
    for (int i = 0; i < 10; ++i)
        cases.append(makeMOCase(QString("COMMON-%1").arg(i),
                                QStringLiteral("burglary forced entry")));

    // 1 record that also contains a rare token "snorkelmask"
    cases.append(makeMOCase("RARE-0", "burglary forced entry snorkelmask"));
    QCOMPARE(cases.size(), 11);

    MOAnalyser ma;
    ma.fit(cases);
    QVERIFY(ma.isFitted());

    // Rare-term query → RARE-0 must be top result
    const auto rareResults = ma.findSimilar(
        QStringLiteral("snorkelmask"), 11, 0.0);

    QVERIFY2(!rareResults.isEmpty(),
             "Rare-term query should return at least one result");
    QCOMPARE(rareResults.first().caseId, QString("RARE-0"));
    QVERIFY2(rareResults.first().similarityScore > 0.0,
             "RARE-0 must have positive score for rare-term query");

    // Common records should have similarity=0 for "snorkelmask" query
    for (int i = 1; i < rareResults.size(); ++i) {
        QVERIFY2(rareResults[i].similarityScore < rareResults[0].similarityScore,
                 "Common records must score lower than rare-term record for rare query");
    }

    // Combined query with rare + common: RARE-0 must still rank first
    // (rare term boosts it above pure common-term records)
    const auto combinedResults = ma.findSimilar(
        QStringLiteral("burglary snorkelmask"), 11, 0.0);
    QVERIFY(!combinedResults.isEmpty());
    QCOMPARE(combinedResults.first().caseId, QString("RARE-0"));
}

// ─────────────────────────────────────────────────────────────────────────────
// testMOAnalyserUnfitted
//
// findSimilar() called without prior fit() must return an empty vector
// and not crash; isFitted() must return false.
// ─────────────────────────────────────────────────────────────────────────────
void TestMOAnalyser::testMOAnalyserUnfitted()
{
    MOAnalyser ma;
    QVERIFY(!ma.isFitted());
    QCOMPARE(ma.caseCount(), 0);

    // Must not crash and must return empty results
    const auto r = ma.findSimilar(
        QStringLiteral("burglary forced entry"), 10, 0.0);
    QVERIFY2(r.isEmpty(),
             "Unfitted analyser must return empty result");
}

// ═════════════════════════════════════════════════════════════════════════════
// TASK 2 — DataExporter roundtrip tests
// ═════════════════════════════════════════════════════════════════════════════

class TestExportRoundtrip : public QObject {
    Q_OBJECT
private slots:
    void testJsonExportRoundtrip();
    void testCsvExportRoundtrip();
    void testLeadsJsonRoundtrip();
    void testMarkdownLeadsFormat();
    void testBenchmarkReportFormat();
};

// ─────────────────────────────────────────────────────────────────────────────
// testJsonExportRoundtrip
// ─────────────────────────────────────────────────────────────────────────────
void TestExportRoundtrip::testJsonExportRoundtrip()
{
    QVector<CrimeEvent> events;
    for (int i = 0; i < 10; ++i) {
        CrimeEvent e = makeEvent(
            QString("EVT-JSON-%1").arg(i),
            51.5 + i * 0.01,
            -0.1 + i * 0.005,
            (i % 2 == 0) ? QStringLiteral("burglary") : QStringLiteral("robbery"),
            0.7 + i * 0.02);
        events.append(e);
    }

    const QJsonArray arr = DataExporter::eventsToJson(events);
    QCOMPARE(arr.size(), 10);

    for (int i = 0; i < 10; ++i) {
        const QJsonObject obj = arr[i].toObject();
        const QString expectedId = QString("EVT-JSON-%1").arg(i);

        QCOMPARE(obj[QStringLiteral("eventId")].toString(), expectedId);

        const QString expectedType =
            (i % 2 == 0) ? QStringLiteral("burglary") : QStringLiteral("robbery");
        QCOMPARE(obj[QStringLiteral("crimeType")].toString(), expectedType);

        QVERIFY2(std::abs(obj[QStringLiteral("lat")].toDouble()
                          - (51.5 + i * 0.01)) < 1e-9,
                 qPrintable(QString("lat mismatch for event %1").arg(i)));
        QVERIFY2(std::abs(obj[QStringLiteral("lon")].toDouble()
                          - (-0.1 + i * 0.005)) < 1e-9,
                 qPrintable(QString("lon mismatch for event %1").arg(i)));

        // "quality" is the JSON key for qualityScore
        QVERIFY2(std::abs(obj[QStringLiteral("quality")].toDouble()
                          - (0.7 + i * 0.02)) < 1e-9,
                 qPrintable(QString("quality mismatch for event %1").arg(i)));
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// testCsvExportRoundtrip
// ─────────────────────────────────────────────────────────────────────────────
void TestExportRoundtrip::testCsvExportRoundtrip()
{
    QVector<CrimeEvent> events;
    for (int i = 0; i < 5; ++i)
        events.append(makeEvent(QString("EVT-CSV-%1").arg(i),
                                51.5 + i * 0.01,
                                -0.1 + i * 0.005));

    const QString csv = DataExporter::eventsToCsv(events);
    QVERIFY(!csv.isEmpty());

    const QStringList lines = csv.split(QLatin1Char('\n'),
                                         Qt::SkipEmptyParts);
    // 1 header + 5 data rows
    QCOMPARE(lines.size(), 6);

    // Verify header
    QVERIFY2(lines[0].startsWith(QLatin1String("event_id")),
             "CSV must start with event_id header");
    const QStringList headerFields =
        lines[0].split(QLatin1Char(','));
    const int nCols = headerFields.size();
    QVERIFY2(nCols >= 5, "Expected at least 5 columns in header");

    // Verify first data row field count matches header
    const QStringList row0Fields = lines[1].split(QLatin1Char(','));
    QCOMPARE(row0Fields.size(), nCols);

    // Verify first row content
    QVERIFY2(lines[1].contains(QStringLiteral("EVT-CSV-0")),
             "First row must contain EVT-CSV-0");
    QVERIFY2(lines[1].contains(QStringLiteral("burglary")),
             "First row must contain crimeType burglary");
    QVERIFY2(lines[1].contains(QStringLiteral("51.500000")),
             "First row must contain lat 51.500000");
}

// ─────────────────────────────────────────────────────────────────────────────
// testLeadsJsonRoundtrip
// ─────────────────────────────────────────────────────────────────────────────
void TestExportRoundtrip::testLeadsJsonRoundtrip()
{
    QVector<InvestigativeLead> leads;
    const QStringList categories = {
        "series_linkage", "mo_similarity", "geographic_profile",
        "network_association", "statistical_anomaly"
    };
    for (int i = 0; i < 5; ++i) {
        leads.append(makeLead(
            i + 1,
            categories[i],
            QString("Lead %1: headline text").arg(i + 1),
            0.5 + i * 0.1));
    }

    const QJsonArray arr = DataExporter::leadsToJson(leads);
    QCOMPARE(arr.size(), 5);

    for (int i = 0; i < 5; ++i) {
        const QJsonObject obj = arr[i].toObject();

        QCOMPARE(obj[QStringLiteral("rank")].toInt(), i + 1);
        QCOMPARE(obj[QStringLiteral("category")].toString(), categories[i]);
        QVERIFY2(obj[QStringLiteral("headline")].toString().contains(
                     QString("Lead %1").arg(i + 1)),
                 "Headline must round-trip through JSON");
        QVERIFY2(std::abs(obj[QStringLiteral("confidence")].toDouble()
                          - (0.5 + i * 0.1)) < 1e-9,
                 qPrintable(QString("confidence mismatch at index %1").arg(i)));

        // Provenance array must be preserved
        const QJsonArray prov = obj[QStringLiteral("provenance")].toArray();
        QCOMPARE(prov.size(), 2);
        QCOMPARE(prov[0].toString(), QStringLiteral("source_A"));
        QCOMPARE(prov[1].toString(), QStringLiteral("model_B"));
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// testMarkdownLeadsFormat
// ─────────────────────────────────────────────────────────────────────────────
void TestExportRoundtrip::testMarkdownLeadsFormat()
{
    QVector<InvestigativeLead> leads;
    leads.append(makeLead(1, "series_linkage",  "Suspect linked to series Alpha",  0.80));
    leads.append(makeLead(2, "mo_similarity",   "MO match: forced entry at night", 0.65));
    leads.append(makeLead(3, "geographic_profile", "Anchor point near Park St",   0.55));

    const QString md =
        DataExporter::leadsToMarkdown(leads, QStringLiteral("Test Report"));

    // Must start with '#'
    QVERIFY2(md.startsWith(QLatin1Char('#')),
             "Markdown must start with '#' heading");

    // Each lead's headline must appear in the output
    QVERIFY2(md.contains(QStringLiteral("Suspect linked to series Alpha")),
             "Lead 1 headline missing from Markdown");
    QVERIFY2(md.contains(QStringLiteral("MO match: forced entry at night")),
             "Lead 2 headline missing from Markdown");
    QVERIFY2(md.contains(QStringLiteral("Anchor point near Park St")),
             "Lead 3 headline missing from Markdown");

    // Confidence values as percentages (leadsToMarkdown uses "XX.X%" format)
    QVERIFY2(md.contains(QLatin1Char('%')),
             "Markdown must contain confidence percentage values");
    QVERIFY2(md.contains(QStringLiteral("80.0%")),
             "80% confidence missing from Markdown (lead 1, conf=0.80)");
    QVERIFY2(md.contains(QStringLiteral("65.0%")),
             "65% confidence missing from Markdown (lead 2, conf=0.65)");
}

// ─────────────────────────────────────────────────────────────────────────────
// testBenchmarkReportFormat
// ─────────────────────────────────────────────────────────────────────────────
void TestExportRoundtrip::testBenchmarkReportFormat()
{
    BenchmarkReport rep;
    rep.nSamples   = 500;
    rep.pai5pct    = 7.2;   // above target 6.0  → PASS
    rep.pai10pct   = 5.1;   // above target 4.5  → PASS
    rep.pai20pct   = 3.5;   // above target 3.0  → PASS
    rep.pei10pct   = 0.72;  // above target 0.6  → PASS
    rep.ser        = 0.55;  // above target 0.4  → PASS
    rep.aucRoc     = 0.88;  // above target 0.85 → PASS
    rep.brierScore = 0.08;  // ≤ target 0.10     → PASS

    const QString md = DataExporter::benchmarkToMarkdown(rep);
    QVERIFY2(!md.isEmpty(), "benchmarkToMarkdown must not return empty string");

    // Must start with the standard header
    QVERIFY2(md.startsWith(QStringLiteral("# SENTINEL Benchmark Report")),
             "Benchmark Markdown must start with standard header");

    // Must contain metric names
    QVERIFY2(md.contains(QStringLiteral("PAI")),
             "Benchmark Markdown missing PAI mention");
    QVERIFY2(md.contains(QStringLiteral("AUC")),
             "Benchmark Markdown missing AUC mention");
    QVERIFY2(md.contains(QStringLiteral("Brier")),
             "Benchmark Markdown missing Brier mention");

    // All metrics pass → output must contain "PASS"
    QVERIFY2(md.contains(QStringLiteral("PASS")),
             "Expected PASS for all above-target metrics");

    // Sample count must appear somewhere
    QVERIFY2(md.contains(QStringLiteral("500")),
             "Sample count 500 must appear in report");
}

// ═════════════════════════════════════════════════════════════════════════════
// TASK 3 — Advanced end-to-end pipeline tests
// ═════════════════════════════════════════════════════════════════════════════

class TestAdvancedEndToEnd : public QObject {
    Q_OBJECT
private slots:
    void testCsvImportToLeadGeneration();
    void testBenchmarkFullPipelineQuality();
    void testProvenanceChainIntegrity();
};

// ─────────────────────────────────────────────────────────────────────────────
// testCsvImportToLeadGeneration
//
// Builds a synthetic 50-row CSV covering 3 lat/lon zones, imports it via
// CsvImporter, inserts events into an in-memory Database, fits a
// PoissonBaseline, wraps it in EnsemblePredictor, finds the highest-risk
// zone, feeds a HintEngineInput with a SeriesMatch and MOMatch into
// HintEngine, generates a LeadReport, and validates the report structure
// including HTML output.
// ─────────────────────────────────────────────────────────────────────────────
void TestAdvancedEndToEnd::testCsvImportToLeadGeneration()
{
    // ── Build synthetic CSV ──────────────────────────────────────────────────
    // Zone A: lat ~51.50 (20 events)   → highest count → highest risk
    // Zone B: lat ~51.60 (15 events)
    // Zone C: lat ~51.40 (15 events)
    // All events on 2025-01-01 (Wednesday) at implicit hour 10:00
    QString csv = QStringLiteral("id,date,crime_type,lat,lon\n");
    for (int i = 0; i < 20; ++i)
        csv += QString("A%1,2025-01-01,burglary,%2,-0.10\n")
                   .arg(i)
                   .arg(51.50 + i * 0.001, 0, 'f', 6);
    for (int i = 0; i < 15; ++i)
        csv += QString("B%1,2025-01-01,burglary,%2,0.00\n")
                   .arg(i)
                   .arg(51.60 + i * 0.001, 0, 'f', 6);
    for (int i = 0; i < 15; ++i)
        csv += QString("C%1,2025-01-01,burglary,%2,-0.20\n")
                   .arg(i)
                   .arg(51.40 + i * 0.001, 0, 'f', 6);

    // Write CSV to a temp file
    QTemporaryFile tmpFile;
    QVERIFY(tmpFile.open());
    tmpFile.write(csv.toUtf8());
    tmpFile.flush();
    tmpFile.close();  // close so CsvImporter can open it read-only

    // ── Import CSV ───────────────────────────────────────────────────────────
    const auto importedEvents =
        CsvImporter::importFile(tmpFile.fileName(), QStringLiteral("test_csv"));

    QVERIFY2(importedEvents.size() == 50,
             qPrintable(QString("Expected 50 imported events, got %1")
                 .arg(importedEvents.size())));

    // ── Insert into in-memory Database ───────────────────────────────────────
    AppConfig cfg;
    cfg.databasePath = QStringLiteral(":memory:");
    Database db(cfg);
    QVERIFY2(db.open(), "In-memory database must open successfully");

    for (const auto& ev : importedEvents)
        db.insertEvent(ev);

    QVERIFY2(db.eventCount() == 50,
             qPrintable(QString("DB must hold 50 events, got %1")
                 .arg(db.eventCount())));

    // ── Fit PoissonBaseline from imported events ──────────────────────────────
    // Derive zone from latitude
    auto zoneFromLat = [](double lat) -> QString {
        if (lat > 51.55) return QStringLiteral("zone_B");
        if (lat < 51.45) return QStringLiteral("zone_C");
        return QStringLiteral("zone_A");
    };

    QVector<PoissonBaseline::EventRecord> poissonRecs;
    for (const auto& ev : importedEvents) {
        PoissonBaseline::EventRecord r;
        r.zoneId     = zoneFromLat(ev.lat.value_or(0.0));
        r.occurredAt = ev.occurredAt.value_or(QDateTime::currentDateTimeUtc());
        r.crimeType  = ev.crimeType;
        poissonRecs.append(r);
    }

    PoissonBaseline poisson;
    poisson.fit(poissonRecs);
    QVERIFY(poisson.isFitted());

    // ── Wrap in EnsemblePredictor ─────────────────────────────────────────────
    EnsemblePredictor ensemble;
    ensemble.setPoisson(&poisson);
    ensemble.setWeights(1.0, 0.0);
    QVERIFY(ensemble.isReady());

    // ── Predict for each zone; find highest-risk zone ─────────────────────────
    const QDateTime predDt(QDate(2025, 1, 1), QTime(10, 0, 0), Qt::UTC);
    const auto predA = ensemble.predict("zone_A", predDt, "burglary", 51.50, -0.10);
    const auto predB = ensemble.predict("zone_B", predDt, "burglary", 51.60,  0.00);
    const auto predC = ensemble.predict("zone_C", predDt, "burglary", 51.40, -0.20);

    // Zone A has the most training events → highest expectedCount
    QString highestZone = "zone_A";
    double  highestCount = predA.expectedCount;
    if (predB.expectedCount > highestCount) {
        highestCount = predB.expectedCount;
        highestZone  = "zone_B";
    }
    if (predC.expectedCount > highestCount) {
        highestCount = predC.expectedCount;
        highestZone  = "zone_C";
    }
    // Zone A has 20 events vs 15 each for B and C → should be highest
    QCOMPARE(highestZone, QString("zone_A"));

    // ── Build HintEngineInput ─────────────────────────────────────────────────
    // Use the first imported event from zone A as the focal event
    CrimeEvent focalEvent = importedEvents[0];

    // Series match
    SeriesMatch sm;
    sm.seriesId        = QStringLiteral("SER-E2E-001");
    sm.memberCount     = 5;
    sm.linkProbability = 0.80;    // > 0.2 threshold → generates a series lead
    sm.compositeScore  = 0.75;
    sm.spatialDistanceM     = 150.0;
    sm.temporalDistanceDays =  3.0;
    sm.moSimilarity         =  0.72;
    sm.method               = QStringLiteral("DBSCAN");

    // MO match via mini MOAnalyser
    MOAnalyser miniMO;
    miniMO.fit({ makeMOCase("MO-E2E-001",
                             "burglary forced entry residential night",
                             true, "convicted", "male 25-35") });
    const auto moMatches = miniMO.findSimilar(
        QStringLiteral("burglary forced entry residential night"), 3, 0.0);

    HintEngineInput input;
    input.event         = focalEvent;
    input.dataQuality   = 0.9;
    input.seriesMatches.append(sm);
    input.moMatches     = moMatches;

    // ── Generate leads ────────────────────────────────────────────────────────
    HintEngine engine;
    const auto leads = engine.generate(input);

    QVERIFY2(leads.size() >= 1,
             qPrintable(QString("Expected >= 1 lead, got %1").arg(leads.size())));

    // ── Generate report ───────────────────────────────────────────────────────
    const LeadReport report =
        LeadReportGenerator::generate(QStringLiteral("CASE-E2E-001"), leads);

    QVERIFY2(report.totalLeads > 0,
             "LeadReport.totalLeads must be > 0");
    QVERIFY2(!report.markdownText.isEmpty(),
             "LeadReport.markdownText must not be empty");
    QVERIFY2(report.markdownText.contains(QStringLiteral("CASE-E2E-001")),
             "Markdown must contain the case ID");

    // ── Validate HTML output ──────────────────────────────────────────────────
    const QString html = LeadReportGenerator::generateHtml(report);
    QVERIFY2(!html.isEmpty(), "generateHtml must not return empty string");
    QVERIFY2(html.contains(QStringLiteral("<!DOCTYPE html>")),
             "HTML must start with DOCTYPE declaration");
    QVERIFY2(html.contains(QStringLiteral("</html>")),
             "HTML must have closing </html> tag");
}

// ─────────────────────────────────────────────────────────────────────────────
// testBenchmarkFullPipelineQuality
//
// Fits PoissonBaseline on 100 zone-A events (no zone-B events).
// Predicts for zone A (high probability) and zone B (near-zero prior).
// Constructs 200 (yTrue, yPred) pairs and verifies that
// BenchmarkMetrics::fullReport() gives nSamples=200 and aucRoc in [0.5, 1.0].
// ─────────────────────────────────────────────────────────────────────────────
void TestAdvancedEndToEnd::testBenchmarkFullPipelineQuality()
{
    // ── 100 zone-A events, all on 2025-01-06 (Monday) at 10:00 ───────────────
    // dow = 0 (Monday), month = 1, hourBin = 5
    const QDateTime trainTime(QDate(2025, 1, 6), QTime(10, 0, 0), Qt::UTC);

    QVector<PoissonBaseline::EventRecord> recs;
    recs.reserve(100);
    for (int i = 0; i < 100; ++i) {
        PoissonBaseline::EventRecord r;
        r.zoneId     = QStringLiteral("bench_zone_A");
        r.occurredAt = trainTime;
        r.crimeType  = QStringLiteral("burglary");
        recs.append(r);
    }

    PoissonBaseline poisson;
    poisson.fit(recs);
    QVERIFY(poisson.isFitted());

    // Wrap in ensemble (Poisson-only)
    EnsemblePredictor ensemble;
    ensemble.setPoisson(&poisson);
    ensemble.setWeights(1.0, 0.0);
    QVERIFY(ensemble.isReady());

    // Predict once for each zone to get representative probabilities
    const auto predA = ensemble.predict(
        QStringLiteral("bench_zone_A"), trainTime, QStringLiteral("burglary"),
        51.50, -0.10);
    const auto predB = ensemble.predict(
        QStringLiteral("bench_zone_B"), trainTime, QStringLiteral("burglary"),
        51.60,  0.00);

    // Zone A should have substantially higher probCrime than zone B
    QVERIFY2(predA.probCrime > predB.probCrime,
             qPrintable(QString("zone A probCrime (%1) should be > zone B (%2)")
                 .arg(predA.probCrime).arg(predB.probCrime)));

    // ── Build 200 (yTrue, yPred) pairs ────────────────────────────────────────
    QVector<double> yTrue(200, 0.0);
    QVector<double> yPred(200, 0.0);

    // First 100: zone A events actually occurred (yTrue = 1)
    for (int i = 0; i < 100; ++i) {
        yTrue[i] = 1.0;
        yPred[i] = predA.probCrime;
    }
    // Next 100: zone B had no crimes (yTrue = 0)
    for (int i = 0; i < 100; ++i) {
        yTrue[100 + i] = 0.0;
        yPred[100 + i] = predB.probCrime;
    }

    // ── Full benchmark report ─────────────────────────────────────────────────
    const BenchmarkReport rep = BenchmarkMetrics::fullReport(yTrue, yPred);

    QCOMPARE(rep.nSamples, 200);

    QVERIFY2(std::isfinite(rep.aucRoc),
             qPrintable(QString("aucRoc is not finite: %1").arg(rep.aucRoc)));
    QVERIFY2(rep.aucRoc >= 0.5,
             qPrintable(QString("aucRoc %1 must be >= 0.5 (better than random)")
                 .arg(rep.aucRoc, 0, 'f', 4)));
    QVERIFY2(rep.aucRoc <= 1.0,
             qPrintable(QString("aucRoc %1 must be <= 1.0").arg(rep.aucRoc)));

    QVERIFY2(std::isfinite(rep.brierScore),
             "Brier score must be finite");
    QVERIFY2(rep.brierScore >= 0.0 && rep.brierScore <= 1.0,
             "Brier score must be in [0, 1]");
}

// ─────────────────────────────────────────────────────────────────────────────
// testProvenanceChainIntegrity
//
// Records ProvenanceLog entries for 5 input events through 3 pipeline stages.
// Generates leads from a HintEngineInput that includes a SeriesMatch and
// AnomalySignal.  Verifies that:
//   • Each lead produced by HintEngine has at least one provenance entry.
//   • ProvenanceLog has entries for each input event ID, and each entry
//     correctly references its own event ID.
// ─────────────────────────────────────────────────────────────────────────────
void TestAdvancedEndToEnd::testProvenanceChainIntegrity()
{
    const QStringList eventIds = {
        "EVT-PROV-001", "EVT-PROV-002", "EVT-PROV-003",
        "EVT-PROV-004", "EVT-PROV-005"
    };

    // ── Record pipeline provenance for each event ─────────────────────────────
    ProvenanceLog log;
    for (const auto& id : eventIds) {
        log.record(id, "ingest",    "csv_import",   "Imported from synthetic CSV");
        log.record(id, "nlp",       "mo_extract",   "MO features extracted");
        log.record(id, "inference", "hint_engine",  "Leads generated");
    }

    // ── Build HintEngineInput with series + anomaly signals ───────────────────
    CrimeEvent focalEvent;
    focalEvent.eventId  = eventIds[0];
    focalEvent.id       = eventIds[0];
    focalEvent.crimeType = QStringLiteral("burglary");
    focalEvent.lat       = 51.5;
    focalEvent.lon       = -0.1;
    focalEvent.ingestedAt = QDateTime::currentDateTimeUtc();

    SeriesMatch sm;
    sm.seriesId        = QStringLiteral("SER-PROV-001");
    sm.memberCount     = 4;
    sm.linkProbability = 0.72;  // > 0.2 → generates a series lead
    sm.compositeScore  = 0.68;
    sm.method          = QStringLiteral("DBSCAN");

    AnomalySignal anomaly;
    anomaly.eventId       = eventIds[0];
    anomaly.isAnomaly     = true;
    anomaly.combinedScore = 0.78;
    anomaly.isolationScore = 0.80;
    anomaly.lofScore       = 0.75;
    anomaly.zScoreTemporal = 2.5;
    anomaly.zScoreSpatial  = 1.8;
    anomaly.signalReasons  = {"temporal_spike", "spatial_outlier"};

    HintEngineInput input;
    input.event         = focalEvent;
    input.dataQuality   = 0.85;
    input.seriesMatches.append(sm);
    input.anomalySignal = anomaly;

    // ── Generate leads ─────────────────────────────────────────────────────────
    HintEngine engine;
    const auto leads = engine.generate(input);

    QVERIFY2(!leads.isEmpty(),
             "HintEngine must produce at least one lead from series + anomaly inputs");

    // ── Verify each lead has at least one provenance entry ────────────────────
    for (const auto& lead : leads) {
        QVERIFY2(!lead.provenance.empty(),
                 qPrintable(QString("Lead '%1' (rank %2) has no provenance entries")
                     .arg(lead.headline).arg(lead.rank)));
    }

    // ── Verify provenance log entries reference the input event IDs ───────────
    for (const auto& id : eventIds) {
        const auto chain = log.chain(id);
        QVERIFY2(chain.size() == 3,
                 qPrintable(QString("Expected 3 provenance entries for %1, got %2")
                     .arg(id).arg(chain.size())));

        // Each entry in the chain must reference the correct event ID
        for (const auto& entry : chain) {
            QCOMPARE(entry.eventId, id);
        }
    }

    // ── Verify chain stage ordering ────────────────────────────────────────────
    const auto chain0 = log.chain(eventIds[0]);
    QCOMPARE(chain0[0].stage, QStringLiteral("ingest"));
    QCOMPARE(chain0[1].stage, QStringLiteral("nlp"));
    QCOMPARE(chain0[2].stage, QStringLiteral("inference"));
}

// ═════════════════════════════════════════════════════════════════════════════
// main
// ═════════════════════════════════════════════════════════════════════════════

static int runTest(QObject* obj, const char* logFile)
{
    QStringList args = {"test", "-o", QString("%1,txt").arg(logFile)};
    return QTest::qExec(obj, args);
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    int r = 0;

    { TestMOAnalyser          t1; r |= runTest(&t1, "mo_analyser.txt");      }
    { TestExportRoundtrip     t2; r |= runTest(&t2, "export_roundtrip.txt"); }
    { TestAdvancedEndToEnd    t3; r |= runTest(&t3, "advanced_e2e.txt");     }

    return r;
}

#include "test_mo_export_e2e.moc"
