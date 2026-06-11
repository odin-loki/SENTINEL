#include <QTest>
#include <cmath>
#include <algorithm>

#include "inference/AnomalyDetector.h"
#include "core/CrimeEvent.h"

static AnomalyFeatureVector fv(const QString& id, double lat, double lon,
                                double tDays, double hourNorm = 0.5,
                                int typeCode = 1)
{
    AnomalyFeatureVector f;
    f.eventId       = id;
    f.lat           = lat;
    f.lon           = lon;
    f.tDays         = tDays;
    f.hourNorm      = hourNorm;
    f.crimeTypeCode = typeCode;
    return f;
}

class TestAnomalyDetectorDeep2 : public QObject
{
    Q_OBJECT
private slots:
    void testZScoreOutlierDetected();
    void testIsolationScoreInRange();
    void testIsolatedPointHigherScore();
    void testEmptyInputNoCrash();
    void testSingleElementInput();
};

// Fit on 20 normally-distributed tDays (0..19), then detect on that batch plus
// one point at tDays=200.  The outlier's zScoreTemporal must satisfy:
//   z = |200 - mean| / std  >> 3
// and must exceed every inlier's temporal z-score.
void TestAnomalyDetectorDeep2::testZScoreOutlierDetected()
{
    QVector<AnomalyFeatureVector> train;
    for (int i = 0; i < 20; ++i)
        train.append(fv(QString("N%1").arg(i),
                        51.5 + i * 0.001, -0.1,
                        static_cast<double>(i)));

    AnomalyDetector det;
    det.fit(train);
    QVERIFY(det.isFitted());

    QVector<AnomalyFeatureVector> batch = train;
    batch.append(fv("OUTLIER", 51.51, -0.1, 200.0));

    const auto results = det.detectAnomalies(batch);
    QCOMPARE(results.size(), batch.size());

    double outlierZ    = -1.0;
    double maxNormalZ  =  0.0;
    for (const auto& sig : results) {
        if (sig.eventId == QStringLiteral("OUTLIER"))
            outlierZ = sig.zScoreTemporal;
        else
            maxNormalZ = std::max(maxNormalZ, sig.zScoreTemporal);
    }

    QVERIFY2(outlierZ > 0.0, "OUTLIER signal must have been found");
    QVERIFY2(outlierZ > 3.0,
             qPrintable(QString("Outlier z-temporal %1 should be > 3.0 (|200-9.5|/std)")
                .arg(outlierZ)));
    QVERIFY2(outlierZ > maxNormalZ,
             qPrintable(QString("Outlier z %1 must exceed max inlier z %2")
                .arg(outlierZ).arg(maxNormalZ)));
}

// For every returned AnomalySignal, isolationScore must lie in [0, 1].
// The method normalises by 2*sqrt(4)=4, then clamps with std::min(...,1.0),
// so this should hold even for extreme spatial/temporal outliers.
void TestAnomalyDetectorDeep2::testIsolationScoreInRange()
{
    QVector<AnomalyFeatureVector> data;
    for (int i = 0; i < 30; ++i)
        data.append(fv(QString("E%1").arg(i),
                       51.0 + (i % 5) * 0.2,
                       -0.5 + (i / 5) * 0.2,
                       static_cast<double>(i * 3)));

    // Add two extreme outliers that maximise the distance-from-centroid term.
    data.append(fv("X1",  60.0,  10.0,  500.0));
    data.append(fv("X2",  40.0, -10.0, -100.0));

    AnomalyDetector det;
    const auto results = det.detectAnomalies(data);
    QCOMPARE(results.size(), data.size());

    for (const auto& sig : results) {
        QVERIFY2(sig.isolationScore >= 0.0 && sig.isolationScore <= 1.0,
                 qPrintable(QString("isolationScore %1 for %2 must be in [0,1]")
                    .arg(sig.isolationScore).arg(sig.eventId)));
    }
}

// With 19 tightly clustered points and one clearly remote point ("FAR"),
// the centroid is pulled near the cluster, so FAR has a much larger distance
// from centroid — and thus a strictly higher isolationScore — than every
// clustered point.
void TestAnomalyDetectorDeep2::testIsolatedPointHigherScore()
{
    QVector<AnomalyFeatureVector> data;
    for (int i = 0; i < 19; ++i)
        data.append(fv(QString("C%1").arg(i),
                       51.5 + i * 0.001, -0.1 + i * 0.001,
                       static_cast<double>(i)));
    data.append(fv("FAR", 59.0, 8.0, 500.0));

    AnomalyDetector det;
    const auto results = det.detectAnomalies(data);
    QCOMPARE(results.size(), data.size());

    double farScore        = -1.0;
    double maxClusterScore =  0.0;
    for (const auto& sig : results) {
        if (sig.eventId == QStringLiteral("FAR"))
            farScore = sig.isolationScore;
        else
            maxClusterScore = std::max(maxClusterScore, sig.isolationScore);
    }

    QVERIFY2(farScore >= 0.0, "FAR signal must be present in results");
    QVERIFY2(farScore > maxClusterScore,
             qPrintable(QString("FAR isolation %1 must exceed cluster max %2")
                .arg(farScore).arg(maxClusterScore)));
}

// fit({}) must leave the detector unfitted; detectAnomalies({}) must return empty.
void TestAnomalyDetectorDeep2::testEmptyInputNoCrash()
{
    AnomalyDetector det;
    det.fit({});
    QVERIFY(!det.isFitted());

    const auto results = det.detectAnomalies({});
    QVERIFY(results.isEmpty());
}

// A single-element batch must produce exactly one finite, non-negative signal.
// Internally: isolationScore returns 0 (centroid == point), localDensityRatio
// returns 1.0 (< 2 points), so no division-by-zero or NaN is possible.
void TestAnomalyDetectorDeep2::testSingleElementInput()
{
    AnomalyDetector det;
    QVector<AnomalyFeatureVector> single;
    single.append(fv("ONE", 51.5, -0.1, 42.0, 0.5, 1));

    const auto results = det.detectAnomalies(single);
    QCOMPARE(results.size(), 1);
    QCOMPARE(results[0].eventId, QString("ONE"));

    QVERIFY(std::isfinite(results[0].combinedScore));
    QVERIFY(std::isfinite(results[0].isolationScore));
    QVERIFY(std::isfinite(results[0].zScoreTemporal));
    QVERIFY(std::isfinite(results[0].zScoreSpatial));
    QVERIFY(results[0].combinedScore >= 0.0);
    QVERIFY(results[0].isolationScore >= 0.0);
}

QTEST_GUILESS_MAIN(TestAnomalyDetectorDeep2)
#include "test_anomaly_detector_deep2.moc"
