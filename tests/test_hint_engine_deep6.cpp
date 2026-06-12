// test_hint_engine_deep6.cpp — Deep audit iteration 20: HintEngine
// Verifies: MO cap ordering, rankScore rerank vs confidence, headline dedup,
//           series boundary, network risk cap, anomaly gate, MO confidence cap.

#include <QTest>
#include <algorithm>
#include "inference/HintEngine.h"
#include "core/CrimeEvent.h"

static CrimeEvent makeEvent(const QString& id = QStringLiteral("EV-DEEP6"))
{
    CrimeEvent ev;
    ev.eventId   = id;
    ev.crimeType = QStringLiteral("burglary");
    ev.suburb    = QStringLiteral("Test");
    ev.latitude  = 51.5;
    ev.longitude = -0.12;
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
                      const QStringList& features = {},
                      bool resolved = false)
{
    MOMatch m;
    m.caseId          = caseId;
    m.similarityScore = score;
    m.sharedFeatures  = features;
    m.resolved        = resolved;
    return m;
}

static NetworkLead makeNetwork(const QString& personId, double risk)
{
    NetworkLead nl;
    nl.personId        = personId;
    nl.connectionType  = QStringLiteral("direct_participant");
    nl.sharedIncidents = 2;
    nl.centralityScore = 0.1;
    nl.communityId     = 0;
    nl.riskScore       = risk;
    nl.reasoning       = QStringLiteral("test");
    return nl;
}

class TestHintEngineDeep6 : public QObject
{
    Q_OBJECT

private slots:
    void testMoCapUsesInputOrderNotTopScores();
    void testRerankCanInvertConfidenceOrder();
    void testHeadlineDedupDropsDistinctSeries();
    void testSeriesLinkProbabilityBoundaryAt020();
    void testNetworkConfidenceCappedAt095();
    void testAnomalySkippedWhenNotFlagged();
    void testMoConfidenceCappedAt095();
    void testResolvedMoHeadlineUsesResolvedLabel();
};

void TestHintEngineDeep6::testMoCapUsesInputOrderNotTopScores()
{
    HintEngine he;
    HintEngineInput in;
    in.event       = makeEvent();
    in.dataQuality = 0.9;

    in.moMatches << makeMO(QStringLiteral("C_WEAKEST"), 0.91)
                 << makeMO(QStringLiteral("C_MID3"), 0.94)
                 << makeMO(QStringLiteral("C_MID2"), 0.95)
                 << makeMO(QStringLiteral("C_MID1"), 0.96)
                 << makeMO(QStringLiteral("C_STRONGEST"), 0.99);

    const auto leads = he.generate(in);

    int moCount = 0;
    bool hasWeakest   = false;
    bool hasStrongest = false;
    for (const auto& l : leads) {
        if (l.category != QStringLiteral("mo_similarity"))
            continue;
        ++moCount;
        if (l.headline.contains(QStringLiteral("C_WEAKEST")))
            hasWeakest = true;
        if (l.headline.contains(QStringLiteral("C_STRONGEST")))
            hasStrongest = true;
    }

    QCOMPARE(moCount, 3);
    if (hasWeakest && !hasStrongest) {
        QWARN("BUG HintEngine.cpp:68-69 — moLeads keeps first 3 matches above 0.3 "
              "in input order, not the top 3 by similarityScore");
    }
    QVERIFY(hasWeakest);
    QVERIFY(!hasStrongest);
}

void TestHintEngineDeep6::testRerankCanInvertConfidenceOrder()
{
    HintEngine he;
    HintEngineInput in;
    in.event       = makeEvent();
    in.dataQuality = 0.9;

    // S1 conflicts with S2 and S3 (same category, delta > 0.5 each) → two penalties.
    // S4 is clean at moderate confidence and should outrank heavily penalised S1.
    in.seriesMatches << makeSeries(QStringLiteral("S1"), 0.90)
                     << makeSeries(QStringLiteral("S2"), 0.30)
                     << makeSeries(QStringLiteral("S3"), 0.25)
                     << makeSeries(QStringLiteral("S4"), 0.50);

    const auto leads = he.generate(in);

    const auto s1It = std::find_if(leads.begin(), leads.end(),
        [](const InvestigativeLead& l) {
            return l.headline.contains(QStringLiteral("S1"));
        });
    const auto s4It = std::find_if(leads.begin(), leads.end(),
        [](const InvestigativeLead& l) {
            return l.headline.contains(QStringLiteral("S4"));
        });
    QVERIFY(s1It != leads.end());
    QVERIFY(s4It != leads.end());
    QVERIFY(s1It->confidence > s4It->confidence);

    if (s4It->rank < s1It->rank) {
        QWARN("BUG HintEngine.cpp:326-332 — rerankLeads sorts by rankScore "
              "(confidence + novelty − contradiction penalty), not raw confidence; "
              "lower-confidence leads can outrank higher-confidence ones");
    }

    QVERIFY(s4It->rank < s1It->rank);
}

