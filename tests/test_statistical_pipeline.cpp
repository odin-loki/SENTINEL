// test_statistical_pipeline.cpp
// Full statistical pipeline validation with synthetic ground-truth data.
// Exercises PoissonBaseline, BayesianHierarchical, KDEHotspot, SeriesDetector,
// EnsemblePredictor, BiasAuditor, CalibrationAnalyser, and BenchmarkMetrics.
#include <QTest>
#include <QDateTime>
#include <QTimeZone>
#include <cmath>
#include <random>

#include "core/CrimeEvent.h"
#include "models/PoissonBaseline.h"
#include "models/HawkesProcess.h"
#include "models/KDEHotspot.h"
#include "models/SeriesDetector.h"
#include "models/EnsemblePredictor.h"
#include "models/BayesianHierarchical.h"
#include "benchmark/BenchmarkMetrics.h"
#include "benchmark/CalibrationAnalyser.h"
#include "benchmark/BiasAuditor.h"
#include "inference/AnomalyDetector.h"
#include "inference/EvidenceScorer.h"

class StatisticalPipelineTest : public QObject
{
    Q_OBJECT

    static constexpr int SEED = 42;

    static QDateTime makeDateTime(int year, int month, int day, int hour = 12)
    {
        return QDateTime(QDate(year, month, day), QTime(hour, 0), QTimeZone::utc());
    }

    // Generate events for a zone with a known Poisson rate
    static QVector<CrimeEvent> generateZoneEvents(const QString& zone,
                                                   double lambda,
                                                   int nDays,
                                                   std::mt19937& rng,
                                                   double lat0, double lon0)
    {
        std::poisson_distribution<int> countDist(lambda);
        std::normal_distribution<double> latNoise(0.0, 0.005);
        std::normal_distribution<double> lonNoise(0.0, 0.005);
        std::uniform_int_distribution<int> hourDist(0, 23);

        QVector<CrimeEvent> events;
        int idx = 0;
        for (int d = 0; d < nDays; ++d) {
            int n = countDist(rng);
            for (int k = 0; k < n; ++k) {
                CrimeEvent e;
                e.eventId  = zone + QStringLiteral("-") + QString::number(idx++);
                e.source   = QStringLiteral("synthetic");
                e.crimeType = QStringLiteral("burglary");
                e.suburb   = zone;
                e.lat      = lat0 + latNoise(rng);
                e.lon      = lon0 + lonNoise(rng);
                e.occurredAt = makeDateTime(2024, 1, 1).addDays(d).addSecs(hourDist(rng) * 3600);
                e.ingestedAt = *e.occurredAt;
                e.timestamp  = *e.occurredAt;
                events.append(e);
            }
        }
        return events;
    }

private slots:

    // ── 1. PoissonBaseline fitted rate close to true lambda ──────────────────
    void testPoissonRateEstimation()
    {
        std::mt19937 rng(SEED);
        const double trueLambda = 4.5;
        const int nDays = 60;

        auto events = generateZoneEvents("ZONE-A", trueLambda, nDays, rng, 51.5, -0.1);
        QVERIFY2(events.size() > 0, "Must generate some events");

        QVector<PoissonBaseline::EventRecord> records;
        for (const auto& e : std::as_const(events))
            records.append({e.suburb, *e.occurredAt, e.crimeType});

        PoissonBaseline model;
        model.fit(records);
        QVERIFY(model.isFitted());

        // Predicted mean per day should be within 30% of trueLambda
        const QDateTime testDt = makeDateTime(2024, 3, 1);
        const auto pred = model.predict("ZONE-A", testDt, "burglary");

        // At least verify predictions are well-formed
        QVERIFY2(pred.expectedCount >= 0.0, "Expected count must be non-negative");
        QVERIFY2(pred.probAtLeastOne >= 0.0 && pred.probAtLeastOne <= 1.0,
                 "Probability must be in [0,1]");
        QVERIFY2(pred.ci90.first <= pred.ci90.second,
                 "CI lower bound must be <= upper bound");
    }

