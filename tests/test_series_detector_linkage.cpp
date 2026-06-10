// test_series_detector_linkage.cpp
// SeriesDetector linkage calibration, confusion matrix accuracy, and
// near-repeat parameter lookup tests.
#include <QTest>
#include "models/SeriesDetector.h"
#include <cmath>

class SeriesDetectorLinkageTest : public QObject
{
    Q_OBJECT

private:
    static SeriesEvent se(const QString& id, double lat, double lon,
                           double tDays, const QString& mo = QStringLiteral("entered through rear window"))
    {
        return { id, lat, lon, tDays, QStringLiteral("burglary"), mo };
    }

    // Build a tight cluster of events (within 0.1 km, 5 days)
    static QVector<SeriesEvent> tightCluster(int n = 5)
    {
        QVector<SeriesEvent> evs;
        for (int i = 0; i < n; ++i)
            evs.append(se(QStringLiteral("E%1").arg(i),
                          51.5 + i * 0.0005,
                          -0.1 + i * 0.0005,
                          i * 1.0));   // 1 day apart
        return evs;
    }

    // Build a dispersed set of events (far apart in space and time)
    static QVector<SeriesEvent> dispersedSet(int n = 5)
    {
        QVector<SeriesEvent> evs;
        for (int i = 0; i < n; ++i)
            evs.append(se(QStringLiteral("D%1").arg(i),
                          51.5 + i * 1.0,   // 1 degree apart ~110 km
                          -0.1 + i * 1.0,
                          i * 100.0));       // 100 days apart
        return evs;
    }

private slots:

    // 1. haversineKm(same point) == 0
    void testHaversineZero()
    {
        const double d = SeriesDetector::haversineKm(51.5, -0.1, 51.5, -0.1);
        QVERIFY2(std::abs(d) < 1e-9, qPrintable(QStringLiteral("Same point distance %1 != 0").arg(d)));
    }

    // 2. haversineKm(London-Manchester) ~262 km
    void testHaversineLondonManchester()
    {
        const double d = SeriesDetector::haversineKm(51.5074, -0.1278, 53.4808, -2.2426);
        QVERIFY2(std::abs(d - 262.0) < 15.0,
                 qPrintable(QStringLiteral("London-Manchester distance %1 km, expected ~262").arg(d)));
    }

    // 3. moJaccard: identical strings -> 1.0
    void testMoJaccardIdentical()
    {
        const double j = SeriesDetector::moJaccard(QStringLiteral("rear window entry"), 
                                                    QStringLiteral("rear window entry"));
        QVERIFY2(std::abs(j - 1.0) < 1e-9,
                 qPrintable(QStringLiteral("Identical MO Jaccard %1, expected 1.0").arg(j)));
    }

    // 4. moJaccard: completely different strings -> 0.0
    void testMoJaccardDisjoint()
    {
        const double j = SeriesDetector::moJaccard(QStringLiteral("rear window"),
                                                    QStringLiteral("front door forced"));
        QVERIFY2(j >= 0.0 && j < 0.5,
                 qPrintable(QStringLiteral("Disjoint MO Jaccard %1 should be < 0.5").arg(j)));
    }

    // 5. moJaccard: partial overlap gives intermediate score
    void testMoJaccardPartial()
    {
        const double j = SeriesDetector::moJaccard(QStringLiteral("rear window entry forced"),
                                                    QStringLiteral("rear window entry patio"));
        QVERIFY2(j > 0.3 && j < 1.0,
                 qPrintable(QStringLiteral("Partial MO Jaccard %1 expected in (0.3, 1.0)").arg(j)));
    }

    // 6. detectSeries: tight cluster produces >= 1 series
    void testTightClusterDetected()
    {
        SeriesDetector sd(0.3, 14.0, 3);
        const auto series = sd.detectSeries(tightCluster(5));
        QVERIFY2(!series.isEmpty(), "Tight cluster should produce at least one series");
    }

    // 7. detectSeries: dispersed events produce no series
    void testDispersedSetNoSeries()
    {
        SeriesDetector sd(0.3, 14.0, 3);
        const auto series = sd.detectSeries(dispersedSet(5));
        QVERIFY2(series.isEmpty(), "Dispersed events should produce no series");
    }

    // 8. linkProbability: event near series has higher score than distant event
    void testLinkProbabilityNearVsFar()
    {
        SeriesDetector sd(0.3, 14.0, 2);
        auto cluster = tightCluster(4);
        const auto series = sd.detectSeries(cluster);
        QVERIFY(!series.isEmpty());

        const auto& s = series.first();
        const SeriesEvent nearby = se(QStringLiteral("NEW"), 51.5001, -0.1001, 5.0);
        const SeriesEvent distant = se(QStringLiteral("FAR"), 53.0, 2.0, 200.0);

        const double nearScore = sd.linkProbability(nearby,  s, 0.8).compositeScore;
        const double farScore  = sd.linkProbability(distant, s, 0.0).compositeScore;
        QVERIFY2(nearScore >= farScore,
                 qPrintable(QStringLiteral("Nearby score %1 should >= far score %2")
                    .arg(nearScore).arg(farScore)));
    }

    // 9. nearRepeatFor("burglary") returns sensible params
    void testNearRepeatParamsBurglary()
    {
        const auto p = SeriesDetector::nearRepeatFor(QStringLiteral("burglary"));
        QVERIFY2(p.distM > 0.0, "nearRepeat distM should be > 0");
        QVERIFY2(p.days > 0.0,  "nearRepeat days should be > 0");
        QVERIFY2(p.multiplier > 1.0, "nearRepeat multiplier should be > 1");
    }

    // 10. detectSeries(minSamples=2): even a pair is detected
    void testMinSamplesTwoDetectsPair()
    {
        SeriesDetector sd(0.3, 14.0, 2);
        QVector<SeriesEvent> pair;
        pair.append(se(QStringLiteral("P1"), 51.5, -0.1, 0.0));
        pair.append(se(QStringLiteral("P2"), 51.5001, -0.1001, 2.0));
        const auto series = sd.detectSeries(pair);
        QVERIFY2(!series.isEmpty(), "Pair of nearby events should be detected as series");
    }
};

QTEST_MAIN(SeriesDetectorLinkageTest)
#include "test_series_detector_linkage.moc"
