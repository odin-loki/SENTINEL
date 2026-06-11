#include <QTest>
#include <algorithm>

#include "inference/HintEngine.h"
#include "core/CrimeEvent.h"

static CrimeEvent makeEvent()
{
    CrimeEvent ev;
    ev.eventId   = QStringLiteral("EV1");
    ev.crimeType = QStringLiteral("robbery");
    ev.suburb    = QStringLiteral("Camden");
    ev.latitude  = 51.5074;
    ev.longitude = -0.1278;
    return ev;
}

static SeriesMatch makeSeries(const QString& id, double prob = 0.75)
{
    SeriesMatch sm;
    sm.seriesId             = id;
    sm.memberCount          = 3;
    sm.linkProbability      = prob;
    sm.compositeScore       = prob;
    sm.spatialDistanceM     = 200.0;
    sm.temporalDistanceDays = 2.0;
    sm.moSimilarity         = 0.8;
    sm.method               = QStringLiteral("composite");
    return sm;
}

static MOMatch makeMO(const QString& caseId, double score,
                       const QStringList& features,
                       const QString& suspectProfile = {})
{
    MOMatch m;
    m.caseId          = caseId;
    m.similarityScore = score;
    m.sharedFeatures  = features;
    m.suspectProfile  = suspectProfile;
    return m;
}

class TestHintEngineDeep2 : public QObject
{
    Q_OBJECT
private slots:
    void testLeadsGeneratedFromCrimeEvents();
    void testRankingOrderedByConfidence();
    void testDeduplicationIdenticalEvents();
    void testContradictionDetected();
    void testLeadCountBounded();
};

// A HintEngineInput with one series match and one MO match must produce at least
// one lead from each respective category.
void TestHintEngineDeep2::testLeadsGeneratedFromCrimeEvents()
{
    HintEngine he;
    HintEngineInput in;
    in.event       = makeEvent();
    in.dataQuality = 0.9;
    in.seriesMatches.append(makeSeries(QStringLiteral("S001"), 0.85));
    in.moMatches.append(makeMO(QStringLiteral("C001"), 0.70,
                               {QStringLiteral("forced_entry"),
                                QStringLiteral("night_time")}));

    const auto leads = he.generate(in);
    QVERIFY2(!leads.isEmpty(), "Series + MO input must generate at least one lead");

    const bool hasSeriesLead = std::any_of(leads.begin(), leads.end(),
        [](const InvestigativeLead& l){
            return l.category == QStringLiteral("series_linkage");
        });
    const bool hasMOLead = std::any_of(leads.begin(), leads.end(),
        [](const InvestigativeLead& l){
            return l.category == QStringLiteral("mo_similarity");
        });

    QVERIFY2(hasSeriesLead, "Series match must produce a series_linkage lead");
    QVERIFY2(hasMOLead,     "MO match must produce a mo_similarity lead");
}

// After generate(), rerankLeads() assigns rank = 1, 2, 3, ... in descending
// rankScore order.  Verify sequential ranks AND that the first lead has at
// least as high a confidence as the last (rankScore is dominated by confidence).
void TestHintEngineDeep2::testRankingOrderedByConfidence()
{
    HintEngine he;
    HintEngineInput in;
    in.event       = makeEvent();
    in.dataQuality = 0.95;
    in.seriesMatches.append(makeSeries(QStringLiteral("S_HIGH"), 0.90));
    in.seriesMatches.append(makeSeries(QStringLiteral("S_LOW"),  0.35));

    const auto leads = he.generate(in);
    QVERIFY2(leads.size() >= 2, "Need at least two leads to verify ordering");

    for (int i = 0; i < leads.size(); ++i) {
        QVERIFY2(leads[i].rank == i + 1,
                 qPrintable(QString("Lead at index %1: expected rank %2, got %3")
                    .arg(i).arg(i + 1).arg(leads[i].rank)));
    }

    QVERIFY2(leads.first().confidence >= leads.last().confidence,
             qPrintable(QString("First lead confidence %1 must be >= last lead %2")
                .arg(leads.first().confidence).arg(leads.last().confidence)));
}

// Two identical SeriesMatch objects produce two identical headlines; the
// deduplication pass inside generate() must keep only the first and discard
// the second.  The result must therefore contain exactly one series_linkage lead.
void TestHintEngineDeep2::testDeduplicationIdenticalEvents()
{
    HintEngine he;
    HintEngineInput in;
    in.event       = makeEvent();
    in.dataQuality = 0.9;

    const SeriesMatch dup = makeSeries(QStringLiteral("SERIES_DUP"), 0.80);
    in.seriesMatches.append(dup);
    in.seriesMatches.append(dup);   // exact duplicate

    const auto leads = he.generate(in);

    int seriesCount = 0;
    for (const auto& l : leads) {
        if (l.category == QStringLiteral("series_linkage"))
            ++seriesCount;
    }

    QVERIFY2(seriesCount == 1,
             qPrintable(QString("Duplicate series match must yield exactly 1 lead, "
                                "got %1").arg(seriesCount)));
}

// A "solo_offender" MO lead and a "group_offender" MO lead carry "solo" and
// "group" respectively in their detail text.  detectContradictions() must
// flag at least one contradiction in the output.
void TestHintEngineDeep2::testContradictionDetected()
{
    HintEngine he;
    HintEngineInput in;
    in.event       = makeEvent();
    in.dataQuality = 0.9;

    in.moMatches.append(makeMO(QStringLiteral("C_SOLO"),  0.80,
        {QStringLiteral("solo_offender"), QStringLiteral("night")},
        QStringLiteral("solo male")));
    in.moMatches.append(makeMO(QStringLiteral("C_GROUP"), 0.75,
        {QStringLiteral("group_offender"), QStringLiteral("afternoon")},
        QStringLiteral("group operation")));

    const auto leads = he.generate(in);

    const bool anyContradiction = std::any_of(leads.begin(), leads.end(),
        [](const InvestigativeLead& l){
            return !l.contradictions.empty();
        });

    QVERIFY2(anyContradiction,
             "Solo-offender vs group-offender MO leads must produce a contradiction");
}

// Feed 60 series + 20 network leads (well above kMaxLeads=50) and verify the
// output is capped at kMaxLeads without crashing.
void TestHintEngineDeep2::testLeadCountBounded()
{
    HintEngine he;
    HintEngineInput in;
    in.event       = makeEvent();
    in.dataQuality = 0.9;

    for (int i = 0; i < 60; ++i)
        in.seriesMatches.append(makeSeries(QString("S%1").arg(i), 0.7));

    for (int i = 0; i < 20; ++i) {
        NetworkLead nl;
        nl.personId        = QString("P%1").arg(i);
        nl.connectionType  = QStringLiteral("direct_participant");
        nl.sharedIncidents = 1;
        nl.centralityScore = 0.3;
        nl.communityId     = 0;
        nl.riskScore       = 0.5;
        nl.reasoning       = QStringLiteral("test");
        in.networkLeads.append(nl);
    }

    const auto leads = he.generate(in);

    QVERIFY2(leads.size() <= HintEngine::kMaxLeads,
             qPrintable(QString("Lead count %1 must be <= kMaxLeads (%2)")
                .arg(leads.size()).arg(HintEngine::kMaxLeads)));
}

QTEST_GUILESS_MAIN(TestHintEngineDeep2)
#include "test_hint_engine_deep2.moc"