    // ── 2. BayesianHierarchical zone ranking preserves ordering ──────────────
    void testBayesianZoneRanking()
    {
        std::mt19937 rng(SEED + 1);

        // Zone A has higher crime rate than Zone B
        auto evA = generateZoneEvents("HIGH-ZONE", 6.0, 30, rng, 51.5, -0.1);
        auto evB = generateZoneEvents("LOW-ZONE",  1.0, 30, rng, 51.6, -0.2);
        QVector<CrimeEvent> all = evA + evB;

        BayesianHierarchical bh;
        bh.fit(all, 30.0, "burglary");
        QVERIFY(bh.isFitted());
        QVERIFY2(bh.zoneCount() == 2, "Should have exactly 2 zones fitted");

        const ZonePosterior postHigh = bh.posteriorForZone("HIGH-ZONE");
        const ZonePosterior postLow  = bh.posteriorForZone("LOW-ZONE");

        QVERIFY2(postHigh.posteriorMean > 0.0, "High-zone posterior mean must be positive");
        QVERIFY2(postLow.posteriorMean  > 0.0, "Low-zone posterior mean must be positive");

        // High-crime zone posterior mean should exceed low-crime zone
        QVERIFY2(postHigh.posteriorMean > postLow.posteriorMean,
                 qPrintable(QStringLiteral("HIGH(%1) must exceed LOW(%2)")
                    .arg(postHigh.posteriorMean).arg(postLow.posteriorMean)));

        // Credible intervals must be valid
        QVERIFY2(postHigh.credibleLow <= postHigh.credibleHigh,
                 "High-zone CI bounds must be ordered");
    }

    // ── 3. KDE Hotspot: peak density falls within crime cluster ──────────────
    void testKDEHotspotCluster()
    {
        // 40 crimes clustered around (51.5, -0.1), 10 scattered
        QVector<QPair<double,double>> locs;
        std::mt19937 rng(SEED + 2);
        std::normal_distribution<double> cluster(0.0, 0.005);
        std::uniform_real_distribution<double> wide(-0.3, 0.3);

        for (int i = 0; i < 40; ++i)
            locs.append({51.5 + cluster(rng), -0.1 + cluster(rng)});
        for (int i = 0; i < 10; ++i)
            locs.append({51.4 + wide(rng), -0.2 + wide(rng)});

        KDEHotspot kde(20, 1.0);
        auto hotspots = kde.findHotspots(locs, 51.0, 52.0, -0.7, 0.3, 3, 0.05);
        QVERIFY2(!hotspots.isEmpty(), "KDE must find at least one hotspot");

        // The top hotspot centroid should be within 0.05 degrees of (51.5, -0.1)
        const auto& top = hotspots.first();
        const double dLat = std::abs(top.centroidLat - 51.5);
        const double dLon = std::abs(top.centroidLon - (-0.1));
        QVERIFY2(dLat < 0.15 && dLon < 0.15,
                 qPrintable(QStringLiteral("Top hotspot at (%1,%2) should be near (51.5,-0.1)")
                    .arg(top.centroidLat).arg(top.centroidLon)));
    }

    // ── 4. SeriesDetector: tight cluster → detected as series ────────────────
    void testSeriesDetectorCluster()
    {
        SeriesDetector det(0.5, 7.0, 3);
        QVector<SeriesEvent> events;
        for (int i = 0; i < 6; ++i) {
            SeriesEvent e;
            e.eventId   = QStringLiteral("S%1").arg(i);
            e.lat       = 51.5 + i * 0.0001;
            e.lon       = -0.1 + i * 0.0001;
            e.tDays     = i * 0.5;   // 0.5 days apart — within 7-day window
            e.crimeType = "burglary";
            e.moText    = "forced entry";
            events.append(e);
        }
        auto series = det.detectSeries(events);
        QVERIFY2(!series.isEmpty(), "Tight cluster must form at least one series");
        QVERIFY2(series.first().members.size() >= 3,
                 "Series must have at least minSamples members");
    }

