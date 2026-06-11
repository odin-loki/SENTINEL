// test_hint_engine_deep3.cpp
// Deep audit of HintEngine: sorting, maxLeads, empty input, contradiction
// detection (same-category confidence delta), and deduplication.

#include <QTest>
#include <algorithm>

#include "inference/HintEngine.h"
#include "core/CrimeEvent.h"

// ─── Helpers ─────────────────────────────────────────────────────────────────

static CrimeEvent makeEvent(const QString& id = QStringLiteral("EV1"),
                             const QString& suburb = QStringLiteral("Suburb"))
{
    CrimeEvent ev;
    ev.eventId   = id;
    ev.crimeType = QStringLiteral("robbery");
    ev.suburb    = suburb;
    ev.latitude  = -33.8688;
    ev.longitude = 151.2093;
    return ev;
}

static SeriesMatch makeSeries(const QString& id, double prob)
{
    SeriesMatch sm;
    sm.seriesId             = id;
    sm.memberCount          = 4;
    sm.linkProbability      = prob;
    sm.compositeScore       = prob;
    sm.spatialDistanceM     = 300.0;
    sm.temporalDistanceDays = 1.5;
    sm.moSimilarity         = 0.7;
    sm.method               = QStringLiteral("composite");
    return sm;
}

static MOMatch makeMO(const QString& caseId, double score,
                       const QStringList& features = {})
{
    MOMatch m;
    m.caseId          = caseId;
    m.similarityScore = score;
    m.sharedFeatures  = features;
    return m;
}

// ─── Test class ───────────────────────────────────────────────────────────────

class TestHintEngineDeep3 : public QObject
{
    Q_OBJECT

private slots:
    void testLeadsSortedByConfidenceDescending();
    void testGenerateReturnsAtMostMaxLeads();
    void testEmptyInputNoCrash();
    void testContradictionSameCategoryHighLowConfidence();
    void testDeduplicationSameCategoryMergesLeads();
};

// ─── Test implementations ────────────────────────────────────────────────────

// generate() must return leads sorted by confidence descending.
// After rerankLeads(), leads[i].confidence >= leads[i+1].confidence for all i.
void TestHintEngineDeep3::testLeadsSortedByConfidenceDescending()
{
    HintEngine he;
    HintEngineInput in;
    in.event       = makeEvent();
    in.dataQuality = 0.95;

    // Three series matches with distinct probabilities
    in.seriesMatches.append(makeSeries(QStringLiteral("S_LOW"),  0.30));
    in.seriesMatches.append(makeSeries(QStringLiteral("S_HIGH"), 0.90));
    in.seriesMatches.append(makeSeries(QStringLiteral("S_MID"),  0.60));

    const auto leads = he.generate(in);
    QVERIFY2(leads.size() >= 3, "Need at least 3 leads to verify ordering");

    for (int i = 0; i + 1 < leads.size(); ++i) {
        QVERIFY2(leads[i].confidence >= leads[i + 1].confidence,
                 qPrintable(QString("Lead[%1].confidence (%2) < Lead[%3].confidence (%4) — "
                                    "leads must be sorted descending by confidence")
                            .arg(i).arg(leads[i].confidence)
                            .arg(i + 1).arg(leads[i + 1].confidence)));
    }
}

// generate() must never return more than kMaxLeads entries even when the
// raw pool is far larger.
void TestHintEngineDeep3::testGenerateReturnsAtMostMaxLeads()
{
    HintEngine he;
    HintEngineInput in;
    in.event       = makeEvent();
    in.dataQuality = 0.8;

    // 80 series matches — well above kMaxLeads = 50
    for (int i = 0; i < 80; ++i)
        in.seriesMatches.append(makeSeries(QString("S%1").arg(i), 0.25 + (i % 5) * 0.1));

    const auto leads = he.generate(in);
    QVERIFY2(leads.size() <= HintEngine::kMaxLeads,
             qPrintable(QString("generate() returned %1 leads; must be <= kMaxLeads (%2)")
                        .arg(leads.size()).arg(HintEngine::kMaxLeads)));
}

// An empty HintEngineInput must produce an empty leads vector — no crash.
void TestHintEngineDeep3::testEmptyInputNoCrash()
{
    HintEngine he;
    HintEngineInput in;
    in.event       = makeEvent();
    in.dataQuality = 1.0;
    // seriesMatches, moMatches, networkLeads all empty; no geoProfile or
    // anomalySignal set

    // Must not crash and must return an empty vector
    const QVector<InvestigativeLead> leads = he.generate(in);
    QVERIFY2(leads.isEmpty(),
             "Empty HintEngineInput must produce an empty leads vector");
}

// Two leads in the same category (series_linkage) with a confidence delta
// exceeding 0.5 must each have at least one contradiction flagged after
// the category-level contradiction detection pass.
void TestHintEngineDeep3::testContradictionSameCategoryHighLowConfidence()
{
    HintEngine he;
    HintEngineInput in;
    in.event       = makeEvent();
    in.dataQuality = 0.9;

    // Same category "series_linkage", confidence 0.90 and 0.25 → delta = 0.65 > 0.5
    in.seriesMatches.append(makeSeries(QStringLiteral("S_STRONG"), 0.90));
    in.seriesMatches.append(makeSeries(QStringLiteral("S_WEAK"),   0.25));

    const auto leads = he.generate(in);
    QVERIFY2(leads.size() >= 2, "Need at least 2 leads for contradiction check");

    const bool anyContradiction = std::any_of(leads.begin(), leads.end(),
        [](const InvestigativeLead& l) {
            return !l.contradictions.empty();
        });

    QVERIFY2(anyContradiction,
             "Two series leads with confidence delta > 0.5 must produce "
             "at least one contradiction flag");
}

// Two identical MOMatch entries produce two identical headlines; the
// deduplication pass in generate() must collapse them into exactly one
// mo_similarity lead.
void TestHintEngineDeep3::testDeduplicationSameCategoryMergesLeads()
{
    HintEngine he;
    HintEngineInput in;
    in.event       = makeEvent();
    in.dataQuality = 0.9;

    const MOMatch dup = makeMO(QStringLiteral("C_DUP"), 0.80,
                               {QStringLiteral("forced_entry"),
                                QStringLiteral("nighttime")});
    in.moMatches.append(dup);
    in.moMatches.append(dup);   // exact duplicate → same headline

    const auto leads = he.generate(in);

    int moCount = 0;
    for (const auto& l : leads) {
        if (l.category == QStringLiteral("mo_similarity"))
            ++moCount;
    }

    QVERIFY2(moCount == 1,
             qPrintable(QString("Duplicate MO matches must deduplicate to exactly 1 lead; "
                                "got %1").arg(moCount)));
}

QTEST_GUILESS_MAIN(TestHintEngineDeep3)
#include "test_hint_engine_deep3.moc"
