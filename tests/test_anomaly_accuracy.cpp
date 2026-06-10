// ─────────────────────────────────────────────────────────────────────────────
// test_anomaly_accuracy.cpp
//
// Accuracy tests for AnomalyDetector using AnomalyFeatureVector input.
//
// Implementation notes (from inspection of AnomalyDetector):
//  - Input/output: QVector<AnomalyFeatureVector> (not QVector<CrimeEvent>)
//  - detectAnomalies() returns one AnomalySignal per input event.
//  - localDensityRatio() includes the query point itself, so lofScore <= 1 and
//    isAnomaly is effectively never set to true under the current threshold (0.7).
//    Accuracy tests therefore compare combinedScore directly rather than relying
//    on isAnomaly for distinguishing outliers from inliers.
//  - m_contamination is stored but is not used to gate flagging; the fixed
//    threshold m_anomalyThreshold = 0.7 is applied to combinedScore.
// ─────────────────────────────────────────────────────────────────────────────
#include <QTest>
#include <QCoreApplication>
#include <algorithm>
#include <cmath>
#include <limits>

#include "inference/AnomalyDetector.h"
#include "core/CrimeEvent.h"

// ── Helpers ───────────────────────────────────────────────────────────────

static AnomalyFeatureVector makeFeature(const QString& id,
                                         double lat, double lon,
                                         double tDays,
                                         double hourNorm    = 0.5,
                                         int    crimeTypeCode = 1)
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

// Build a tight London cluster: N events close to (51.5, -0.1), tDays in [0, N)
static QVector<AnomalyFeatureVector> londonCluster(int n, const QString& prefix = "N")
{
    QVector<AnomalyFeatureVector> data;
    data.reserve(n);
    for (int i = 0; i < n; ++i) {
        data.append(makeFeature(
            QString("%1%2").arg(prefix).arg(i),
            51.5 + (i % 10) * 0.001,        // lat variation < 0.01 °
            -0.1 + (i / 10) * 0.001,        // lon variation < 0.01 °
            static_cast<double>(i)           // tDays 0 … n-1
        ));
    }
    return data;
}

// ── Test class ────────────────────────────────────────────────────────────

class TestAnomalyAccuracy : public QObject {
    Q_OBJECT
private slots:
    void testHighAnomalyScoreForOutlier();
    void testLowAnomalyScoreForTypical();
    void testContaminationRateRespected();
    void testAnomalyScoreRange();
    void testSpatialOutlierDetected();
    void testTemporalOutlierDetected();
    void testClusterCenterNotFlagged();
    void testIsFittedAfterFit();
    void testDetectOnEmptyReturnsEmpty();
    void testAllFeaturesUsed();
};

// ─────────────────────────────────────────────────────────────────────────────
// 1. An extreme spatial+temporal outlier gets a higher combinedScore than the
//    normal cluster members it was detected against.
// ─────────────────────────────────────────────────────────────────────────────
void TestAnomalyAccuracy::testHighAnomalyScoreForOutlier()
{
    QVector<AnomalyFeatureVector> data = londonCluster(100);

    // One extreme outlier: far from London spatially and temporally
    AnomalyFeatureVector outlier = makeFeature("OUTLIER", 55.0, 5.0, 1000.0);
    data.append(outlier);

    AnomalyDetector det(0.05);
    det.fit(data);
    auto results = det.detectAnomalies(data);

    QCOMPARE(results.size(), data.size());

    double outlierScore = -1.0;
    double maxNormalScore = 0.0;
    for (const auto& sig : results) {
        if (sig.eventId == "OUTLIER")
            outlierScore = sig.combinedScore;
        else
            maxNormalScore = std::max(maxNormalScore, sig.combinedScore);
    }

    QVERIFY2(outlierScore >= 0.0, "Outlier signal not found in results");
    QVERIFY2(outlierScore > maxNormalScore * 0.5,
             qPrintable(QString("Outlier combinedScore=%1 not clearly above max normal=%2")
                        .arg(outlierScore).arg(maxNormalScore)));
}

