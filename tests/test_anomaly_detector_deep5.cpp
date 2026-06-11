// test_anomaly_detector_deep5.cpp — Deep audit iteration 17: AnomalyDetector
// Verifies: LOF density ratio, combined-score threshold (0.65), contamination param.

#include <QTest>
#include "inference/AnomalyDetector.h"
#include <algorithm>
#include <cmath>
#include <limits>

class AnomalyDetectorDeep5Test : public QObject
{
    Q_OBJECT

private:
    static AnomalyFeatureVector ev(const QString& id,
                                   double lat, double lon, double tDays,
                                   double hourNorm = 0.5, int crimeCode = 1)
    {
        return {id, lat, lon, tDays, hourNorm, crimeCode};
    }

    static double recomputeCombined(const AnomalySignal& sig)
    {
        const double lofNorm = std::min((sig.lofScore - 1.0) / 4.0 + 0.25, 1.0);
        const double zTNorm  = std::min(sig.zScoreTemporal / 3.0, 1.0);
        const double zSNorm  = std::min(sig.zScoreSpatial  / 3.0, 1.0);
        return 0.4 * sig.isolationScore +
               0.4 * std::max(lofNorm, 0.0) +
               0.1 * zTNorm +
               0.1 * zSNorm;
    }

private slots:

    void testLofNearOneForHomogeneousCluster()
    {
        AnomalyDetector det;
        QVector<AnomalyFeatureVector> events;
        for (int i = 0; i < 25; ++i)
            events.append(ev(QString("c%1").arg(i), 51.50, -0.10, 100.0));

        const auto results = det.detectAnomalies(events);
        for (const auto& sig : results) {
            QVERIFY2(std::abs(sig.lofScore - 1.0) < 0.05,
                     qPrintable(QStringLiteral("identical cluster member %1 lof %2 expected ~1")
                         .arg(sig.eventId).arg(sig.lofScore)));
        }
    }

    void testLofHighestForSpatialOutlier()
    {
        AnomalyDetector det;
        QVector<AnomalyFeatureVector> events;
        for (int i = 0; i < 30; ++i)
            events.append(ev(QString("c%1").arg(i), 51.50 + i * 0.01, -0.10, 100.0));
        events.append(ev("far_outlier", 58.0, 4.0, 100.0));

        const auto results = det.detectAnomalies(events);
        double maxClusterLof = 0.0;
        double outlierLof    = 0.0;
        for (const auto& sig : results) {
            if (sig.eventId == QStringLiteral("far_outlier"))
                outlierLof = sig.lofScore;
            else
                maxClusterLof = std::max(maxClusterLof, sig.lofScore);
        }
        QVERIFY2(outlierLof > maxClusterLof,
                 qPrintable(QStringLiteral("outlier lof %1 should exceed cluster max %2")
                     .arg(outlierLof).arg(maxClusterLof)));
    }

    void testLofIncreasesWithOutlierSeverity()
    {
        AnomalyDetector det;
        QVector<AnomalyFeatureVector> base;
        for (int i = 0; i < 20; ++i)
            base.append(ev(QString("b%1").arg(i), 51.5, -0.1, 100.0));

        auto lofForOutlier = [&](double lat, double lon) {
            QVector<AnomalyFeatureVector> events = base;
            events.append(ev("o", lat, lon, 100.0));
            const auto results = det.detectAnomalies(events);
            for (const auto& sig : results) {
                if (sig.eventId == QStringLiteral("o"))
                    return sig.lofScore;
            }
            return 0.0;
        };

        const double mild   = lofForOutlier(52.0, -0.05);
        const double severe = lofForOutlier(60.0, 5.0);
        QVERIFY2(severe > mild,
                 qPrintable(QStringLiteral("severe lof %1 should exceed mild %2")
                     .arg(severe).arg(mild)));
    }