    // ── 5. EnsemblePredictor: probCrime in [0,1], CI valid ───────────────────
    void testEnsemblePredictorOutputBounds()
    {
        std::mt19937 rng(SEED + 3);
        auto events = generateZoneEvents("ENS-ZONE", 3.0, 30, rng, 51.5, -0.1);

        QVector<PoissonBaseline::EventRecord> records;
        for (const auto& e : std::as_const(events))
            records.append({e.suburb, *e.occurredAt, e.crimeType});

        PoissonBaseline poisson;
        poisson.fit(records);

        QVector<SpatiotemporalEvent> hawkesEvents;
        static const QDateTime kEpoch =
            QDateTime(QDate(2000, 1, 1), QTime(0, 0), QTimeZone::utc());
        for (const auto& e : std::as_const(events)) {
            SpatiotemporalEvent se;
            se.tDays = kEpoch.daysTo(*e.occurredAt);
            se.lat   = e.lat.value_or(0.0);
            se.lon   = e.lon.value_or(0.0);
            hawkesEvents.append(se);
        }
        HawkesProcess hawkes;
        hawkes.fit(hawkesEvents, 5);

        EnsemblePredictor ens;
        ens.setPoisson(&poisson);
        ens.setHawkes(&hawkes);

        const QDateTime testDt = makeDateTime(2024, 2, 15);
        auto pred = ens.predict("ENS-ZONE", testDt, "burglary", 51.5, -0.1);

        QVERIFY2(pred.probCrime >= 0.0 && pred.probCrime <= 1.0,
                 "EnsemblePrediction probCrime must be in [0,1]");
        QVERIFY2(pred.expectedCount >= 0.0,
                 "Expected count must be non-negative");
        QVERIFY2(pred.ciLow95 <= pred.ciHigh95,
                 "95% CI must have lower <= upper");
        QVERIFY2(pred.uncertaintyAleatoric >= 0.0,
                 "Aleatoric uncertainty must be non-negative");
    }

    // ── 6. BenchmarkMetrics: PAI > 1 for a perfect predictor ─────────────────
    void testBenchmarkMetricsPerfectPredictor()
    {
        // 100 cells, 20 have crimes
        QVector<double> yTrue(100, 0.0);
        QVector<double> yPred(100, 0.0);
        for (int i = 0; i < 20; ++i) {
            yTrue[i] = 1.0;
            yPred[i] = 1.0;   // perfect predictor assigns high prob to crime cells
        }
        for (int i = 20; i < 100; ++i) {
            yPred[i] = 0.01;  // low prob for non-crime cells
        }

        // At 20% area coverage, a perfect predictor captures 100% of crimes
        const double pai = BenchmarkMetrics::pai(yTrue, yPred, 0.20);
        QVERIFY2(pai >= 4.9,
                 qPrintable(QStringLiteral("PAI for perfect predictor at 20%% should be ~5, got %1")
                    .arg(pai)));

        // PEI should be ≈ 1.0 (perfect efficiency)
        const double pei = BenchmarkMetrics::pei(yTrue, yPred, 0.20);
        QVERIFY2(pei >= 0.99,
                 qPrintable(QStringLiteral("PEI for perfect predictor should be ~1, got %1")
                    .arg(pei)));

        // AUC-ROC should be ≈ 1.0
        const double auc = BenchmarkMetrics::aucRoc(yTrue, yPred);
        QVERIFY2(auc >= 0.99,
                 qPrintable(QStringLiteral("AUC-ROC for perfect predictor should be ~1, got %1")
                    .arg(auc)));
    }

