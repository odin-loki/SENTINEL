// Deep audit iteration 14 — HintEngine (deep4)
// Verifies: confidence ranking, headline dedup, solo/group contradictions,
//           maxLeads cap, geo/anomaly/network leads, post-rerank rank refs.

#include <QTest>
#include <algorithm>
#include "inference/HintEngine.h"
#include "core/CrimeEvent.h"

static CrimeEvent makeEvent()
{
    CrimeEvent ev;
    ev.eventId   = QStringLiteral("EV-DEEP4");
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
                       const QString& suspectProfile = {})
{
    MOMatch m;
    m.caseId          = caseId;
    m.similarityScore = score;
    m.sharedFeatures  = features;
    m.suspectProfile  = suspectProfile;
    return m;
}

class TestHintEngineDeep4 : public QObject
{
    Q_OBJECT

private slots:
    void testLeadsSortedByConfidenceDescending();
    void testSequentialRanksAfterRerank();
    void testMaxLeadsCap();
    void testEmptyInputNoCrash();
    void testDeduplicationByHeadline();
    void testContradictionSoloVsGroup();
    void testContradictionSameCategoryDelta();
    void testContradictionMessagesUseFinalRanks();
    void testGeoLeadGeneratedWhenProfilePresent();
    void testAnomalyLeadOnlyWhenFlagged();
    void testNetworkLeadsIncluded();
};

void TestHintEngineDeep4::testLeadsSortedByConfidenceDescending()
{
    HintEngine he;
    HintEngineInput in;
    in.event       = makeEvent();
    in.dataQuality = 0.95;
    in.seriesMatches << makeSeries(QStringLiteral("S_LOW"), 0.30)
                     << makeSeries(QStringLiteral("S_HIGH"), 0.90)
                     << makeSeries(QStringLiteral("S_MID"), 0.60);

    const auto leads = he.generate(in);
    QVERIFY(leads.size() >= 3);
    for (int i = 0; i + 1 < leads.size(); ++i)
        QVERIFY(leads[i].confidence >= leads[i + 1].confidence);
}

void TestHintEngineDeep4::testSequentialRanksAfterRerank()
{
    HintEngine he;
    HintEngineInput in;
    in.event       = makeEvent();
    in.dataQuality = 0.9;
    in.seriesMatches << makeSeries(QStringLiteral("S1"), 0.85)
                     << makeSeries(QStringLiteral("S2"), 0.55);

    const auto leads = he.generate(in);
    for (int i = 0; i < leads.size(); ++i)
        QCOMPARE(leads[i].rank, i + 1);
}

void TestHintEngineDeep4::testMaxLeadsCap()
{
    HintEngine he;
    HintEngineInput in;
    in.event       = makeEvent();
    in.dataQuality = 1.0;

    for (int i = 0; i < 60; ++i)
        in.seriesMatches.append(makeSeries(QStringLiteral("S%1").arg(i), 0.5 + (i % 10) * 0.01));

    const auto leads = he.generate(in);
    QCOMPARE(leads.size(), HintEngine::kMaxLeads);
}

void TestHintEngineDeep4::testEmptyInputNoCrash()
{
    HintEngine he;
    HintEngineInput in;
    in.event       = makeEvent();
    in.dataQuality = 1.0;

    const auto leads = he.generate(in);
    QVERIFY(leads.isEmpty());
}

void TestHintEngineDeep4::testDeduplicationByHeadline()
{
    HintEngine he;
    HintEngineInput in;
    in.event       = makeEvent();
    in.dataQuality = 0.9;

    const SeriesMatch dup = makeSeries(QStringLiteral("SERIES_DUP"), 0.80);
    in.seriesMatches << dup << dup;

    const auto leads = he.generate(in);
    int seriesCount = 0;
    for (const auto& l : leads) {
        if (l.category == QStringLiteral("series_linkage"))
            ++seriesCount;
    }
    QCOMPARE(seriesCount, 1);
}

void TestHintEngineDeep4::testContradictionSoloVsGroup()
{
    HintEngine he;
    HintEngineInput in;
    in.event       = makeEvent();
    in.dataQuality = 0.9;
    in.moMatches << makeMO(QStringLiteral("C_SOLO"), 0.80,
                            { QStringLiteral("solo_offender") },
                            QStringLiteral("solo male"))
                 << makeMO(QStringLiteral("C_GROUP"), 0.75,
                            { QStringLiteral("group_offender") },
                            QStringLiteral("group operation"));

    const auto leads = he.generate(in);
    const bool any = std::any_of(leads.begin(), leads.end(),
        [](const InvestigativeLead& l) { return !l.contradictions.empty(); });
    QVERIFY(any);
}

