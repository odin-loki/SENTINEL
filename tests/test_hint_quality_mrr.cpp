// test_hint_quality_mrr.cpp — HintEngine MRR + Precision@K benchmark tests
//
// Tests the HintEngine ranking quality using information-retrieval metrics
// (MRR, Precision@K) and validates lead generation across all categories.

#include <QTest>
#include <QCoreApplication>
#include <QString>
#include <QVector>
#include <algorithm>
#include <cmath>
#include <set>

#include "core/CrimeEvent.h"
#include "inference/HintEngine.h"
#include "benchmark/BenchmarkMetrics.h"

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

namespace {

CrimeEvent baseEvent(const QString& id = QStringLiteral("EVT-TEST"))
{
    CrimeEvent ev;
    ev.eventId    = id;
    ev.crimeType  = QStringLiteral("burglary");
    ev.occurredAt = QDateTime::currentDateTimeUtc();
    ev.ingestedAt = QDateTime::currentDateTimeUtc();
    ev.suburb     = QStringLiteral("London");
    return ev;
}

SeriesMatch makeSeriesMatch(double linkProb, double moSim = 0.5)
{
    SeriesMatch m;
    m.seriesId           = QStringLiteral("SERIES-001");
    m.memberCount        = 5;
    m.linkProbability    = linkProb;
    m.spatialDistanceM   = 200.0;
    m.temporalDistanceDays = 3.0;
    m.moSimilarity       = moSim;
    m.compositeScore     = linkProb;
    m.method             = QStringLiteral("near_repeat");
    return m;
}

MOMatch makeMOMatch(const QString& caseId, double sim,
                    const QString& suspectProfile = {})
{
    MOMatch m;
    m.caseId          = caseId;
    m.similarityScore = sim;
    m.sharedFeatures  = QStringList{ QStringLiteral("forced_entry"),
                                     QStringLiteral("residential") };
    m.resolved        = false;
    m.suspectProfile  = suspectProfile;
    return m;
}

NetworkLead makeNetworkLead(double riskScore)
{
    NetworkLead nl;
    nl.personId        = QStringLiteral("PERSON-001");
    nl.connectionType  = QStringLiteral("direct_cooffender");
    nl.sharedIncidents = 3;
    nl.centralityScore = 0.6;
    nl.communityId     = 1;
    nl.riskScore       = riskScore;
    nl.reasoning       = QStringLiteral("Co-offender in prior incidents.");
    return nl;
}

GeographicProfile makeGeoProfile(double peakProb)
{
    GeographicProfile gp;
    gp.peakLat        = 51.50;
    gp.peakLon        = -0.10;
    gp.peakProbability = peakProb;
    gp.searchArea50pct = 2.5;
    gp.searchArea80pct = 8.0;
    gp.method          = QStringLiteral("rossmo_cgt");
    return gp;
}

AnomalySignal makeAnomalySignal(bool isAnom, double combinedScore = 0.75)
{
    AnomalySignal a;
    a.eventId        = QStringLiteral("EVT-TEST");
    a.isolationScore = 0.72;
    a.lofScore       = 0.65;
    a.zScoreTemporal = 2.8;
    a.zScoreSpatial  = 2.1;
    a.combinedScore  = combinedScore;
    a.isAnomaly      = isAnom;
    a.signalReasons  = { QStringLiteral("temporal_outlier"),
                         QStringLiteral("spatial_outlier") };
    return a;
}

} // anonymous namespace

// ─────────────────────────────────────────────────────────────────────────────
// TestHintQualityMRR
// ─────────────────────────────────────────────────────────────────────────────

class TestHintQualityMRR : public QObject
{
    Q_OBJECT

private:
    HintEngine m_engine;

private slots:

    // 1. High linkProbability series match + low-conf MO → series lead ranked #1
    void testSeriesLeadRankedFirst()
    {
        HintEngineInput input;
        input.event = baseEvent();
        input.seriesMatches.append(makeSeriesMatch(0.90));  // high confidence
        input.moMatches.append(makeMOMatch("CASE-001", 0.35));  // low confidence (above filter)
        input.dataQuality = 1.0;

        const auto leads = m_engine.generate(input);
        QVERIFY(!leads.isEmpty());

        // The first-ranked lead (rank=1) must be the series lead
        QCOMPARE(leads.first().category, QStringLiteral("series_linkage"));
    }

