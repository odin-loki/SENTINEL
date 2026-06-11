// Iteration 15 — SeriesDetector deep4: DBSCAN border points, link probability, empty input
#include <QtTest/QtTest>
#include "models/SeriesDetector.h"
#include "core/CrimeEvent.h"

class SeriesDetectorDeep4Test : public QObject
{
    Q_OBJECT

    static SeriesEvent sev(const QString& id, double lat, double lon, double tDays,
                           const QString& mo = QStringLiteral("forced_entry night"),
                           const QString& type = QStringLiteral("burglary"))
    {
        SeriesEvent e;
        e.eventId   = id;
        e.lat       = lat;
        e.lon       = lon;
        e.tDays     = tDays;
        e.crimeType = type;
        e.moText    = mo;
        return e;
    }

    static CrimeSeries makeSeries(const QVector<SeriesEvent>& members)
    {
        CrimeSeries s;
        s.seriesId          = QStringLiteral("SERIES-0001");
        s.members             = members;
        s.dominantCrimeType   = QStringLiteral("burglary");
        s.centroidLat         = members.first().lat;
        s.centroidLon         = members.first().lon;
        s.firstDays           = members.first().tDays;
        s.lastDays            = members.last().tDays;
        return s;
    }

private slots:

    void testDetectSeriesEmptyInput()
    {
        SeriesDetector sd(0.5, 14.0, 3);
        const auto series = sd.detectSeries({});
        QVERIFY(series.isEmpty());
    }

    void testDetectCrimeEventEmptyInput()
    {
        SeriesDetector sd(0.5, 14.0, 3);
        const auto series = sd.detect({});
        QVERIFY(series.isEmpty());
    }

    void testBorderPointPromotedWhenReachableFromCore()
    {
        // Core cluster of 4 + one border point within eps but below minPts density.
        SeriesDetector sd(1.0, 30.0, 4);
        QVector<SeriesEvent> events;
        for (int i = 0; i < 4; ++i)
            events.append(sev(QStringLiteral("C%1").arg(i), 51.5, -0.1, static_cast<double>(i)));
        events.append(sev(QStringLiteral("BORDER"), 51.5008, -0.1, 2.0));

        const auto series = sd.detectSeries(events);
        QCOMPARE(series.size(), 1);
        QCOMPARE(series[0].members.size(), 5);
        bool hasBorder = false;
        for (const auto& m : series[0].members) {
            if (m.eventId == QStringLiteral("BORDER"))
                hasBorder = true;
        }
        QVERIFY2(hasBorder, "border point reachable from core must join the cluster");
    }

    void testNoisePointOutsideEpsilonExcluded()
    {
        SeriesDetector sd(0.3, 14.0, 3);
        QVector<SeriesEvent> events;
        for (int i = 0; i < 4; ++i)
            events.append(sev(QStringLiteral("L%1").arg(i), 51.5, -0.1, static_cast<double>(i)));
        events.append(sev(QStringLiteral("NOISE"), 53.48, -2.24, 5.0));

        const auto series = sd.detectSeries(events);
        QCOMPARE(series.size(), 1);
        QCOMPARE(series[0].members.size(), 4);
        for (const auto& m : series[0].members)
            QVERIFY(m.eventId != QStringLiteral("NOISE"));
    }

    void testLinkProbabilityLowForDistantEvent()
    {
        SeriesDetector sd(0.5, 14.0, 3);
        QVector<SeriesEvent> members;
        for (int i = 0; i < 3; ++i)
            members.append(sev(QStringLiteral("M%1").arg(i), 51.5, -0.1, static_cast<double>(i)));

        SeriesEvent far;
        far.eventId   = QStringLiteral("FAR");
        far.lat       = 53.0;
        far.lon       = -1.5;
        far.tDays     = 100.0;
        far.crimeType = QStringLiteral("burglary");
        far.moText    = QStringLiteral("unrelated daytime");

        const auto match = sd.linkProbability(far, makeSeries(members), 0.0);
        QVERIFY2(match.linkProbability < 0.15,
                 qPrintable(QStringLiteral("distant event link prob=%1 expected < 0.15")
                                .arg(match.linkProbability)));
        QVERIFY(match.spatialDistanceM > 100000.0);
    }

    void testLinkProbabilityHighForNearRepeat()
    {
        SeriesDetector sd(0.5, 14.0, 3);
        QVector<SeriesEvent> members;
        for (int i = 0; i < 3; ++i)
            members.append(sev(QStringLiteral("M%1").arg(i), 51.5, -0.1, static_cast<double>(i)));

        SeriesEvent near;
        near.eventId   = QStringLiteral("NEAR");
        near.lat       = 51.50005;
        near.lon       = -0.10005;
        near.tDays     = 3.5;
        near.crimeType = QStringLiteral("burglary");
        near.moText    = QStringLiteral("forced_entry night");

        const double moSim = SeriesDetector::moJaccard(near.moText, members[0].moText);
        const auto match   = sd.linkProbability(near, makeSeries(members), moSim);

        QVERIFY2(match.linkProbability > 0.25,
                 qPrintable(QStringLiteral("near-repeat link prob=%1 expected > 0.25")
                                .arg(match.linkProbability)));
        QVERIFY(match.compositeScore > 0.5);
        QCOMPARE(match.method, QStringLiteral("NearRepeat-DBSCAN"));
    }

    void testLinkProbabilityNeverExceedsCap()
    {
        SeriesDetector sd(0.5, 14.0, 3);
        QVector<SeriesEvent> members;
        for (int i = 0; i < 5; ++i)
            members.append(sev(QStringLiteral("M%1").arg(i), 51.5, -0.1, static_cast<double>(i)));

        SeriesEvent twin = members[0];
        twin.eventId = QStringLiteral("TWIN");

        // moSimilarity deliberately above 1.0 — implementation must clamp composite.
        const auto match = sd.linkProbability(twin, makeSeries(members), 2.0);
        QVERIFY2(match.linkProbability <= 0.95,
                 qPrintable(QStringLiteral("link prob must be capped at 0.95, got %1")
                                .arg(match.linkProbability)));
        QVERIFY(match.compositeScore <= 1.0);
    }

    void testLinkProbabilityIncreasesWithMoSimilarity()
    {
        SeriesDetector sd(0.5, 14.0, 3);
        QVector<SeriesEvent> members;
        for (int i = 0; i < 3; ++i)
            members.append(sev(QStringLiteral("M%1").arg(i), 51.5, -0.1, static_cast<double>(i)));

        SeriesEvent candidate = members[0];
        candidate.eventId = QStringLiteral("NEW");
        candidate.tDays   = 2.0;

        const auto lowMo  = sd.linkProbability(candidate, makeSeries(members), 0.0);
        const auto highMo = sd.linkProbability(candidate, makeSeries(members), 1.0);
        QVERIFY2(highMo.linkProbability > lowMo.linkProbability,
                 qPrintable(QStringLiteral("MO similarity should raise link prob: low=%1 high=%2")
                                .arg(lowMo.linkProbability).arg(highMo.linkProbability)));
    }
};

QTEST_GUILESS_MAIN(SeriesDetectorDeep4Test)
#include "test_series_detector_deep4.moc"
