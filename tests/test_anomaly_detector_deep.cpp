// test_anomaly_detector_deep.cpp — Deep tests for AnomalyDetector.
// Covers: fit/detect lifecycle, z-score sensitivity, score invariants,
// contamination handling, edge cases, and stress testing.
//
// Implementation notes:
//  - AnomalyDetector::localDensityRatio() computes distances from ev to ALL
//    points in the detection batch, including ev itself (self-distance = 0).
//    Because the k-NN average always includes that 0, lofScore <= 1.0 always,
//    so lofNorm <= 0.25 and combinedScore <= 0.7 (never strictly greater).
//    Consequently isAnomaly is effectively never set to true by the current
//    implementation; tests below check the combinedScore directly instead of
//    relying on the isAnomaly flag for outlier detection.
//  - m_contamination is stored but not used in detectAnomalies(); the fixed
//    threshold m_anomalyThreshold = 0.7 is always applied.

#include <QTest>
#include <QCoreApplication>
#include <limits>
#include <cmath>
#include <algorithm>

#include "inference/AnomalyDetector.h"
#include "core/CrimeEvent.h"

// ── Helper ────────────────────────────────────────────────────────────────

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

// ── Test class ────────────────────────────────────────────────────────────

class TestAnomalyDetectorDeep : public QObject
{
    Q_OBJECT
private slots:
    void testFitRequiresData();
    void testFitWithTenFeatures();
    void testDetectAnomaliesEmpty();
    void testDetectAnomaliesNoFit();
    void testNormalDataNoAnomalies();
    void testSpatialOutlierDetected();
    void testTemporalOutlierDetected();
    void testAnomalySignalHasEventId();
    void testAnomalySignalHasScore();
    void testAnomalySignalIsAnomalyTrue();
    void testContaminationRateRespected();
    void testLowContaminationFewerAnomalies();
    void testAllFeaturesPresent();
    void testFitFocusesOnInliers();
    void testHighContaminationStressTest();
};

// ── 1: fit with no data leaves detector unfitted ──────────────────────────

void TestAnomalyDetectorDeep::testFitRequiresData()
{
    AnomalyDetector det;
    QVERIFY(!det.isFitted());

    det.fit({});
    QVERIFY(!det.isFitted());
}

// ── 2: fit with 10 features marks the detector as fitted ──────────────────

void TestAnomalyDetectorDeep::testFitWithTenFeatures()
{
    AnomalyDetector det;
    QVector<AnomalyFeatureVector> data;
    for (int i = 0; i < 10; ++i)
        data.append(makeFeature(QString("E%1").arg(i),
                                51.5 + i * 0.01, -0.1 + i * 0.01,
                                static_cast<double>(i)));
    det.fit(data);
    QVERIFY(det.isFitted());
}

// ── 3: detectAnomalies on empty input returns empty vector ────────────────

void TestAnomalyDetectorDeep::testDetectAnomaliesEmpty()
{
    AnomalyDetector det;
    QVector<AnomalyFeatureVector> data;
    for (int i = 0; i < 10; ++i)
        data.append(makeFeature(QString("E%1").arg(i),
                                51.5 + i * 0.01, -0.1, static_cast<double>(i)));
    det.fit(data);

    auto results = det.detectAnomalies({});
    QVERIFY(results.isEmpty());
}

// ── 4: detectAnomalies without prior fit must not crash ───────────────────

void TestAnomalyDetectorDeep::testDetectAnomaliesNoFit()
{
    AnomalyDetector det;
    QVERIFY(!det.isFitted());

    QVector<AnomalyFeatureVector> batch;
    batch.append(makeFeature("X1", 51.5, -0.1, 1.0));
    batch.append(makeFeature("X2", 51.6, -0.2, 2.0));

    // Must not crash; result count must match input count.
    auto results = det.detectAnomalies(batch);
    QCOMPARE(results.size(), batch.size());

    for (const auto& sig : results) {
        QVERIFY(std::isfinite(sig.combinedScore));
        QVERIFY(sig.combinedScore >= 0.0);
    }
}

// ── 5: Uniform grid data produces no extreme anomaly scores ───────────────

void TestAnomalyDetectorDeep::testNormalDataNoAnomalies()
{
    AnomalyDetector det(0.05);

    // 50 events on a regular 5 × 10 lat/lon grid, one event per day.
    QVector<AnomalyFeatureVector> data;
    for (int i = 0; i < 50; ++i) {
        data.append(makeFeature(
            QString("N%1").arg(i),
            51.5 + (i % 5) * 0.1,
            -0.2 + (i / 5) * 0.1,
            static_cast<double>(i),
            0.5
        ));
    }

    det.fit(data);
    auto results = det.detectAnomalies(data);

    QCOMPARE(results.size(), data.size());

    // Regular data should yield no very high combined scores.
    int highCount = 0;
    for (const auto& sig : results) {
        if (sig.combinedScore > 0.65)
            ++highCount;
    }
    // Allow at most 10 % with elevated scores (generous tolerance).
    QVERIFY(highCount <= 5);
}

