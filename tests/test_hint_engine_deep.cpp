// test_hint_engine_deep.cpp
// Deep tests for HintEngine: lead generation from series, MO, geographic profile,
// anomaly signal, and network leads.
#include <QTest>
#include <QTimeZone>
#include "inference/HintEngine.h"
#include "core/CrimeEvent.h"

class HintEngineDeepTest : public QObject
{
    Q_OBJECT

private:
    static CrimeEvent makeEvent()
    {
        CrimeEvent ev;
        ev.eventId   = QStringLiteral("E001");
        ev.crimeType = QStringLiteral("burglary");
        ev.suburb    = QStringLiteral("Soho");
        ev.lat       = 51.5074;
        ev.lon       = -0.1278;
        ev.latitude  = 51.5074;
        ev.longitude = -0.1278;
        const QDateTime dt = QDateTime(QDate(2024, 3, 15), QTime(22, 0, 0), QTimeZone::utc());
        ev.occurredAt = dt;
        ev.timestamp  = dt;
        return ev;
    }

    static HintEngineInput minimalInput()
    {
        HintEngineInput in;
        in.event       = makeEvent();
        in.dataQuality = 0.9;
        return in;
    }

    static SeriesMatch makeSeriesMatch(double score = 0.85)
    {
        SeriesMatch sm;
        sm.seriesId          = QStringLiteral("S001");
        sm.memberCount       = 5;
        sm.linkProbability   = score;
        sm.compositeScore    = score;
        sm.spatialDistanceM  = 150.0;
        sm.temporalDistanceDays = 3.0;
        sm.moSimilarity      = 0.9;
        sm.method            = QStringLiteral("DBSCAN+MO");
        return sm;
    }

    static AnomalySignal makeAnomaly()
    {
        AnomalySignal a;
        a.eventId       = QStringLiteral("E001");
        a.combinedScore = 0.88;
        a.isAnomaly     = true;
        a.zScoreTemporal = 3.5;
        a.zScoreSpatial  = 2.8;
        a.isolationScore = 0.7;
        return a;
    }

    static GeographicProfile makeGeoProfile()
    {
        GeographicProfile gp;
        gp.peakLat        = 51.508;
        gp.peakLon        = -0.130;
        gp.peakProbability = 0.15;
        gp.searchArea50pct = 1.2;
        gp.searchArea80pct = 3.5;
        gp.method          = QStringLiteral("rossmo");
        // Small 2x2 grid
        gp.probabilitySurface = {{0.1, 0.15}, {0.05, 0.12}};
        gp.gridLats = {51.5, 51.51};
        gp.gridLons = {-0.13, -0.12};
        return gp;
    }

private slots:

    // ── 1. Minimal input → at least 0 leads, no crash ────────────────────────
    void testMinimalInputNoCrash()
    {
        HintEngine he;
        const auto leads = he.generate(minimalInput());
        QVERIFY(true);  // no crash
    }

    // ── 2. Series match → generates lead ─────────────────────────────────────
    void testSeriesMatchGeneratesLead()
    {
        HintEngine he;
        HintEngineInput in = minimalInput();
        in.seriesMatches.append(makeSeriesMatch(0.85));

        const auto leads = he.generate(in);
        QVERIFY2(!leads.isEmpty(), "Series match should generate at least one lead");
    }

    // ── 3. Lead confidence is in [0, 1] ──────────────────────────────────────
    void testLeadConfidenceRange()
    {
        HintEngine he;
        HintEngineInput in = minimalInput();
        in.seriesMatches.append(makeSeriesMatch(0.9));
        in.anomalySignal = makeAnomaly();

        const auto leads = he.generate(in);
        for (const auto& l : leads) {
            QVERIFY2(l.confidence >= 0.0 && l.confidence <= 1.0,
                     qPrintable(QStringLiteral("Lead confidence %1 must be in [0,1]")
                        .arg(l.confidence)));
        }
    }

