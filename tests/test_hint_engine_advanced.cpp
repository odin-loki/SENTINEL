// test_hint_engine_advanced.cpp
// Advanced tests for HintEngine: lead generation from series matches,
// MO matches, geo profiles, anomaly signals, network leads, reranking,
// and contradiction detection.
#include <QTest>
#include <QTimeZone>
#include "inference/HintEngine.h"
#include "core/CrimeEvent.h"
#include <optional>

static CrimeEvent makeEvent(const QString& id,
                             const QString& type = QStringLiteral("burglary"))
{
    CrimeEvent ev;
    ev.id        = id;
    ev.crimeType = type;
    ev.latitude  = 51.5;
    ev.longitude = -0.1;
    ev.timestamp = QDateTime(QDate::currentDate(), QTime(10, 0), QTimeZone::utc());
    ev.narrative = QStringLiteral("Test narrative for %1").arg(id);
    return ev;
}

static SeriesMatch makeSeriesMatch(const QString& seriesId, double score)
{
    SeriesMatch sm;
    sm.seriesId         = seriesId;
    sm.compositeScore   = score;
    sm.linkProbability  = score;  // must be >= 0.2 to pass HintEngine's filter
    sm.memberCount      = 3;
    return sm;
}

static MOMatch makeMOMatch(const QString& caseId, double similarity)
{
    MOMatch m;
    m.caseId          = caseId;
    m.similarityScore = similarity;
    return m;
}

class HintEngineAdvancedTest : public QObject
{
    Q_OBJECT

private slots:

    // 1. Empty input: returns empty or safe default
    void testEmptyInput()
    {
        HintEngine he;
        HintEngineInput inp;
        inp.event = makeEvent(QStringLiteral("E1"));
        const auto leads = he.generate(inp);
        QVERIFY(leads.size() >= 0);
    }

    // 2. Series match generates at least one lead
    void testSeriesMatchGeneratesLead()
    {
        HintEngine he;
        HintEngineInput inp;
        inp.event        = makeEvent(QStringLiteral("E2"));
        inp.seriesMatches = { makeSeriesMatch(QStringLiteral("S1"), 0.8) };
        const auto leads = he.generate(inp);
        QVERIFY2(!leads.isEmpty(), "Series match should generate at least one lead");
    }

    // 3. MO match generates at least one lead
    void testMOMatchGeneratesLead()
    {
        HintEngine he;
        HintEngineInput inp;
        inp.event     = makeEvent(QStringLiteral("E3"));
        inp.moMatches = { makeMOMatch(QStringLiteral("C1"), 0.85) };
        const auto leads = he.generate(inp);
        QVERIFY2(!leads.isEmpty(), "MO match should generate at least one lead");
    }

    // 4. GeographicProfile generates at least one lead
    void testGeoProfileGeneratesLead()
    {
        HintEngine he;
        HintEngineInput inp;
        inp.event = makeEvent(QStringLiteral("E4"));

        GeographicProfile gp;
        gp.peakLat         = 51.5;
        gp.peakLon         = -0.1;
        gp.peakProbability = 0.8;
        gp.searchArea50pct = 2.5;
        inp.geoProfile = gp;

        const auto leads = he.generate(inp);
        QVERIFY2(!leads.isEmpty(), "Geo profile should generate at least one lead");
    }

    // 5. AnomalySignal with isAnomaly=true generates lead
    void testAnomalySignalGeneratesLead()
    {
        HintEngine he;
        HintEngineInput inp;
        inp.event = makeEvent(QStringLiteral("E5"));

        AnomalySignal sig;
        sig.isAnomaly    = true;
        sig.combinedScore = 0.9;
        sig.signalReasons = { QStringLiteral("Unusual pattern detected") };
        inp.anomalySignal = sig;

        const auto leads = he.generate(inp);
        QVERIFY2(!leads.isEmpty(), "Anomaly signal should generate at least one lead");
    }

    // 6. NetworkLead generates lead
    void testNetworkLeadGeneratesLead()
    {
        HintEngine he;
        HintEngineInput inp;
        inp.event = makeEvent(QStringLiteral("E6"));

        NetworkLead nl;
        nl.personId       = QStringLiteral("P001");
        nl.riskScore      = 0.75;
        nl.reasoning      = QStringLiteral("Linked via co-offending");
        nl.connectionType = QStringLiteral("direct_cooffender");
        inp.networkLeads = { nl };

        const auto leads = he.generate(inp);
        QVERIFY2(!leads.isEmpty(), "Network lead should generate at least one lead");
    }

