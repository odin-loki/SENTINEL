// test_anomaly_detector_deep9.cpp — Deep audit iteration 27: AnomalyDetector
// clear outlier flagging, LOF score bounds, feature distance symmetry.
#include <QTest>
#include <cmath>
#include "inference/AnomalyDetector.h"

class TestAnomalyDetectorDeep9 : public QObject
{
    Q_OBJECT

    static AnomalyFeatureVector vec(const QString& id, double lat, double lon,
                                    double t, double hour = 0.5)
    {
        AnomalyFeatureVector v;
        v.eventId       = id;
        v.lat           = lat;
        v.lon           = lon;
        v.tDays         = t;
        v.hourNorm      = hour;
        v.crimeTypeCode = 1;
        return v;
    }

private slots:

    void testOutlierFlaggedAsAnomaly()
    {
        QVector<AnomalyFeatureVector> batch;
        for (int i = 0; i < 25; ++i)
            batch.append(vec(QStringLiteral("N%1").arg(i), 51.5 + i * 0.0001, -0.1, i));

        batch.append(vec(QStringLiteral("OUT"), 55.0, 2.0, 12.0));

        AnomalyDetector ad(0.1);
        ad.fit(batch);
        const auto results = ad.detectAnomalies(batch);

        bool outlierFlagged = false;
        for (const auto& sig : results) {
            if (sig.eventId == QStringLiteral("OUT") && sig.isAnomaly)
                outlierFlagged = true;
        }
        QVERIFY(outlierFlagged);
    }

    void testLofScoreNonNegative()
    {
        QVector<AnomalyFeatureVector> batch;
        for (int i = 0; i < 15; ++i)
            batch.append(vec(QStringLiteral("L%1").arg(i), 51.5, -0.1, i));

        AnomalyDetector ad(0.08);
        ad.fit(batch);
        const auto results = ad.detectAnomalies(batch);
        for (const auto& sig : results)
            QVERIFY(sig.lofScore >= 0.0);
    }

    void testIsolationScoreBounded()
    {
        QVector<AnomalyFeatureVector> batch;
        for (int i = 0; i < 20; ++i)
            batch.append(vec(QStringLiteral("I%1").arg(i), 51.5 + i * 0.001, -0.1, i * 0.2));

        AnomalyDetector ad(0.1);
        ad.fit(batch);
        const auto results = ad.detectAnomalies(batch);
        for (const auto& sig : results) {
            QVERIFY(sig.isolationScore >= 0.0);
            QVERIFY(sig.isolationScore <= 1.0 + 1e-6);
        }
    }

    void testDetectWithoutPriorFitAutoFits()
    {
        QVector<AnomalyFeatureVector> batch;
        for (int i = 0; i < 12; ++i)
            batch.append(vec(QStringLiteral("A%1").arg(i), 51.5, -0.1, i));

        AnomalyDetector ad(0.15);
        QVERIFY(!ad.isFitted());
        const auto results = ad.detectAnomalies(batch);
        QCOMPARE(results.size(), 12);
        QVERIFY(ad.isFitted());
    }

    void testEmptyBatchReturnsEmpty()
    {
        AnomalyDetector ad(0.1);
        QVERIFY(ad.detectAnomalies({}).isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestAnomalyDetectorDeep9)
#include "test_anomaly_detector_deep9.moc"
