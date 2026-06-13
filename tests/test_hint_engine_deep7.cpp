// test_hint_engine_deep7.cpp — Deep audit iteration 22: HintEngine
// Verifies: geo confidence cap, geo floor, series/MO filters, rerank scoring,
//           provenance arrow join, empty input, dataQuality rank bonus.

#include <QTest>
#include <algorithm>
#include "inference/HintEngine.h"
#include "core/CrimeEvent.h"

namespace {

QString provenanceStr(const QStringList& chain)
{
    return chain.join(QStringLiteral(" \xe2\x86\x92 "));
}

CrimeEvent makeEvent(const QString& id = QStringLiteral("EV-DEEP7"))
{
    CrimeEvent ev;
    ev.eventId   = id;
    ev.crimeType = QStringLiteral("burglary");
    ev.suburb    = QStringLiteral("Test");
    ev.latitude  = 51.5;
    ev.longitude = -0.12;
    return ev;
}

SeriesMatch makeSeries(const QString& id, double prob)
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

MOMatch makeMO(const QString& caseId, double score)
{
    MOMatch m;
    m.caseId          = caseId;
    m.similarityScore = score;
    return m;
}

GeographicProfile makeGeoProfile(double peakProb)
{
    GeographicProfile gp;
    gp.peakLat         = 51.51;
    gp.peakLon         = -0.11;
    gp.peakProbability = peakProb;
    gp.searchArea50pct = 1.0;
    gp.searchArea80pct = 2.0;
    gp.method          = QStringLiteral("rossmo_cgt");
    return gp;
}

double mirroredRankScore(double confidence, double dataQuality, int contradictions = 0)
{
    const double qualityBonus = (dataQuality > 0.7) ? 1.0 : 0.5;
    const int nContra = std::max(contradictions, 1);
    const double novelty = 1.0 / static_cast<double>(nContra);
    const double penalty = (contradictions > 0) ? 0.1 * contradictions : 0.0;
    return 0.7 * confidence + 0.2 * novelty + 0.1 * qualityBonus - penalty;
}

} // namespace

class TestHintEngineDeep7 : public QObject
{
    Q_OBJECT

private slots:
    void testGeoLeadConfidenceCappedAt085();
    void testGeoSkippedWhenPeakProbabilityLow();
    void testSeriesBelow020Filtered();
    void testMoMaxThreeLeads();
    void testRankScoreRerankChangesOrder();
    void testProvenanceStrJoinsWithArrow();
    void testEmptyInputReturnsEmptyLeads();
    void testDataQualityScalesRankScoreBonus();
};

void TestHintEngineDeep7::testGeoLeadConfidenceCappedAt085()
{
    HintEngine he;
    HintEngineInput in;
    in.event       = makeEvent();
    in.dataQuality = 0.9;
    in.geoProfile  = makeGeoProfile(0.99);

    const auto leads = he.generate(in);
    const auto it = std::find_if(leads.begin(), leads.end(),
        [](const InvestigativeLead& l) {
            return l.category == QStringLiteral("geographic_profile");
        });
    QVERIFY(it != leads.end());
    QVERIFY2(it->confidence <= 0.85,
             qPrintable(QStringLiteral("geo confidence %1 should cap at 0.85")
                            .arg(it->confidence)));
    QCOMPARE(it->confidence, 0.85);
}

void TestHintEngineDeep7::testGeoSkippedWhenPeakProbabilityLow()
{
    HintEngine he;
    HintEngineInput in;
    in.event       = makeEvent();
    in.dataQuality = 0.9;
    in.geoProfile  = makeGeoProfile(0.001);

    const auto leads = he.generate(in);
    QVERIFY(std::none_of(leads.begin(), leads.end(),
        [](const InvestigativeLead& l) {
            return l.category == QStringLiteral("geographic_profile");
        }));
}

void TestHintEngineDeep7::testSeriesBelow020Filtered()
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

void TestHintEngineDeep7::testMoMaxThreeLeads()
{
    HintEngine he;
    HintEngineInput in;
    in.event       = makeEvent();
    in.dataQuality = 0.9;

    for (int i = 0; i < 6; ++i)
        in.moMatches << makeMO(QStringLiteral("C%1").arg(i), 0.40 + i * 0.05);

    const auto leads = he.generate(in);
    int moCount = 0;
    for (const auto& l : leads) {
        if (l.category == QStringLiteral("mo_similarity"))
            ++moCount;
    }
    QCOMPARE(moCount, 3);
}

void TestHintEngineDeep7::testRankScoreRerankChangesOrder()
{
    HintEngine he;
    HintEngineInput in;
    in.event       = makeEvent();
    in.dataQuality = 0.9;
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
    QVERIFY(s4It->rank < s1It->rank);
}

void TestHintEngineDeep7::testProvenanceStrJoinsWithArrow()
{
    HintEngine he;
    HintEngineInput in;
    in.event       = makeEvent();
    in.dataQuality = 0.9;
    in.seriesMatches << makeSeries(QStringLiteral("S_PROV"), 0.75);

    const auto leads = he.generate(in);
    QVERIFY(!leads.isEmpty());

    QStringList chain;
    for (const auto& step : leads[0].provenance)
        chain << step;

    const QString joined = provenanceStr(chain);
    QCOMPARE(joined,
             QStringLiteral("SeriesDetector.link_probability \xe2\x86\x92 near_repeat_composite_kernel"));
    QVERIFY(joined.contains(QStringLiteral(" \xe2\x86\x92 ")));
}

void TestHintEngineDeep7::testEmptyInputReturnsEmptyLeads()
{
    HintEngine he;
    HintEngineInput in;
    in.event = makeEvent();

    const auto leads = he.generate(in);
    QVERIFY(leads.isEmpty());
}

void TestHintEngineDeep7::testDataQualityScalesRankScoreBonus()
{
    const double confidence = 0.55;
    const double highQualityScore = mirroredRankScore(confidence, 0.95);
    const double lowQualityScore  = mirroredRankScore(confidence, 0.30);

    QVERIFY2(highQualityScore > lowQualityScore,
             qPrintable(QStringLiteral("high DQ rankScore %1 should exceed low DQ %2")
                            .arg(highQualityScore).arg(lowQualityScore)));

    HintEngine he;
    HintEngineInput in;
    in.event         = makeEvent();
    in.seriesMatches << makeSeries(QStringLiteral("S_DQ"), confidence);
    in.dataQuality   = 0.95;
    const auto highLeads = he.generate(in);

    in.dataQuality = 0.30;
    const auto lowLeads = he.generate(in);

    QVERIFY(!highLeads.isEmpty());
    QVERIFY(!lowLeads.isEmpty());
    QCOMPARE(highLeads[0].confidence, lowLeads[0].confidence);
}

QTEST_GUILESS_MAIN(TestHintEngineDeep7)
#include "test_hint_engine_deep7.moc"