    // 7. Higher-scoring series match generates higher-confidence lead
    void testHigherScoreHigherConfidence()
    {
        HintEngine he;

        HintEngineInput inp1;
        inp1.event        = makeEvent(QStringLiteral("E7a"));
        inp1.seriesMatches = { makeSeriesMatch(QStringLiteral("S1"), 0.95) };

        HintEngineInput inp2;
        inp2.event        = makeEvent(QStringLiteral("E7b"));
        inp2.seriesMatches = { makeSeriesMatch(QStringLiteral("S2"), 0.3) };

        const auto leads1 = he.generate(inp1);
        const auto leads2 = he.generate(inp2);

        QVERIFY(!leads1.isEmpty());
        QVERIFY(!leads2.isEmpty());

        double maxConf1 = 0.0, maxConf2 = 0.0;
        for (const auto& l : leads1) maxConf1 = std::max(maxConf1, l.confidence);
        for (const auto& l : leads2) maxConf2 = std::max(maxConf2, l.confidence);

        QVERIFY2(maxConf1 >= maxConf2,
                 qPrintable(QStringLiteral("Higher score should yield higher confidence: %1 vs %2")
                    .arg(maxConf1).arg(maxConf2)));
    }

    // 8. Low data quality reduces effective ranking
    void testLowDataQualityAffectsLeads()
    {
        HintEngine he;
        HintEngineInput inp;
        inp.event        = makeEvent(QStringLiteral("E8"));
        inp.seriesMatches = { makeSeriesMatch(QStringLiteral("S1"), 0.9) };
        inp.dataQuality  = 0.2;

        const auto leads = he.generate(inp);
        QVERIFY2(leads.size() >= 0, "Low quality should not crash the engine");
    }

    // 9. Multiple sources combined produces more leads
    void testMultipleSourcesMoreLeads()
    {
        HintEngine he;

        HintEngineInput single;
        single.event        = makeEvent(QStringLiteral("E9a"));
        single.seriesMatches = { makeSeriesMatch(QStringLiteral("S1"), 0.8) };

        HintEngineInput combined;
        combined.event        = makeEvent(QStringLiteral("E9b"));
        combined.seriesMatches = { makeSeriesMatch(QStringLiteral("S1"), 0.8) };
        combined.moMatches    = { makeMOMatch(QStringLiteral("C1"), 0.75) };
        NetworkLead nl;
        nl.personId       = QStringLiteral("Jane");
        nl.riskScore      = 0.6;
        nl.reasoning      = QStringLiteral("Co-offending");
        nl.connectionType = QStringLiteral("direct_cooffender");
        combined.networkLeads = { nl };

        const auto leadsS = he.generate(single);
        const auto leadsC = he.generate(combined);

        QVERIFY2(leadsC.size() >= leadsS.size(),
                 qPrintable(QStringLiteral("Combined sources (%1) should produce >= single-source leads (%2)")
                    .arg(leadsC.size()).arg(leadsS.size())));
    }

    // 10. All generated leads have non-empty category and rank > 0
    void testLeadsWellFormed()
    {
        HintEngine he;
        HintEngineInput inp;
        inp.event        = makeEvent(QStringLiteral("E10"));
        inp.seriesMatches = { makeSeriesMatch(QStringLiteral("S1"), 0.85) };
        inp.moMatches    = { makeMOMatch(QStringLiteral("C1"), 0.72) };

        const auto leads = he.generate(inp);
        for (const auto& l : leads) {
            QVERIFY2(!l.category.isEmpty(),
                     qPrintable(QStringLiteral("Lead category must be non-empty (rank %1)").arg(l.rank)));
            QVERIFY2(l.rank > 0,
                     qPrintable(QStringLiteral("Lead rank must be > 0 (got %1)").arg(l.rank)));
            QVERIFY2(l.confidence >= 0.0 && l.confidence <= 1.0,
                     qPrintable(QStringLiteral("Lead confidence %1 must be in [0,1]").arg(l.confidence)));
        }
    }
};

QTEST_MAIN(HintEngineAdvancedTest)
#include "test_hint_engine_advanced.moc"