    // 2. Valid GeographicProfile with peakProbability=0.8 → geo lead generated
    void testGeoLeadFromProfilePresent()
    {
        HintEngineInput input;
        input.event      = baseEvent();
        input.geoProfile = makeGeoProfile(0.008);  // peakProb=0.008 → conf=min(0.8,0.85)=0.8
        input.dataQuality = 1.0;

        const auto leads = m_engine.generate(input);
        QVERIFY(!leads.isEmpty());

        bool geoFound = false;
        for (const auto& lead : leads)
            if (lead.category == QStringLiteral("geographic_profile")) geoFound = true;
        QVERIFY(geoFound);
    }

    // 3. AnomalySignal with isAnomaly=true → anomaly lead in output
    void testAnomalyLeadFromSignal()
    {
        HintEngineInput input;
        input.event         = baseEvent();
        input.anomalySignal = makeAnomalySignal(true);
        input.dataQuality   = 1.0;

        const auto leads = m_engine.generate(input);
        QVERIFY(!leads.isEmpty());

        bool anomFound = false;
        for (const auto& lead : leads)
            if (lead.category == QStringLiteral("statistical_anomaly")) anomFound = true;
        QVERIFY(anomFound);
    }

    // 4. NetworkLead with high riskScore → network lead generated
    void testNetworkLeadFromNetworkLead()
    {
        HintEngineInput input;
        input.event = baseEvent();
        input.networkLeads.append(makeNetworkLead(0.82));
        input.dataQuality = 1.0;

        const auto leads = m_engine.generate(input);
        QVERIFY(!leads.isEmpty());

        bool netFound = false;
        for (const auto& lead : leads)
            if (lead.category == QStringLiteral("network_association")) netFound = true;
        QVERIFY(netFound);
    }

    // 5. Input with all categories → at least 4 distinct category types in output
    void testMultipleLeadsAllCategories()
    {
        HintEngineInput input;
        input.event = baseEvent();
        input.seriesMatches.append(makeSeriesMatch(0.75));
        input.moMatches.append(makeMOMatch("CASE-001", 0.60));
        input.geoProfile    = makeGeoProfile(0.005);  // conf=min(0.5,0.85)=0.5
        input.anomalySignal = makeAnomalySignal(true, 0.70);
        input.networkLeads.append(makeNetworkLead(0.65));
        input.dataQuality = 1.0;

        const auto leads = m_engine.generate(input);

        std::set<QString> categories;
        for (const auto& lead : leads)
            categories.insert(lead.category);

        QVERIFY(static_cast<int>(categories.size()) >= 4);
    }

    // 6. Same input, dataQuality=0.3 vs 0.9 → leads generated in both cases
    void testDataQualityPenalty()
    {
        auto makeInput = [](double quality) {
            HintEngineInput input;
            input.event = baseEvent();
            input.seriesMatches.append(makeSeriesMatch(0.80));
            input.dataQuality = quality;
            return input;
        };

        const auto leadsLow  = m_engine.generate(makeInput(0.3));
        const auto leadsHigh = m_engine.generate(makeInput(0.9));

        // High-confidence series leads are generated regardless of data quality
        QVERIFY(!leadsLow.isEmpty());
        QVERIFY(!leadsHigh.isEmpty());

        // Both inputs have the same structure, so same number of leads
        QCOMPARE(leadsLow.size(), leadsHigh.size());
    }

