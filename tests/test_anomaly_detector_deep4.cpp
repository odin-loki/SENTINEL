// Deep audit iteration 14 — AnomalyDetector (deep4)
// Verifies: 5-dim feature space, isolation score, LOF, auto-fit on detectAnomalies.

#include <QTest>
#include "inference/AnomalyDetector.h"
#include <cmath>

class AnomalyDetectorDeep4Test : public QObject
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

    void testEmptyInputDoesNotSetFitted()
    {
        AnomalyDetector det;
        QVERIFY(!det.isFitted());
        det.detectAnomalies({});
        QVERIFY(!det.isFitted());
    }

    void testSingleEventHighIsolationScore()
    {
        AnomalyDetector det;
        const auto results = det.detectAnomalies({ev("e1", 51.5, -0.1, 100.0)});
        QCOMPARE(results.size(), 1);
        QVERIFY2(results[0].isolationScore > 0.5,
                 qPrintable(QStringLiteral("single event isolation %1 should be > 0.5")
                     .arg(results[0].isolationScore)));
    }

    void testIdenticalEventsLowIsolationScore()
    {
        AnomalyDetector det;
        QVector<AnomalyFeatureVector> events;
        for (int i = 0; i < 20; ++i)
            events.append(ev(QString("e%1").arg(i), 51.5, -0.1, 100.0));
        const auto results = det.detectAnomalies(events);
        for (const auto& sig : results) {
            QVERIFY2(sig.isolationScore < 0.5,
                     qPrintable(QStringLiteral("event %1 isolation %2 should be < 0.5")
                         .arg(sig.eventId).arg(sig.isolationScore)));
        }
    }

    void testCrimeTypeOutlierHigherIsolation()
    {
        AnomalyDetector det;
        QVector<AnomalyFeatureVector> events;
        for (int i = 0; i < 15; ++i)
            events.append(ev(QString("c%1").arg(i), 51.5, -0.1, 100.0, 0.5, 1));
        events.append(ev("type_outlier", 51.5, -0.1, 100.0, 0.5, 99));

        const auto results = det.detectAnomalies(events);
        double clusterIso = 0.0, outlierIso = 0.0;
        for (const auto& sig : results) {
            if (sig.eventId == QStringLiteral("type_outlier"))
                outlierIso = sig.isolationScore;
            else
                clusterIso = std::max(clusterIso, sig.isolationScore);
        }
        QVERIFY2(outlierIso > clusterIso,
                 qPrintable(QStringLiteral("crime-type outlier iso %1 should exceed cluster max %2")
                     .arg(outlierIso).arg(clusterIso)));
    }

    void testLofScorePositiveForBatch()
    {
        AnomalyDetector det;
        QVector<AnomalyFeatureVector> events;
        for (int i = 0; i < 10; ++i)
            events.append(ev(QString("e%1").arg(i), 51.0 + i * 0.05, -0.1, 100.0 + i));
        const auto results = det.detectAnomalies(events);
        for (const auto& sig : results) {
            QVERIFY2(sig.lofScore > 0.0,
                     qPrintable(QStringLiteral("event %1: lofScore %2 should be > 0")
                         .arg(sig.eventId).arg(sig.lofScore)));
        }
    }

    void testSpatialOutlierHigherLof()
    {
        AnomalyDetector det;
        QVector<AnomalyFeatureVector> events;
        for (int i = 0; i < 20; ++i)
            events.append(ev(QString("c%1").arg(i), 51.0 + i * 0.01, -0.1, 100.0));
        events.append(ev("spatial_outlier", 60.0, 10.0, 100.0));

        const auto results = det.detectAnomalies(events);
        double clusterLof = 0.0, outlierLof = 0.0;
        for (const auto& sig : results) {
            if (sig.eventId == QStringLiteral("spatial_outlier"))
                outlierLof = sig.lofScore;
            else
                clusterLof = std::max(clusterLof, sig.lofScore);
        }
        QVERIFY2(outlierLof >= clusterLof,
                 qPrintable(QStringLiteral("spatial outlier lof %1 should be >= cluster max %2")
                     .arg(outlierLof).arg(clusterLof)));
    }

    void testCombinedScoreInRange()
    {
        AnomalyDetector det;
        QVector<AnomalyFeatureVector> events;
        for (int i = 0; i < 20; ++i)
            events.append(ev(QString("e%1").arg(i), 51.0 + i * 0.05, -0.1, 100.0 + i * 2.0));
        const auto results = det.detectAnomalies(events);
        for (const auto& sig : results) {
            QVERIFY2(sig.combinedScore >= 0.0 && sig.combinedScore <= 1.0,
                     qPrintable(QStringLiteral("event %1: combinedScore %2 not in [0,1]")
                         .arg(sig.eventId).arg(sig.combinedScore)));
        }
    }

    void testClearOutlierFlaggedAsAnomaly()
    {
        AnomalyDetector det;
        QVector<AnomalyFeatureVector> events;
        for (int i = 0; i < 20; ++i)
            events.append(ev(QString("c%1").arg(i), 51.0 + i * 0.01, -0.1, 100.0));
        events.append(ev("outlier", 60.0, 10.0, 500.0));

        const auto results = det.detectAnomalies(events);
        bool found = false, flagged = false;
        for (const auto& sig : results) {
            if (sig.eventId == QStringLiteral("outlier")) {
                found   = true;
                flagged = sig.isAnomaly;
                QVERIFY2(sig.isolationScore > 0.5,
                         qPrintable(QStringLiteral("outlier isolation %1 should be > 0.5")
                             .arg(sig.isolationScore)));
                break;
            }
        }
        QVERIFY(found);
        QVERIFY2(flagged, "clear outlier should have isAnomaly = true");
    }

    void testZScoreTemporalAtMeanIsZero()
    {
        AnomalyDetector det;
        QVector<AnomalyFeatureVector> training;
        for (int i = 1; i <= 10; ++i)
            training.append(ev(QString("t%1").arg(i), 51.5, -0.1, static_cast<double>(i)));
        det.fit(training);
        const auto results = det.detectAnomalies({ev("q", 51.5, -0.1, 5.5)});
        QVERIFY2(results[0].zScoreTemporal < 0.01,
                 qPrintable(QStringLiteral("zScoreTemporal at mean should be ~0, got %1")
                     .arg(results[0].zScoreTemporal)));
    }
};

QTEST_GUILESS_MAIN(AnomalyDetectorDeep4Test)
#include "test_anomaly_detector_deep4.moc"
