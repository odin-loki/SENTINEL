// test_hint_anomaly_evidence.cpp — Iteration 6 deep tests for HintEngine,
// AnomalyDetector, and EvidenceScorer.
#include <QTest>
#include <QMap>
#include <cmath>
#include <algorithm>

#include "inference/HintEngine.h"
#include "inference/AnomalyDetector.h"
#include "inference/EvidenceScorer.h"
#include "core/CrimeEvent.h"

namespace {

CrimeEvent makeEvent(const QString& id = QStringLiteral("EVT-001"))
{
    CrimeEvent ev;
    ev.eventId    = id;
    ev.crimeType  = QStringLiteral("burglary");
    ev.occurredAt = QDateTime(QDate(2024, 6, 1), QTime(14, 0, 0), QTimeZone::utc());
    ev.lat        = 51.505;
    ev.lon        = -0.095;
    return ev;
}

SeriesMatch makeSeriesMatch(const QString& seriesId, double linkProb)
{
    SeriesMatch m;
    m.seriesId             = seriesId;
    m.memberCount          = 4;
    m.linkProbability      = linkProb;
    m.compositeScore       = linkProb;
    m.spatialDistanceM     = 120.0;
    m.temporalDistanceDays = 2.0;
    m.moSimilarity         = 0.75;
    m.method               = QStringLiteral("near_repeat");
    return m;
}

MOMatch makeMOMatch(const QString& caseId, double sim,
                    const QStringList& features = {})
{
    MOMatch m;
    m.caseId          = caseId;
    m.similarityScore = sim;
    m.sharedFeatures  = features.isEmpty()
        ? QStringList{ QStringLiteral("forced_entry") }
        : features;
    m.resolved        = false;
    return m;
}

AnomalyFeatureVector makeFeature(const QString& id, double lat, double lon,
                                 double tDays, double hourNorm = 0.5,
                                 int crimeTypeCode = 1)
{
    AnomalyFeatureVector f;
    f.eventId       = id;
    f.lat           = lat;
    f.lon           = lon;
    f.tDays         = tDays;
    f.hourNorm      = hourNorm;
    f.crimeTypeCode = crimeTypeCode;
    return f;
}

} // namespace

class HintAnomalyEvidenceTest : public QObject
{
    Q_OBJECT

private slots:

    // ── HintEngine ───────────────────────────────────────────────────────────

    void testHintEngineGeneratesLeads()
    {
        HintEngine engine;
        HintEngineInput input;
        input.event = makeEvent();
        input.seriesMatches.append(makeSeriesMatch(QStringLiteral("SER-001"), 0.85));
        input.dataQuality = 1.0;

        const auto leads = engine.generate(input);
        QVERIFY2(!leads.isEmpty(), "HintEngine should generate at least one lead");
    }

    void testHintEngineRankedByConfidence()
    {
        HintEngine engine;
        HintEngineInput input;
        input.event = makeEvent();
        input.seriesMatches.append(makeSeriesMatch(QStringLiteral("SER-HIGH"), 0.92));
        input.seriesMatches.append(makeSeriesMatch(QStringLiteral("SER-LOW"), 0.45));
        input.networkLeads.append([] {
            NetworkLead nl;
            nl.personId        = QStringLiteral("P1");
            nl.connectionType  = QStringLiteral("co_offender");
            nl.sharedIncidents = 2;
            nl.riskScore       = 0.35;
            nl.reasoning       = QStringLiteral("Peripheral link");
            return nl;
        }());
        input.dataQuality = 1.0;

        const auto leads = engine.generate(input);
        QVERIFY2(leads.size() >= 2, "Need multiple leads to verify ordering");

        for (int i = 1; i < leads.size(); ++i) {
            QVERIFY2(leads[i - 1].confidence >= leads[i].confidence,
                     qPrintable(QStringLiteral("Leads not in confidence-descending order at %1")
                                    .arg(i)));
        }
    }

    void testHintEngineDeduplication()
    {
        HintEngine engine;
        HintEngineInput input;
        input.event = makeEvent();

        const auto dup = makeSeriesMatch(QStringLiteral("SER-DUP"), 0.88);
        input.seriesMatches.append(dup);
        input.seriesMatches.append(dup);

        const auto leads = engine.generate(input);
        int dupCount = 0;
        for (const auto& lead : leads) {
            if (lead.headline.contains(QStringLiteral("SER-DUP")))
                ++dupCount;
        }
        QCOMPARE(dupCount, 1);
    }

