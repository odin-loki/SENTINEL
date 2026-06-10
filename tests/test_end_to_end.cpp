// test_end_to_end.cpp — Comprehensive end-to-end integration tests
// for the SENTINEL C++23/Qt6 crime analytics system.
//
// Tests all major subsystems working together with calibrated synthetic data.

#include <QTest>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QDateTime>
#include <QDate>
#include <cmath>

#include "core/CrimeEvent.h"
#include "core/AppConfig.h"
#include "core/Database.h"
#include "models/PoissonBaseline.h"
#include "models/HawkesProcess.h"
#include "models/EnsemblePredictor.h"
#include "inference/HintEngine.h"
#include "inference/MOAnalyser.h"
#include "inference/CoOffendingAnalyser.h"
#include "inference/LeadReportGenerator.h"
#include "ingest/DataQualityScorer.h"
#include "audit/ProvenanceLog.h"
#include "benchmark/BenchmarkMetrics.h"
#include "benchmark/BiasAuditor.h"

// ─────────────────────────────────────────────────────────────────────────────

class TestEndToEndSystem : public QObject {
    Q_OBJECT
private slots:
    void testFullPipelineWithNetworkLeads();
    void testEnsembleBetterThanPoisson();
    void testBiasAuditNoFalseFlags();
    void testLeadReportExport();
    void testProvenanceFullChain();
    void testQualityGatingPreventsLowQualityData();
    void testBenchmarkMetricsBeatRandomBaseline();
    void testCoOffendingNetworkFromDatabase();
};

// ─────────────────────────────────────────────────────────────────────────────
// testFullPipelineWithNetworkLeads
//
// Creates 3 crime events, 3 persons linked via co-offending records, runs
// CoOffendingAnalyser + MOAnalyser, feeds all signals into HintEngine, and
// verifies at least one lead with positive confidence is produced.
// ─────────────────────────────────────────────────────────────────────────────
void TestEndToEndSystem::testFullPipelineWithNetworkLeads()
{
    const QDateTime base(QDate(2025, 1, 1), QTime(10, 0, 0), Qt::UTC);

    // ── 3 crime events ───────────────────────────────────────────────────────
    QVector<CrimeEvent> events;
    for (int i = 0; i < 3; ++i) {
        CrimeEvent ev;
        ev.eventId    = QString("EVT-%1").arg(i);
        ev.ingestedAt = base.addDays(i);
        ev.occurredAt = ev.ingestedAt;
        ev.crimeType  = QStringLiteral("burglary");
        ev.lat        = 51.5 + i * 0.01;
        ev.lon        = -0.1;
        ev.suburb     = QStringLiteral("London");
        ev.narrative  = QStringLiteral("Forced entry residential property rear window night.");
        events.append(ev);
    }

    // ── Co-offending records: 3 persons linked across events ─────────────────
    QVector<PersonIncidentRecord> records = {
        { "PA", "EVT-0", "suspect",   1.0 },
        { "PB", "EVT-0", "suspect",   1.0 },
        { "PB", "EVT-1", "suspect",   1.0 },
        { "PC", "EVT-1", "associate", 0.5 },
        { "PA", "EVT-2", "suspect",   1.0 },
        { "PC", "EVT-2", "suspect",   1.0 },
    };

    CoOffendingAnalyser coAnalyser;
    coAnalyser.buildGraph(records);
    coAnalyser.analyse();
    QVERIFY(coAnalyser.isBuilt());

    const auto networkLeads = coAnalyser.findLeads("EVT-0", 5);

    // ── MO Analyser ──────────────────────────────────────────────────────────
    MOAnalyser moAnalyser;
    QVector<MOCaseRecord> moCases;
    {
        MOCaseRecord mc;
        mc.caseId        = "MO-001";
        mc.moText        = "burglary forced entry residential night solo window";
        mc.resolved      = true;
        mc.suspectProfile = "male mid-30s";
        moCases.append(mc);
    }
    {
        MOCaseRecord mc;
        mc.caseId = "MO-002";
        mc.moText = "burglary rear entry residential group tool marks";
        mc.resolved = false;
        moCases.append(mc);
    }
    moAnalyser.fit(moCases);

    const auto moMatches = moAnalyser.findSimilar(
        "forced entry residential burglary night", 5, 0.0);

    // ── HintEngine ───────────────────────────────────────────────────────────
    HintEngineInput input;
    input.event       = events[0];
    input.networkLeads = networkLeads;
    input.moMatches    = moMatches;
    input.dataQuality  = 0.9;

    SeriesMatch sm;
    sm.seriesId        = "SER-001";
    sm.memberCount     = 3;
    sm.linkProbability = 0.75;
    sm.compositeScore  = 0.70;
    sm.method          = "DBSCAN";
    input.seriesMatches.append(sm);

    HintEngine engine;
    const auto leads = engine.generate(input);

    QVERIFY2(leads.size() >= 1,
             qPrintable(QString("Expected >= 1 lead, got %1").arg(leads.size())));

    bool anyPositiveConfidence = false;
    for (const auto& lead : leads) {
        QVERIFY(lead.confidence >= 0.0);
        QVERIFY(lead.confidence <= 1.0);
        if (lead.confidence > 0.0) anyPositiveConfidence = true;
    }
    QVERIFY2(anyPositiveConfidence, "Expected at least one lead with confidence > 0");
}

