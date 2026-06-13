// test_anomaly_detector_deep7.cpp — Deep audit iteration 22: AnomalyDetector
// Verifies: contamination threshold, single-event isolation, empty batch, auto-fit,
// spatial reason tagging, combined score bounds, anomaly rate sensitivity.

#include <QTest>
#include "inference/AnomalyDetector.h"
#include <algorithm>
#include <cmath>

class AnomalyDetectorDeep7Test : public QObject
{
    Q_OBJECT

private:
    static AnomalyFeatureVector ev(const QString& id,
                                   double lat, double lon, double tDays,
                                   double hourNorm = 0.5, int crimeCode = 1)
    {
        return {id, lat, lon, tDays, hourNorm, crimeCode};
    }

    static QVector<AnomalyFeatureVector> clusteredBatch()
    {
        QVector<AnomalyFeatureVector> events;
        for (int i = 0; i < 20; ++i)
            events.append(ev(QStringLiteral("c%1").arg(i), 51.5, -0.1, 100.0 + i));
        events.append(ev(QStringLiteral("space_outlier"), 59.0, 8.0, 100.0));
        return events;
    }

private slots:
    void testContaminationAffectsAnomalyThreshold();
    void testSingleEventIsolationScoreIsOne();
    void testEmptyDetectReturnsEmpty();
    void testUnfittedAutoFitsOnBatch();
    void testSpatialOutlierGetsSpatialReason();
    void testCombinedScoreBoundedZeroToOne();
    void testHighContaminationFlagsMoreAnomalies();
    void testAutoFitLeavesDetectorFitted();
};

// ─── Tests ─────────────────────────────────────────────────────────────────

void AnomalyDetectorDeep7Test::testContaminationAffectsAnomalyThreshold()
{
    const auto events = clusteredBatch();

    AnomalyDetector lowContam(0.05);
    lowContam.fit(events);
    const auto lowResults = lowContam.detectAnomalies(events);

    AnomalyDetector highContam(0.2);
    highContam.fit(events);
    const auto highResults = highContam.detectAnomalies(events);

    int lowAnomalies = 0;
    int highAnomalies = 0;
    for (const auto& sig : lowResults) {
        if (sig.isAnomaly) ++lowAnomalies;
    }
    for (const auto& sig : highResults) {
        if (sig.isAnomaly) ++highAnomalies;
    }

    QVERIFY2(highAnomalies >= lowAnomalies,
             qPrintable(QStringLiteral("contamination 0.2 flagged %1 vs 0.05 flagged %2")
                            .arg(highAnomalies).arg(lowAnomalies)));

    // Default 0.05 threshold ≈ 0.65; 0.2 threshold clamps to 0.4.
    const auto outlierLow  = std::find_if(lowResults.begin(), lowResults.end(),
        [](const AnomalySignal& s) { return s.eventId == QStringLiteral("space_outlier"); });
    const auto outlierHigh = std::find_if(highResults.begin(), highResults.end(),
        [](const AnomalySignal& s) { return s.eventId == QStringLiteral("space_outlier"); });
    QVERIFY(outlierLow != lowResults.end());
    QVERIFY(outlierHigh != highResults.end());
    QVERIFY(outlierLow->combinedScore > 0.4);
    QVERIFY(outlierHigh->combinedScore > 0.4);
}

void AnomalyDetectorDeep7Test::testSingleEventIsolationScoreIsOne()
{
    AnomalyDetector det;
    const auto results = det.detectAnomalies({
        ev(QStringLiteral("solo"), 51.5, -0.1, 100.0),
    });

    QCOMPARE(results.size(), 1);
    QCOMPARE(results[0].isolationScore, 1.0);
}

void AnomalyDetectorDeep7Test::testEmptyDetectReturnsEmpty()
{
    AnomalyDetector det;
    const auto results = det.detectAnomalies({});
    QVERIFY(results.isEmpty());
    QVERIFY(!det.isFitted());
}