void TestHintEngineDeep4::testContradictionSameCategoryDelta()
{
    HintEngine he;
    HintEngineInput in;
    in.event       = makeEvent();
    in.dataQuality = 0.9;
    in.seriesMatches << makeSeries(QStringLiteral("S_STRONG"), 0.90)
                     << makeSeries(QStringLiteral("S_WEAK"), 0.25);

    const auto leads = he.generate(in);
    const bool any = std::any_of(leads.begin(), leads.end(),
        [](const InvestigativeLead& l) { return !l.contradictions.empty(); });
    QVERIFY(any);
}

void TestHintEngineDeep4::testContradictionMessagesUseFinalRanks()
{
    HintEngine he;
    HintEngineInput in;
    in.event       = makeEvent();
    in.dataQuality = 0.9;
    in.moMatches << makeMO(QStringLiteral("C_SOLO"), 0.95,
                            { QStringLiteral("solo_offender") })
                 << makeMO(QStringLiteral("C_GROUP"), 0.40,
                            { QStringLiteral("group_offender") });

    const auto leads = he.generate(in);
    QVERIFY(leads.size() >= 2);

    for (const auto& lead : leads) {
        for (const auto& msg : lead.contradictions) {
            const int start = msg.indexOf(QStringLiteral("rank ")) + 5;
            const int end   = msg.indexOf(QChar(' '), start);
            const int citedRank = msg.mid(start, end - start).toInt();
            QVERIFY2(citedRank >= 1 && citedRank <= leads.size(),
                     qPrintable(QStringLiteral("Invalid rank reference in: %1").arg(msg)));
            QCOMPARE(leads[citedRank - 1].rank, citedRank);
        }
    }
}

void TestHintEngineDeep4::testGeoLeadGeneratedWhenProfilePresent()
{
    HintEngine he;
    HintEngineInput in;
    in.event       = makeEvent();
    in.dataQuality = 0.9;

    GeographicProfile gp;
    gp.peakLat         = 51.51;
    gp.peakLon         = -0.11;
    gp.peakProbability = 0.02;
    gp.searchArea50pct = 1.5;
    gp.searchArea80pct = 4.0;
    gp.method          = QStringLiteral("rossmo_cgt");
    in.geoProfile      = gp;

    const auto leads = he.generate(in);
    QVERIFY(!leads.isEmpty());
    QVERIFY(std::any_of(leads.begin(), leads.end(),
        [](const InvestigativeLead& l) {
            return l.category == QStringLiteral("geographic_profile");
        }));
}

void TestHintEngineDeep4::testAnomalyLeadOnlyWhenFlagged()
{
    HintEngine he;
    HintEngineInput in;
    in.event = makeEvent();

    AnomalySignal sig;
    sig.eventId       = QStringLiteral("EV-DEEP4");
    sig.isAnomaly     = false;
    sig.combinedScore = 0.9;
    in.anomalySignal  = sig;

    const auto noLead = he.generate(in);
    QVERIFY(std::none_of(noLead.begin(), noLead.end(),
        [](const InvestigativeLead& l) {
            return l.category == QStringLiteral("statistical_anomaly");
        }));

    sig.isAnomaly      = true;
    sig.signalReasons  = { QStringLiteral("isolation") };
    in.anomalySignal   = sig;
    const auto withLead = he.generate(in);
    QVERIFY(std::any_of(withLead.begin(), withLead.end(),
        [](const InvestigativeLead& l) {
            return l.category == QStringLiteral("statistical_anomaly");
        }));
}

void TestHintEngineDeep4::testNetworkLeadsIncluded()
{
    HintEngine he;
    HintEngineInput in;
    in.event = makeEvent();

    NetworkLead nl;
    nl.personId        = QStringLiteral("P001");
    nl.connectionType  = QStringLiteral("co_offender");
    nl.sharedIncidents = 3;
    nl.centralityScore = 0.4;
    nl.communityId     = 1;
    nl.riskScore       = 0.75;
    nl.reasoning       = QStringLiteral("Shared 3 incidents");
    in.networkLeads.append(nl);

    const auto leads = he.generate(in);
    QVERIFY(std::any_of(leads.begin(), leads.end(),
        [](const InvestigativeLead& l) {
            return l.category == QStringLiteral("network_association");
        }));
}

QTEST_GUILESS_MAIN(TestHintEngineDeep4)
#include "test_hint_engine_deep4.moc"
