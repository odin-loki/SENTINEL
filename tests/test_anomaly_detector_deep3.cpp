// Deep audit iteration 12 — AnomalyDetector
// Covers: isolation score (single / identical events), Z-score at mean and ±3σ,
// empty input, combinedScore range, outlier flagged as anomaly.

#include <QTest>
#include "inference/AnomalyDetector.h"
#include <cmath>

class AnomalyDetectorDeep3Test : public QObject
{
    Q_OBJECT

private:
    // Convenience builder
    static AnomalyFeatureVector ev(const QString& id,
                                   double lat, double lon, double tDays,
                                   double hourNorm = 0.5, int crimeCode = 1)
    {
        return {id, lat, lon, tDays, hourNorm, crimeCode};
    }

private slots:

    // ── Single event: isolated by definition → high isolation score ────────────
    //
    // Bug fix applied: AnomalyDetector::isolationScore() now returns 1.0 for a
    // single-element context (previously returned 0 because the event equalled
    // its own centroid). A single event is trivially isolated.

    void testSingleEventHighIsolationScore()
    {
        AnomalyDetector det;
        const QVector<AnomalyFeatureVector> events = {ev("e1", 51.5, -0.1, 100.0)};
        const auto results = det.detectAnomalies(events);
        QCOMPARE(results.size(), 1);
        QVERIFY2(results[0].isolationScore > 0.5,
                 qPrintable(QString("single event isolation score %1 should be > 0.5")
                     .arg(results[0].isolationScore)));
    }

    // ── Event identical to all others → low isolation score ───────────────────

    void testIdenticalEventsLowIsolationScore()
    {
        AnomalyDetector det;
        QVector<AnomalyFeatureVector> events;
        for (int i = 0; i < 20; ++i)
            events.append(ev(QString("e%1").arg(i), 51.5, -0.1, 100.0));

        const auto results = det.detectAnomalies(events);
        QCOMPARE(results.size(), 20);
        for (const auto& sig : results) {
            QVERIFY2(sig.isolationScore < 0.5,
                     qPrintable(QString("event %1: isolation %2 should be < 0.5 (at centroid)")
                         .arg(sig.eventId).arg(sig.isolationScore)));
        }
    }

    // ── Z-score: event at mean → zScore ≈ 0 ──────────────────────────────────

    void testZScoreTemporalAtMeanIsZero()
    {
        // Fit on tDays = 1..10 → tMean = 5.5
        AnomalyDetector det;
        QVector<AnomalyFeatureVector> training;
        for (int i = 1; i <= 10; ++i)
            training.append(ev(QString("t%1").arg(i), 51.5, -0.1, static_cast<double>(i)));
        det.fit(training);

        const QVector<AnomalyFeatureVector> test = {ev("q", 51.5, -0.1, 5.5)};
        const auto results = det.detectAnomalies(test);
        QCOMPARE(results.size(), 1);
        QVERIFY2(results[0].zScoreTemporal < 0.01,
                 qPrintable(QString("zScoreTemporal at mean should be ≈ 0, got %1")
                     .arg(results[0].zScoreTemporal)));
    }

    void testZScoreSpatialAtMeanIsZero()
    {
        // Fit on lat 51.0..51.9 (step 0.1), lon = -0.1 for all.
        // Spatial mean: latMean ≈ 51.45, lonMean = -0.1.
        AnomalyDetector det;
        QVector<AnomalyFeatureVector> training;
        for (int i = 0; i < 10; ++i)
            training.append(ev(QString("t%1").arg(i), 51.0 + i * 0.1, -0.1, 100.0));
        det.fit(training);

        // Event at spatial centroid
        const QVector<AnomalyFeatureVector> test = {ev("q", 51.45, -0.1, 100.0)};
        const auto results = det.detectAnomalies(test);
        QCOMPARE(results.size(), 1);
        QVERIFY2(results[0].zScoreSpatial < 0.05,
                 qPrintable(QString("zScoreSpatial at centroid should be ≈ 0, got %1")
                     .arg(results[0].zScoreSpatial)));
    }

