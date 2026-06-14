// test_series_detector_deep8.cpp — Deep audit iteration 27: SeriesDetector
// moJaccard empty strings, dominant crime type, temporal span, isolated event.
#include <QTest>
#include "models/SeriesDetector.h"
#include "core/CrimeEvent.h"

class TestSeriesDetectorDeep8 : public QObject
{
    Q_OBJECT

    static CrimeEvent ev(const QString& id, double lat, double lon,
                         const QDateTime& dt, const QString& type,
                         const QString& mo = QStringLiteral("forced entry"))
    {
        CrimeEvent e;
        e.eventId    = id;
        e.suburb     = QStringLiteral("SD8");
        e.lat        = lat;
        e.lon        = lon;
        e.latitude   = lat;
        e.longitude  = lon;
        e.timestamp  = dt;
        e.occurredAt = dt;
        e.crimeType  = type;
        e.narrative  = mo;
        return e;
    }

private slots:

    void testMoJaccardEmptyReturnsZero()
    {
        QCOMPARE(SeriesDetector::moJaccard(QString{}, QStringLiteral("text")), 0.0);
        QCOMPARE(SeriesDetector::moJaccard(QStringLiteral("a"), QString{}), 0.0);
    }

    void testMoJaccardIdenticalIsOne()
    {
        const QString mo = QStringLiteral("rear entry crowbar");
        QCOMPARE(SeriesDetector::moJaccard(mo, mo), 1.0);
    }

    void testDominantCrimeTypeInSeries()
    {
        const QDateTime base(QDate(2024, 4, 1), QTime(9, 0), Qt::UTC);
        QVector<CrimeEvent> events;
        for (int i = 0; i < 4; ++i)
            events.append(ev(QStringLiteral("B%1").arg(i), 51.5 + i * 1e-6, -0.1,
                             base.addDays(i), QStringLiteral("burglary")));

        SeriesDetector det(0.5, 14.0, 3);
        const auto series = det.detect(events);
        QVERIFY(!series.isEmpty());
        QCOMPARE(series.first().dominantCrimeType, QStringLiteral("burglary"));
    }

    void testIsolatedEventNoSeries()
    {
        const QDateTime base(QDate(2024, 5, 1), QTime(0, 0), Qt::UTC);
        const auto events = QVector<CrimeEvent>{
            ev(QStringLiteral("LONE"), 40.0, -74.0, base, QStringLiteral("theft"))
        };
        SeriesDetector det(0.3, 14.0, 3);
        QVERIFY(det.detect(events).isEmpty());
    }

    void testSeriesTemporalSpanPositive()
    {
        const QDateTime base(QDate(2024, 6, 1), QTime(0, 0), Qt::UTC);
        QVector<CrimeEvent> events;
        for (int i = 0; i < 4; ++i)
            events.append(ev(QStringLiteral("T%1").arg(i), 51.50 + i * 1e-6, -0.12,
                             base.addDays(i * 2), QStringLiteral("robbery")));

        const auto series = SeriesDetector(0.4, 20.0, 3).detect(events);
        QVERIFY(!series.isEmpty());
        QVERIFY(series.first().lastDays >= series.first().firstDays);
    }
};

QTEST_GUILESS_MAIN(TestSeriesDetectorDeep8)
#include "test_series_detector_deep8.moc"