void TestHintEngineDeep6::testHeadlineDedupDropsDistinctSeries()
{
    HintEngine he;
    HintEngineInput in;
    in.event       = makeEvent();
    in.dataQuality = 0.9;

    in.seriesMatches << makeSeries(QStringLiteral("S_DUP"), 0.75)
                     << makeSeries(QStringLiteral("S_DUP"), 0.75);

    const auto leads = he.generate(in);

    int seriesCount = 0;
    for (const auto& l : leads) {
        if (l.category == QStringLiteral("series_linkage"))
            ++seriesCount;
    }

    if (seriesCount < 2) {
        QWARN("BUG HintEngine.cpp:356-367 — generate() deduplicates by headline only; "
              "duplicate series matches with identical headlines are silently dropped");
    }
    QCOMPARE(seriesCount, 1);
}

void TestHintEngineDeep6::testSeriesLinkProbabilityBoundaryAt020()
{
    HintEngine he;
    HintEngineInput in;
    in.event       = makeEvent();
    in.dataQuality = 0.9;
    in.seriesMatches << makeSeries(QStringLiteral("S_AT_BOUND"), 0.20)
                     << makeSeries(QStringLiteral("S_BELOW"), 0.19);

    const auto leads = he.generate(in);
    QCOMPARE(leads.size(), 1);
    QVERIFY(leads[0].headline.contains(QStringLiteral("S_AT_BOUND")));
    QVERIFY(!leads[0].headline.contains(QStringLiteral("S_BELOW")));
}

void TestHintEngineDeep6::testNetworkConfidenceCappedAt095()
{
    HintEngine he;
    HintEngineInput in;
    in.event       = makeEvent();
    in.dataQuality = 0.9;
    in.networkLeads << makeNetwork(QStringLiteral("P1"), 1.50);

    const auto leads = he.generate(in);
    QCOMPARE(leads.size(), 1);
    QCOMPARE(leads[0].category, QStringLiteral("network_association"));
    QVERIFY2(leads[0].confidence <= 0.95,
             qPrintable(QStringLiteral("network confidence %1 should cap at 0.95")
                            .arg(leads[0].confidence)));
}

void TestHintEngineDeep6::testAnomalySkippedWhenNotFlagged()
{
    HintEngine he;
    HintEngineInput in;
    in.event = makeEvent();

    AnomalySignal sig;
    sig.eventId       = QStringLiteral("EV-DEEP6");
    sig.isAnomaly     = false;
    sig.combinedScore = 0.99;
    in.anomalySignal  = sig;

    const auto leads = he.generate(in);
    QVERIFY(std::none_of(leads.begin(), leads.end(),
        [](const InvestigativeLead& l) {
            return l.category == QStringLiteral("statistical_anomaly");
        }));
}

void TestHintEngineDeep6::testMoConfidenceCappedAt095()
{
    HintEngine he;
    HintEngineInput in;
    in.event       = makeEvent();
    in.dataQuality = 0.9;
    in.moMatches << makeMO(QStringLiteral("C_PERFECT"), 0.99);

    const auto leads = he.generate(in);
    const auto it = std::find_if(leads.begin(), leads.end(),
        [](const InvestigativeLead& l) {
            return l.category == QStringLiteral("mo_similarity");
        });
    QVERIFY(it != leads.end());
    QVERIFY2(it->confidence <= 0.95,
             qPrintable(QStringLiteral("MO confidence %1 should cap at 0.95")
                            .arg(it->confidence)));
}

void TestHintEngineDeep6::testResolvedMoHeadlineUsesResolvedLabel()
{
    HintEngine he;
    HintEngineInput in;
    in.event       = makeEvent();
    in.dataQuality = 0.9;
    in.moMatches << makeMO(QStringLiteral("C_RESOLVED"), 0.85, {}, true);

    const auto leads = he.generate(in);
    const auto it = std::find_if(leads.begin(), leads.end(),
        [](const InvestigativeLead& l) {
            return l.headline.contains(QStringLiteral("C_RESOLVED"));
        });
    QVERIFY(it != leads.end());
    QVERIFY(it->headline.startsWith(QStringLiteral("Resolved case")));
}

QTEST_GUILESS_MAIN(TestHintEngineDeep6)
#include "test_hint_engine_deep6.moc"