void AnomalyDetectorDeep7Test::testUnfittedAutoFitsOnBatch()
{
    AnomalyDetector det;
    QVERIFY(!det.isFitted());

    const auto events = clusteredBatch();
    const auto results = det.detectAnomalies(events);

    QVERIFY(det.isFitted());
    QCOMPARE(results.size(), events.size());
}

void AnomalyDetectorDeep7Test::testSpatialOutlierGetsSpatialReason()
{
    AnomalyDetector det;
    QVector<AnomalyFeatureVector> events;
    for (int i = 0; i < 18; ++i)
        events.append(ev(QStringLiteral("n%1").arg(i), 51.5, -0.1, 100.0));
    events.append(ev(QStringLiteral("far_away"), 58.0, 7.5, 100.0));

    const auto results = det.detectAnomalies(events);
    for (const auto& sig : results) {
        if (sig.eventId != QStringLiteral("far_away"))
            continue;

        QVERIFY2(sig.zScoreSpatial > 2.0,
                 qPrintable(QStringLiteral("spatial z=%1 should exceed 2.0")
                                .arg(sig.zScoreSpatial)));
        const auto it = std::find(sig.signalReasons.begin(), sig.signalReasons.end(),
                                  QStringLiteral("spatial_outlier"));
        QVERIFY(it != sig.signalReasons.end());
        return;
    }
    QFAIL("far_away event not found in results");
}

void AnomalyDetectorDeep7Test::testCombinedScoreBoundedZeroToOne()
{
    AnomalyDetector det;
    QVector<AnomalyFeatureVector> events;
    for (int i = 0; i < 22; ++i)
        events.append(ev(QStringLiteral("e%1").arg(i),
                         51.0 + i * 0.04, -0.1 + i * 0.008,
                         80.0 + i * 5.0, static_cast<double>(i) / 21.0, i % 6));
    events.append(ev(QStringLiteral("wild"), 62.0, 11.0, 950.0, 1.0, 77));

    const auto results = det.detectAnomalies(events);
    for (const auto& sig : results) {
        QVERIFY2(sig.combinedScore >= 0.0 && sig.combinedScore <= 1.0,
                 qPrintable(QStringLiteral("combined score %1 out of [0,1] for %2")
                                .arg(sig.combinedScore).arg(sig.eventId)));
    }
}

void AnomalyDetectorDeep7Test::testHighContaminationFlagsMoreAnomalies()
{
    const auto events = clusteredBatch();

    AnomalyDetector strictDet(0.05);
    const auto strictResults = strictDet.detectAnomalies(events);

    AnomalyDetector looseDet(0.2);
    const auto looseResults = looseDet.detectAnomalies(events);

    const auto countAnomalies = [](const QVector<AnomalySignal>& anomalySignals) {
        int n = 0;
        for (const auto& sig : anomalySignals)
            if (sig.isAnomaly) ++n;
        return n;
    };

    QVERIFY2(countAnomalies(looseResults) >= countAnomalies(strictResults),
             "higher contamination should not reduce anomaly count on same batch");
}

void AnomalyDetectorDeep7Test::testAutoFitLeavesDetectorFitted()
{
    AnomalyDetector det;
    const auto first = det.detectAnomalies({
        ev(QStringLiteral("a"), 51.5, -0.1, 100.0),
        ev(QStringLiteral("b"), 51.6, -0.09, 101.0),
        ev(QStringLiteral("c"), 51.55, -0.095, 100.5),
    });
    QVERIFY(det.isFitted());

    const auto second = det.detectAnomalies({
        ev(QStringLiteral("d"), 51.52, -0.092, 100.2),
        ev(QStringLiteral("e"), 51.58, -0.088, 100.8),
    });
    QVERIFY(det.isFitted());
    QCOMPARE(second.size(), 2);
    QVERIFY(!first.isEmpty());
}

QTEST_GUILESS_MAIN(AnomalyDetectorDeep7Test)
#include "test_anomaly_detector_deep7.moc"
