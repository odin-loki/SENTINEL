// test_near_repeat_deep9.cpp — Deep audit iteration 28: NearRepeatVictimisation
// bandwidthFor crime types, analyse pair ordering, window/bandwidth accessors.
#include <QTest>
#include <cmath>
#include "models/NearRepeatVictimisation.h"
#include "models/SeriesDetector.h"

class TestNearRepeatDeep9 : public QObject
{
    Q_OBJECT

    static SeriesEvent ev(const QString& id, double lat, double lon, double t)
    {
        SeriesEvent e;
        e.eventId   = id;
        e.lat       = lat;
        e.lon       = lon;
        e.tDays     = t;
        e.crimeType = QStringLiteral("burglary");
        return e;
    }

private slots:

    void testBandwidthForBurglaryPositive()
    {
        const double bw = NearRepeatVictimisation::bandwidthFor(QStringLiteral("burglary"));
        QVERIFY(bw > 0.0);
    }

    void testBandwidthForUnknownUsesDefault()
    {
        const double known = NearRepeatVictimisation::bandwidthFor(QStringLiteral("burglary"));
        const double unk   = NearRepeatVictimisation::bandwidthFor(QStringLiteral("unknown_xyz"));
        QVERIFY(unk > 0.0);
        QVERIFY(unk <= known * 3.0);
    }

    void testAnalyseReturnsSpatialDistance()
    {
        NearRepeatVictimisation nrv(300.0, 21.0);
        const auto alerts = nrv.analyse({
            ev(QStringLiteral("A"), 51.5, -0.1, 0.0),
            ev(QStringLiteral("B"), 51.50001, -0.10001, 2.0),
        });
        QVERIFY(!alerts.isEmpty());
        QVERIFY(alerts.first().spatialDistanceM >= 0.0);
        QVERIFY(alerts.first().temporalDistanceDays >= 0.0);
    }

    void testKnoxFiniteForSpreadEvents()
    {
        NearRepeatVictimisation nrv(200.0, 14.0);
        QVector<SeriesEvent> events;
        for (int i = 0; i < 6; ++i)
            events.append(ev(QStringLiteral("S%1").arg(i), 51.5 + i * 1e-5, -0.1, static_cast<double>(i)));
        const double knox = nrv.knoxStatistic(events);
        QVERIFY(std::isfinite(knox));
        QVERIFY2(knox >= 1.0, qPrintable(QStringLiteral("knox=%1").arg(knox)));
    }

    void testAccessorsMatchConstructor()
    {
        NearRepeatVictimisation nrv(450.0, 7.0);
        QCOMPARE(nrv.bandwidthM(), 450.0);
        QCOMPARE(nrv.windowDays(), 7.0);
    }
};

QTEST_GUILESS_MAIN(TestNearRepeatDeep9)
#include "test_near_repeat_deep9.moc"