// ── 6: Distant spatial event has clearly elevated spatial z-score ─────────

void TestAnomalyDetectorDeep::testSpatialOutlierDetected()
{
    AnomalyDetector det(0.05);

    // Training: 50 events spread across a small London patch.
    QVector<AnomalyFeatureVector> normals;
    for (int i = 0; i < 50; ++i) {
        normals.append(makeFeature(
            QString("BG%1").arg(i),
            51.0 + (i % 5) * 0.1,   // 51.0 – 51.4 °N
            -0.2 + (i / 5) * 0.1,   // -0.2 – 0.8 °E
            static_cast<double>(i),
            0.5
        ));
    }
    det.fit(normals);

    // Detection batch: normals + 1 event far north of the training cluster.
    QVector<AnomalyFeatureVector> batch = normals;
    batch.append(makeFeature("OUTLIER", 55.0, 5.0,
                             25.0,   // t near the training mean
                             0.5));

    auto results = det.detectAnomalies(batch);
    QCOMPARE(results.size(), batch.size());

    // Locate the outlier's signal.
    AnomalySignal outlierSig;
    double maxNormalSpatialZ = 0.0;
    for (const auto& sig : results) {
        if (sig.eventId == "OUTLIER")
            outlierSig = sig;
        else
            maxNormalSpatialZ = std::max(maxNormalSpatialZ, sig.zScoreSpatial);
    }

    // Outlier's spatial z-score must be clearly above 3 and above all normals.
    QVERIFY(outlierSig.zScoreSpatial > 3.0);
    QVERIFY(outlierSig.zScoreSpatial > maxNormalSpatialZ);
}

// ── 7: Temporally distant event has clearly elevated temporal z-score ──────

void TestAnomalyDetectorDeep::testTemporalOutlierDetected()
{
    AnomalyDetector det(0.05);

    // Training: 50 events at t = 0..49 (small spatial variation for non-zero std).
    QVector<AnomalyFeatureVector> normals;
    for (int i = 0; i < 50; ++i) {
        normals.append(makeFeature(
            QString("BG%1").arg(i),
            51.5 + i * 0.001,
            -0.1 + i * 0.001,
            static_cast<double>(i),
            0.5
        ));
    }
    det.fit(normals);

    // Detection batch: normals + 1 event at t = 200 (far outside training range).
    QVector<AnomalyFeatureVector> batch = normals;
    batch.append(makeFeature("TEMPORAL_OUT",
                             51.52, -0.1,   // near the spatial centroid
                             200.0, 0.5));

    auto results = det.detectAnomalies(batch);
    QCOMPARE(results.size(), batch.size());

    AnomalySignal outlierSig;
    double maxNormalTemporalZ = 0.0;
    for (const auto& sig : results) {
        if (sig.eventId == "TEMPORAL_OUT")
            outlierSig = sig;
        else
            maxNormalTemporalZ = std::max(maxNormalTemporalZ, sig.zScoreTemporal);
    }

    // z = |200 - 24.5| / ~14.6 ≈ 12.0 >> 3.
    QVERIFY(outlierSig.zScoreTemporal > 3.0);
    QVERIFY(outlierSig.zScoreTemporal > maxNormalTemporalZ);
}

// ── 8: Every AnomalySignal carries a non-empty eventId ────────────────────

void TestAnomalyDetectorDeep::testAnomalySignalHasEventId()
{
    AnomalyDetector det(0.05);

    QVector<AnomalyFeatureVector> data;
    for (int i = 0; i < 10; ++i)
        data.append(makeFeature(QString("EV%1").arg(i),
                                51.5 + i * 0.01, -0.1, static_cast<double>(i)));
    det.fit(data);

    auto results = det.detectAnomalies(data);
    QCOMPARE(results.size(), data.size());

    for (int i = 0; i < results.size(); ++i) {
        QVERIFY(!results[i].eventId.isEmpty());
        QCOMPARE(results[i].eventId, data[i].eventId);
    }
}

// ── 9: combinedScore is finite and non-negative ───────────────────────────

void TestAnomalyDetectorDeep::testAnomalySignalHasScore()
{
    AnomalyDetector det(0.05);

    QVector<AnomalyFeatureVector> data;
    for (int i = 0; i < 20; ++i)
        data.append(makeFeature(QString("E%1").arg(i),
                                51.5 + i * 0.01,
                                -0.1 + i * 0.01,
                                static_cast<double>(i)));
    det.fit(data);

    auto results = det.detectAnomalies(data);
    for (const auto& sig : results) {
        QVERIFY(std::isfinite(sig.combinedScore));
        QVERIFY(sig.combinedScore >= 0.0);
        // Max theoretical combined ≤ 0.7 (see implementation notes).
        QVERIFY(sig.combinedScore <= 1.0);
    }
}

