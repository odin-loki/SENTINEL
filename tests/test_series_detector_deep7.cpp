// test_series_detector_deep7.cpp — Deep audit iteration 24: SeriesDetector
// DBSCAN minPts, linkage params, cluster isolation, moJaccard, linkProbability.
#include <QTest>
#include <QDateTime>
#include <cmath>
#include "models/SeriesDetector.h"
#include "core/CrimeEvent.h"

class TestSeriesDetectorDeep7 : public QObject
{
    Q_OBJECT

    static CrimeEvent makeEvent(const QString& id, double lat, double lon,
                                const QDateTime& dt,
                                const QString& mo = QStringLiteral("forced entry"))
    {
        CrimeEvent e;
        e.eventId    = id;
        e.suburb     = QStringLiteral("Series7");
        e.lat        = lat;
        e.lon        = lon;
        e.latitude   = lat;
        e.longitude  = lon;
        e.timestamp  = dt;
        e.occurredAt = dt;
        e.crimeType  = QStringLiteral("burglary");
        e.narrative  = mo;
        return e;
    }

private slots:

    void testMinPtsBoundaryRequiresThreeEvents()
    {
        const QDateTime base(QDate(2024, 2, 1), QTime(10, 0), Qt::UTC);
        QVector<CrimeEvent> events;
        for (int i = 0; i < 2; ++i)
            events.append(makeEvent(QStringLiteral("S%1").arg(i),
                                    51.50 + i * 1e-6, -0.12, base.addDays(i)));

        SeriesDetector det(0.5, 14.0, 3);
        const auto series = det.detect(events);
        QVERIFY(series.isEmpty());
    }

    void testClusterFormsWithTightSpatialTemporal()
    {
        const QDateTime base(QDate(2024, 3, 1), QTime(8, 0), Qt::UTC);
        QVector<CrimeEvent> events;
        for (int i = 0; i < 5; ++i)
            events.append(makeEvent(QStringLiteral("C%1").arg(i),
                                    51.5074 + i * 1e-7, -0.1278 + i * 1e-7,
                                    base.addDays(i)));

        SeriesDetector det(0.3, 14.0, 3);
        const auto series = det.detect(events);
        QVERIFY(!series.isEmpty());
        QVERIFY(series.first().members.size() >= 3);
    }

    void testLinkProbabilityInUnitInterval()
    {
        SeriesDetector det(0.4, 14.0, 3);
        CrimeSeries cs;
        cs.seriesId = QStringLiteral("SER-1");
        for (int i = 0; i < 4; ++i) {
            SeriesEvent m;
            m.eventId   = QStringLiteral("E%1").arg(i);
            m.lat       = 51.5;
            m.lon       = -0.1;
            m.tDays     = static_cast<double>(i);
            m.crimeType = QStringLiteral("burglary");
            m.moText    = QStringLiteral("forced entry");
            cs.members.append(m);
        }

        SeriesEvent probe;
        probe.eventId   = QStringLiteral("P");
        probe.lat       = 51.50001;
        probe.lon       = -0.10001;
        probe.tDays     = 4.5;
        probe.crimeType = QStringLiteral("burglary");
        probe.moText    = QStringLiteral("forced entry window");

        const double moSim = SeriesDetector::moJaccard(probe.moText, cs.members.first().moText);
        const auto match = det.linkProbability(probe, cs, moSim);
        QVERIFY2(match.linkProbability >= 0.0 && match.linkProbability <= 1.0,
                 qPrintable(QStringLiteral("p=%1").arg(match.linkProbability)));
    }

    void testMoJaccardEmptyStringsZero()
    {
        const double j = SeriesDetector::moJaccard(QString(), QString());
        QCOMPARE(j, 0.0);
    }

    void testMoJaccardIdenticalStringsOne()
    {
        const QString mo = QStringLiteral("smashed window forced entry");
        const double j = SeriesDetector::moJaccard(mo, mo);
        QVERIFY2(std::abs(j - 1.0) < 1e-9,
                 qPrintable(QStringLiteral("j=%1").arg(j)));
    }

    void testTemporalDistanceIncreasesWithGap()
    {
        SeriesDetector det(0.4, 14.0, 3);
        CrimeSeries cs;
        cs.seriesId = QStringLiteral("SER-T");
        SeriesEvent anchor;
        anchor.eventId = QStringLiteral("A0");
        anchor.lat = 51.5; anchor.lon = -0.1;
        anchor.tDays = 0.0;
        anchor.moText = QStringLiteral("entry");
        cs.members.append(anchor);

        SeriesEvent nearProbe = anchor;
        nearProbe.eventId = QStringLiteral("N");
        nearProbe.tDays   = 1.0;

        SeriesEvent farProbe = anchor;
        farProbe.eventId = QStringLiteral("F");
        farProbe.tDays   = 30.0;

        const auto nearMatch = det.linkProbability(nearProbe, cs, 0.5);
        const auto farMatch  = det.linkProbability(farProbe, cs, 0.5);
        QVERIFY2(nearMatch.linkProbability >= farMatch.linkProbability,
                 "near temporal probe should have >= link probability than far probe");
    }

    void testIsolatedEventNotInAnySeries()
    {
        const QDateTime base(QDate(2024, 4, 1), QTime(12, 0), Qt::UTC);
        QVector<CrimeEvent> events;
        events.append(makeEvent(QStringLiteral("ISO"), 40.0, -74.0, base));
        for (int i = 0; i < 4; ++i)
            events.append(makeEvent(QStringLiteral("CL%1").arg(i),
                                    51.5074, -0.1278, base.addDays(i)));

        SeriesDetector det(0.3, 14.0, 3);
        const auto series = det.detect(events);
        bool isolatedFound = true;
        for (const auto& s : series) {
            for (const auto& m : s.members) {
                if (m.eventId == QStringLiteral("ISO"))
                    isolatedFound = false;
            }
        }
        QVERIFY(isolatedFound);
    }
};

QTEST_GUILESS_MAIN(TestSeriesDetectorDeep7)
#include "test_series_detector_deep7.moc"