    void testCombinedScoreMatchesWeightedFormula()
    {
        AnomalyDetector det;
        QVector<AnomalyFeatureVector> events;
        for (int i = 0; i < 15; ++i)
            events.append(ev(QString("e%1").arg(i), 51.0 + i * 0.03, -0.1 + i * 0.01,
                             100.0 + i * 5.0, 0.1 * i, 1 + (i % 3)));
        events.append(ev("outlier", 55.0, 2.0, 900.0, 0.9, 50));

        const auto results = det.detectAnomalies(events);
        for (const auto& sig : results) {
            const double expected = recomputeCombined(sig);
            QVERIFY2(std::abs(sig.combinedScore - expected) < 1e-12,
                     qPrintable(QStringLiteral("event %1 combined %2 vs formula %3")
                         .arg(sig.eventId).arg(sig.combinedScore).arg(expected)));
        }
    }

    void testIsAnomalyEqualsCombinedAboveThreshold()
    {
        AnomalyDetector det;
        QVector<AnomalyFeatureVector> events;
        for (int i = 0; i < 20; ++i)
            events.append(ev(QString("c%1").arg(i), 51.0 + i * 0.01, -0.1, 100.0));
        events.append(ev("outlier", 60.0, 10.0, 500.0));

        const auto results = det.detectAnomalies(events);
        for (const auto& sig : results) {
            const bool expected = sig.combinedScore > 0.65;
            QCOMPARE(sig.isAnomaly, expected);
        }
    }

    void testClearOutlierExceedsCombinedThreshold()
    {
        AnomalyDetector det;
        QVector<AnomalyFeatureVector> events;
        for (int i = 0; i < 25; ++i)
            events.append(ev(QString("c%1").arg(i), 51.0 + i * 0.01, -0.1, 100.0));
        events.append(ev("outlier", 60.0, 10.0, 500.0, 0.95, 99));

        const auto results = det.detectAnomalies(events);
        for (const auto& sig : results) {
            if (sig.eventId == QStringLiteral("outlier")) {
                QVERIFY2(sig.combinedScore > 0.65,
                         qPrintable(QStringLiteral("outlier combined %1 should exceed 0.65")
                             .arg(sig.combinedScore)));
                QVERIFY(sig.isAnomaly);
                return;
            }
        }
        QFAIL("outlier event not found");
    }

    void testContaminationDoesNotAlterDetectionOutput()
    {
        // Known limitation: m_contamination is stored but detection uses fixed 0.65 threshold.
        QVector<AnomalyFeatureVector> data;
        for (int i = 0; i < 40; ++i)
            data.append(ev(QString("e%1").arg(i), 51.5 + i * 0.01, -0.1, static_cast<double>(i)));

        AnomalyDetector detLow(0.01);
        detLow.fit(data);
        const auto resultsLow = detLow.detectAnomalies(data);

        AnomalyDetector detHigh(0.50);
        detHigh.fit(data);
        const auto resultsHigh = detHigh.detectAnomalies(data);

        QCOMPARE(resultsLow.size(), resultsHigh.size());
        for (int i = 0; i < resultsLow.size(); ++i) {
            QCOMPARE(resultsLow[i].combinedScore, resultsHigh[i].combinedScore);
            QCOMPARE(resultsLow[i].isAnomaly, resultsHigh[i].isAnomaly);
            QCOMPARE(resultsLow[i].lofScore, resultsHigh[i].lofScore);
        }
    }

    void testLowDensityReasonWhenLofNormHigh()
    {
        AnomalyDetector det;
        QVector<AnomalyFeatureVector> events;
        for (int i = 0; i < 20; ++i)
            events.append(ev(QString("c%1").arg(i), 51.0 + i * 0.01, -0.1, 100.0));
        events.append(ev("sparse", 59.0, 8.0, 100.0));

        const auto results = det.detectAnomalies(events);
        for (const auto& sig : results) {
            if (sig.eventId != QStringLiteral("sparse"))
                continue;
            const double lofNorm = std::min((sig.lofScore - 1.0) / 4.0 + 0.25, 1.0);
            if (lofNorm > 0.5) {
                const auto it = std::find(sig.signalReasons.begin(), sig.signalReasons.end(),
                                          QStringLiteral("low_density_region"));
                QVERIFY(it != sig.signalReasons.end());
            }
            return;
        }
        QFAIL("sparse outlier not found");
    }
};

QTEST_GUILESS_MAIN(AnomalyDetectorDeep5Test)
#include "test_anomaly_detector_deep5.moc"