    void testHintEngineContradictionDetection()
    {
        HintEngine engine;
        HintEngineInput input;
        input.event = makeEvent();
        input.moMatches.append(makeMOMatch(QStringLiteral("CASE-SOLO"), 0.75,
                                           { QStringLiteral("solo") }));
        input.moMatches.append(makeMOMatch(QStringLiteral("CASE-GROUP"), 0.70,
                                           { QStringLiteral("group") }));
        input.dataQuality = 1.0;

        const auto leads = engine.generate(input);
        QVERIFY2(leads.size() >= 2, "Need at least two MO leads");

        bool foundContradiction = false;
        for (const auto& lead : leads) {
            if (!lead.contradictions.empty()) {
                foundContradiction = true;
                break;
            }
        }
        QVERIFY2(foundContradiction,
                 "Contradiction between solo and group leads should be detected");
    }

    void testHintEngineEmptyInput()
    {
        HintEngine engine;
        HintEngineInput input;
        input.event = makeEvent();

        const auto leads = engine.generate(input);
        QVERIFY(leads.isEmpty());
    }

    void testHintEngineHighConfidenceFirst()
    {
        HintEngine engine;
        HintEngineInput input;
        input.event = makeEvent();
        input.seriesMatches.append(makeSeriesMatch(QStringLiteral("SER-TOP"), 0.95));
        input.moMatches.append(makeMOMatch(QStringLiteral("CASE-LOW"), 0.40));
        input.dataQuality = 1.0;

        const auto leads = engine.generate(input);
        QVERIFY2(!leads.isEmpty(), "Expected at least one lead");

        const auto maxIt = std::max_element(
            leads.begin(), leads.end(),
            [](const InvestigativeLead& a, const InvestigativeLead& b) {
                return a.confidence < b.confidence;
            });
        QCOMPARE(leads.first().confidence, maxIt->confidence);
        QCOMPARE(leads.first().rank, 1);
    }

    void testHintEngineMaxLeads()
    {
        HintEngine engine;
        HintEngineInput input;
        input.event = makeEvent();
        input.dataQuality = 1.0;

        for (int i = 0; i < 200; ++i) {
            input.seriesMatches.append(
                makeSeriesMatch(QStringLiteral("SER-%1").arg(i), 0.5 + (i % 50) * 0.01));
            NetworkLead nl;
            nl.personId        = QStringLiteral("P-%1").arg(i);
            nl.connectionType  = QStringLiteral("co_offender");
            nl.sharedIncidents = 1;
            nl.riskScore       = 0.4 + (i % 10) * 0.05;
            nl.reasoning       = QStringLiteral("Network link");
            input.networkLeads.append(nl);
        }

        const auto leads = engine.generate(input);
        QVERIFY2(leads.size() <= HintEngine::kMaxLeads,
                 qPrintable(QStringLiteral("Expected at most %1 leads, got %2")
                                .arg(HintEngine::kMaxLeads).arg(leads.size())));
    }

    // ── AnomalyDetector ──────────────────────────────────────────────────────

    void testAnomalyDetectorIsoScoreRange()
    {
        AnomalyDetector det;
        QVector<AnomalyFeatureVector> batch;
        for (int i = 0; i < 30; ++i) {
            batch.append(makeFeature(QStringLiteral("E%1").arg(i),
                                   51.5 + i * 0.01, -0.1 + i * 0.01,
                                   static_cast<double>(i)));
        }

        const auto results = det.detectAnomalies(batch);
        QCOMPARE(results.size(), batch.size());

        for (const auto& sig : results) {
            QVERIFY2(sig.isolationScore >= 0.0 && sig.isolationScore <= 1.0,
                     qPrintable(QStringLiteral("isolationScore %1 out of [0,1] for %2")
                                    .arg(sig.isolationScore).arg(sig.eventId)));
        }
    }

    void testAnomalyDetectorCombinedScoreWeights()
    {
        AnomalyDetector det;
        QVector<AnomalyFeatureVector> batch;
        for (int i = 0; i < 25; ++i) {
            batch.append(makeFeature(QStringLiteral("N%1").arg(i),
                                   51.5 + (i % 5) * 0.05,
                                   -0.1 + (i / 5) * 0.05,
                                   static_cast<double>(i)));
        }
        det.fit(batch);

        const auto results = det.detectAnomalies(batch);
        QVERIFY(!results.isEmpty());

        for (const auto& sig : results) {
            const double lofNorm = std::min((sig.lofScore - 1.0) / 4.0 + 0.25, 1.0);
            const double zTNorm  = std::min(sig.zScoreTemporal / 3.0, 1.0);
            const double zSNorm  = std::min(sig.zScoreSpatial / 3.0, 1.0);
            const double expected = 0.4 * sig.isolationScore +
                                    0.4 * std::max(lofNorm, 0.0) +
                                    0.1 * zTNorm +
                                    0.1 * zSNorm;
            QVERIFY2(std::abs(sig.combinedScore - expected) < 1e-9,
                     qPrintable(QStringLiteral("combinedScore mismatch for %1: got %2 expected %3")
                                    .arg(sig.eventId).arg(sig.combinedScore).arg(expected)));
        }
    }

