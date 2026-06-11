// test_near_repeat_deep4.cpp — Deep audit iteration 12 (round 4) for NearRepeatVictimisation
// Verifies Knox ratio, exponential decay kernels, and significance threshold (>1).

#include <QtTest>
#include <cmath>
#include "models/NearRepeatVictimisation.h"
#include "models/SeriesDetector.h"

class TestNearRepeatDeep4 : public QObject
{
    Q_OBJECT

    static SeriesEvent ev(const QString& id, double lat, double lon, double tDays)
    {
        SeriesEvent e;
        e.eventId   = id;
        e.lat       = lat;
        e.lon       = lon;
        e.tDays     = tDays;
        e.crimeType = QStringLiteral("burglary");
        return e;
    }

    NearRepeatVictimisation m_nrv{200.0, 14.0};

private slots:

    void testSpatialDecayExponentialFormula()
    {
        const double bw  = 200.0;
        const double dist = 50.0;
        const double expected = std::exp(-dist / bw);

        NearRepeatVictimisation nrv(bw, 14.0);
        const double score = nrv.alertScore(dist, 0.0);
        QVERIFY2(std::abs(score - expected) < 1e-9,
                 qPrintable(QString("spatial exp decay: got %1 expected %2")
                                .arg(score).arg(expected)));
    }

    void testTemporalDecayExponentialFormula()
    {
        const double win = 14.0;
        const double dt  = 3.5;
        const double expected = std::exp(-dt / win);

        NearRepeatVictimisation nrv(200.0, win);
        const double score = nrv.alertScore(0.0, dt);
        QVERIFY2(std::abs(score - expected) < 1e-9,
                 qPrintable(QString("temporal exp decay: got %1 expected %2")
                                .arg(score).arg(expected)));
    }

    void testAlertScoreIsProductOfDecays()
    {
        const double dist = 80.0;
        const double dt   = 2.0;
        const double expected = std::exp(-dist / 200.0) * std::exp(-dt / 14.0);
        const double got = m_nrv.alertScore(dist, dt);
        QVERIFY2(std::abs(got - expected) < 1e-9,
                 qPrintable(QString("alertScore=%1 expected %2").arg(got).arg(expected)));
    }

    void testDecayZeroBeyondBandwidthOrWindow()
    {
        QVERIFY2(m_nrv.alertScore(201.0, 0.0) < 1e-12, "beyond spatial bandwidth → 0");
        QVERIFY2(m_nrv.alertScore(0.0, 15.0) < 1e-12, "beyond temporal window → 0");
    }

    void testKnoxObservedOverExpectedRatio()
    {
        QVector<SeriesEvent> events;
        events << ev("A", 0.0, 0.0,     0.0)
               << ev("B", 0.0, 0.001,   1.0);

        const double knox = m_nrv.knoxStatistic(events);
        // Manual: 1 near pair / (1 * pSpace * pTime) ≈ 7.96 (see deep2 audit)
        QVERIFY2(knox > 7.0 && knox < 9.0,
                 qPrintable(QString("Knox ratio=%1 expected ≈7.96").arg(knox)));
    }

    void testKnoxSignificanceThresholdGreaterThanOne()
    {
        QVector<SeriesEvent> cluster;
        cluster << ev("E1", 51.5000, -0.1000, 0.0)
                << ev("E2", 51.5001, -0.1001, 0.5)
                << ev("E3", 51.5002, -0.1002, 1.0);

        const double knox = m_nrv.knoxStatistic(cluster);
        QVERIFY2(knox > 1.0,
                 qPrintable(QString("clustered events Knox=%1 must exceed 1.0 threshold")
                                .arg(knox)));
    }

    void testKnoxWellSeparatedNearUnity()
    {
        QVector<SeriesEvent> spread;
        for (int i = 0; i < 8; ++i)
            spread << ev(QString("E%1").arg(i), 51.0 + i * 0.08, 0.0, i * 25.0);

        const double knox = m_nrv.knoxStatistic(spread);
        QVERIFY2(knox >= 0.0 && knox <= 2.0,
                 qPrintable(QString("well-separated events Knox=%1 should not be extreme")
                                .arg(knox)));
    }

    void testKnoxEmptyOrSingleReturnsNeutral()
    {
        QCOMPARE(m_nrv.knoxStatistic({}), 1.0);
        QCOMPARE(m_nrv.knoxStatistic({ ev("A", 0.0, 0.0, 0.0) }), 1.0);
    }

    void testKnoxZeroWhenNoNearPairs()
    {
        QVector<SeriesEvent> far;
        far << ev("A", 0.0,  0.0,  0.0)
            << ev("B", 10.0, 10.0, 100.0);
        QCOMPARE(m_nrv.knoxStatistic(far), 0.0);
    }

    void testAnalyseNearPairProducesPositiveAlert()
    {
        QVector<SeriesEvent> events;
        events << ev("A", 0.0, 0.0,   0.0)
               << ev("B", 0.0, 0.001, 1.0);

        const auto alerts = m_nrv.analyse(events);
        QVERIFY2(!alerts.isEmpty(), "near pair must produce alert");
        const double expectedScore = std::exp(-111.0 / 200.0) * std::exp(-1.0 / 14.0);
        QVERIFY2(alerts.first().alertScore > 0.5,
                 qPrintable(QString("alert score=%1 expected >0.5").arg(alerts.first().alertScore)));
        QVERIFY2(alerts.first().alertScore <= expectedScore + 0.05,
                 "alert score should follow exponential product within tolerance");
    }
};

QTEST_GUILESS_MAIN(TestNearRepeatDeep4)
#include "test_near_repeat_deep4.moc"