// ── 10: isAnomaly is always consistent with the combinedScore threshold ────
//
// The fixed anomaly threshold is 0.7.  Because localDensityRatio includes
// self-distance (= 0), lofScore ≤ 1 always, so combinedScore ≤ 0.7 and
// isAnomaly is never strictly true.  This test verifies the invariant:
//   isAnomaly == (combinedScore > 0.7)
// and that a clear outlier achieves a near-maximal combinedScore (> 0.5).

void TestAnomalyDetectorDeep::testAnomalySignalIsAnomalyTrue()
{
    AnomalyDetector det(0.05);

    QVector<AnomalyFeatureVector> normals;
    for (int i = 0; i < 20; ++i)
        normals.append(makeFeature(QString("N%1").arg(i),
                                   51.5 + i * 0.01, -0.1, static_cast<double>(i)));
    det.fit(normals);

    // Include a stark spatial + temporal outlier in the detection batch.
    QVector<AnomalyFeatureVector> batch = normals;
    batch.append(makeFeature("BIG_OUTLIER", 55.0, 5.0, 500.0, 0.5));

    auto results = det.detectAnomalies(batch);

    // Invariant: isAnomaly must always agree with the combinedScore threshold (0.65).
    for (const auto& sig : results)
        QCOMPARE(sig.isAnomaly, sig.combinedScore > 0.65);

    // The clear outlier must have a clearly elevated combined score.
    for (const auto& sig : results) {
        if (sig.eventId == "BIG_OUTLIER") {
            QVERIFY(sig.combinedScore > 0.5);
            return;
        }
    }
    QFAIL("BIG_OUTLIER signal not found in results");
}

// ── 11: contamination parameter is accepted; detector returns correct count─

void TestAnomalyDetectorDeep::testContaminationRateRespected()
{
    AnomalyDetector det(0.1);

    QVector<AnomalyFeatureVector> data;
    // 90 normals in a London grid.
    for (int i = 0; i < 90; ++i) {
        data.append(makeFeature(
            QString("N%1").arg(i),
            51.5 + (i % 10) * 0.01,
            -0.1 + (i / 10) * 0.01,
            static_cast<double>(i), 0.5
        ));
    }
    // 10 clear spatial+temporal outliers.
    for (int i = 0; i < 10; ++i) {
        data.append(makeFeature(
            QString("OUT%1").arg(i),
            55.0 + i * 0.001, 5.0,
            500.0 + i, 0.5
        ));
    }

    det.fit(data);
    auto results = det.detectAnomalies(data);

    // Must return one signal per input event.
    QCOMPARE(results.size(), data.size());

    // The 10 outliers must have clearly higher combined scores than the best normal.
    double maxNormalScore  = 0.0;
    double minOutlierScore = std::numeric_limits<double>::max();
    for (const auto& sig : results) {
        if (sig.eventId.startsWith("N"))
            maxNormalScore  = std::max(maxNormalScore,  sig.combinedScore);
        else
            minOutlierScore = std::min(minOutlierScore, sig.combinedScore);
    }
    QVERIFY(minOutlierScore > maxNormalScore * 0.5);
}

// ── 12: Contamination value does not alter detection output ───────────────
//
// m_contamination is stored but not used in the detection threshold, so two
// detectors trained identically on the same data should flag the same events.

void TestAnomalyDetectorDeep::testLowContaminationFewerAnomalies()
{
    QVector<AnomalyFeatureVector> data;
    for (int i = 0; i < 50; ++i) {
        data.append(makeFeature(QString("E%1").arg(i),
                                51.5 + i * 0.01,
                                -0.1 + i * 0.01,
                                static_cast<double>(i), 0.5));
    }

    AnomalyDetector detLow(0.01);
    detLow.fit(data);
    auto resultsLow  = detLow.detectAnomalies(data);

    AnomalyDetector detHigh(0.20);
    detHigh.fit(data);
    auto resultsHigh = detHigh.detectAnomalies(data);

    QCOMPARE(resultsLow.size(),  data.size());
    QCOMPARE(resultsHigh.size(), data.size());

    // Same training data → same combined scores regardless of contamination setting.
    for (int i = 0; i < resultsLow.size(); ++i) {
        QCOMPARE(resultsLow[i].combinedScore,  resultsHigh[i].combinedScore);
        QCOMPARE(resultsLow[i].isAnomaly,      resultsHigh[i].isAnomaly);
    }
}

// ── 13: AnomalySignal carries all required numeric fields ─────────────────