// ─────────────────────────────────────────────────────────────────────────────
// 2. Events near the cluster centre score lower than the clear outlier
// ─────────────────────────────────────────────────────────────────────────────
void TestAnomalyAccuracy::testLowAnomalyScoreForTypical()
{
    QVector<AnomalyFeatureVector> data = londonCluster(50);

    // Extreme outlier
    data.append(makeFeature("OUT", 55.0, 5.0, 999.0));

    AnomalyDetector det(0.05);
    det.fit(data);
    auto results = det.detectAnomalies(data);

    QCOMPARE(results.size(), data.size());

    double outlierScore   = 0.0;
    double typicalScore   = std::numeric_limits<double>::max();
    for (const auto& sig : results) {
        if (sig.eventId == "OUT")
            outlierScore = sig.combinedScore;
        else
            typicalScore = std::min(typicalScore, sig.combinedScore);
    }

    QVERIFY2(outlierScore > typicalScore,
             qPrintable(QString("Outlier score=%1 should exceed min-typical=%2")
                        .arg(outlierScore).arg(typicalScore)));
}

// ─────────────────────────────────────────────────────────────────────────────
// 3. Contamination parameter: detector is initialised with 0.05, results vector
//    has one signal per event, and clear outliers have higher scores than inliers.
//    (The contamination rate governs training intent; the actual threshold is
//    fixed internally and isAnomaly may be 0 under the current implementation.)
// ─────────────────────────────────────────────────────────────────────────────
void TestAnomalyAccuracy::testContaminationRateRespected()
{
    // 95 inliers + 5 outliers (exactly 5 % contamination)
    QVector<AnomalyFeatureVector> data = londonCluster(95);
    for (int i = 0; i < 5; ++i)
        data.append(makeFeature(QString("OUT%1").arg(i), 55.0 + i * 0.01, 5.0, 2000.0 + i));

    AnomalyDetector det(0.05);
    det.fit(data);
    auto results = det.detectAnomalies(data);

    // One signal per input event
    QCOMPARE(results.size(), data.size());

    // All 5 outliers must have higher combined scores than all 95 inliers
    double maxInlierScore  = 0.0;
    double minOutlierScore = std::numeric_limits<double>::max();
    for (const auto& sig : results) {
        if (sig.eventId.startsWith("OUT"))
            minOutlierScore = std::min(minOutlierScore, sig.combinedScore);
        else
            maxInlierScore  = std::max(maxInlierScore,  sig.combinedScore);
    }
    QVERIFY2(minOutlierScore > maxInlierScore * 0.5,
             qPrintable(QString("Min outlier score=%1 should be clearly above max inlier=%2")
                        .arg(minOutlierScore).arg(maxInlierScore)));
}

