// Deep audit iteration 17 — HintEngine (deep5)
// Verifies: rerank scoring (dataQuality, contradiction penalty), geo/anomaly
//           threshold edge cases, MO/series filters, contradiction via detail text.

#include <QTest>
#include <algorithm>
#include "inference/HintEngine.h"
#include "core/CrimeEvent.h"

static CrimeEvent makeEvent(const QString& id = QStringLiteral("EV-DEEP5"))
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
                      const QString& suspectProfile = {})
{
    MOMatch m;
    m.caseId          = caseId;
    m.similarityScore = score;
    m.sharedFeatures  = features;
    m.suspectProfile  = suspectProfile;
    return m;
}

class TestHintEngineDeep5 : public QObject
{
    Q_OBJECT

private slots:
    void testRerankContradictionPenaltyDemotesLead();
    void testGeoLeadSkippedAtProbabilityFloor();
    void testGeoLeadGeneratedJustAboveFloor();
    void testGeoConfidenceProportionalToPeakProbability();
    void testSeriesBelowLinkThresholdExcluded();
    void testMoLeadCapAndSimilarityThreshold();
    void testAnomalyLeadDetailIncludesSignalReasons();
    void testContradictionDetectedViaDetailSharedFeatures();
};

void TestHintEngineDeep5::testRerankContradictionPenaltyDemotesLead()
{
    HintEngine he;
    HintEngineInput in;
    in.event       = makeEvent();
    in.dataQuality = 0.9;
    in.moMatches << makeMO(QStringLiteral("C_SOLO"), 0.92,
                            { QStringLiteral("solo_offender") },
                            QStringLiteral("solo male suspect"))
                 << makeMO(QStringLiteral("C_GROUP"), 0.88,
                            { QStringLiteral("group_offender") },
                            QStringLiteral("group operation"));

    const auto leads = he.generate(in);
    QVERIFY(leads.size() >= 2);

    const auto soloIt = std::find_if(leads.begin(), leads.end(),
        [](const InvestigativeLead& l) {
            return l.detail.contains(QStringLiteral("solo_offender"));
        });
    const auto groupIt = std::find_if(leads.begin(), leads.end(),
        [](const InvestigativeLead& l) {
            return l.detail.contains(QStringLiteral("group_offender"));
        });
    QVERIFY(soloIt != leads.end());
    QVERIFY(groupIt != leads.end());
    QVERIFY(!soloIt->contradictions.empty());
    QVERIFY(!groupIt->contradictions.empty());
}

void TestHintEngineDeep5::testGeoLeadSkippedAtProbabilityFloor()
{
    HintEngine he;
    HintEngineInput in;
    in.event       = makeEvent();
    in.dataQuality = 0.9;

    GeographicProfile gp;
    gp.peakLat         = 51.51;
    gp.peakLon         = -0.11;
    gp.peakProbability = 0.001;
    gp.searchArea50pct = 1.0;
    gp.searchArea80pct = 2.0;
    gp.method          = QStringLiteral("rossmo_cgt");
    in.geoProfile      = gp;

    const auto leads = he.generate(in);
    QVERIFY(std::none_of(leads.begin(), leads.end(),
        [](const InvestigativeLead& l) {
            return l.category == QStringLiteral("geographic_profile");
        }));
}

void TestHintEngineDeep5::testGeoLeadGeneratedJustAboveFloor()
{
    HintEngine he;
    HintEngineInput in;
    in.event       = makeEvent();
    in.dataQuality = 0.9;

    GeographicProfile gp;
    gp.peakLat         = 51.51;
    gp.peakLon         = -0.11;
    gp.peakProbability = 0.0011;
    gp.searchArea50pct = 1.0;
    gp.searchArea80pct = 2.0;
    gp.method          = QStringLiteral("rossmo_cgt");
    in.geoProfile      = gp;

    const auto leads = he.generate(in);
    QVERIFY(std::any_of(leads.begin(), leads.end(),
        [](const InvestigativeLead& l) {
            return l.category == QStringLiteral("geographic_profile");
        }));
}