    void testAnomalyDetectorSignalReasonsPopulated()
    {
        AnomalyDetector det;
        QVector<AnomalyFeatureVector> normals;
        for (int i = 0; i < 40; ++i) {
            normals.append(makeFeature(QStringLiteral("BG%1").arg(i),
                                       51.0 + (i % 5) * 0.1,
                                       -0.2 + (i / 5) * 0.1,
                                       static_cast<double>(i)));
        }
        det.fit(normals);

        QVector<AnomalyFeatureVector> batch = normals;
        batch.append(makeFeature(QStringLiteral("OUTLIER"), 55.0, 5.0, 20.0));

        const auto results = det.detectAnomalies(batch);
        AnomalySignal outlierSig;
        for (const auto& sig : results) {
            if (sig.eventId == QStringLiteral("OUTLIER"))
                outlierSig = sig;
        }

        QVERIFY2(!outlierSig.signalReasons.empty(),
                 "Extreme spatial outlier should have non-empty signalReasons");
    }

    void testAnomalyDetectorAutoFit()
    {
        AnomalyDetector det;
        QVERIFY(!det.isFitted());

        QVector<AnomalyFeatureVector> batch;
        batch.append(makeFeature(QStringLiteral("A"), 51.5, -0.1, 1.0));
        batch.append(makeFeature(QStringLiteral("B"), 51.6, -0.2, 2.0));

        const auto results = det.detectAnomalies(batch);
        QVERIFY(det.isFitted());
        QCOMPARE(results.size(), 2);
    }

    void testAnomalyDetectorNormalEventNotFlagged()
    {
        AnomalyDetector det;
        QVector<AnomalyFeatureVector> training;
        for (int i = 0; i < 40; ++i) {
            training.append(makeFeature(QStringLiteral("T%1").arg(i),
                                        51.5, -0.1,
                                        static_cast<double>(i)));
        }
        det.fit(training);

        const auto centroid = makeFeature(QStringLiteral("CENTROID"), 51.5, -0.1, 19.5);
        const auto results  = det.detectAnomalies({ centroid });

        QVERIFY(!results.isEmpty());
        QVERIFY2(!results.first().isAnomaly,
                 qPrintable(QStringLiteral("Centroid event combinedScore=%1 should not be anomaly")
                                .arg(results.first().combinedScore)));
    }

    void testAnomalyDetectorSpatialOutlierFlagged()
    {
        AnomalyDetector det;
        QVector<AnomalyFeatureVector> normals;
        for (int i = 0; i < 40; ++i) {
            normals.append(makeFeature(QStringLiteral("N%1").arg(i),
                                       51.0 + (i % 5) * 0.05,
                                       -0.2 + (i / 5) * 0.05,
                                       static_cast<double>(i)));
        }
        det.fit(normals);

        QVector<AnomalyFeatureVector> batch = normals;
        batch.append(makeFeature(QStringLiteral("FAR"), 58.0, 8.0, 20.0));

        const auto results = det.detectAnomalies(batch);
        AnomalySignal outlierSig;
        for (const auto& sig : results) {
            if (sig.eventId == QStringLiteral("FAR"))
                outlierSig = sig;
        }

        QVERIFY2(outlierSig.combinedScore > 0.5,
                 qPrintable(QStringLiteral("Spatial outlier combinedScore=%1 expected > 0.5")
                                .arg(outlierSig.combinedScore)));
        QVERIFY(outlierSig.zScoreSpatial > 2.0);
    }

    // ── EvidenceScorer ───────────────────────────────────────────────────────

    void testEvidenceScorerLROneNoChange()
    {
        EvidenceScorer scorer;
        const double prior = 0.3;

        QMap<QString, bool> presence;
        presence[QStringLiteral("_unknown_evidence_type_")] = true;

        const auto result = scorer.score(prior, presence);
        QVERIFY2(std::abs(result.posteriorProbability - prior) < 1e-9,
                 qPrintable(QStringLiteral("LR=1.0 should leave posterior unchanged: prior=%1 post=%2")
                                .arg(prior).arg(result.posteriorProbability)));
        QVERIFY2(std::abs(result.overallLikelihoodRatio - 1.0) < 1e-9,
                 "Overall LR should be 1.0 for neutral evidence");
    }