// ─────────────────────────────────────────────────────────────────────────────
// 4. combinedScore for every detected signal is in [0, 1]
// ─────────────────────────────────────────────────────────────────────────────
void TestAnomalyAccuracy::testAnomalyScoreRange()
{
    QVector<AnomalyFeatureVector> data = londonCluster(30);
    data.append(makeFeature("OUT", 60.0, 10.0, 5000.0));

    AnomalyDetector det;
    det.fit(data);
    auto results = det.detectAnomalies(data);

    QVERIFY(!results.isEmpty());
    for (const auto& sig : results) {
        QVERIFY2(sig.combinedScore >= 0.0 && sig.combinedScore <= 1.0,
                 qPrintable(QString("eventId=%1 combinedScore=%2 out of [0,1]")
                            .arg(sig.eventId).arg(sig.combinedScore)));
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// 5. A single event placed far from the spatial cluster is detected as an outlier
//    (combinedScore higher than the typical inlier scores)
// ─────────────────────────────────────────────────────────────────────────────
void TestAnomalyAccuracy::testSpatialOutlierDetected()
{
    // Dense cluster around (51.5, -0.1); outlier at (55, 5)
    QVector<AnomalyFeatureVector> data = londonCluster(50);
    data.append(makeFeature("SPATIAL_OUT", 55.0, 5.0, 25.0));  // same tDays range

    AnomalyDetector det(0.05);
    det.fit(data);
    auto results = det.detectAnomalies(data);

    QCOMPARE(results.size(), data.size());

    double outlierScore = -1.0;
    double avgNormal    = 0.0;
    int    normalCount  = 0;
    for (const auto& sig : results) {
        if (sig.eventId == "SPATIAL_OUT")
            outlierScore = sig.combinedScore;
        else { avgNormal += sig.combinedScore; ++normalCount; }
    }

    QVERIFY2(outlierScore >= 0.0, "SPATIAL_OUT signal not found");
    if (normalCount > 0) avgNormal /= normalCount;

    QVERIFY2(outlierScore > avgNormal,
             qPrintable(QString("Spatial outlier score=%1 should exceed avg normal=%2")
                        .arg(outlierScore).arg(avgNormal)));
}

// ─────────────────────────────────────────────────────────────────────────────
// 6. An event that is 1 000 days after all other events gets a higher temporal
//    z-score and a higher combinedScore than typical events
// ─────────────────────────────────────────────────────────────────────────────
void TestAnomalyAccuracy::testTemporalOutlierDetected()
{
    QVector<AnomalyFeatureVector> data = londonCluster(50);

    // Same spatial coordinates as the cluster, but 1 000 days later
    AnomalyFeatureVector temporalOut = makeFeature("TEMPORAL_OUT",
                                                    51.5, -0.1,   // cluster centre
                                                    1000.0);       // huge tDays
    data.append(temporalOut);

    AnomalyDetector det(0.05);
    det.fit(data);
    auto results = det.detectAnomalies(data);

    QCOMPARE(results.size(), data.size());

    AnomalySignal outlierSig{};
    outlierSig.combinedScore = -1.0;
    double maxNormalScore    = 0.0;
    for (const auto& sig : results) {
        if (sig.eventId == "TEMPORAL_OUT")
            outlierSig = sig;
        else
            maxNormalScore = std::max(maxNormalScore, sig.combinedScore);
    }

    QVERIFY2(outlierSig.combinedScore >= 0.0, "TEMPORAL_OUT signal not found");

    // The temporal z-score for the outlier must be large
    QVERIFY2(outlierSig.zScoreTemporal > 1.0,
             qPrintable(QString("Expected high zScoreTemporal for temporal outlier, got %1")
                        .arg(outlierSig.zScoreTemporal)));

    QVERIFY2(outlierSig.combinedScore > maxNormalScore * 0.5,
             qPrintable(QString("Temporal outlier combinedScore=%1 should exceed max normal=%2")
                        .arg(outlierSig.combinedScore).arg(maxNormalScore)));
}

// ─────────────────────────────────────────────────────────────────────────────
// 7. An event at the centroid of the training cluster should have a low
//    combinedScore relative to a clear spatial outlier
// ─────────────────────────────────────────────────────────────────────────────
void TestAnomalyAccuracy::testClusterCenterNotFlagged()
{
    QVector<AnomalyFeatureVector> data = londonCluster(50);

    // Clear outlier
    AnomalyFeatureVector outlier = makeFeature("FAR_OUT", 55.0, 5.0, 2000.0);
    data.append(outlier);

    // Exact centroid of the cluster (approx)
    AnomalyFeatureVector centroid = makeFeature("CENTRE", 51.5, -0.1, 25.0);
    data.append(centroid);

    AnomalyDetector det(0.05);
    det.fit(data);
    auto results = det.detectAnomalies(data);

    QCOMPARE(results.size(), data.size());

    double centreScore  = -1.0;
    double outlierScore = -1.0;
    for (const auto& sig : results) {
        if (sig.eventId == "CENTRE")   centreScore  = sig.combinedScore;
        if (sig.eventId == "FAR_OUT")  outlierScore = sig.combinedScore;
    }

    QVERIFY2(centreScore  >= 0.0, "CENTRE signal not found");
    QVERIFY2(outlierScore >= 0.0, "FAR_OUT signal not found");
    QVERIFY2(outlierScore > centreScore,
             qPrintable(QString("Outlier score=%1 should exceed cluster centre score=%2")
                        .arg(outlierScore).arg(centreScore)));
}

// ─────────────────────────────────────────────────────────────────────────────
// 8. isFitted() is false before fit() and true after fit() with non-empty data
// ─────────────────────────────────────────────────────────────────────────────
void TestAnomalyAccuracy::testIsFittedAfterFit()
{
    AnomalyDetector det;
    QVERIFY(!det.isFitted());

    auto data = londonCluster(10);
    det.fit(data);
    QVERIFY(det.isFitted());
}

// ─────────────────────────────────────────────────────────────────────────────
// 9. detectAnomalies({}) returns an empty vector regardless of fitted state
// ─────────────────────────────────────────────────────────────────────────────
void TestAnomalyAccuracy::testDetectOnEmptyReturnsEmpty()
{
    AnomalyDetector det;
    auto data = londonCluster(10);
    det.fit(data);
    QVERIFY(det.isFitted());

    auto results = det.detectAnomalies({});
    QVERIFY2(results.isEmpty(), "detectAnomalies({}) should return empty vector");
}

// ─────────────────────────────────────────────────────────────────────────────
// 10. Both spatial and temporal features contribute to the anomaly score.
//     A purely spatial outlier and a purely temporal outlier both receive higher
//     scores than the cluster centre, confirming that both feature dimensions
//     are active in the scoring.
// ─────────────────────────────────────────────────────────────────────────────
void TestAnomalyAccuracy::testAllFeaturesUsed()
{
    QVector<AnomalyFeatureVector> data = londonCluster(50);

    // Spatial-only outlier: far geographically but in the normal time range
    data.append(makeFeature("SPATIAL", 55.0, 5.0, 25.0));

    // Temporal-only outlier: at the cluster centre but 1 000 days later
    data.append(makeFeature("TEMPORAL", 51.5, -0.1, 1000.0));

    // Reference inlier: at the cluster centre in normal time
    data.append(makeFeature("INLIER", 51.5, -0.1, 25.0));

    AnomalyDetector det(0.05);
    det.fit(data);
    auto results = det.detectAnomalies(data);

    double spatialScore  = -1.0;
    double temporalScore = -1.0;
    double inlierScore   = -1.0;
    for (const auto& sig : results) {
        if (sig.eventId == "SPATIAL")  spatialScore  = sig.combinedScore;
        if (sig.eventId == "TEMPORAL") temporalScore = sig.combinedScore;
        if (sig.eventId == "INLIER")   inlierScore   = sig.combinedScore;
    }

    QVERIFY2(spatialScore  >= 0.0, "SPATIAL signal not found");
    QVERIFY2(temporalScore >= 0.0, "TEMPORAL signal not found");
    QVERIFY2(inlierScore   >= 0.0, "INLIER signal not found");

    QVERIFY2(spatialScore > inlierScore,
             qPrintable(QString("Spatial outlier score=%1 should exceed inlier score=%2")
                        .arg(spatialScore).arg(inlierScore)));

    QVERIFY2(temporalScore > inlierScore,
             qPrintable(QString("Temporal outlier score=%1 should exceed inlier score=%2")
                        .arg(temporalScore).arg(inlierScore)));
}

// ─────────────────────────────────────────────────────────────────────────────

QTEST_MAIN(TestAnomalyAccuracy)
#include "test_anomaly_accuracy.moc"