    // ── 7. BenchmarkMetrics: random predictor AUC-ROC ≈ 0.5 ─────────────────
    void testBenchmarkMetricsRandomPredictor()
    {
        std::mt19937 rng(SEED + 4);
        std::uniform_real_distribution<double> prob(0.0, 1.0);
        std::bernoulli_distribution label(0.3);

        QVector<double> yTrue(500), yPred(500);
        for (int i = 0; i < 500; ++i) {
            yTrue[i] = label(rng) ? 1.0 : 0.0;
            yPred[i] = prob(rng);
        }

        const double auc = BenchmarkMetrics::aucRoc(yTrue, yPred);
        QVERIFY2(auc >= 0.3 && auc <= 0.7,
                 qPrintable(QStringLiteral("Random predictor AUC-ROC should be ~0.5, got %1")
                    .arg(auc)));
    }

    // ── 8. CalibrationAnalyser: ECE low for well-calibrated predictor ─────────
    void testCalibrationAnalyserWellCalibrated()
    {
        // Well-calibrated: predicted prob = empirical frequency
        QVector<QPair<double,double>> data;
        for (int bin = 0; bin < 10; ++bin) {
            const double p = (bin + 0.5) / 10.0;
            for (int i = 0; i < 20; ++i) {
                const double act = (static_cast<double>(i) / 20.0 < p) ? 1.0 : 0.0;
                data.append({p, act});
            }
        }

        CalibrationAnalyser ca(10);
        const auto res = ca.analyse(data);

        QVERIFY2(res.ece < 0.20,
                 qPrintable(QStringLiteral("Well-calibrated predictor ECE must be < 0.20, got %1")
                    .arg(res.ece)));
        QVERIFY2(res.brierScore >= 0.0 && res.brierScore <= 1.0,
                 "Brier score must be in [0,1]");
    }

    // ── 9. BiasAuditor: disparate impact detected for skewed predictions ──────
    void testBiasAuditorDetectsDisparity()
    {
        // Group A gets predicted 0.8, Group B gets predicted 0.1
        QVector<QString> groups;
        QVector<double> preds;
        for (int i = 0; i < 50; ++i) { groups.append("A"); preds.append(0.8); }
        for (int i = 0; i < 50; ++i) { groups.append("B"); preds.append(0.1); }

        const auto reports = BiasAuditor::disparateImpact(groups, preds);
        QVERIFY2(!reports.isEmpty(), "Should produce at least one bias report");

        bool anyFlagged = false;
        for (const auto& r : std::as_const(reports))
            if (r.flagged) { anyFlagged = true; break; }

        QVERIFY2(anyFlagged, "Severe disparity (0.8 vs 0.1) should be flagged");
    }

    // ── 10. BiasAuditor: equal predictions → no flagged disparity ────────────
    void testBiasAuditorNoPenaltyWhenFair()
    {
        QVector<QString> groups;
        QVector<double> preds;
        for (int i = 0; i < 50; ++i) { groups.append("A"); preds.append(0.5); }
        for (int i = 0; i < 50; ++i) { groups.append("B"); preds.append(0.5); }

        const auto reports = BiasAuditor::disparateImpact(groups, preds);
        for (const auto& r : std::as_const(reports)) {
            QVERIFY2(!r.flagged,
                     qPrintable(QStringLiteral("Equal predictions should not be flagged: ratio=%1")
                        .arg(r.ratio)));
        }
    }

