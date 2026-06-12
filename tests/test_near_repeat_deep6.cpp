// test_near_repeat_deep6.cpp — Deep audit iteration 20: NearRepeatVictimisation
// analyse() pairing, Knox area floor, zero-bandwidth edge cases, crime-type routing.

#include <QtTest>
#include <cmath>
#include "models/NearRepeatVictimisation.h"
#include "models/SeriesDetector.h"

class TestNearRepeatDeep6 : public QObject
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

    // ── analyse() uses current event crime type, not prior ───────────────────

    void testAnalyseUsesCurrentEventCrimeType()
    {
        // Prior burglary + current robbery at dt=10: robbery window=7d → score 0
        NearRepeatVictimisation nrv(400.0, 14.0);
        QVector<SeriesEvent> events;
        events << ev(QStringLiteral("P1"), 51.5000, -0.1000, 0.0, QStringLiteral("burglary"))
               << ev(QStringLiteral("C1"), 51.5001, -0.1001, 10.0, QStringLiteral("robbery"));

        const auto alerts = nrv.analyse(events);
        QVERIFY2(alerts.isEmpty(),
                 "analyse must apply current (robbery) 7-day window — 10-day gap → no alert");
    }

    void testAnalyseNegativeTDaysUsesAbsoluteGap()
    {
        NearRepeatVictimisation nrv(200.0, 14.0);
        QVector<SeriesEvent> events;
        events << ev(QStringLiteral("A"), 51.5000, -0.1000, 5.0)
               << ev(QStringLiteral("B"), 51.5001, -0.1001, -2.0);

        const auto alerts = nrv.analyse(events);
        QVERIFY2(!alerts.isEmpty(), "negative tDays must pair via abs(dt)=7");
        QVERIFY2(alerts.first().temporalDistanceDays > 6.9
                 && alerts.first().temporalDistanceDays < 7.1,
                 qPrintable(QStringLiteral("expected |5-(-2)|=7, got %1")
                                .arg(alerts.first().temporalDistanceDays)));
    }

    // ── Knox statistic: co-located cluster + minimum area floor ───────────────

    void testKnoxColocatedClusterExceedsUnity()
    {
        NearRepeatVictimisation nrv(200.0, 14.0);
        QVector<SeriesEvent> events;
        for (int i = 0; i < 4; ++i)
            events << ev(QStringLiteral("E%1").arg(i), 51.501, -0.100, static_cast<double>(i));

        const double knox = nrv.knoxStatistic(events);
        QVERIFY2(knox > 1.0,
                 qPrintable(QStringLiteral("co-located cluster Knox=%1 should exceed 1.0").arg(knox)));
    }

    void testKnoxUsesConstructorParamsNotCrimeTypeCalibration()
    {
        // BUG (documented): knoxStatistic always uses m_bandwidthM/m_windowDays,
        // not per-crime-type calibration from SeriesDetector::nearRepeatFor().
        NearRepeatVictimisation nrv(50.0, 3.0); // tight params
        QVector<SeriesEvent> events;
        events << ev(QStringLiteral("A"), 51.5000, -0.1000, 0.0, QStringLiteral("burglary"))
               << ev(QStringLiteral("B"), 51.5004, -0.1004, 2.0, QStringLiteral("robbery"));

        const double knoxTight = nrv.knoxStatistic(events);
        NearRepeatVictimisation nrvLoose(500.0, 30.0);
        const double knoxLoose = nrvLoose.knoxStatistic(events);

        QVERIFY2(knoxTight != knoxLoose,
                 "Knox must vary with constructor bandwidth/window (crime type ignored)");
    }

    // ── Zero / invalid kernel parameters ──────────────────────────────────────

    void testZeroBandwidthProducesNoAlerts()
    {
        NearRepeatVictimisation nrv(0.0, 14.0);
        QVector<SeriesEvent> events;
        events << ev(QStringLiteral("A"), 51.5000, -0.1000, 0.0)
               << ev(QStringLiteral("B"), 51.5000, -0.1000, 0.1);

        QVERIFY(nrv.analyse(events).isEmpty());
        QCOMPARE(nrv.alertScore(0.0, 0.0), 0.0);
    }

    void testEmptyCrimeTypeFallsBackToConstructorDefaults()
    {
        NearRepeatVictimisation nrv(150.0, 10.0);
        const double withType = nrv.alertScore(75.0, 5.0, QStringLiteral("burglary"));
        const double noType   = nrv.alertScore(75.0, 5.0, QString());
        QVERIFY2(withType != noType,
                 "empty crimeType must use constructor defaults, not burglary calibration");
        const double expected = std::exp(-75.0 / 150.0) * std::exp(-5.0 / 10.0);
        QVERIFY2(std::abs(noType - expected) < 1e-9,
                 qPrintable(QStringLiteral("empty type score=%1 expected %2").arg(noType).arg(expected)));
    }

    // ── Pair enumeration: n events → n*(n-1)/2 candidate prior pairs ─────────

    void testAnalyseTripleEventAlertCount()
    {
        NearRepeatVictimisation nrv(200.0, 14.0);
        QVector<SeriesEvent> events;
        events << ev(QStringLiteral("E0"), 51.5000, -0.1000, 0.0)
               << ev(QStringLiteral("E1"), 51.5001, -0.1001, 0.5)
               << ev(QStringLiteral("E2"), 51.5002, -0.1002, 1.0);

        const auto alerts = nrv.analyse(events);
        QCOMPARE(alerts.size(), 3); // pairs (1,0), (2,0), (2,1)
        for (const auto& a : alerts) {
            QVERIFY2(a.alertScore > 0.0 && a.alertScore <= 1.0,
                     qPrintable(QStringLiteral("alert score %1 out of (0,1]").arg(a.alertScore)));
            QVERIFY(!a.eventId.isEmpty());
            QVERIFY(!a.priorEventId.isEmpty());
        }
    }
};

QTEST_GUILESS_MAIN(TestNearRepeatDeep6)
#include "test_near_repeat_deep6.moc"