    // ── Z-score: event at mean ± 3·std → |zScore| ≈ 3 ────────────────────────

    void testZScoreTemporalAt3StdIsThree()
    {
        // Fit on tDays = 1..10 → tMean = 5.5, tStd = sqrt(82.5/9) ≈ 3.028.
        AnomalyDetector det;
        QVector<AnomalyFeatureVector> training;
        for (int i = 1; i <= 10; ++i)
            training.append(ev(QString("t%1").arg(i), 51.5, -0.1, static_cast<double>(i)));
        det.fit(training);

        // tMean=5.5, tStd≈3.028 → query at 5.5 + 3*3.028 = 14.583
        const double tQuery = 14.583;
        const QVector<AnomalyFeatureVector> test = {ev("q", 51.5, -0.1, tQuery)};
        const auto results = det.detectAnomalies(test);
        QCOMPARE(results.size(), 1);
        QVERIFY2(std::abs(results[0].zScoreTemporal - 3.0) < 0.15,
                 qPrintable(QString("zScoreTemporal at mean+3σ should be ≈ 3.0, got %1")
                     .arg(results[0].zScoreTemporal)));
    }

    void testZScoreTemporalAtMinus3StdIsThree()
    {
        AnomalyDetector det;
        QVector<AnomalyFeatureVector> training;
        for (int i = 1; i <= 10; ++i)
            training.append(ev(QString("t%1").arg(i), 51.5, -0.1, static_cast<double>(i)));
        det.fit(training);

        // tMean=5.5, tStd≈3.028 → query at 5.5 - 3*3.028 = -3.583
        const double tQuery = -3.583;
        const QVector<AnomalyFeatureVector> test = {ev("q", 51.5, -0.1, tQuery)};
        const auto results = det.detectAnomalies(test);
        QCOMPARE(results.size(), 1);
        // zScoreTemporal = |tDays - mean| / std; the absolute value gives ≈ 3.
        QVERIFY2(std::abs(results[0].zScoreTemporal - 3.0) < 0.15,
                 qPrintable(QString("zScoreTemporal at mean-3σ should be ≈ 3.0, got %1")
                     .arg(results[0].zScoreTemporal)));
    }

    // ── detectAnomalies returns empty list for empty input ────────────────────

    void testEmptyInputReturnsEmpty()
    {
        AnomalyDetector det;
        const auto results = det.detectAnomalies({});
        QVERIFY2(results.isEmpty(),
                 "detectAnomalies({}) must return an empty list");
    }

    void testEmptyInputDoesNotSetFitted()
    {
        AnomalyDetector det;
        QVERIFY(!det.isFitted());
        det.detectAnomalies({});
        QVERIFY(!det.isFitted());
    }

    // ── combinedScore in [0,1] for all cases ─────────────────────────────────

    void testCombinedScoreInRangeNormalBatch()
    {
        AnomalyDetector det;
        QVector<AnomalyFeatureVector> events;
        for (int i = 0; i < 20; ++i)
            events.append(ev(QString("e%1").arg(i), 51.0 + i * 0.05, -0.1, 100.0 + i * 2.0));
        const auto results = det.detectAnomalies(events);
        QCOMPARE(results.size(), 20);
        for (const auto& sig : results) {
            QVERIFY2(sig.combinedScore >= 0.0 && sig.combinedScore <= 1.0,
                     qPrintable(QString("event %1: combinedScore %2 not in [0,1]")
                         .arg(sig.eventId).arg(sig.combinedScore)));
        }
    }

