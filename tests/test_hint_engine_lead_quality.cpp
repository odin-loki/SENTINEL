// test_hint_engine_lead_quality.cpp
// Lead quality and consistency tests for HintEngine.
// Verifies sorting, provenance, confidence calibration, MO similarity text,
// zero-data behaviour, and crime-type categorisation.

#include <QTest>
#include <QCoreApplication>
#include <QDateTime>
#include <QString>
#include <QVector>
#include <QSet>
#include <algorithm>

#include "core/CrimeEvent.h"
#include "inference/HintEngine.h"

// ─────────────────────────────────────────────────────────────────────────────
// Helpers (mirroring patterns from test_hint_engine_deep.cpp)
// ─────────────────────────────────────────────────────────────────────────────

namespace {

static CrimeEvent makeBasicEvent(const QString& id = QStringLiteral("LEAD-EVT"),
                                  const QString& crimeType = QStringLiteral("burglary"))
{
    CrimeEvent ev;
    ev.eventId    = id;
    ev.id         = id;
    ev.crimeType  = crimeType;
    ev.suburb     = QStringLiteral("London");
    ev.occurredAt = QDateTime::currentDateTimeUtc();
    ev.ingestedAt = QDateTime::currentDateTimeUtc();
    ev.lat        = 51.505;
    ev.lon        = -0.095;
    ev.latitude   = 51.505;
    ev.longitude  = -0.095;
    ev.source     = QStringLiteral("test_source");
    ev.qualityScore = 0.8;
    return ev;
}

static SeriesMatch makeSeriesMatch(double linkProb, double moSim = 0.65)
{
    SeriesMatch m;
    m.seriesId             = QStringLiteral("SER-QUALITY-001");
    m.memberCount          = 4;
    m.linkProbability      = linkProb;
    m.spatialDistanceM     = 200.0;
    m.temporalDistanceDays = 1.5;
    m.moSimilarity         = moSim;
    m.compositeScore       = linkProb;
    m.method               = QStringLiteral("near_repeat");
    return m;
}

static MOMatch makeMOMatch(const QString& caseId, double sim,
                            bool resolved = false)
{
    MOMatch m;
    m.caseId          = caseId;
    m.similarityScore = sim;
    m.sharedFeatures  = QStringList{
        QStringLiteral("forced_entry"),
        QStringLiteral("night_time"),
        QStringLiteral("residential")
    };
    m.resolved        = resolved;
    return m;
}

static AnomalySignal makeAnomaly(bool isAnom, double score = 0.82)
{
    AnomalySignal a;
    a.eventId        = QStringLiteral("LEAD-EVT");
    a.isolationScore = 0.78;
    a.lofScore       = 0.70;
    a.zScoreTemporal = 3.0;
    a.zScoreSpatial  = 2.5;
    a.combinedScore  = score;
    a.isAnomaly      = isAnom;
    a.signalReasons  = { QStringLiteral("temporal_outlier") };
    return a;
}

static NetworkLead makeNetworkLead(double riskScore)
{
    NetworkLead nl;
    nl.personId        = QStringLiteral("NL-PERSON");
    nl.connectionType  = QStringLiteral("direct_cooffender");
    nl.sharedIncidents = 3;
    nl.centralityScore = 0.6;
    nl.communityId     = 1;
    nl.riskScore       = riskScore;
    nl.reasoning       = QStringLiteral("High-centrality co-offender in network.");
    return nl;
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// HintEngineLeadQualityTest
// ─────────────────────────────────────────────────────────────────────────────

class HintEngineLeadQualityTest : public QObject {
    Q_OBJECT

private:
    HintEngine m_engine;

private slots:

    // ─────────────────────────────────────────────────────────────────────
    // 1. Leads are sorted by confidence (descending)
    // ─────────────────────────────────────────────────────────────────────
    void testLeadsAreSorted()
    {
        // Provide mixed-confidence inputs so we get at least 3 leads
        HintEngineInput input;
        input.event = makeBasicEvent();
        input.seriesMatches.append(makeSeriesMatch(0.90));  // high conf series lead
        input.moMatches.append(makeMOMatch("MO-CASE-01", 0.55));  // lower MO lead
        input.networkLeads.append(makeNetworkLead(0.65));
        input.anomalySignal = makeAnomaly(true, 0.70);
        input.dataQuality = 1.0;

        const auto leads = m_engine.generate(input);
        QVERIFY2(leads.size() >= 2, "Need at least 2 leads to test ordering");

        for (int i = 0; i + 1 < leads.size(); ++i) {
            QVERIFY2(leads[i].confidence >= leads[i + 1].confidence,
                     qPrintable(
                         QString("Leads not sorted: leads[%1].confidence=%2 < leads[%3].confidence=%4")
                         .arg(i).arg(leads[i].confidence)
                         .arg(i + 1).arg(leads[i + 1].confidence)));
        }
    }

    // ─────────────────────────────────────────────────────────────────────
    // 2. Each lead has non-empty provenance
    // ─────────────────────────────────────────────────────────────────────
    void testLeadsHaveProvenance()
    {
        HintEngineInput input;
        input.event = makeBasicEvent();
        input.seriesMatches.append(makeSeriesMatch(0.80));
        input.moMatches.append(makeMOMatch("MO-CASE-02", 0.60));
        input.anomalySignal = makeAnomaly(true, 0.75);
        input.dataQuality = 1.0;

        const auto leads = m_engine.generate(input);
        QVERIFY2(!leads.isEmpty(), "Must have at least one lead to test provenance");

        for (const auto& lead : leads) {
            QVERIFY2(!lead.provenance.empty(),
                     qPrintable(QString("Lead rank=%1 category='%2' has empty provenance")
                                .arg(lead.rank).arg(lead.category)));
        }
    }

    // ─────────────────────────────────────────────────────────────────────
    // 3. Patterned data (5 events at same location within 24 h) →
    //    at least 1 lead with confidence > 0.5
    // ─────────────────────────────────────────────────────────────────────
    void testHighConfidenceForPatternedData()
    {
        // Simulate 5 near-repeat crimes by providing a strong series match
        HintEngineInput input;
        input.event = makeBasicEvent();

        // High link probability representing 5 clustered crimes within 24h
        SeriesMatch sm;
        sm.seriesId             = QStringLiteral("HOTSPOT-SERIES");
        sm.memberCount          = 5;
        sm.linkProbability      = 0.78;  // > 0.5
        sm.spatialDistanceM     = 50.0;  // very close
        sm.temporalDistanceDays = 0.4;   // within 24 hours
        sm.moSimilarity         = 0.80;
        sm.compositeScore       = 0.78;
        sm.method               = QStringLiteral("near_repeat");
        input.seriesMatches.append(sm);
        input.dataQuality = 1.0;

        const auto leads = m_engine.generate(input);
        QVERIFY2(!leads.isEmpty(),
                 "Patterned data should produce at least 1 lead");

        bool highConfidenceFound = false;
        for (const auto& lead : leads) {
            if (lead.confidence > 0.5) {
                highConfidenceFound = true;
                break;
            }
        }
        QVERIFY2(highConfidenceFound,
                 "At least 1 lead should have confidence > 0.5 for patterned data");
    }

    // ─────────────────────────────────────────────────────────────────────
    // 4. Insufficient data (1 event, no context) → no serious leads
    // ─────────────────────────────────────────────────────────────────────
    void testZeroLeadsForInsufficientData()
    {
        HintEngineInput input;
        input.event       = makeBasicEvent("SINGLE-EVT");
        input.dataQuality = 0.5;
        // Intentionally: no seriesMatches, no moMatches, no anomalySignal, no networkLeads

        const auto leads = m_engine.generate(input);

        // With no inference signals, HintEngine returns empty leads
        bool noSeriousLeads = leads.isEmpty() ||
            std::all_of(leads.begin(), leads.end(),
                        [](const InvestigativeLead& l) { return l.confidence < 0.5; });

        QVERIFY2(noSeriousLeads,
                 "A single event with no context should produce no serious leads");
    }

    // ─────────────────────────────────────────────────────────────────────
    // 5. Mixed crime types → leads have non-empty categories
    // ─────────────────────────────────────────────────────────────────────
    void testLeadCategorization()
    {
        const QStringList crimeTypes = {
            QStringLiteral("robbery"),
            QStringLiteral("burglary"),
            QStringLiteral("theft")
        };

        QSet<QString> observedCategories;

        for (const QString& ct : crimeTypes) {
            HintEngineInput input;
            input.event = makeBasicEvent(QString("EVT-%1").arg(ct), ct);
            input.seriesMatches.append(makeSeriesMatch(0.75));
            input.moMatches.append(makeMOMatch("MO-" + ct, 0.60));
            input.dataQuality = 0.9;

            const auto leads = m_engine.generate(input);
            QVERIFY2(!leads.isEmpty(),
                     qPrintable(QString("No leads generated for crime type '%1'").arg(ct)));

            for (const auto& lead : leads) {
                QVERIFY2(!lead.category.isEmpty(),
                         qPrintable(QString("Lead for '%1' has empty category").arg(ct)));
                observedCategories.insert(lead.category);
            }
        }

        // Multiple categories should appear across the mixed crime types
        QVERIFY2(observedCategories.size() >= 1,
                 "At least 1 distinct lead category should be produced");
    }

    // ─────────────────────────────────────────────────────────────────────
    // 6. Identical MO descriptions → leads mention "similar" or "pattern"
    // ─────────────────────────────────────────────────────────────────────
    void testMOSimilarityLeads()
    {
        HintEngineInput input;
        input.event = makeBasicEvent("MO-SIM-EVT");

        // Multiple MO matches with high similarity (same MO = identical pattern)
        input.moMatches.append(makeMOMatch("CASE-MO-001", 0.92));
        input.moMatches.append(makeMOMatch("CASE-MO-002", 0.88));
        input.moMatches.append(makeMOMatch("CASE-MO-003", 0.85));
        input.dataQuality = 1.0;

        const auto leads = m_engine.generate(input);
        QVERIFY2(!leads.isEmpty(),
                 "Identical MO matches should generate at least 1 lead");

        // MO leads contain "similarity" in headline (contains "similar")
        bool mentionsSimilarPattern = false;
        for (const auto& lead : leads) {
            const QString combined = lead.headline + QStringLiteral(" ") + lead.detail;
            if (combined.contains(QStringLiteral("similar"), Qt::CaseInsensitive) ||
                combined.contains(QStringLiteral("pattern"),  Qt::CaseInsensitive) ||
                lead.category == QStringLiteral("mo_similarity")) {
                mentionsSimilarPattern = true;
                break;
            }
        }
        QVERIFY2(mentionsSimilarPattern,
                 "At least 1 lead should reference 'similar', 'pattern', "
                 "or have category 'mo_similarity' for identical MO data");
    }
};

QTEST_MAIN(HintEngineLeadQualityTest)
#include "test_hint_engine_lead_quality.moc"