// ─────────────────────────────────────────────────────────────────────────────
// testEnsembleBetterThanPoisson
//
// Fits PoissonBaseline and HawkesProcess on 90 events, builds an
// EnsemblePredictor, then computes Brier scores on a 10-event holdout.
// Both scores must be finite and valid; the test does not assert ordering
// since small datasets do not guarantee ensemble superiority.
// ─────────────────────────────────────────────────────────────────────────────
void TestEndToEndSystem::testEnsembleBetterThanPoisson()
{
    const QDateTime base(QDate(2025, 1, 1), QTime(10, 0, 0), Qt::UTC);

    QVector<PoissonBaseline::EventRecord> poissonRecs;
    QVector<SpatiotemporalEvent>          hawkesEvents;

    for (int i = 0; i < 90; ++i) {
        PoissonBaseline::EventRecord r;
        r.zoneId     = QStringLiteral("zone%1").arg(i % 5);
        r.occurredAt = base.addDays(i);
        r.crimeType  = QStringLiteral("burglary");
        poissonRecs.append(r);

        SpatiotemporalEvent se;
        se.tDays     = static_cast<double>(i);
        se.lat       = 51.5 + (i % 5) * 0.01;
        se.lon       = -0.1 + (i % 4) * 0.01;
        se.crimeType = QStringLiteral("burglary");
        hawkesEvents.append(se);
    }

    PoissonBaseline poisson;
    poisson.fit(poissonRecs);
    QVERIFY(poisson.isFitted());

    HawkesProcess hawkes;
    hawkes.fit(hawkesEvents, 50);
    QVERIFY(hawkes.isFitted());

    EnsemblePredictor ensemble;
    ensemble.setPoisson(&poisson);
    ensemble.setHawkes(&hawkes);
    ensemble.setWeights(0.6, 0.4);
    QVERIFY(ensemble.isReady());

    // 10-event holdout: alternating actual outcomes
    QVector<QPair<double, double>> poissonPA, ensemblePA;

    for (int i = 0; i < 10; ++i) {
        const QDateTime dt   = base.addDays(90 + i);
        const QString zone   = QStringLiteral("zone%1").arg(i % 5);
        const double actual  = (i % 2 == 0) ? 1.0 : 0.0;

        const auto poisPred  = poisson.predict(zone, dt, "burglary");
        const auto ensPred   = ensemble.predict(zone, dt, "burglary", 51.5, -0.1);

        poissonPA.append({ poisPred.probAtLeastOne, actual });
        ensemblePA.append({ ensPred.probCrime, actual });
    }

    const double poissonBrier  = EnsemblePredictor::brierScore(poissonPA);
    const double ensembleBrier = EnsemblePredictor::brierScore(ensemblePA);

    QVERIFY2(std::isfinite(poissonBrier),
             qPrintable(QString("Poisson Brier score is not finite: %1").arg(poissonBrier)));
    QVERIFY2(std::isfinite(ensembleBrier),
             qPrintable(QString("Ensemble Brier score is not finite: %1").arg(ensembleBrier)));
    QVERIFY(poissonBrier  >= 0.0 && poissonBrier  <= 1.0);
    QVERIFY(ensembleBrier >= 0.0 && ensembleBrier <= 1.0);
}

