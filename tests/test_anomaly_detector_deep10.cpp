// test_anomaly_detector_deep10.cpp — Deep audit iteration 29: AnomalyDetector
// signal reasons, combined score bounds, fit state, category via HintEngine path.
#include <QTest>
#include <cmath>
#include "inference/AnomalyDetector.h"

class TestAnomalyDetectorDeep10 : public QObject
{
    Q_OBJECT

    static AnomalyFeatureVector vec(const QString& id, double lat, double lon, double t)
    {
        AnomalyFeatureVector v;
        v.eventId       = id;
        v.lat           = lat;
        v.lon           = lon;
        v.tDays         = t;
        v.hourNorm      = 0.5;
        v.crimeTypeCode = 2;
        return v;
    }

private slots:

    void testAnomalyHasSignalReasons()
    {
        QVector<AnomalyFeatureVector> batch;
        for (int i = 0; i < 20; ++i)
            batch.append(vec(QStringLiteral("N%1").arg(i), 51.5 + i * 0.0001, -0.1, i));
        batch.append(vec(QStringLiteral("OUT"), 53.0, 1.5, 50.0));

        AnomalyDetector ad(0.12);
        ad.fit(batch);
        const auto results = ad.detectAnomalies(batch);

        bool outlierWithReasons = false;
        for (const auto& sig : results) {
            if (sig.eventId == QStringLiteral("OUT") && sig.isAnomaly
                && !sig.signalReasons.empty())
                outlierWithReasons = true;
        }
        QVERIFY(outlierWithReasons);
    }

    void testCombinedScoreNonNegative()
    {
        QVector<AnomalyFeatureVector> batch;
        for (int i = 0; i < 14; ++i)
            batch.append(vec(QStringLiteral("C%1").arg(i), 51.5, -0.1, i));

        AnomalyDetector ad(0.1);
        const auto results = ad.detectAnomalies(batch);
        for (const auto& sig : results)
            QVERIFY(sig.combinedScore >= 0.0);
    }

    void testFitSetsFittedFlag()
    {
        QVector<AnomalyFeatureVector> batch;
        for (int i = 0; i < 10; ++i)
            batch.append(vec(QStringLiteral("F%1").arg(i), 51.5, -0.1, i));

        AnomalyDetector ad(0.1);
        QVERIFY(!ad.isFitted());
        ad.fit(batch);
        QVERIFY(ad.isFitted());
    }

    void testZScoresFiniteForCluster()
    {
        QVector<AnomalyFeatureVector> batch;
        for (int i = 0; i < 18; ++i)
            batch.append(vec(QStringLiteral("Z%1").arg(i), 51.5, -0.1, i * 0.5));

        AnomalyDetector ad(0.08);
        ad.fit(batch);
        const auto results = ad.detectAnomalies(batch);
        for (const auto& sig : results) {
            QVERIFY(std::isfinite(sig.zScoreTemporal));
            QVERIFY(std::isfinite(sig.zScoreSpatial));
        }
    }

    void testHigherContaminationAllowsMoreAnomalies()
    {
        QVector<AnomalyFeatureVector> batch;
        for (int i = 0; i < 30; ++i)
            batch.append(vec(QStringLiteral("H%1").arg(i), 51.5 + i * 0.0002, -0.1, i));
        batch.append(vec(QStringLiteral("EDGE"), 52.0, 0.5, 15.0));

        AnomalyDetector low(0.02);
        AnomalyDetector high(0.25);
        low.fit(batch);
        high.fit(batch);

        int lowCount = 0, highCount = 0;
        for (const auto& s : low.detectAnomalies(batch))
            if (s.isAnomaly) ++lowCount;
        for (const auto& s : high.detectAnomalies(batch))
            if (s.isAnomaly) ++highCount;

        QVERIFY(highCount >= lowCount);
    }
};

QTEST_GUILESS_MAIN(TestAnomalyDetectorDeep10)
#include "test_anomaly_detector_deep10.moc"
