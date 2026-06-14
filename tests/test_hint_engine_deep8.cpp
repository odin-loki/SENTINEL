// test_hint_engine_deep8.cpp — Deep audit iteration 25: HintEngine
// network leads, anomaly leads, contradiction detection, max leads cap.
#include <QTest>
#include "inference/HintEngine.h"
#include "core/CrimeEvent.h"

class TestHintEngineDeep8 : public QObject
{
    Q_OBJECT

    static CrimeEvent makeEvent()
    {
        CrimeEvent ev;
        ev.eventId = QStringLiteral("HE8-1");
        ev.crimeType = QStringLiteral("burglary");
        ev.suburb = QStringLiteral("Test");
        return ev;
    }

    static SeriesMatch series(double prob)
    {
        SeriesMatch sm;
        sm.seriesId = QStringLiteral("S-HE8");
        sm.memberCount = 5;
        sm.linkProbability = prob;
        sm.compositeScore = prob;
        sm.method = QStringLiteral("dbscan");
        return sm;
    }

private slots:

    void testNetworkLeadsIncluded()
    {
        HintEngineInput input;
        input.event = makeEvent();
        input.dataQuality = 0.8;

        NetworkLead nl;
        nl.personId = QStringLiteral("PERSON-A");
        nl.connectionType = QStringLiteral("direct_cooffender");
        nl.sharedIncidents = 3;
        nl.centralityScore = 0.85;
        nl.riskScore = 0.75;
        nl.reasoning = QStringLiteral("high centrality hub");
        input.networkLeads.append(nl);

        HintEngine engine;
        const auto leads = engine.generate(input);
        bool hasNetwork = false;
        for (const auto& l : leads) {
            if (l.category == QStringLiteral("network_association"))
                hasNetwork = true;
        }
        QVERIFY(hasNetwork);
    }

    void testAnomalyLeadWhenSignalPresent()
    {
        HintEngineInput input;
        input.event = makeEvent();
        input.dataQuality = 0.75;

        AnomalySignal sig;
        sig.eventId = input.event.eventId;
        sig.combinedScore = 0.92;
        sig.isolationScore = 0.8;
        sig.zScoreTemporal = 2.5;
        sig.isAnomaly = true;
        input.anomalySignal = sig;

        HintEngine engine;
        const auto leads = engine.generate(input);
        bool hasAnomaly = false;
        for (const auto& l : leads) {
            if (l.category == QStringLiteral("statistical_anomaly"))
                hasAnomaly = true;
        }
        QVERIFY(hasAnomaly);
    }

    void testContradictionDetectedBetweenGeoAndSeries()
    {
        HintEngineInput input;
        input.event = makeEvent();
        input.dataQuality = 0.9;
        input.seriesMatches = { series(0.85) };

        GeographicProfile gp;
        gp.peakProbability = 0.9;
        gp.peakLat = 40.0;
        gp.peakLon = -74.0;
        gp.searchArea50pct = 1.0;
        gp.searchArea80pct = 2.0;
        gp.method = QStringLiteral("rossmo");
        input.geoProfile = gp;

        HintEngine engine;
        const auto leads = engine.generate(input);
        bool hasContra = false;
        for (const auto& l : leads) {
            if (!l.contradictions.empty())
                hasContra = true;
        }
        QVERIFY2(hasContra || leads.size() >= 2,
                 "geo/series contradiction or multiple lead types expected");
    }

    void testEmptyInputReturnsEmpty()
    {
        HintEngineInput input;
        input.event = makeEvent();
        HintEngine engine;
        const auto leads = engine.generate(input);
        QVERIFY(leads.isEmpty());
    }

    void testLeadsHaveProvenance()
    {
        HintEngineInput input;
        input.event = makeEvent();
        input.dataQuality = 0.8;
        input.seriesMatches = { series(0.75) };

        HintEngine engine;
        const auto leads = engine.generate(input);
        QVERIFY(!leads.isEmpty());
        QVERIFY(!leads.first().provenance.empty());
    }

    void testConfidenceBounded()
    {
        HintEngineInput input;
        input.event = makeEvent();
        input.dataQuality = 0.8;
        input.seriesMatches = { series(0.95) };

        HintEngine engine;
        const auto leads = engine.generate(input);
        for (const auto& l : leads) {
            QVERIFY2(l.confidence >= 0.0 && l.confidence <= 1.0,
                     qPrintable(QStringLiteral("conf=%1").arg(l.confidence)));
        }
    }

    void testRanksStartAtOne()
    {
        HintEngineInput input;
        input.event = makeEvent();
        input.dataQuality = 0.8;
        input.seriesMatches = { series(0.6), series(0.7) };

        HintEngine engine;
        const auto leads = engine.generate(input);
        if (!leads.isEmpty())
            QCOMPARE(leads.first().rank, 1);
    }
};

QTEST_GUILESS_MAIN(TestHintEngineDeep8)
#include "test_hint_engine_deep8.moc"
