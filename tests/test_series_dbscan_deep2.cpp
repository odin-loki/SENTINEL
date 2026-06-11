#include <QtTest>
#include "models/SeriesDetector.h"

class TestSeriesDbscanDeep2 : public QObject
{
    Q_OBJECT

private slots:

    // DBSCAN: border-point promotion — noise points reachable from a core point
    // must join the cluster (the fix applied in iteration 10).
    void testBorderPointPromotion()
    {
        // 4 co-located points form a core (minSamples=3 → 4 >= 3).
        // 1 isolated point at +0.001 deg offset is reachable from any core point
        // (epsKm=1.0 >> 0.001*111 km) but cannot itself be a core (only 1 neighbour
        // — itself).  Under the old (broken) code the isolated point stayed noise.
        // Under the fixed code it must be promoted to a border cluster member.
        SeriesDetector sd(1.0, 30.0, 3);
        QVector<SeriesEvent> events;
        for (int i = 0; i < 4; ++i) {
            SeriesEvent e;
            e.eventId   = QStringLiteral("E%1").arg(i);
            e.lat       = 51.5;
            e.lon       = -0.1;
            e.tDays     = i;
            e.crimeType = QStringLiteral("burglary");
            events.append(e);
        }
        // The border point
        SeriesEvent border;
        border.eventId   = QStringLiteral("EBORDER");
        border.lat       = 51.5009;  // ~100 m away — within eps=1 km
        border.lon       = -0.1;
        border.tDays     = 2.0;
        border.crimeType = QStringLiteral("burglary");
        events.append(border);

        const auto series = sd.detectSeries(events);
        QCOMPARE(series.size(), 1);
        QVERIFY2(series[0].members.size() == 5,
                 qPrintable(QStringLiteral("Expected 5 members, got %1")
                            .arg(series[0].members.size())));
    }

    void testAllNoiseWhenSpread()
    {
        SeriesDetector sd(0.1, 1.0, 3);
        QVector<SeriesEvent> events;
        for (int i = 0; i < 5; ++i) {
            SeriesEvent e;
            e.eventId   = QStringLiteral("N%1").arg(i);
            e.lat       = 51.5 + i * 10.0;  // far apart
            e.lon       = -0.1;
            e.tDays     = i;
            e.crimeType = QStringLiteral("burglary");
            events.append(e);
        }
        const auto series = sd.detectSeries(events);
        QCOMPARE(series.size(), 0);
    }

    void testDuplicateEventsIgnored()
    {
        SeriesDetector sd(0.5, 14.0, 3);
        SeriesEvent e;
        e.eventId   = QStringLiteral("SAME");
        e.lat       = 51.5;
        e.lon       = -0.1;
        e.tDays     = 1.0;
        e.crimeType = QStringLiteral("burglary");

        QVector<SeriesEvent> events;
        for (int i = 0; i < 5; ++i)
            events.append(e);

        const auto series = sd.detectSeries(events);
        QVERIFY2(series.isEmpty() || series[0].members.size() <= 1,
                 "Duplicate events must be deduplicated");
    }

    void testHaversineKnownDistance()
    {
        const double londonToParisKm = SeriesDetector::haversineKm(
            51.5074, -0.1278, 48.8566, 2.3522);
        QVERIFY2(std::abs(londonToParisKm - 341.0) < 3.0,
                 qPrintable(QStringLiteral("London-Paris distance expected ~341 km, got %1")
                            .arg(londonToParisKm)));
    }

    void testHaversineSamePoint()
    {
        const double d = SeriesDetector::haversineKm(51.5, -0.1, 51.5, -0.1);
        QVERIFY2(d < 1e-9,
                 qPrintable(QStringLiteral("Same-point distance must be ~0, got %1").arg(d)));
    }

    void testMoJaccardIdentical()
    {
        QVERIFY(qFuzzyCompare(SeriesDetector::moJaccard(
            QStringLiteral("knife robbery cash"), QStringLiteral("knife robbery cash")), 1.0));
    }

    void testMoJaccardDisjoint()
    {
        QVERIFY(qFuzzyCompare(SeriesDetector::moJaccard(
            QStringLiteral("knife"), QStringLiteral("gun")), 0.0));
    }

    void testMoJaccardBothEmpty()
    {
        QVERIFY(qFuzzyCompare(SeriesDetector::moJaccard(QString{}, QString{}), 0.0));
    }

    void testMoJaccardOneEmpty()
    {
        QVERIFY(qFuzzyCompare(SeriesDetector::moJaccard(
            QStringLiteral("knife"), QString{}), 0.0));
    }

    void testMoJaccardPartial()
    {
        const double j = SeriesDetector::moJaccard(
            QStringLiteral("knife robbery cash"),
            QStringLiteral("knife gun cash"));
        // intersection={knife,cash}=2, union={knife,robbery,cash,gun}=4 → 0.5
        QVERIFY2(std::abs(j - 0.5) < 1e-9,
                 qPrintable(QStringLiteral("Jaccard expected 0.5, got %1").arg(j)));
    }

    void testLinkProbabilityInBounds()
    {
        SeriesDetector sd(0.3, 14.0, 3);
        QVector<SeriesEvent> members;
        for (int i = 0; i < 3; ++i) {
            SeriesEvent e;
            e.eventId   = QStringLiteral("M%1").arg(i);
            e.lat       = 51.50 + i * 0.001;
            e.lon       = -0.10;
            e.tDays     = i * 2.0;
            e.crimeType = QStringLiteral("burglary");
            members.append(e);
        }
        CrimeSeries series;
        series.seriesId          = QStringLiteral("TEST-SERIES");
        series.members           = members;
        series.dominantCrimeType = QStringLiteral("burglary");

        SeriesEvent newEvent;
        newEvent.lat     = 51.501;
        newEvent.lon     = -0.100;
        newEvent.tDays   = 5.0;
        newEvent.crimeType = QStringLiteral("burglary");

        const auto match = sd.linkProbability(newEvent, series, 0.8);
        QVERIFY2(match.linkProbability >= 0.0 && match.linkProbability <= 0.95,
                 qPrintable(QStringLiteral("Link probability %1 out of [0,0.95]")
                            .arg(match.linkProbability)));
        QVERIFY(match.compositeScore >= 0.0 && match.compositeScore <= 1.0);
    }

    void testNearRepeatForBurglary()
    {
        const auto params = SeriesDetector::nearRepeatFor(QStringLiteral("burglary"));
        QVERIFY(params.distM > 0.0);
        QVERIFY(params.days  > 0.0);
        QVERIFY(params.multiplier > 1.0);
        QVERIFY2(params.distM != SeriesDetector::NearRepeatParams{}.distM
                 || params.days != SeriesDetector::NearRepeatParams{}.days,
                 "burglary must use calibrated parameters, not defaults");
    }

    void testNearRepeatForUnknown()
    {
        const auto params = SeriesDetector::nearRepeatFor(QStringLiteral("unknown_crime_type_xyz"));
        const auto def    = SeriesDetector::NearRepeatParams{};
        QVERIFY(qFuzzyCompare(params.distM, def.distM));
        QVERIFY(qFuzzyCompare(params.days,  def.days));
    }

    void testDetectSeriesEmpty()
    {
        SeriesDetector sd;
        QVERIFY(sd.detectSeries({}).isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestSeriesDbscanDeep2)
#include "test_series_dbscan_deep2.moc"
