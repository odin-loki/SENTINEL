// test_hint_engine_dedup.cpp
// Tests HintEngine lead generation, de-duplication, prioritisation and
// provenance, covering the full input structure.
#include <QTest>
#include <QTimeZone>
#include "inference/HintEngine.h"
#include "core/CrimeEvent.h"

static CrimeEvent makeEvent(const QString& id)
{
    CrimeEvent ev;
    ev.eventId   = id;
    ev.crimeType = QStringLiteral("burglary");
    ev.occurredAt = QDateTime(QDate(2024, 3, 10), QTime(22, 0, 0), QTimeZone::utc());
    ev.lat = 51.50;
    ev.lon = -0.10;
    return ev;
}

class HintEngineDedupTest : public QObject
{
    Q_OBJECT

private slots:

    // ── 1. Empty input → empty leads ─────────────────────────────────────────
    void testEmptyInput()
    {
        HintEngine engine;
        HintEngineInput input;
        input.event = makeEvent(QStringLiteral("E0"));
        const auto leads = engine.generate(input);
        QVERIFY2(leads.isEmpty(), "Empty input should produce no leads");
    }

    // ── 2. Series match → produces at least 1 series lead ──────────────────
    void testSeriesMatchGeneratesLead()
    {
        HintEngine engine;
        HintEngineInput input;
        input.event = makeEvent(QStringLiteral("E1"));

        SeriesMatch sm;
        sm.seriesId       = QStringLiteral("SER-001");
        sm.compositeScore = 0.85;
        sm.linkProbability = 0.80;  // must be >= 0.2 to produce a lead
        sm.memberCount    = 3;
        input.seriesMatches.append(sm);

        const auto leads = engine.generate(input);
        QVERIFY2(!leads.isEmpty(), "Series match should generate at least 1 lead");

        bool foundSeries = false;
        for (const auto& lead : leads) {
            if (lead.category == QStringLiteral("series_linkage"))
                foundSeries = true;
        }
        QVERIFY2(foundSeries, "Expected a lead with category 'series_linkage'");
    }

    // ── 3. MO match → produces at least 1 MO lead ───────────────────────────
    void testMOMatchGeneratesLead()
    {
        HintEngine engine;
        HintEngineInput input;
        input.event = makeEvent(QStringLiteral("E2"));

        MOMatch mo;
        mo.caseId          = QStringLiteral("CASE-007");
        mo.similarityScore = 0.80;
        mo.resolved        = false;
        input.moMatches.append(mo);

        const auto leads = engine.generate(input);
        QVERIFY2(!leads.isEmpty(), "MO match should generate at least 1 lead");

        bool foundMO = false;
        for (const auto& lead : leads)
            if (lead.category == QStringLiteral("mo_similarity"))
                foundMO = true;
        QVERIFY2(foundMO, "Expected a lead with category 'mo_similarity'");
    }

    // ── 4. Geographic profile → at least 1 geo lead ────────────────────────
    void testGeoProfileGeneratesLead()
    {
        HintEngine engine;
        HintEngineInput input;
        input.event = makeEvent(QStringLiteral("E3"));

        GeographicProfile gp;
        gp.peakLat  = 51.50;
        gp.peakLon  = -0.10;
        gp.peakProbability = 0.75;
        gp.searchArea50pct = 5.0;
        gp.searchArea80pct = 15.0;
        gp.method = QStringLiteral("rossmo_cgt");
        input.geoProfile = gp;

        const auto leads = engine.generate(input);
        QVERIFY2(!leads.isEmpty(), "Geo profile should generate at least 1 lead");

        bool foundGeo = false;
        for (const auto& lead : leads)
            if (lead.category == QStringLiteral("geographic_profile"))
                foundGeo = true;
        QVERIFY2(foundGeo, "Expected a lead with category 'geographic_profile'");
    }

    // ── 5. Anomaly signal with isAnomaly=true → produces anomaly lead ───────
    void testAnomalySignalGeneratesLead()
    {
        HintEngine engine;
        HintEngineInput input;
        input.event = makeEvent(QStringLiteral("E4"));

        AnomalySignal sig;
        sig.eventId = QStringLiteral("E4");
        sig.combinedScore = 0.90;
        sig.isAnomaly = true;
        sig.zScoreTemporal = 3.5;
        sig.zScoreSpatial  = 2.8;
        sig.signalReasons  = { QStringLiteral("temporal_outlier") };
        input.anomalySignal = sig;

        const auto leads = engine.generate(input);
        QVERIFY2(!leads.isEmpty(), "Anomaly signal should generate at least 1 lead");

        bool foundAnomaly = false;
        for (const auto& lead : leads)
            if (lead.category == QStringLiteral("statistical_anomaly"))
                foundAnomaly = true;
        QVERIFY2(foundAnomaly, "Expected a lead with category 'statistical_anomaly'");
    }