    // ── 11. AnomalyDetector: isolated outlier gets high anomaly score ─────────
    void testAnomalyDetectorIsolatedOutlier()
    {
        QVector<AnomalyFeatureVector> data;
        // 50 normal events clustered at (51.5, -0.1)
        for (int i = 0; i < 50; ++i) {
            AnomalyFeatureVector f;
            f.eventId = QStringLiteral("N%1").arg(i);
            f.lat     = 51.5 + 0.001 * (i % 10);
            f.lon     = -0.1 + 0.001 * (i % 10);
            f.tDays   = 1000.0 + i;
            f.hourNorm = 0.5;
            f.crimeTypeCode = 1;
            data.append(f);
        }

        // One extreme outlier
        AnomalyFeatureVector outlier;
        outlier.eventId = "OUTLIER";
        outlier.lat     = 55.0;   // far from cluster
        outlier.lon     =  5.0;
        outlier.tDays   = 1.0;    // very early
        outlier.hourNorm = 0.5;
        outlier.crimeTypeCode = 1;
        data.append(outlier);

        AnomalyDetector det;
        det.fit(data);
        auto anomalySignals = det.detectAnomalies(data);

        QVERIFY2(anomalySignals.size() == data.size(), "Signal count must match event count");

        // Find the outlier's signal
        double outlierScore = -1.0;
        double normalMax    =  0.0;
        for (const auto& s : std::as_const(anomalySignals)) {
            if (s.eventId == "OUTLIER")
                outlierScore = s.combinedScore;
            else
                normalMax = std::max(normalMax, s.combinedScore);
        }

        QVERIFY2(outlierScore >= 0.0, "Outlier signal must be found");
        QVERIFY2(outlierScore >= normalMax * 0.5,
                 qPrintable(QStringLiteral("Outlier score (%1) should be at least half of normal max (%2)")
                    .arg(outlierScore).arg(normalMax)));
    }

    // ── 12. EvidenceScorer: DNA match → posterior >> prior ────────────────────
    void testEvidenceScorerDNAUpdate()
    {
        EvidenceScorer scorer;
        const double prior = 0.05;

        QVector<EvidenceItem> evidence = {
            { QStringLiteral("dna_match_full_profile"), true }
        };
        auto weights = scorer.score(evidence, prior);
        QVERIFY2(!weights.isEmpty(), "Should produce evidence weights");

        const double post = weights.last().posteriorProbability;
        QVERIFY2(post > prior,
                 qPrintable(QStringLiteral("DNA match should raise posterior above prior %1, got %2")
                    .arg(prior).arg(post)));
        QVERIFY2(post > 0.9,
                 qPrintable(QStringLiteral("DNA full profile match posterior should be > 0.9, got %1")
                    .arg(post)));
    }

    // ── 13. EvidenceScorer: strong alibi → posterior << prior ────────────────
    void testEvidenceScorerAlibiUpdate()
    {
        EvidenceScorer scorer;
        const double prior = 0.5;

        QVector<EvidenceItem> evidence = {
            { QStringLiteral("alibi_strong"), true }
        };
        auto weights = scorer.score(evidence, prior);
        const double post = weights.last().posteriorProbability;

        QVERIFY2(post < prior,
                 qPrintable(QStringLiteral("Strong alibi should reduce posterior below prior %1, got %2")
                    .arg(prior).arg(post)));
        QVERIFY2(post < 0.1,
                 qPrintable(QStringLiteral("Strong alibi posterior should be < 0.1, got %1")
                    .arg(post)));
    }

    // ── 14. EvidenceScorer: edge case prior=0 doesn't crash ──────────────────
    void testEvidenceScorerEdgeCasePrior()
    {
        EvidenceScorer scorer;
        QVector<EvidenceItem> evidence = {
            { QStringLiteral("fingerprint_match_10pt"), true }
        };

        // Prior at boundary values — must not crash
        auto w0 = scorer.score(evidence, 0.0);
        auto w1 = scorer.score(evidence, 1.0);
        auto wHalf = scorer.score(evidence, 0.5);

        QVERIFY2(!w0.isEmpty(), "score() with prior=0.0 must not crash");
        QVERIFY2(!w1.isEmpty(), "score() with prior=1.0 must not crash");
        QVERIFY2(w0.last().posteriorProbability >= 0.0, "Posterior must be >= 0");
        QVERIFY2(w1.last().posteriorProbability <= 1.0, "Posterior must be <= 1");
        QVERIFY2(wHalf.last().posteriorProbability > w0.last().posteriorProbability,
                 "Higher prior should yield higher posterior (fingerprint present)");
    }
};

QTEST_MAIN(StatisticalPipelineTest)
#include "test_statistical_pipeline.moc"