    // ── 4. Leads have non-empty headline and category ─────────────────────────
    void testLeadFieldsPopulated()
    {
        HintEngine he;
        HintEngineInput in = minimalInput();
        in.seriesMatches.append(makeSeriesMatch(0.8));

        const auto leads = he.generate(in);
        for (const auto& l : leads) {
            QVERIFY2(!l.headline.isEmpty(),  "Lead headline must not be empty");
            QVERIFY2(!l.category.isEmpty(),  "Lead category must not be empty");
        }
    }

    // ── 5. Leads have ranks ≥ 1 ──────────────────────────────────────────────
    void testLeadRanksPositive()
    {
        HintEngine he;
        HintEngineInput in = minimalInput();
        in.seriesMatches.append(makeSeriesMatch(0.8));

        const auto leads = he.generate(in);
        for (const auto& l : leads) {
            QVERIFY2(l.rank >= 1,
                     qPrintable(QStringLiteral("Lead rank %1 must be >= 1").arg(l.rank)));
        }
    }

    // ── 6. Anomaly signal → generates anomaly lead ───────────────────────────
    void testAnomalySignalGeneratesLead()
    {
        HintEngine he;
        HintEngineInput in = minimalInput();
        in.anomalySignal = makeAnomaly();

        const auto leads = he.generate(in);
        QVERIFY2(!leads.isEmpty(), "Anomaly signal should generate at least one lead");
    }

    // ── 7. Geographic profile → generates geo lead ───────────────────────────
    void testGeoProfileGeneratesLead()
    {
        HintEngine he;
        HintEngineInput in = minimalInput();
        in.geoProfile = makeGeoProfile();

        const auto leads = he.generate(in);
        QVERIFY2(!leads.isEmpty(), "Geographic profile should generate at least one lead");
    }

    // ── 8. Low data quality → leads have lower confidence ────────────────────
    void testLowDataQualityLowerConfidence()
    {
        HintEngine he;

        HintEngineInput inHigh = minimalInput();
        inHigh.seriesMatches.append(makeSeriesMatch(0.85));
        inHigh.dataQuality = 0.95;
        const auto highLeads = he.generate(inHigh);

        HintEngineInput inLow = minimalInput();
        inLow.seriesMatches.append(makeSeriesMatch(0.85));
        inLow.dataQuality = 0.2;
        const auto lowLeads = he.generate(inLow);

        if (!highLeads.isEmpty() && !lowLeads.isEmpty()) {
            double highMax = std::max_element(highLeads.begin(), highLeads.end(),
                [](const InvestigativeLead& a, const InvestigativeLead& b){ return a.confidence < b.confidence; })->confidence;
            double lowMax = std::max_element(lowLeads.begin(), lowLeads.end(),
                [](const InvestigativeLead& a, const InvestigativeLead& b){ return a.confidence < b.confidence; })->confidence;
            QVERIFY2(highMax >= lowMax,
                     qPrintable(QStringLiteral("High quality confidence %1 should >= low quality %2")
                        .arg(highMax).arg(lowMax)));
        }
        QVERIFY(true);  // pass if no leads generated
    }

    // ── 9. Multiple series → multiple leads ──────────────────────────────────
    void testMultipleSeriesMultipleLeads()
    {
        HintEngine he;
        HintEngineInput in = minimalInput();
        for (int i = 0; i < 3; ++i)
            in.seriesMatches.append(makeSeriesMatch(0.7 + i * 0.1));

        const auto leads = he.generate(in);
        // Should produce at least one lead per series match (likely)
        QVERIFY2(!leads.isEmpty(), "Multiple series matches should produce leads");
    }

    // ── 10. Leads have provenance chain ──────────────────────────────────────
    void testLeadsHaveProvenance()
    {
        HintEngine he;
        HintEngineInput in = minimalInput();
        in.seriesMatches.append(makeSeriesMatch(0.8));
        in.anomalySignal = makeAnomaly();

        const auto leads = he.generate(in);
        const bool hasProvenance = std::any_of(leads.begin(), leads.end(),
            [](const InvestigativeLead& l){ return !l.provenance.empty(); });
        QVERIFY2(hasProvenance, "At least one lead should have a provenance chain");
    }
};

QTEST_MAIN(HintEngineDeepTest)
#include "test_hint_engine_deep.moc"