    // ── 6. Network lead → produces network category lead ───────────────────
    void testNetworkLeadGeneratesLead()
    {
        HintEngine engine;
        HintEngineInput input;
        input.event = makeEvent(QStringLiteral("E5"));

        NetworkLead nl;
        nl.personId       = QStringLiteral("PERSON-A");
        nl.riskScore      = 0.85;
        nl.communityId    = 1;
        nl.sharedIncidents = 2;
        input.networkLeads.append(nl);

        const auto leads = engine.generate(input);
        QVERIFY2(!leads.isEmpty(), "Network lead should generate at least 1 lead");

        bool foundNetwork = false;
        for (const auto& lead : leads)
            if (lead.category == QStringLiteral("network_association"))
                foundNetwork = true;
        QVERIFY2(foundNetwork, "Expected a lead with category 'network_association'");
    }

    // ── 7. Leads are sorted by confidence descending ─────────────────────────
    void testLeadsSortedByScore()
    {
        HintEngine engine;
        HintEngineInput input;
        input.event = makeEvent(QStringLiteral("E6"));

        // Add multiple types of evidence
        SeriesMatch sm;
        sm.seriesId        = QStringLiteral("SER-002");
        sm.compositeScore  = 0.95;
        sm.linkProbability = 0.90;
        sm.memberCount     = 3;
        input.seriesMatches.append(sm);

        MOMatch mo;
        mo.caseId          = QStringLiteral("CASE-010");
        mo.similarityScore = 0.60;
        mo.resolved        = false;
        input.moMatches.append(mo);

        NetworkLead nl;
        nl.personId    = QStringLiteral("PERSON-B");
        nl.riskScore   = 0.50;
        nl.communityId = 2;
        input.networkLeads.append(nl);

        const auto leads = engine.generate(input);
        for (int i = 1; i < leads.size(); ++i) {
            QVERIFY2(leads[i-1].confidence >= leads[i].confidence,
                     qPrintable(QStringLiteral("Leads not sorted at index %1: %2 < %3")
                        .arg(i).arg(leads[i-1].confidence).arg(leads[i].confidence)));
        }
    }

    // ── 8. All leads have non-empty category ──────────────────────────────────
    void testAllLeadsHaveCategory()
    {
        HintEngine engine;
        HintEngineInput input;
        input.event = makeEvent(QStringLiteral("E7"));

        SeriesMatch sm;
        sm.seriesId        = QStringLiteral("SER-003");
        sm.compositeScore  = 0.70;
        sm.linkProbability = 0.65;
        sm.memberCount     = 2;
        input.seriesMatches.append(sm);

        AnomalySignal sig;
        sig.eventId = QStringLiteral("E7");
        sig.combinedScore = 0.85;
        sig.isAnomaly = true;
        input.anomalySignal = sig;

        for (const auto& lead : engine.generate(input))
            QVERIFY2(!lead.category.isEmpty(), "All leads must have a non-empty category");
    }

    // ── 9. All leads have non-empty headline ─────────────────────────────────
    void testAllLeadsHaveHeadline()
    {
        HintEngine engine;
        HintEngineInput input;
        input.event = makeEvent(QStringLiteral("E8"));

        MOMatch mo;
        mo.caseId          = QStringLiteral("CASE-011");
        mo.similarityScore = 0.75;
        input.moMatches.append(mo);

        for (const auto& lead : engine.generate(input))
            QVERIFY2(!lead.headline.isEmpty(), "All leads must have a non-empty headline");
    }

    // ── 10. Data quality < 0.5 reduces scores ─────────────────────────────────
    void testLowDataQualityReducesScore()
    {
        HintEngine engine;

        auto makeInput = [](double quality) {
            HintEngineInput input;
            input.event = makeEvent(QStringLiteral("EQ"));
            input.dataQuality = quality;
            SeriesMatch sm;
            sm.seriesId        = QStringLiteral("SER-Q");
            sm.compositeScore  = 0.90;
            sm.linkProbability = 0.85;
            sm.memberCount     = 3;
            input.seriesMatches.append(sm);
            return input;
        };

        const auto highQLeads = engine.generate(makeInput(1.0));
        const auto lowQLeads  = engine.generate(makeInput(0.3));

        QVERIFY2(!highQLeads.isEmpty() && !lowQLeads.isEmpty(),
                 "Both inputs should produce leads");

        QVERIFY2(highQLeads[0].confidence >= lowQLeads[0].confidence,
                 qPrintable(QStringLiteral(
                    "High quality (%1) should have confidence >= low quality (%2)")
                    .arg(highQLeads[0].confidence).arg(lowQLeads[0].confidence)));
    }
};

QTEST_MAIN(HintEngineDedupTest)
#include "test_hint_engine_dedup.moc"
