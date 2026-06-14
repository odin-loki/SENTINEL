// test_series_detector_deep6.cpp — Deep audit iteration 21: SeriesDetector
// Verifies: dominant type, MO Jaccard, link caps, noise exclusion, CrimeEvent path, series IDs.

#include <QtTest/QtTest>
#include "models/SeriesDetector.h"
#include "core/CrimeEvent.h"

class SeriesDetectorDeep6Test : public QObject
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
        s.seriesId          = QStringLiteral("SERIES-0001");
        s.members           = members;
        s.dominantCrimeType = dominant;
        return s;
    }

private slots:

    void testDominantCrimeTypeIsMajority()
    {
        SeriesDetector sd(0.5, 30.0, 3);
        QVector<SeriesEvent> events;
        events.append(sev(QStringLiteral("B1"), 51.5, -0.1, 0.0, QStringLiteral("burglary")));
        events.append(sev(QStringLiteral("B2"), 51.5001, -0.1, 1.0, QStringLiteral("burglary")));
        events.append(sev(QStringLiteral("B3"), 51.5002, -0.1, 2.0, QStringLiteral("burglary")));
        events.append(sev(QStringLiteral("B4"), 51.5003, -0.1, 3.0, QStringLiteral("burglary")));
        events.append(sev(QStringLiteral("R1"), 51.5004, -0.1, 4.0, QStringLiteral("robbery")));

        const auto series = sd.detectSeries(events);
        QCOMPARE(series.size(), 1);
        QCOMPARE(series[0].dominantCrimeType, QStringLiteral("burglary"));
        QCOMPARE(series[0].members.size(), 4);
    }

    void testUnknownCrimeTypeUsesDefaultNearRepeatParams()
    {
        const auto known    = SeriesDetector::nearRepeatFor(QStringLiteral("burglary"));
        const auto unknown  = SeriesDetector::nearRepeatFor(QStringLiteral("arson_xyz"));
        const auto defaults = SeriesDetector::NearRepeatParams{};

        QCOMPARE(unknown.distM, defaults.distM);
        QCOMPARE(unknown.days, defaults.days);
        QCOMPARE(unknown.multiplier, defaults.multiplier);
        QVERIFY(known.distM != unknown.distM);
    }

    void testMoJaccardIdenticalAndDisjoint()
    {
        const double identical = SeriesDetector::moJaccard(
            QStringLiteral("forced entry night"),
            QStringLiteral("forced entry night"));
        QCOMPARE(identical, 1.0);

        const double disjoint = SeriesDetector::moJaccard(
            QStringLiteral("alpha beta"),
            QStringLiteral("gamma delta"));
        QCOMPARE(disjoint, 0.0);

        QCOMPARE(SeriesDetector::moJaccard(QString(), QString()), 0.0);
        QCOMPARE(SeriesDetector::moJaccard(QStringLiteral("a"), QString()), 0.0);
    }

    void testLinkProbabilityMoSimilarityClampedAndCapped()
    {
        SeriesDetector sd(0.5, 14.0, 3);
        QVector<SeriesEvent> members;
        for (int i = 0; i < 3; ++i)
            members.append(sev(QStringLiteral("M%1").arg(i), 51.5, -0.1, static_cast<double>(i)));

        SeriesEvent candidate = sev(QStringLiteral("NEW"), 51.5, -0.1, 1.0);
        CrimeSeries series = makeSeries(members);

        const auto mid  = sd.linkProbability(candidate, series, 0.5);
        const auto atOne = sd.linkProbability(candidate, series, 1.0);
        const auto above = sd.linkProbability(candidate, series, 5.0);

        QVERIFY(mid.linkProbability <= 0.95);
        QVERIFY(above.linkProbability <= 0.95);
        QCOMPARE(atOne.compositeScore, above.compositeScore);
        QCOMPARE(atOne.linkProbability, above.linkProbability);
        QVERIFY(atOne.compositeScore > mid.compositeScore);
    }

    void testLinkProbabilityDistantEventLowerScore()
    {
        SeriesDetector sd(0.5, 14.0, 3);
        QVector<SeriesEvent> members;
        for (int i = 0; i < 3; ++i)
            members.append(sev(QStringLiteral("M%1").arg(i), 51.5, -0.1, static_cast<double>(i)));

        CrimeSeries series = makeSeries(members);
        const SeriesEvent nearEv  = sev(QStringLiteral("NEAR"), 51.5001, -0.1, 1.0);
        const SeriesEvent farEv   = sev(QStringLiteral("FAR"), 53.0, -0.5, 1.0);

        const auto nearMatch = sd.linkProbability(nearEv, series, 0.8);
        const auto farMatch  = sd.linkProbability(farEv, series, 0.8);

        QVERIFY2(nearMatch.compositeScore > farMatch.compositeScore,
                 qPrintable(QStringLiteral("near composite=%1 far=%2")
                                .arg(nearMatch.compositeScore)
                                .arg(farMatch.compositeScore)));
        QVERIFY(nearMatch.linkProbability > farMatch.linkProbability);
    }

    void testNoisePointsExcludedWhenBelowMinSamples()
    {
        SeriesDetector sd(0.3, 14.0, 4);
        QVector<SeriesEvent> events;
        for (int i = 0; i < 3; ++i)
            events.append(sev(QStringLiteral("C%1").arg(i), 51.5, -0.1, static_cast<double>(i)));
        events.append(sev(QStringLiteral("ISO"), 52.5, -0.5, 5.0));

        const auto series = sd.detectSeries(events);
        QCOMPARE(series.size(), 0);
    }

    void testSeriesIdZeroPaddedFormat()
    {
        SeriesDetector sd(0.5, 30.0, 3);
        QVector<SeriesEvent> west;
        QVector<SeriesEvent> east;
        for (int i = 0; i < 3; ++i) {
            west.append(sev(QStringLiteral("W%1").arg(i), 51.5, -0.1, static_cast<double>(i)));
            east.append(sev(QStringLiteral("E%1").arg(i), 52.5, -0.1, static_cast<double>(i + 10)));
        }
        QVector<SeriesEvent> events = west + east;

        const auto series = sd.detectSeries(events);
        QCOMPARE(series.size(), 2);
        for (const auto& s : series)
            QVERIFY(s.seriesId.startsWith(QStringLiteral("SERIES-")));
    }

    void testDetectCrimeEventOverloadClustersByCoords()
    {
        SeriesDetector sd(0.5, 365.0, 3);
        QVector<CrimeEvent> events;
        const QDateTime epoch = QDateTime(QDate(2000, 1, 1), QTime(0, 0, 0), QTimeZone::utc());

        for (int i = 0; i < 3; ++i) {
            CrimeEvent ev;
            ev.eventId    = QStringLiteral("CE%1").arg(i);
            ev.lat        = 51.5 + i * 0.0001;
            ev.lon        = -0.1;
            ev.occurredAt = epoch.addDays(i);
            ev.crimeType  = QStringLiteral("burglary");
            events.append(ev);
        }

        const auto series = sd.detect(events);
        QCOMPARE(series.size(), 1);
        QCOMPARE(series[0].members.size(), 3);
    }
};

QTEST_GUILESS_MAIN(SeriesDetectorDeep6Test)
#include "test_series_detector_deep6.moc"
