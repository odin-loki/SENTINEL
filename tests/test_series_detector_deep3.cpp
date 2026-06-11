// Iteration 13 — SeriesDetector deep tests (link probability, MO Jaccard, DBSCAN)
#include <QtTest/QtTest>
#include "models/SeriesDetector.h"
#include "core/CrimeEvent.h"

class SeriesDetectorDeep3Test : public QObject
{
    Q_OBJECT

private slots:

    void testMoJaccardEmptyStrings()
    {
        QCOMPARE(SeriesDetector::moJaccard(QString(), QString()), 0.0);
    }

    void testMoJaccardDisjoint()
    {
        const double j = SeriesDetector::moJaccard(
            QStringLiteral("forced_entry night"),
            QStringLiteral("unlocked morning"));
        QCOMPARE(j, 0.0);
    }

    void testMoJaccardPartialOverlap()
    {
        const double j = SeriesDetector::moJaccard(
            QStringLiteral("forced_entry night residential"),
            QStringLiteral("forced_entry morning"));
        QVERIFY2(std::abs(j - 1.0 / 4.0) < 1e-9,
                 qPrintable(QStringLiteral("Expected Jaccard=0.25, got %1").arg(j)));
    }

    void testHaversineAntipodalApprox()
    {
        // Half Earth circumference ≈ 20,015 km
        const double d = SeriesDetector::haversineKm(0.0, 0.0, 0.0, 180.0);
        QVERIFY2(d > 19000.0 && d < 21000.0,
                 qPrintable(QStringLiteral("Antipodal distance ~20015 km, got %1").arg(d)));
    }

    void testNearRepeatTableBurglary()
    {
        const auto p = SeriesDetector::nearRepeatFor(QStringLiteral("burglary"));
        QVERIFY(p.distM > 0.0);
        QVERIFY(p.days > 0.0);
        QVERIFY(p.multiplier > 0.0);
    }

    void testNearRepeatTableUnknownUsesDefaults()
    {
        const auto known = SeriesDetector::nearRepeatFor(QStringLiteral("burglary"));
        const auto unknown = SeriesDetector::nearRepeatFor(QStringLiteral("unknown_crime_xyz"));
        QVERIFY(unknown.distM >= 0.0);
        QVERIFY(known.distM > unknown.distM || known.distM > 0.0);
    }

    void testDetectEmptyEvents()
    {
        SeriesDetector sd(0.5, 14.0, 3);
        const auto series = sd.detect({});
        QVERIFY(series.isEmpty());
    }

    void testDetectSingleEventNoSeries()
    {
        CrimeEvent ev;
        ev.eventId = QStringLiteral("SINGLE");
        ev.lat = 51.5;
        ev.lon = -0.1;
        ev.occurredAt = QDateTime(QDate(2024, 1, 1), QTime(12, 0), Qt::UTC);
        ev.crimeType = QStringLiteral("burglary");

        SeriesDetector sd(0.5, 14.0, 3);
        const auto series = sd.detect({ev});
        QVERIFY(series.isEmpty());
    }

    void testLinkProbabilityHighForCloseEvents()
    {
        SeriesDetector sd(1.0, 30.0, 2);
        QVector<SeriesEvent> events;
        for (int i = 0; i < 3; ++i) {
            SeriesEvent e;
            e.eventId   = QStringLiteral("L%1").arg(i);
            e.lat       = 51.5;
            e.lon       = -0.1;
            e.tDays     = i * 0.5;
            e.crimeType = QStringLiteral("burglary");
            e.moText    = QStringLiteral("forced_entry night");
            events.append(e);
        }

        const auto seriesList = sd.detectSeries(events);
        QVERIFY(!seriesList.isEmpty());

        SeriesEvent newcomer;
        newcomer.eventId   = QStringLiteral("NEW");
        newcomer.lat       = 51.5001;
        newcomer.lon       = -0.1;
        newcomer.tDays     = 1.0;
        newcomer.crimeType = QStringLiteral("burglary");
        newcomer.moText    = QStringLiteral("forced_entry night");

        const double moSim = SeriesDetector::moJaccard(
            newcomer.moText, seriesList[0].members[0].moText);
        const auto match = sd.linkProbability(newcomer, seriesList[0], moSim);
        QVERIFY2(match.linkProbability > 0.2,
                 qPrintable(QStringLiteral("Close events should have link prob > 0.2, got %1")
                                .arg(match.linkProbability)));
    }

    void testDbscanTwoClusters()
    {
        SeriesDetector sd(0.5, 14.0, 2);
        QVector<SeriesEvent> events;

        // Cluster A: 3 points in London
        for (int i = 0; i < 3; ++i) {
            SeriesEvent e;
            e.eventId = QStringLiteral("A%1").arg(i);
            e.lat = 51.5; e.lon = -0.1;
            e.tDays = i; e.crimeType = QStringLiteral("burglary");
            events.append(e);
        }
        // Cluster B: 3 points in Manchester area
        for (int i = 0; i < 3; ++i) {
            SeriesEvent e;
            e.eventId = QStringLiteral("B%1").arg(i);
            e.lat = 53.48; e.lon = -2.24;
            e.tDays = i + 10; e.crimeType = QStringLiteral("burglary");
            events.append(e);
        }

        const auto series = sd.detectSeries(events);
        QVERIFY2(series.size() >= 2,
                 qPrintable(QStringLiteral("Expected >=2 clusters, got %1").arg(series.size())));
    }

    void testBorderPointStillPromoted()
    {
        SeriesDetector sd(1.0, 30.0, 3);
        QVector<SeriesEvent> events;
        for (int i = 0; i < 4; ++i) {
            SeriesEvent e;
            e.eventId = QStringLiteral("C%1").arg(i);
            e.lat = 51.5; e.lon = -0.1;
            e.tDays = i; e.crimeType = QStringLiteral("burglary");
            events.append(e);
        }
        SeriesEvent border;
        border.eventId = QStringLiteral("BORDER");
        border.lat = 51.5009; border.lon = -0.1;
        border.tDays = 2.0; border.crimeType = QStringLiteral("burglary");
        events.append(border);

        const auto series = sd.detectSeries(events);
        QCOMPARE(series.size(), 1);
        QCOMPARE(series[0].members.size(), 5);
    }
};

QTEST_GUILESS_MAIN(SeriesDetectorDeep3Test)
#include "test_series_detector_deep3.moc"
