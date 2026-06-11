// Iteration 18 — SeriesDetector deep5: dedup, metadata, multi-cluster, crime-type lookup
#include <QtTest/QtTest>
#include "models/SeriesDetector.h"
#include "core/CrimeEvent.h"

class SeriesDetectorDeep5Test : public QObject
{
    Q_OBJECT

    static SeriesEvent sev(const QString& id, double lat, double lon, double tDays,
                           const QString& type = QStringLiteral("burglary"),
                           const QString& mo = QStringLiteral("forced_entry night"))
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

    static CrimeSeries makeSeries(const QVector<SeriesEvent>& members,
                                  const QString& dominant = QStringLiteral("burglary"))
    {
        CrimeSeries s;
        s.seriesId        = QStringLiteral("SERIES-0001");
        s.members         = members;
        s.centroidLat     = 0.0;
        s.centroidLon     = 0.0;
        s.dominantCrimeType = dominant;
        return s;
    }

private slots:

    void testDuplicateEventIdKeepsFirstOnly()
    {
        SeriesDetector sd(0.5, 14.0, 3);
        QVector<SeriesEvent> events;
        events.append(sev(QStringLiteral("DUP"), 51.5, -0.1, 0.0));
        events.append(sev(QStringLiteral("DUP"), 51.51, -0.11, 1.0));
        events.append(sev(QStringLiteral("DUP"), 51.52, -0.12, 2.0));
        for (int i = 0; i < 3; ++i)
            events.append(sev(QStringLiteral("U%1").arg(i),
                              51.5 + i * 0.0001, -0.1, static_cast<double>(i + 1)));

        const auto series = sd.detectSeries(events);
        int dupCount = 0;
        for (const auto& s : series) {
            for (const auto& m : s.members) {
                if (m.eventId == QStringLiteral("DUP"))
                    ++dupCount;
            }
        }
        QCOMPARE(dupCount, 1);
    }

    void testEmptyEventIdNotDeduplicated()
    {
        SeriesDetector sd(0.5, 14.0, 3);
        QVector<SeriesEvent> events;
        for (int i = 0; i < 4; ++i)
            events.append(sev(QString(), 51.5 + i * 0.0001, -0.1, static_cast<double>(i)));

        const auto series = sd.detectSeries(events);
        QCOMPARE(series.size(), 1);
        QCOMPARE(series[0].members.size(), 4);
    }

    void testMembersSortedChronologically()
    {
        SeriesDetector sd(0.5, 14.0, 3);
        QVector<SeriesEvent> events;
        events.append(sev(QStringLiteral("C"), 51.5, -0.1, 10.0));
        events.append(sev(QStringLiteral("A"), 51.5001, -0.1, 2.0));
        events.append(sev(QStringLiteral("B"), 51.5002, -0.1, 5.0));

        const auto series = sd.detectSeries(events);
        QCOMPARE(series.size(), 1);
        QCOMPARE(series[0].members.size(), 3);
        QVERIFY(series[0].members[0].tDays <= series[0].members[1].tDays);
        QVERIFY(series[0].members[1].tDays <= series[0].members[2].tDays);
        QCOMPARE(series[0].firstDays, 2.0);
        QCOMPARE(series[0].lastDays, 10.0);
    }

    void testTwoSpatialClustersSeparateSeries()
    {
        SeriesDetector sd(0.5, 30.0, 3);
        QVector<SeriesEvent> events;
        for (int i = 0; i < 3; ++i)
            events.append(sev(QStringLiteral("W%1").arg(i), 51.5, -0.1, static_cast<double>(i)));
        for (int i = 0; i < 3; ++i)
            events.append(sev(QStringLiteral("E%1").arg(i), 52.5, -0.1, static_cast<double>(i + 10)));

        const auto series = sd.detectSeries(events);
        QCOMPARE(series.size(), 2);
    }

    void testNearRepeatCrimeTypeNormalization()
    {
        const auto spaced = SeriesDetector::nearRepeatFor(QStringLiteral("Drug Offence"));
        const auto keyed  = SeriesDetector::nearRepeatFor(QStringLiteral("drug_offence"));
        QCOMPARE(spaced.distM, keyed.distM);
        QCOMPARE(spaced.days, keyed.days);
        QCOMPARE(spaced.multiplier, keyed.multiplier);
    }

    void testHaversineZeroDistance()
    {
        const double d = SeriesDetector::haversineKm(51.5, -0.1, 51.5, -0.1);
        QVERIFY(std::abs(d) < 1e-9);
    }

    void testLinkProbabilityCrimeTypeAffectsMultiplier()
    {
        SeriesDetector sd(0.5, 14.0, 3);
        QVector<SeriesEvent> members;
        for (int i = 0; i < 3; ++i)
            members.append(sev(QStringLiteral("M%1").arg(i), 51.5, -0.1, static_cast<double>(i)));

        SeriesEvent candidate = members[0];
        candidate.eventId = QStringLiteral("NEW");
        candidate.tDays   = 1.0;

        CrimeSeries burglarySeries = makeSeries(members, QStringLiteral("burglary"));
        CrimeSeries assaultSeries  = makeSeries(members, QStringLiteral("assault"));

        const auto burglaryMatch = sd.linkProbability(candidate, burglarySeries, 0.5);
        const auto assaultMatch  = sd.linkProbability(candidate, assaultSeries, 0.5);

        QVERIFY2(burglaryMatch.linkProbability > assaultMatch.linkProbability,
                 qPrintable(QStringLiteral("burglary multiplier=%1 should exceed assault=%2")
                                .arg(burglaryMatch.linkProbability)
                                .arg(assaultMatch.linkProbability)));
    }

    void testDetectSeriesCentroidIsMemberMean()
    {
        SeriesDetector sd(0.5, 14.0, 3);
        QVector<SeriesEvent> events;
        events.append(sev(QStringLiteral("A"), 51.50, -0.10, 0.0));
        events.append(sev(QStringLiteral("B"), 51.5001, -0.0999, 1.0));
        events.append(sev(QStringLiteral("C"), 51.5002, -0.1001, 2.0));

        const auto series = sd.detectSeries(events);
        QCOMPARE(series.size(), 1);
        const double expectedLat = (51.50 + 51.5001 + 51.5002) / 3.0;
        const double expectedLon = (-0.10 + -0.0999 + -0.1001) / 3.0;
        QVERIFY(std::abs(series[0].centroidLat - expectedLat) < 1e-9);
        QVERIFY(std::abs(series[0].centroidLon - expectedLon) < 1e-9);
    }
};

QTEST_GUILESS_MAIN(SeriesDetectorDeep5Test)
#include "test_series_detector_deep5.moc"