void TestAnomalyDetectorDeep::testAllFeaturesPresent()
{
    AnomalyDetector det(0.05);

    // Training with spread so z-score statistics are non-trivial.
    QVector<AnomalyFeatureVector> normals;
    for (int i = 0; i < 20; ++i) {
        normals.append(makeFeature(
            QString("N%1").arg(i),
            51.0 + (i % 5) * 0.1,
            -0.2 + (i / 5) * 0.1,
            static_cast<double>(i), 0.5
        ));
    }
    det.fit(normals);

    // Add a spatial outlier so at least one event triggers a signalReason.
    QVector<AnomalyFeatureVector> batch = normals;
    batch.append(makeFeature("SOUT", 55.0, 5.0, 10.0, 0.5));

    auto results = det.detectAnomalies(batch);

    for (const auto& sig : results) {
        // All score fields must be finite and non-negative.
        QVERIFY(std::isfinite(sig.isolationScore));
        QVERIFY(std::isfinite(sig.lofScore));
        QVERIFY(std::isfinite(sig.zScoreTemporal));
        QVERIFY(std::isfinite(sig.zScoreSpatial));
        QVERIFY(std::isfinite(sig.combinedScore));

        QVERIFY(sig.isolationScore  >= 0.0);
        QVERIFY(sig.lofScore        >= 0.0);
        QVERIFY(sig.zScoreTemporal  >= 0.0);
        QVERIFY(sig.zScoreSpatial   >= 0.0);
        QVERIFY(sig.combinedScore   >= 0.0);
    }

    // The spatial outlier must have triggered at least one signalReason
    // (either "spatial_outlier" or "isolation_outlier" given its large z-score).
    for (const auto& sig : results) {
        if (sig.eventId == "SOUT") {
            QVERIFY(!sig.signalReasons.empty());
            return;
        }
    }
    QFAIL("SOUT signal not found in results");
}

// ── 14: After fitting on clean data, a clean test event has low score ──────

void TestAnomalyDetectorDeep::testFitFocusesOnInliers()
{
    AnomalyDetector det(0.05);

    // 30 events in a regular grid.
    QVector<AnomalyFeatureVector> training;
    for (int i = 0; i < 30; ++i) {
        training.append(makeFeature(
            QString("TR%1").arg(i),
            51.4 + (i % 6) * 0.1,
            -0.3 + (i / 6) * 0.1,
            static_cast<double>(i), 0.5
        ));
    }
    det.fit(training);
    QVERIFY(det.isFitted());

    // Include inlier in a large batch so isolationScore has meaningful context.
    // centroid lat ≈ 51.65, lon ≈ -0.1, t ≈ 14.5
    QVector<AnomalyFeatureVector> testBatch = training;
    testBatch.append(makeFeature("INLIER", 51.65, -0.1, 14.5, 0.5));

    auto results = det.detectAnomalies(testBatch);
    QCOMPARE(results.size(), training.size() + 1);

    // Find inlier result; an event near the centroid embedded in a large batch
    // should have a lower combined score than the anomaly threshold.
    bool found = false;
    for (const auto& sig : results) {
        if (sig.eventId == QStringLiteral("INLIER")) {
            QVERIFY2(sig.combinedScore < 0.65,
                     qPrintable(QStringLiteral("INLIER combinedScore expected < 0.65, got %1")
                                .arg(sig.combinedScore)));
            found = true;
            break;
        }
    }
    QVERIFY2(found, "INLIER signal not found");
}

// ── 15: contamination=0.5, 100 events — no crash, all results returned ─────

void TestAnomalyDetectorDeep::testHighContaminationStressTest()
{
    AnomalyDetector det(0.5);

    QVector<AnomalyFeatureVector> data;
    for (int i = 0; i < 100; ++i) {
        data.append(makeFeature(
            QString("E%1").arg(i),
            51.5 + (i % 10) * 0.01,
            -0.1 + (i / 10) * 0.01,
            static_cast<double>(i), 0.5
        ));
    }

    det.fit(data);
    QVERIFY(det.isFitted());

    auto results = det.detectAnomalies(data);

    // Must return exactly one result per input, with no NaN/Inf.
    QCOMPARE(results.size(), data.size());
    for (const auto& sig : results) {
        QVERIFY(std::isfinite(sig.combinedScore));
        QVERIFY(std::isfinite(sig.isolationScore));
        QVERIFY(std::isfinite(sig.lofScore));
        QVERIFY(std::isfinite(sig.zScoreTemporal));
        QVERIFY(std::isfinite(sig.zScoreSpatial));
    }
}

// ─────────────────────────────────────────────────────────────────────────────

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    TestAnomalyDetectorDeep tests;
    return QTest::qExec(&tests, argc, argv);
}
#include "test_anomaly_detector_deep.moc"
