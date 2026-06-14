// test_near_repeat_deep8.cpp — Deep audit iteration 24: NearRepeatVictimisation
// Knox statistic, decay kernels, bandwidth per crime type, alert score bounds.
#include <QTest>
#include <cmath>
#include "models/NearRepeatVictimisation.h"
#include "models/SeriesDetector.h"

class TestNearRepeatDeep8 : public QObject
{
    Q_OBJECT

    static SeriesEvent ev(const QString& id, double lat, double lon, double tDays,
                          const QString& type = QStringLiteral("burglary"))
    {
        SeriesEvent e;
        e.eventId   = id;
        e.lat       = lat;
        e.lon       = lon;
        e.tDays     = tDays;
        e.crimeType = type;
        return e;
    }

private slots:

    void testKnoxStatisticAtLeastOneForCluster()
    {
        NearRepeatVictimisation nrv(200.0, 14.0);
        QVector<SeriesEvent> events;
        for (int i = 0; i < 6; ++i)
            events.append(ev(QStringLiteral("K%1").arg(i),
                             51.5 + i * 1e-5, -0.1, static_cast<double>(i)));

        const double knox = nrv.knoxStatistic(events);
        QVERIFY(std::isfinite(knox));
        QVERIFY2(knox >= 1.0, qPrintable(QStringLiteral("knox=%1").arg(knox)));
    }

    void testAlertScoreInUnitInterval()
    {
        NearRepeatVictimisation nrv(200.0, 14.0);
        const double score = nrv.alertScore(50.0, 3.0, QStringLiteral("burglary"));
        QVERIFY2(score >= 0.0 && score <= 1.0,
                 qPrintable(QStringLiteral("score=%1").arg(score)));
    }

    void testAnalyseNeedsAtLeastTwoEvents()
    {
        NearRepeatVictimisation nrv;
        QVERIFY(nrv.analyse({ ev(QStringLiteral("A"), 51.5, -0.1, 0.0) }).isEmpty());
    }

    void testTemporalDecayBeyondWindowZero()
    {
        NearRepeatVictimisation nrv(200.0, 14.0);
        const double inWin  = nrv.alertScore(10.0, 7.0);
        const double outWin = nrv.alertScore(10.0, 20.0);
        QVERIFY(inWin > 0.0);
        QCOMPARE(outWin, 0.0);
    }

    void testBandwidthForCrimeTypesPositive()
    {
        const double burg  = NearRepeatVictimisation::bandwidthFor(QStringLiteral("burglary"));
        const double theft = NearRepeatVictimisation::bandwidthFor(QStringLiteral("theft"));
        QVERIFY2(burg > 0.0, qPrintable(QStringLiteral("burg=%1").arg(burg)));
        QVERIFY2(theft > 0.0, qPrintable(QStringLiteral("theft=%1").arg(theft)));
    }

    void testColocatedEventsProduceAlerts()
    {
        NearRepeatVictimisation nrv(200.0, 14.0);
        QVector<SeriesEvent> events;
        events.append(ev(QStringLiteral("P"), 51.5, -0.1, 0.0));
        events.append(ev(QStringLiteral("C"), 51.50001, -0.10001, 0.5));
        const auto alerts = nrv.analyse(events);
        QVERIFY(!alerts.isEmpty());
        QVERIFY2(alerts.first().alertScore > 0.5,
                 qPrintable(QStringLiteral("score=%1").arg(alerts.first().alertScore)));
    }

    void testZeroBandwidthNoAlerts()
    {
        NearRepeatVictimisation nrv(0.0, 14.0);
        QVector<SeriesEvent> events;
        events.append(ev(QStringLiteral("A"), 51.5, -0.1, 0.0));
        events.append(ev(QStringLiteral("B"), 51.5, -0.1, 0.1));
        QVERIFY(nrv.analyse(events).isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestNearRepeatDeep8)
#include "test_near_repeat_deep8.moc"