    void testCombinedScoreInRangeSingleEvent()
    {
        AnomalyDetector det;
        const auto results = det.detectAnomalies({ev("x", 51.5, -0.1, 100.0)});
        QCOMPARE(results.size(), 1);
        QVERIFY2(results[0].combinedScore >= 0.0 && results[0].combinedScore <= 1.0,
                 qPrintable(QString("single-event combinedScore %1 not in [0,1]")
                     .arg(results[0].combinedScore)));
    }

    // ── Clearly outlier event (far from all others) gets isAnomaly = true ─────
    //
    // Bug fix applied: m_anomalyThreshold was 0.7, but the maximum achievable
    // combinedScore is exactly 0.7 (lofScore ≤ 1.0 always → lofNorm ≤ 0.25,
    // max combined = 0.4+0.1+0.1+0.1 = 0.7). Threshold lowered to 0.65 so that
    // events with all four signal components saturated are correctly flagged.

    void testClearOutlierFlaggedAsAnomaly()
    {
        AnomalyDetector det;
        QVector<AnomalyFeatureVector> events;
        // 20 cluster events: compact in lat, same lon, same time.
        for (int i = 0; i < 20; ++i)
            events.append(ev(QString("c%1").arg(i), 51.0 + i * 0.01, -0.1, 100.0));
        // One far outlier: very different lat, lon, and time.
        events.append(ev("outlier", 60.0, 10.0, 500.0));

        const auto results = det.detectAnomalies(events);
        QCOMPARE(results.size(), 21);

        bool found   = false;
        bool flagged = false;
        for (const auto& sig : results) {
            if (sig.eventId == QStringLiteral("outlier")) {
                found   = true;
                flagged = sig.isAnomaly;
                // Also verify the component signals are present.
                QVERIFY2(sig.isolationScore > 0.5,
                         qPrintable(QString("outlier isolationScore=%1 should be > 0.5")
                             .arg(sig.isolationScore)));
                break;
            }
        }
        QVERIFY2(found,   "Outlier event not found in results");
        QVERIFY2(flagged, "Clearly outlier event should have isAnomaly = true");
    }

    void testNormalEventNotFlaggedAsAnomaly()
    {
        AnomalyDetector det;
        QVector<AnomalyFeatureVector> events;
        for (int i = 0; i < 20; ++i)
            events.append(ev(QString("c%1").arg(i), 51.0 + i * 0.01, -0.1, 100.0));

        const auto results = det.detectAnomalies(events);
        // Event exactly at centroid of the cluster should not be anomalous.
        // centroid ≈ lat=51.095, lon=-0.1, t=100
        // Any in-cluster event has small isoScore and low z-scores.
        int anomalies = 0;
        for (const auto& sig : results)
            if (sig.isAnomaly) ++anomalies;
        // No in-cluster event should be flagged — combined score stays well below threshold.
        QVERIFY2(anomalies == 0,
                 qPrintable(QString("%1 in-cluster events wrongly flagged as anomaly")
                     .arg(anomalies)));
    }

    // ── isFitted flag set after fit() ────────────────────────────────────────

    void testIsFittedAfterExplicitFit()
    {
        AnomalyDetector det;
        QVERIFY(!det.isFitted());
        QVector<AnomalyFeatureVector> data;
        for (int i = 0; i < 5; ++i)
            data.append(ev(QString("d%1").arg(i), 51.0 + i * 0.1, -0.1, 100.0 + i));
        det.fit(data);
        QVERIFY(det.isFitted());
    }

    void testAutoFitOnFirstDetect()
    {
        AnomalyDetector det;
        QVERIFY(!det.isFitted());
        QVector<AnomalyFeatureVector> events;
        for (int i = 0; i < 5; ++i)
            events.append(ev(QString("e%1").arg(i), 51.0 + i * 0.1, -0.1, 100.0));
        det.detectAnomalies(events);
        QVERIFY(det.isFitted());
    }
};

QTEST_GUILESS_MAIN(AnomalyDetectorDeep3Test)
#include "test_anomaly_detector_deep3.moc"