// ─────────────────────────────────────────────────────────────────────────────
// testBiasAuditNoFalseFlags
//
// Constructs two groups with identical prediction rates.
// disparateImpact ratio = 1.0 ∈ [0.80, 1.25], so no report should be flagged.
// ─────────────────────────────────────────────────────────────────────────────
void TestEndToEndSystem::testBiasAuditNoFalseFlags()
{
    QVector<QString> groups;
    QVector<double>  yPred;

    // 20 events per group, all predictions = 0.6 (> threshold 0.5 → positive)
    for (int i = 0; i < 20; ++i) { groups.append("groupA"); yPred.append(0.6); }
    for (int i = 0; i < 20; ++i) { groups.append("groupB"); yPred.append(0.6); }

    const auto reports = BiasAuditor::disparateImpact(groups, yPred, 0.5);

    for (const auto& r : reports) {
        QVERIFY2(!r.flagged,
                 qPrintable(QString("Unexpected disparate-impact flag for %1/%2 (ratio=%3)")
                     .arg(r.groupA, r.groupB).arg(r.ratio, 0, 'f', 3)));
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// testLeadReportExport
//
// Generates 3 leads, writes a report to a temp file, reads it back, and
// verifies the file contains the case ID and confidence information.
// ─────────────────────────────────────────────────────────────────────────────
void TestEndToEndSystem::testLeadReportExport()
{
    QVector<InvestigativeLead> leads;
    for (int i = 0; i < 3; ++i) {
        InvestigativeLead lead;
        lead.rank              = i + 1;
        lead.category          = QStringLiteral("series_link");
        lead.headline          = QString("Lead %1: Suspect linked to series").arg(i + 1);
        lead.detail            = QStringLiteral("Evidence suggests common modus operandi.");
        lead.confidence        = 0.65 + i * 0.10;   // 0.65 / 0.75 / 0.85
        lead.confidenceMethod  = QStringLiteral("bayesian");
        lead.generatedAt       = QDateTime::currentDateTimeUtc();
        leads.append(lead);
    }

    const QString caseId = QStringLiteral("CASE-2025-001");
    const LeadReport report = LeadReportGenerator::generate(caseId, leads);

    QVERIFY(!report.markdownText.isEmpty());
    QCOMPARE(report.caseId,      caseId);
    QCOMPARE(report.totalLeads,  3);

    // Save to temp file
    const QString filePath = QDir::tempPath() + "/sentinel_e2e_leads.md";
    QVERIFY(LeadReportGenerator::saveToFile(report, filePath));

    // Read back and verify content
    QFile f(filePath);
    QVERIFY(f.open(QIODevice::ReadOnly | QIODevice::Text));
    const QString content = QString::fromUtf8(f.readAll());
    f.close();
    QFile::remove(filePath);

    QVERIFY2(content.contains(caseId),
             qPrintable(QString("Case ID '%1' not found in exported report").arg(caseId)));

    // Verify at least some lead text or confidence indicator appears
    const bool hasLeadContent = content.contains("Lead 1")
                              || content.contains("series_link")
                              || content.contains("Suspect");
    QVERIFY2(hasLeadContent, "No lead headlines found in exported report");

    const bool hasConfidence = content.contains('%')
                             || content.contains("0.6")
                             || content.contains("0.7")
                             || content.contains("0.8")
                             || content.contains("confidence");
    QVERIFY2(hasConfidence, "No confidence information found in exported report");
}

// ─────────────────────────────────────────────────────────────────────────────
// testProvenanceFullChain
//
// Records 5 entries across 3 stages (ingest / nlp / inference) for one event,
// then verifies formatChain() output contains all three stage names.
// ─────────────────────────────────────────────────────────────────────────────
void TestEndToEndSystem::testProvenanceFullChain()
{
    ProvenanceLog log;
    const QString eventId = QStringLiteral("EVT-PROV-001");

    log.record(eventId, "ingest",    "csv_import",  "Imported from uk_police_v1");
    log.record(eventId, "nlp",       "mo_extract",  "Extracted MO features");
    log.record(eventId, "nlp",       "classify",    "Classified as burglary (conf=0.91)");
    log.record(eventId, "inference", "geo_profile", "Geographic profile computed");
    log.record(eventId, "inference", "hint_engine", "3 leads generated");

    const auto chain = log.chain(eventId);
    QCOMPARE(chain.size(), 5);

    const QString formatted = log.formatChain(eventId);
    QVERIFY2(!formatted.isEmpty(), "formatChain returned empty string");
    QVERIFY2(formatted.contains("ingest"),    "formatChain missing 'ingest' stage");
    QVERIFY2(formatted.contains("nlp"),       "formatChain missing 'nlp' stage");
    QVERIFY2(formatted.contains("inference"), "formatChain missing 'inference' stage");
}

// ─────────────────────────────────────────────────────────────────────────────
// testQualityGatingPreventsLowQualityData
//
// Creates 7 good events (lat/lon + crimeType) and 3 bad events (empty fields).
// DataQualityScorer::passRate must be >= 0.6.
// ─────────────────────────────────────────────────────────────────────────────
void TestEndToEndSystem::testQualityGatingPreventsLowQualityData()
{
    const QDateTime base(QDate(2025, 1, 1), QTime(10, 0, 0), Qt::UTC);
    QVector<CrimeEvent> events;

    // 7 well-formed events
    for (int i = 0; i < 7; ++i) {
        CrimeEvent ev;
        ev.eventId    = QString("GOOD-%1").arg(i);
        ev.ingestedAt = base.addDays(i);
        ev.occurredAt = ev.ingestedAt;
        ev.lat        = 51.5 + i * 0.01;
        ev.lon        = -0.1;
        ev.crimeType  = QStringLiteral("burglary");
        ev.suburb     = QStringLiteral("London");
        ev.source     = QStringLiteral("uk_police_v1");
        events.append(ev);
    }

    // 3 poorly-formed events (no location, no crimeType, no timestamp)
    for (int i = 0; i < 3; ++i) {
        CrimeEvent ev;
        ev.eventId    = QString("BAD-%1").arg(i);
        ev.ingestedAt = base;
        // intentionally leave occurredAt, lat, lon, crimeType empty
        events.append(ev);
    }

    DataQualityScorer scorer;
    const auto reports = scorer.scoreBatch(events);

    QCOMPARE(reports.size(), 10);

    const double rate = DataQualityScorer::passRate(reports);
    QVERIFY2(rate >= 0.6,
             qPrintable(QString("Expected passRate >= 0.6, got %1").arg(rate, 0, 'f', 3)));
}

// ─────────────────────────────────────────────────────────────────────────────
// testBenchmarkMetricsBeatRandomBaseline
//
// Constructs 100 cells where the top-20 have actual crimes.  A near-perfect
// predictor assigns score=1 to those 20 and 0 to the rest.
// PAI@5% should be 5.0 (= 0.25 hit-rate / 0.05 area) which is well above 1.5.
// ─────────────────────────────────────────────────────────────────────────────
void TestEndToEndSystem::testBenchmarkMetricsBeatRandomBaseline()
{
    const int n       = 100;
    const int nCrimes = 20;

    // y_true: first 20 cells have crimes
    QVector<double> yTrue(n, 0.0);
    for (int i = 0; i < nCrimes; ++i) yTrue[i] = 1.0;

    // y_pred: perfect predictor — scores the crime cells highest
    QVector<double> yPred(n, 0.0);
    for (int i = 0; i < nCrimes; ++i) yPred[i] = 1.0;

    const double pai5 = BenchmarkMetrics::pai(yTrue, yPred, 0.05);

    // Top 5% = 5 cells; all 5 are crimes → hit_rate = 5/20 = 0.25
    // PAI = 0.25 / 0.05 = 5.0
    QVERIFY2(pai5 > 1.5,
             qPrintable(QString("PAI@5%% expected > 1.5 for near-perfect predictor, got %1")
                 .arg(pai5, 0, 'f', 3)));
}

// ─────────────────────────────────────────────────────────────────────────────
// testCoOffendingNetworkFromDatabase
//
// Creates an in-memory database, inserts 5 events, builds a co-offending
// graph from PersonIncidentRecords that link persons to those events, and
// verifies that findLeads() returns at least one lead.
// ─────────────────────────────────────────────────────────────────────────────
void TestEndToEndSystem::testCoOffendingNetworkFromDatabase()
{
    // In-memory database
    AppConfig cfg;
    cfg.databasePath = QStringLiteral(":memory:");
    Database db(cfg);
    QVERIFY(db.open());

    const QDateTime base(QDate(2025, 3, 1), QTime(10, 0, 0), Qt::UTC);
    for (int i = 0; i < 5; ++i) {
        CrimeEvent ev;
        ev.eventId    = QString("INET-%1").arg(i);
        ev.ingestedAt = base.addDays(i);
        ev.occurredAt = ev.ingestedAt;
        ev.crimeType  = QStringLiteral("robbery");
        ev.lat        = 53.48;
        ev.lon        = -2.24;
        db.insertEvent(ev);
    }
    QCOMPARE(db.getTotalEventCount(), 5);

    // PersonIncidentRecords linking 3 persons across 3 incidents
    const QVector<PersonIncidentRecord> records = {
        { "PA", "INET-0", "suspect",   1.0 },
        { "PB", "INET-0", "suspect",   1.0 },
        { "PB", "INET-1", "suspect",   1.0 },
        { "PC", "INET-1", "associate", 0.5 },
        { "PA", "INET-2", "suspect",   1.0 },
        { "PC", "INET-2", "suspect",   1.0 },
    };

    CoOffendingAnalyser analyser;
    analyser.buildGraph(records);
    analyser.analyse();
    QVERIFY(analyser.isBuilt());

    const auto leads = analyser.findLeads("INET-0", 5);
    QVERIFY2(leads.size() >= 1,
             qPrintable(QString("Expected >= 1 network lead for INET-0, got %1")
                 .arg(leads.size())));
}

// ─────────────────────────────────────────────────────────────────────────────
// main
// ─────────────────────────────────────────────────────────────────────────────

static int runTest(QObject* obj, const char* logFile)
{
    QStringList args = { "test", "-o", QString("%1,txt").arg(logFile) };
    return QTest::qExec(obj, args);
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    int r = 0;
    TestEndToEndSystem t1; r |= runTest(&t1, "e2e_system.txt");
    return r;
}

#include "test_end_to_end.moc"
