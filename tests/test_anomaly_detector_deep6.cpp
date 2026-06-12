// test_anomaly_detector_deep6.cpp — Deep audit iteration 20: AnomalyDetector
// Verifies: signal-reason thresholds, score bounds, explicit-fit parity, event-id fidelity.

#include <QTest>
#include "inference/AnomalyDetector.h"
#include <algorithm>
#include <cmath>

class AnomalyDetectorDeep6Test : public QObject
{
    Q_OBJECT

private:
    static AnomalyFeatureVector ev(const QString& id,
                                   double lat, double lon, double tDays,
                                   double hourNorm = 0.5, int crimeCode = 1)
    {
        return {id, lat, lon, tDays, hourNorm, crimeCode};
    }

private slots:

    void testEventIdsPreservedInResults()
    {
        AnomalyDetector det;
        QVector<AnomalyFeatureVector> events;
        for (int i = 0; i < 12; ++i)
            events.append(ev(QString("evt_%1").arg(i), 51.5, -0.1, 100.0 + i));

        const auto results = det.detectAnomalies(events);
        QCOMPARE(results.size(), events.size());
        for (int i = 0; i < events.size(); ++i)
            QCOMPARE(results[i].eventId, events[i].eventId);
    }

    void testExplicitFitMatchesAutoFitScores()
    {
        QVector<AnomalyFeatureVector> events;
        for (int i = 0; i < 18; ++i)
            events.append(ev(QString("e%1").arg(i), 51.0 + i * 0.02, -0.1 + i * 0.005,
                             100.0 + i * 3.0, 0.05 * i, 1 + (i % 4)));
        events.append(ev("outlier", 57.0, 3.0, 800.0, 0.95, 42));

        AnomalyDetector autoDet;
        const auto autoResults = autoDet.detectAnomalies(events);

        AnomalyDetector fitDet;
        fitDet.fit(events);
        QVERIFY(fitDet.isFitted());
        const auto fitResults = fitDet.detectAnomalies(events);

        QCOMPARE(autoResults.size(), fitResults.size());
        for (int i = 0; i < autoResults.size(); ++i) {
            QCOMPARE(autoResults[i].eventId, fitResults[i].eventId);
            QVERIFY2(std::abs(autoResults[i].combinedScore - fitResults[i].combinedScore) < 1e-12,
                     qPrintable(QStringLiteral("event %1 auto %2 vs fit %3")
                         .arg(autoResults[i].eventId)
                         .arg(autoResults[i].combinedScore)
                         .arg(fitResults[i].combinedScore)));
            QCOMPARE(autoResults[i].isAnomaly, fitResults[i].isAnomaly);
        }
    }

    void testTemporalOutlierAddsSignalReason()
    {
        AnomalyDetector det;
        QVector<AnomalyFeatureVector> events;
        for (int i = 0; i < 20; ++i)
            events.append(ev(QString("c%1").arg(i), 51.5, -0.1, 100.0));
        events.append(ev("time_outlier", 51.5, -0.1, 900.0));

        const auto results = det.detectAnomalies(events);
        for (const auto& sig : results) {
            if (sig.eventId != QStringLiteral("time_outlier"))
                continue;
            QVERIFY2(sig.zScoreTemporal > 2.0,
                     qPrintable(QStringLiteral("temporal z %1 should exceed 2.0")
                         .arg(sig.zScoreTemporal)));
            const auto it = std::find(sig.signalReasons.begin(), sig.signalReasons.end(),
                                      QStringLiteral("temporal_outlier"));
            QVERIFY(it != sig.signalReasons.end());
            return;
        }
        QFAIL("time_outlier not found");
    }

    void testSpatialOutlierAddsSignalReason()
    {
        AnomalyDetector det;
        QVector<AnomalyFeatureVector> events;
        for (int i = 0; i < 20; ++i)
            events.append(ev(QString("c%1").arg(i), 51.5, -0.1, 100.0));
        events.append(ev("space_outlier", 59.0, 8.0, 100.0));

        const auto results = det.detectAnomalies(events);
        for (const auto& sig : results) {
            if (sig.eventId != QStringLiteral("space_outlier"))
                continue;
            QVERIFY2(sig.zScoreSpatial > 2.0,
                     qPrintable(QStringLiteral("spatial z %1 should exceed 2.0")
                         .arg(sig.zScoreSpatial)));
            const auto it = std::find(sig.signalReasons.begin(), sig.signalReasons.end(),
                                      QStringLiteral("spatial_outlier"));
            QVERIFY(it != sig.signalReasons.end());
            return;
        }
        QFAIL("space_outlier not found");
    }

