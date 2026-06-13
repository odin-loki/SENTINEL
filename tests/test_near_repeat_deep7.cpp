// test_near_repeat_deep7.cpp — Deep audit iteration 22: NearRepeatVictimisation
// Verifies: zero bandwidth, Knox ratio, co-located alerts, temporal decay,
//           crime-type bandwidth, minimum event count.

#include <QtTest>
#include <cmath>
#include "models/NearRepeatVictimisation.h"
#include "models/SeriesDetector.h"

class TestNearRepeatDeep7 : public QObject
{
    Q_OBJECT

    static SeriesEvent ev(const QString& id, double lat, double lon,
                          double tDays, const QString& crimeType = QStringLiteral("burglary"))
    {
        SeriesEvent e;
        e.eventId   = id;
        e.lat       = lat;
        e.lon       = lon;
        e.tDays     = tDays;
        e.crimeType = crimeType;
        return e;
    }

private slots:
    void testZeroBandwidthReturnsNoAlerts();
    void testKnoxStatisticAtLeastOneForSpreadEvents();
    void testColocatedEventsHighAlertScore();
    void testTemporalDecayBeyondWindowZero();
    void testCrimeTypeSpecificBandwidth();
    void testAnalyseNeedsAtLeastTwoEvents();
    void testZeroWindowDaysReturnsNoAlerts();
    void testKnoxSingleEventReturnsUnity();
};

void TestNearRepeatDeep7::testZeroBandwidthReturnsNoAlerts()
{
    NearRepeatVictimisation nrv(0.0, 14.0);
    QVector<SeriesEvent> events;
    events << ev(QStringLiteral("A"), 51.5000, -0.1000, 0.0)
           << ev(QStringLiteral("B"), 51.5000, -0.1000, 0.1);

    QVERIFY(nrv.analyse(events).isEmpty());
    QCOMPARE(nrv.alertScore(0.0, 0.0), 0.0);
}

void TestNearRepeatDeep7::testKnoxStatisticAtLeastOneForSpreadEvents()
{
    NearRepeatVictimisation nrv(200.0, 14.0);
    QVector<SeriesEvent> events;
    const double lats[] = { 51.500, 51.501, 51.499, 51.5005, 51.4995, 51.5012 };
    const double lons[] = { -0.100, -0.099, -0.101, -0.1005, -0.0995, -0.1008 };
    const double days[] = { 0.0, 2.0, 5.0, 8.0, 11.0, 13.0 };
    for (int i = 0; i < 6; ++i) {
        events << ev(QStringLiteral("E%1").arg(i),
                     lats[i], lons[i], days[i]);
    }

    const double knox = nrv.knoxStatistic(events);
    QVERIFY2(knox >= 1.0,
             qPrintable(QStringLiteral("near-repeat Knox=%1 should be >= 1.0").arg(knox)));
}

void TestNearRepeatDeep7::testColocatedEventsHighAlertScore()
{
    NearRepeatVictimisation nrv(200.0, 14.0);
    const double score = nrv.alertScore(5.0, 1.0, QStringLiteral("burglary"));

    QVERIFY2(score > 0.9,
             qPrintable(QStringLiteral("co-located near-repeat score %1 should be high")
                            .arg(score)));

    QVector<SeriesEvent> events;
    events << ev(QStringLiteral("P"), 51.5000, -0.1000, 0.0)
           << ev(QStringLiteral("C"), 51.50001, -0.10001, 0.5);

    const auto alerts = nrv.analyse(events);
    QVERIFY(!alerts.isEmpty());
    QVERIFY2(alerts.first().alertScore > 0.9,
             qPrintable(QStringLiteral("analyse alert score %1 should be high")
                            .arg(alerts.first().alertScore)));
}

void TestNearRepeatDeep7::testTemporalDecayBeyondWindowZero()
{
    NearRepeatVictimisation nrv(200.0, 14.0);

    const double inWindow  = nrv.alertScore(10.0, 7.0);
    const double outWindow = nrv.alertScore(10.0, 15.0);

    QVERIFY(inWindow > 0.0);
    QCOMPARE(outWindow, 0.0);
}

void TestNearRepeatDeep7::testCrimeTypeSpecificBandwidth()
{
    const double burglaryBw = NearRepeatVictimisation::bandwidthFor(QStringLiteral("burglary"));
    const double robberyBw  = NearRepeatVictimisation::bandwidthFor(QStringLiteral("robbery"));
    QVERIFY2(burglaryBw != robberyBw,
             qPrintable(QStringLiteral("burglary bw=%1 robbery bw=%2 should differ")
                            .arg(burglaryBw).arg(robberyBw)));

    NearRepeatVictimisation nrv(200.0, 14.0);
    const double at250mBurglary = nrv.alertScore(250.0, 5.0, QStringLiteral("burglary"));
    const double at250mRobbery  = nrv.alertScore(250.0, 5.0, QStringLiteral("robbery"));

    QVERIFY2(at250mRobbery > at250mBurglary,
             qPrintable(QStringLiteral("robbery wider bandwidth: burglary=%1 robbery=%2")
                            .arg(at250mBurglary).arg(at250mRobbery)));
    QCOMPARE(at250mBurglary, 0.0);
}

void TestNearRepeatDeep7::testAnalyseNeedsAtLeastTwoEvents()
{
    NearRepeatVictimisation nrv(200.0, 14.0);

    QVERIFY(nrv.analyse({}).isEmpty());
    QVERIFY(nrv.analyse({ ev(QStringLiteral("ONLY"), 51.5, -0.1, 0.0) }).isEmpty());

    QVector<SeriesEvent> pair;
    pair << ev(QStringLiteral("A"), 51.5000, -0.1000, 0.0)
         << ev(QStringLiteral("B"), 51.5001, -0.1001, 0.5);
    QVERIFY(!nrv.analyse(pair).isEmpty());
}

void TestNearRepeatDeep7::testZeroWindowDaysReturnsNoAlerts()
{
    NearRepeatVictimisation nrv(200.0, 0.0);
    QVector<SeriesEvent> events;
    events << ev(QStringLiteral("A"), 51.5000, -0.1000, 0.0)
           << ev(QStringLiteral("B"), 51.5000, -0.1000, 0.0);

    QVERIFY(nrv.analyse(events).isEmpty());
    QCOMPARE(nrv.alertScore(0.0, 0.0), 0.0);
}

void TestNearRepeatDeep7::testKnoxSingleEventReturnsUnity()
{
    NearRepeatVictimisation nrv(200.0, 14.0);
    const double knox = nrv.knoxStatistic({ ev(QStringLiteral("SOLO"), 51.5, -0.1, 0.0) });
    QCOMPARE(knox, 1.0);
}

QTEST_GUILESS_MAIN(TestNearRepeatDeep7)
#include "test_near_repeat_deep7.moc"