void TestHintEngineDeep5::testGeoConfidenceProportionalToPeakProbability()
{
    HintEngine he;
    HintEngineInput in;
    in.event       = makeEvent();
    in.dataQuality = 0.9;

    GeographicProfile gp;
    gp.peakLat         = 51.51;
    gp.peakLon         = -0.11;
    gp.peakProbability = 0.02;
    gp.searchArea50pct = 1.0;
    gp.searchArea80pct = 2.0;
    gp.method          = QStringLiteral("rossmo_cgt");
    in.geoProfile      = gp;

    const auto leads = he.generate(in);
    const auto it = std::find_if(leads.begin(), leads.end(),
        [](const InvestigativeLead& l) {
            return l.category == QStringLiteral("geographic_profile");
        });
    QVERIFY(it != leads.end());
    QVERIFY2(it->confidence <= 0.5,
             qPrintable(QStringLiteral("geo confidence %1 should reflect 2%% peak probability, not saturate")
                            .arg(it->confidence)));
}

void TestHintEngineDeep5::testSeriesBelowLinkThresholdExcluded()
{
    HintEngine he;
    HintEngineInput in;
    in.event       = makeEvent();
    in.dataQuality = 0.9;
    in.seriesMatches << makeSeries(QStringLiteral("S_BELOW"), 0.19)
                     << makeSeries(QStringLiteral("S_ABOVE"), 0.21);

    const auto leads = he.generate(in);
    QCOMPARE(leads.size(), 1);
    QVERIFY(leads[0].headline.contains(QStringLiteral("S_ABOVE")));
    QVERIFY(!leads[0].headline.contains(QStringLiteral("S_BELOW")));
}

void TestHintEngineDeep5::testMoLeadCapAndSimilarityThreshold()
{
    HintEngine he;
    HintEngineInput in;
    in.event       = makeEvent();
    in.dataQuality = 0.9;

    for (int i = 0; i < 5; ++i)
        in.moMatches.append(makeMO(QStringLiteral("C%1").arg(i), 0.5 + i * 0.05));

    in.moMatches.prepend(makeMO(QStringLiteral("LOW"), 0.25));
    in.moMatches.prepend(makeMO(QStringLiteral("SKIP"), 0.29));

    const auto leads = he.generate(in);
    int moCount = 0;
    for (const auto& l : leads) {
        if (l.category == QStringLiteral("mo_similarity"))
            ++moCount;
    }
    QCOMPARE(moCount, 3);
    QVERIFY(std::none_of(leads.begin(), leads.end(),
        [](const InvestigativeLead& l) {
            return l.headline.contains(QStringLiteral("SKIP"));
        }));
}

void TestHintEngineDeep5::testAnomalyLeadDetailIncludesSignalReasons()
{
    HintEngine he;
    HintEngineInput in;
    in.event = makeEvent();

    AnomalySignal sig;
    sig.eventId         = QStringLiteral("EV-DEEP5");
    sig.isAnomaly       = true;
    sig.combinedScore   = 0.82;
    sig.isolationScore  = 0.7;
    sig.lofScore        = 0.6;
    sig.zScoreTemporal  = 2.5;
    sig.zScoreSpatial   = 1.8;
    sig.signalReasons   = { QStringLiteral("isolation_forest"),
                            QStringLiteral("lof_outlier") };
    in.anomalySignal    = sig;

    const auto leads = he.generate(in);
    const auto it = std::find_if(leads.begin(), leads.end(),
        [](const InvestigativeLead& l) {
            return l.category == QStringLiteral("statistical_anomaly");
        });
    QVERIFY(it != leads.end());
    QVERIFY(it->detail.contains(QStringLiteral("isolation_forest")));
    QVERIFY(it->detail.contains(QStringLiteral("lof_outlier")));
    QCOMPARE(it->confidence, 0.82);
}

void TestHintEngineDeep5::testContradictionDetectedViaDetailSharedFeatures()
{
    HintEngine he;
    HintEngineInput in;
    in.event       = makeEvent();
    in.dataQuality = 0.9;
    in.seriesMatches << makeSeries(QStringLiteral("S_HIGH"), 0.90)
                     << makeSeries(QStringLiteral("S_LOW"), 0.30);

    const auto leads = he.generate(in);
    QVERIFY(leads.size() >= 2);

    const bool categoryConflict = std::any_of(leads.begin(), leads.end(),
        [](const InvestigativeLead& l) {
            return std::any_of(l.contradictions.begin(), l.contradictions.end(),
                [](const QString& msg) {
                    return msg.contains(QStringLiteral("Confidence conflict"));
                });
        });
    QVERIFY(categoryConflict);
}

QTEST_GUILESS_MAIN(TestHintEngineDeep5)
#include "test_hint_engine_deep5.moc"