    void testEvidenceScorerLRHighIncreasesPost()
    {
        EvidenceScorer scorer;
        const double prior = 0.1;

        QMap<QString, bool> presence;
        presence[QStringLiteral("cctv_clear_face")] = true;

        const auto result = scorer.score(prior, presence);
        QVERIFY2(result.posteriorProbability > prior,
                 qPrintable(QStringLiteral("High LR should increase posterior: prior=%1 post=%2")
                                .arg(prior).arg(result.posteriorProbability)));
    }

    void testEvidenceScorerLRLowDecreasesPost()
    {
        EvidenceScorer scorer;
        const double prior = 0.5;

        QMap<QString, bool> presence;
        presence[QStringLiteral("alibi_strong")] = true;

        const auto result = scorer.score(prior, presence);
        QVERIFY2(result.posteriorProbability < prior,
                 qPrintable(QStringLiteral("Low LR should decrease posterior: prior=%1 post=%2")
                                .arg(prior).arg(result.posteriorProbability)));
    }

    void testEvidenceScorerMultipleItemsMultiply()
    {
        EvidenceScorer scorer;
        const double prior = 0.1;

        QMap<QString, bool> single;
        single[QStringLiteral("cctv_partial")] = true;

        QMap<QString, bool> pair;
        pair[QStringLiteral("cctv_partial")] = true;
        pair[QStringLiteral("informant_tip_reliable")] = true;

        const auto rSingle = scorer.score(prior, single);
        const auto rPair   = scorer.score(prior, pair);

        QVERIFY2(rPair.posteriorProbability > rSingle.posteriorProbability,
                 qPrintable(QStringLiteral("Two LR items should raise posterior more: single=%1 pair=%2")
                                .arg(rSingle.posteriorProbability)
                                .arg(rPair.posteriorProbability)));
        QVERIFY(rPair.overallLikelihoodRatio > rSingle.overallLikelihoodRatio);
    }

    void testEvidenceScorerReliabilityWeighting()
    {
        EvidenceScorer scorer;
        const double prior = 0.1;

        QMap<QString, bool> highRel;
        highRel[QStringLiteral("fingerprint_match_10pt")] = true;

        QMap<QString, bool> lowRel;
        lowRel[QStringLiteral("informant_tip_unreliable")] = true;

        const auto rHigh = scorer.score(prior, highRel);
        const auto rLow  = scorer.score(prior, lowRel);

        const double shiftHigh = rHigh.posteriorProbability - prior;
        const double shiftLow  = rLow.posteriorProbability - prior;

        QVERIFY2(shiftHigh > shiftLow,
                 qPrintable(QStringLiteral("High-reliability evidence should shift posterior more: "
                                            "highShift=%1 lowShift=%2")
                                .arg(shiftHigh).arg(shiftLow)));
    }

    void testEvidenceScorerPostProbInRange()
    {
        EvidenceScorer scorer;
        const auto types = scorer.availableEvidenceTypes();

        QMap<QString, bool> allPresent;
        for (const auto& t : types)
            allPresent[t] = true;

        const auto result = scorer.score(0.05, allPresent);
        QVERIFY2(result.posteriorProbability >= 0.0 && result.posteriorProbability <= 1.0,
                 qPrintable(QStringLiteral("Posterior %1 out of [0,1]")
                                .arg(result.posteriorProbability)));
    }

    void testEvidenceScorerZeroReliabilityNeutral()
    {
        EvidenceScorer scorer;
        const double prior = 0.25;

        EvidenceItem item;
        item.type    = QStringLiteral("_unknown_zero_reliability_");
        item.present = true;

        const auto weights = scorer.score({ item }, prior);
        QVERIFY(!weights.isEmpty());
        QVERIFY2(std::abs(weights.first().likelihoodRatio - 1.0) < 1e-9,
                 "Unknown evidence (reliability=0) should contribute LR=1.0");
        QVERIFY2(std::abs(weights.first().posteriorProbability - prior) < 1e-9,
                 qPrintable(QStringLiteral("Zero-reliability evidence should not change posterior: "
                                            "prior=%1 post=%2")
                                .arg(prior).arg(weights.first().posteriorProbability)));
    }
};

QTEST_MAIN(HintAnomalyEvidenceTest)
#include "test_hint_anomaly_evidence.moc"
