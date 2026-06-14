// test_anomaly_detector_deep8.cpp — Deep audit iteration 25: AnomalyDetector
// contamination threshold, auto-fit, combined score bounds, isolation.
#include <QTest>
#include <cmath>
#include "inference/AnomalyDetector.h"

class TestAnomalyDetectorDeep8 : public QObject
{
    Q_OBJECT

    static AnomalyFeatureVector vec(const QString& id, double lat, double lon, double t)
    {
        AnomalyFeatureVector v;
        v.eventId = id;
        v.lat = lat;
        v.lon = lon;
        v.tDays = t;
        v.hourNorm = 0.5;
        v.crimeTypeCode = 1;
        return v;
    }

private slots:

    void testAutoFitOnDetect()
    {
        QVector<AnomalyFeatureVector> batch;
        for (int i = 0; i < 20; ++i)
            batch.append(vec(QStringLiteral("N%1").arg(i), 51.5 + i * 0.001, -0.1, i));

        AnomalyDetector ad(0.1);
        const auto results = ad.detectAnomalies(batch);
        QCOMPARE(results.size(), 20);
        QVERIFY(ad.isFitted());
    }

    void testCombinedScoreInUnitInterval()
    {
        QVector<AnomalyFeatureVector> batch;
        for (int i = 0; i < 30; ++i)
            batch.append(vec(QStringLiteral("C%1").arg(i), 51.5, -0.1, i * 0.5));

        AnomalyDetector ad(0.05);
        ad.fit(batch);
        const auto results = ad.detectAnomalies(batch);
        for (const auto& sig : results) {
            QVERIFY2(sig.combinedScore >= 0.0 && sig.combinedScore <= 1.0,
                     qPrintable(QStringLiteral("score=%1").arg(sig.combinedScore)));
        }
    }

    void testOutlierScoresHigher()
    {
        QVector<AnomalyFeatureVector> batch;
        for (int i = 0; i < 25; ++i)
            batch.append(vec(QStringLiteral("B%1").arg(i), 51.5, -0.1, i));

        batch.append(vec(QStringLiteral("OUT"), 55.0, -0.5, 100.0));

        AnomalyDetector ad(0.05);
        ad.fit(batch);
        const auto results = ad.detectAnomalies(batch);
        double maxNormal = 0.0;
        double outlier = 0.0;
        for (const auto& sig : results) {
            if (sig.eventId == QStringLiteral("OUT"))
                outlier = sig.combinedScore;
            else
                maxNormal = std::max(maxNormal, sig.combinedScore);
        }
        QVERIFY2(outlier >= maxNormal,
                 qPrintable(QStringLiteral("outlier=%1 maxNormal=%2").arg(outlier).arg(maxNormal)));
    }

    void testHighContaminationFlagsMore()
    {
        QVector<AnomalyFeatureVector> batch;
        for (int i = 0; i < 20; ++i)
            batch.append(vec(QStringLiteral("H%1").arg(i), 51.5 + i * 0.01, -0.1, i));

        AnomalyDetector low(0.05);
        AnomalyDetector high(0.25);
        low.fit(batch);
        high.fit(batch);

        int lowFlags = 0, highFlags = 0;
        for (const auto& sig : low.detectAnomalies(batch))
            if (sig.isAnomaly) ++lowFlags;
        for (const auto& sig : high.detectAnomalies(batch))
            if (sig.isAnomaly) ++highFlags;

        QVERIFY2(highFlags >= lowFlags,
                 qPrintable(QStringLiteral("low=%1 high=%2").arg(lowFlags).arg(highFlags)));
    }

    void testSingleEventBatch()
    {
        AnomalyDetector ad;
        const auto results = ad.detectAnomalies({ vec(QStringLiteral("S1"), 51.5, -0.1, 0.0) });
        QCOMPARE(results.size(), 1);
    }

    void testEmptyBatchReturnsEmpty()
    {
        AnomalyDetector ad;
        QVERIFY(ad.detectAnomalies({}).isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestAnomalyDetectorDeep8)
#include "test_anomaly_detector_deep8.moc"