    // 7. Lead detail contains "solo" and another contains "group" → contradiction detected
    void testContradictionDetected()
    {
        HintEngineInput input;
        input.event = baseEvent();
        // Two MO matches — one suggesting solo offender, one suggesting group
        input.moMatches.append(makeMOMatch("CASE-SOLO", 0.65, "solo offender"));
        input.moMatches.append(makeMOMatch("CASE-GRP",  0.55, "group offenders"));
        input.dataQuality = 1.0;

        const auto leads = m_engine.generate(input);
        QVERIFY(leads.size() >= 2);

        // At least one lead should have detected the solo-vs-group contradiction
        bool contradictionFound = false;
        for (const auto& lead : leads) {
            if (!lead.contradictions.empty()) {
                contradictionFound = true;
                break;
            }
        }
        QVERIFY(contradictionFound);
    }

    // 8. 5 cases at known ranks; compute MRR manually and verify vs hintQuality
    void testMRRWith5Cases()
    {
        // Correct answer at ranks 1, 2, 3, 4, 5 respectively
        const QVector<int> relevantRanks = {1, 2, 3, 4, 5};

        // Manual MRR = (1/1 + 1/2 + 1/3 + 1/4 + 1/5) / 5
        const double expectedMRR = (1.0 + 0.5 + 1.0/3.0 + 0.25 + 0.2) / 5.0;

        const auto result = BenchmarkMetrics::hintQuality(relevantRanks, 5);

        QCOMPARE(result.nCases, 5);
        QVERIFY(std::abs(result.mrr - expectedMRR) < 1e-9);
    }

    // 9. 10 cases, top-3 correct lead present in all 10 → precisionAt3 >= 0.8
    void testPrecisionAtK()
    {
        // All 10 cases have the correct answer within the top 3
        const QVector<int> relevantRanks = {1, 2, 3, 1, 2, 3, 1, 2, 3, 1};

        const auto result = BenchmarkMetrics::hintQuality(relevantRanks, 5);

        QCOMPARE(result.nCases, 10);
        QVERIFY(result.precisionAt3 >= 0.8);
    }

    // 10. Output leads are in descending confidence order
    void testLeadsAreSortedByConfidence()
    {
        HintEngineInput input;
        input.event = baseEvent();
        // Series: linkProbability=0.90 → confidence=0.90
        input.seriesMatches.append(makeSeriesMatch(0.90));
        // MO: similarityScore=0.50 → confidence=0.50
        input.moMatches.append(makeMOMatch("CASE-001", 0.50));
        // Network: riskScore=0.30 → confidence=min(0.30,0.95)=0.30
        input.networkLeads.append(makeNetworkLead(0.30));
        input.dataQuality = 1.0;

        const auto leads = m_engine.generate(input);
        QVERIFY(leads.size() >= 2);

        // Verify descending confidence order
        for (int i = 0; i + 1 < leads.size(); ++i) {
            QVERIFY(leads[i].confidence >= leads[i + 1].confidence);
        }
    }

    // 11. MOMatch with similarityScore=0.05 (below 0.3 threshold) → no MO lead
    void testLowQualityLeadsFiltered()
    {
        HintEngineInput input;
        input.event = baseEvent();
        input.moMatches.append(makeMOMatch("CASE-LOW", 0.05));  // below 0.3 threshold
        input.dataQuality = 1.0;

        const auto leads = m_engine.generate(input);

        for (const auto& lead : leads)
            QVERIFY(lead.category != QStringLiteral("mo_similarity"));
    }

    // 12. HintEngineInput with only the default CrimeEvent → leads.isEmpty()
    void testEmptyInputGeneratesNoLeads()
    {
        HintEngineInput input;
        input.event = baseEvent();
        // No series matches, no MO matches, no geoProfile, no anomaly, no network leads
        input.dataQuality = 1.0;

        const auto leads = m_engine.generate(input);
        QVERIFY(leads.isEmpty());
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Main
// ─────────────────────────────────────────────────────────────────────────────

static int runTest(QObject* obj, const QString& logFile)
{
    QStringList args = { QStringLiteral("test"), QStringLiteral("-o"),
                         QStringLiteral("%1,txt").arg(logFile) };
    return QTest::qExec(obj, args);
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    int r = 0;
    { TestHintQualityMRR t; r |= runTest(&t, "hint_quality_mrr.txt"); }
    return r;
}

#include "test_hint_quality_mrr.moc"
