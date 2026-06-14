// test_hint_engine_deep10.cpp — Deep audit iteration 29: HintEngine
// series linkage, anomaly signal, geographic profile, low-quality dampening.
#include <QTest>
#include "inference/HintEngine.h"
#include "core/CrimeEvent.h"

class TestHintEngineDeep10 : public QObject
{
    Q_OBJECT

    static CrimeEvent makeEvent()
    {
        CrimeEvent ev;
        ev.eventId   = QStringLiteral("HE10-1");
        ev.crimeType = QStringLiteral("burglary");
        ev.suburb    = QStringLiteral("Test");
        return ev;
    }

private slots:

    void testSeriesLinkageAboveThreshold()
    {
        HintEngineInput input;
        input.event       = makeEvent();
        input.dataQuality = 0.9;

        SeriesMatch sm;
        sm.seriesId        = QStringLiteral("SER-10");
        sm.memberCount     = 5;
        sm.linkProbability = 0.65;
        sm.method          = QStringLiteral("NearRepeat-DBSCAN");
        input.seriesMatches = { sm };

        const auto leads = HintEngine().generate(input);
        bool found = false;
        for (const auto& l : leads) {
            if (l.category == QStringLiteral("series_linkage"))
                found = true;
        }
        QVERIFY(found);
    }

    void testAnomalySignalProducesLead()
    {
        HintEngineInput input;
        input.event       = makeEvent();
        input.dataQuality = 0.85;

        AnomalySignal sig;
        sig.eventId       = QStringLiteral("AN-10");
        sig.isAnomaly     = true;
        sig.combinedScore = 0.82;
        sig.isolationScore = 0.7;
        sig.lofScore      = 1.2;
        sig.signalReasons = { QStringLiteral("spatial_outlier") };
        input.anomalySignal = sig;

        const auto leads = HintEngine().generate(input);
        bool found = false;
        for (const auto& l : leads) {
            if (l.category == QStringLiteral("statistical_anomaly"))
                found = true;
        }
        QVERIFY(found);
    }

    void testGeographicProfileLead()
    {
        HintEngineInput input;
        input.event       = makeEvent();
        input.dataQuality = 0.9;

        GeographicProfile gp;
        gp.peakLat         = 51.5;
        gp.peakLon         = -0.1;
        gp.peakProbability = 0.42;
        gp.searchArea50pct = 1.2;
        gp.searchArea80pct = 2.5;
        gp.method          = QStringLiteral("rossmo_cgt");
        input.geoProfile   = gp;

        const auto leads = HintEngine().generate(input);
        bool found = false;
        for (const auto& l : leads) {
            if (l.category == QStringLiteral("geographic_profile"))
                found = true;
        }
        QVERIFY(found);
    }

    void testLowDataQualityReducesConfidence()
    {
        HintEngineInput hi, lo;
        hi.event = lo.event = makeEvent();
        hi.dataQuality = 0.95;
        lo.dataQuality = 0.2;

        NetworkLead nl;
        nl.personId        = QStringLiteral("P-10");
        nl.connectionType  = QStringLiteral("direct_cooffender");
        nl.sharedIncidents = 3;
        nl.centralityScore = 0.8;
        nl.riskScore       = 0.75;
        hi.networkLeads = lo.networkLeads = { nl };

        const auto hiLeads = HintEngine().generate(hi);
        const auto loLeads = HintEngine().generate(lo);
        QVERIFY(!hiLeads.isEmpty() && !loLeads.isEmpty());
        QVERIFY(hiLeads.first().confidence >= loLeads.first().confidence);
    }

    void testAnomalyWithoutFlagSkipped()
    {
        HintEngineInput input;
        input.event       = makeEvent();
        input.dataQuality = 0.9;

        AnomalySignal sig;
        sig.eventId   = QStringLiteral("NO-FLAG");
        sig.isAnomaly = false;
        input.anomalySignal = sig;

        const auto leads = HintEngine().generate(input);
        for (const auto& l : leads)
            QVERIFY(l.category != QStringLiteral("statistical_anomaly"));
    }
};

QTEST_GUILESS_MAIN(TestHintEngineDeep10)
#include "test_hint_engine_deep10.moc"
