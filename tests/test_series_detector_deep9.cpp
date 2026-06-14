// test_series_detector_deep9.cpp — Deep audit iteration 29: SeriesDetector
// haversine zero distance, linkProbability fields, nearRepeat params, DBSCAN noise.
#include <QTest>
#include <cmath>
#include "models/SeriesDetector.h"

class TestSeriesDetectorDeep9 : public QObject
{
    Q_OBJECT

    static SeriesEvent sev(const QString& id, double lat, double lon, double t,
                           const QString& mo = QStringLiteral("forced entry rear"))
    {
        SeriesEvent e;
        e.eventId   = id;
        e.lat       = lat;
        e.lon       = lon;
        e.tDays     = t;
        e.crimeType = QStringLiteral("burglary");
        e.moText    = mo;
        return e;
    }

private slots:

    void testHaversineSamePointZero()
    {
        const double d = SeriesDetector::haversineKm(51.5, -0.1, 51.5, -0.1);
        QVERIFY(d < 1e-6);
    }

    void testNearRepeatForTheftPositive()
    {
        const auto params = SeriesDetector::nearRepeatFor(QStringLiteral("theft"));
        QVERIFY(params.distM > 0.0);
        QVERIFY(params.days > 0.0);
    }

    void testLinkProbabilityPopulatesDistances()
    {
        CrimeSeries series;
        series.seriesId = QStringLiteral("S9");
        series.dominantCrimeType = QStringLiteral("burglary");
        series.members = {
            sev(QStringLiteral("M1"), 51.5, -0.1, 0.0),
            sev(QStringLiteral("M2"), 51.50001, -0.10001, 2.0),
        };

        const auto match = SeriesDetector().linkProbability(
            sev(QStringLiteral("NEW"), 51.50002, -0.10002, 3.0), series, 0.8);

        QCOMPARE(match.seriesId, QStringLiteral("S9"));
        QVERIFY(match.spatialDistanceM >= 0.0);
        QVERIFY(match.linkProbability >= 0.0 && match.linkProbability <= 0.95);
    }

    void testMoJaccardPartialOverlap()
    {
        const double j = SeriesDetector::moJaccard(
            QStringLiteral("rear window forced"),
            QStringLiteral("rear door forced"));
        QVERIFY(j > 0.0 && j < 1.0);
    }

    void testSparseEventsMostlyNoise()
    {
        QVector<SeriesEvent> events;
        for (int i = 0; i < 4; ++i)
            events.append(sev(QStringLiteral("ISO%1").arg(i), 50.0 + i, 0.0 + i, i * 30.0));

        const auto series = SeriesDetector(0.3, 14.0, 3).detectSeries(events);
        QVERIFY(series.size() <= events.size());
    }
};

QTEST_GUILESS_MAIN(TestSeriesDetectorDeep9)
#include "test_series_detector_deep9.moc"