    void testIsolationOutlierAddsSignalReason()
    {
        AnomalyDetector det;
        QVector<AnomalyFeatureVector> events;
        for (int i = 0; i < 15; ++i)
            events.append(ev(QString("c%1").arg(i), 51.5, -0.1, 100.0, 0.5, 1));
        events.append(ev("iso_outlier", 51.5, -0.1, 100.0, 0.5, 99));

        const auto results = det.detectAnomalies(events);
        for (const auto& sig : results) {
            if (sig.eventId != QStringLiteral("iso_outlier"))
                continue;
            QVERIFY2(sig.isolationScore > 0.6,
                     qPrintable(QStringLiteral("isolation %1 should exceed 0.6")
                         .arg(sig.isolationScore)));
            const auto it = std::find(sig.signalReasons.begin(), sig.signalReasons.end(),
                                      QStringLiteral("isolation_outlier"));
            QVERIFY(it != sig.signalReasons.end());
            return;
        }
        QFAIL("iso_outlier not found");
    }

    void testAllScoresBoundedZeroToOne()
    {
        AnomalyDetector det;
        QVector<AnomalyFeatureVector> events;
        for (int i = 0; i < 25; ++i)
            events.append(ev(QString("e%1").arg(i), 51.0 + i * 0.03, -0.1 + i * 0.01,
                             50.0 + i * 10.0, static_cast<double>(i) / 24.0, i % 7));
        events.append(ev("wild", 62.0, 12.0, 999.0, 1.0, 88));

        const auto results = det.detectAnomalies(events);
        for (const auto& sig : results) {
            QVERIFY2(sig.isolationScore >= 0.0 && sig.isolationScore <= 1.0,
                     qPrintable(QStringLiteral("isolation %1 out of [0,1]")
                         .arg(sig.isolationScore)));
            QVERIFY2(sig.combinedScore >= 0.0 && sig.combinedScore <= 1.0,
                     qPrintable(QStringLiteral("combined %1 out of [0,1]")
                         .arg(sig.combinedScore)));
            QVERIFY2(sig.lofScore >= 0.0,
                     qPrintable(QStringLiteral("lof %1 must be non-negative")
                         .arg(sig.lofScore)));
            QVERIFY2(sig.zScoreTemporal >= 0.0 && sig.zScoreSpatial >= 0.0,
                     "z-scores must be non-negative (abs values)");
        }
    }

    void testPrefitRemainsFittedAcrossDetectCalls()
    {
        QVector<AnomalyFeatureVector> train;
        for (int i = 0; i < 30; ++i)
            train.append(ev(QString("t%1").arg(i), 51.5, -0.1, 100.0));

        AnomalyDetector det;
        det.fit(train);
        QVERIFY(det.isFitted());

        const auto first  = det.detectAnomalies(train);
        const auto second = det.detectAnomalies(train);
        QVERIFY(det.isFitted());

        QCOMPARE(first.size(), second.size());
        for (int i = 0; i < first.size(); ++i) {
            QCOMPARE(first[i].combinedScore, second[i].combinedScore);
            QCOMPARE(first[i].isAnomaly, second[i].isAnomaly);
        }
    }

    void testTwoIdenticalEventsLofNearOne()
    {
        AnomalyDetector det;
        const auto results = det.detectAnomalies({
            ev("a", 51.5, -0.1, 100.0),
            ev("b", 51.5, -0.1, 100.0),
        });
        QCOMPARE(results.size(), 2);
        for (const auto& sig : results) {
            QVERIFY2(std::abs(sig.lofScore - 1.0) < 0.1,
                     qPrintable(QStringLiteral("duplicate pair lof %1 expected ~1")
                         .arg(sig.lofScore)));
        }
    }
};

QTEST_GUILESS_MAIN(AnomalyDetectorDeep6Test)
#include "test_anomaly_detector_deep6.moc"
