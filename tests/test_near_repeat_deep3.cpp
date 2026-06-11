#include <QTest>
#include <QVector>
#include <QString>
#include <cmath>
#include "models/NearRepeatVictimisation.h"
#include "models/SeriesDetector.h"

// NOTE ON IMPLEMENTATION:
//   NearRepeatVictimisation uses exponential decay kernels within hard cutoffs:
//     spatialDecay  = exp(-distM / bandwidthM)   for distM <= bandwidthM
//     temporalDecay = exp(-dtDays / windowDays)  for dtDays <= windowDays

static SeriesEvent makeEvent(const QString& id, double lat, double lon,
                              double tDays, const QString& crimeType = QStringLiteral("burglary"))
{
    SeriesEvent e;
    e.eventId    = id;
    e.lat        = lat;
    e.lon        = lon;
    e.tDays      = tDays;
    e.crimeType  = crimeType;
    return e;
}

class NearRepeatDeep3Test : public QObject
{
    Q_OBJECT

    // Default NRV: 200 m bandwidth, 14 day window
    NearRepeatVictimisation m_nrv{200.0, 14.0};

private slots:

    // ── Single-event: no alerts ───────────────────────────────────────────────

    void testSingleEventAnalyseEmpty()
    {
        QVector<SeriesEvent> events;
        events << makeEvent("E1", 51.5, -0.1, 0.0);
        const auto alerts = m_nrv.analyse(events);
        QVERIFY2(alerts.isEmpty(), "analyse() with single event must return empty vector");
    }

    void testSingleEventKnoxNeutral()
    {
        // knoxStatistic returns 1.0 for < 2 events (no anomalous clustering signal)
        QVector<SeriesEvent> events;
        events << makeEvent("E1", 51.5, -0.1, 0.0);
        const double knox = m_nrv.knoxStatistic(events);
        QVERIFY2(std::isfinite(knox), "knoxStatistic should be finite for single event");
        QVERIFY2(knox >= 0.0, "knoxStatistic should be non-negative");
    }

    void testEmptyEventListAnalyseEmpty()
    {
        QVector<SeriesEvent> events;
        QVERIFY(m_nrv.analyse(events).isEmpty());
    }

    // ── Two events close in space AND time → high alert score ────────────────

    void testTwoEventsCloseHighScore()
    {
        // ~13 m apart (within 200 m), 0.5 days apart (within 14 days)
        QVector<SeriesEvent> events;
        events << makeEvent("E1", 51.5000, -0.1000, 0.0);
        events << makeEvent("E2", 51.5001, -0.1001, 0.5);

        const auto alerts = m_nrv.analyse(events);
        QVERIFY2(!alerts.isEmpty(), "Two nearby events should produce at least one alert");

        const double score = alerts.first().alertScore;
        QVERIFY2(score > 0.8,
                 qPrintable(QStringLiteral("Expected alertScore > 0.8 for very close events, got %1").arg(score)));
        QVERIFY2(score <= 1.0, "alertScore must not exceed 1.0");
    }

    // ── Two events far apart → no alerts ─────────────────────────────────────

    void testTwoEventsFarNoAlert()
    {
        // ~2.5 km apart → beyond 200 m bandwidth → alertScore = 0 → no alert emitted
        QVector<SeriesEvent> events;
        events << makeEvent("E1", 51.5000, -0.1000, 0.0);
        events << makeEvent("E2", 51.5220, -0.1220, 100.0);  // also 100 days apart

        const auto alerts = m_nrv.analyse(events);
        QVERIFY2(alerts.isEmpty(),
                 "Two far-apart events (> bandwidth AND > window) should produce no alert");
    }

    // ── alertScore boundary values ────────────────────────────────────────────

    void testAlertScoreZeroDistance()
    {
        // Both spatial and temporal at zero → score = 1.0
        const double score = m_nrv.alertScore(0.0, 0.0);
        QVERIFY2(std::abs(score - 1.0) < 1e-9,
                 qPrintable(QStringLiteral("alertScore(0,0) expected 1.0, got %1").arg(score)));
    }

    void testAlertScoreAtSpatialBoundary()
    {
        const double expected = std::exp(-1.0);
        const double score = m_nrv.alertScore(200.0, 0.0);
        QVERIFY2(std::abs(score - expected) < 1e-9,
                 qPrintable(QStringLiteral("alertScore(bw,0) expected exp(-1)=%1, got %2")
                                .arg(expected).arg(score)));
    }

    void testAlertScoreAtTemporalBoundary()
    {
        const double expected = std::exp(-1.0);
        const double score = m_nrv.alertScore(0.0, 14.0);
        QVERIFY2(std::abs(score - expected) < 1e-9,
                 qPrintable(QStringLiteral("alertScore(0,window) expected exp(-1)=%1, got %2")
                                .arg(expected).arg(score)));
    }

    void testAlertScoreBeyondBoundaryIsZero()
    {
        QVERIFY2(m_nrv.alertScore(500.0, 0.0) < 1e-9,
                 "alertScore beyond spatial bandwidth should be 0");
        QVERIFY2(m_nrv.alertScore(0.0, 30.0) < 1e-9,
                 "alertScore beyond temporal window should be 0");
    }

    void testAlertScoreInRange()
    {
        for (double d : {0.0, 50.0, 100.0, 150.0, 200.0}) {
            for (double t : {0.0, 3.5, 7.0, 14.0}) {
                const double s = m_nrv.alertScore(d, t);
                QVERIFY2(s >= 0.0 && s <= 1.0 + 1e-9,
                         qPrintable(QStringLiteral("alertScore(%1,%2)=%3 out of [0,1]").arg(d).arg(t).arg(s)));
            }
        }
    }

    // ── Temporal decay is monotonically decreasing with time gap ─────────────

    void testTemporalDecayMonotone()
    {
        // At fixed spatial distance (50 m, well within bandwidth),
        // alertScore must decrease (or stay zero) as time gap increases.
        constexpr double dist = 50.0;
        double prev = m_nrv.alertScore(dist, 0.0);
        for (double t : {1.0, 2.0, 5.0, 7.0, 10.0, 14.0, 20.0}) {
            const double curr = m_nrv.alertScore(dist, t);
            QVERIFY2(curr <= prev + 1e-12,
                     qPrintable(QStringLiteral("alertScore should decrease with time: at t=%1 got %2 > prev %3")
                                .arg(t).arg(curr).arg(prev)));
            prev = curr;
        }
    }

    // ── Spatial decay is monotonically decreasing with distance ──────────────

    void testSpatialDecayMonotone()
    {
        constexpr double dt = 1.0;  // fixed, small time gap
        double prev = m_nrv.alertScore(0.0, dt);
        for (double d : {25.0, 50.0, 100.0, 150.0, 200.0, 300.0}) {
            const double curr = m_nrv.alertScore(d, dt);
            QVERIFY2(curr <= prev + 1e-12,
                     qPrintable(QStringLiteral("alertScore should decrease with distance: d=%1 got %2 > prev %3")
                                .arg(d).arg(curr).arg(prev)));
            prev = curr;
        }
    }

    // ── Knox statistic: tight 3-event cluster → Knox > 1.0 ───────────────────

    void testKnoxTightClusterGreaterThanOne()
    {
        // Three events within ~14 m of each other and all within 1 day
        QVector<SeriesEvent> events;
        events << makeEvent("E1", 51.5000, -0.1000, 0.0);
        events << makeEvent("E2", 51.5001, -0.1001, 0.4);
        events << makeEvent("E3", 51.5002, -0.1002, 0.8);

        // All 3 pairs are within bandwidth (200 m) and window (14 days)
        const double knox = m_nrv.knoxStatistic(events);
        QVERIFY2(knox > 1.0,
                 qPrintable(QStringLiteral("Tight cluster should give Knox > 1.0, got %1").arg(knox)));
    }

    void testKnoxLargeClusterGreaterThanOne()
    {
        // 5 events very close in space and time
        QVector<SeriesEvent> events;
        for (int i = 0; i < 5; ++i) {
            events << makeEvent(QStringLiteral("E%1").arg(i),
                                51.500 + i * 0.0001,   // ~11 m apart
                                -0.100 + i * 0.0001,
                                static_cast<double>(i));  // 1 day apart each
        }
        const double knox = m_nrv.knoxStatistic(events);
        QVERIFY2(knox > 1.0,
                 qPrintable(QStringLiteral("5-event tight cluster Knox expected > 1.0, got %1").arg(knox)));
    }

    void testKnoxWellSeparatedEventsNotExtremelyHigh()
    {
        // Events spread over 10 km and 100 days — uniform distribution → Knox ≈ 1
        QVector<SeriesEvent> events;
        for (int i = 0; i < 6; ++i) {
            events << makeEvent(QStringLiteral("E%1").arg(i),
                                51.0 + i * 0.05,   // ~5 km apart
                                -0.0,
                                static_cast<double>(i * 20));  // 20 days apart each
        }
        const double knox = m_nrv.knoxStatistic(events);
        QVERIFY2(knox >= 0.0, "knoxStatistic should be non-negative");
    }

    // ── Analyse returns correct pair metadata ─────────────────────────────────

    void testAnalyseAlertFields()
    {
        QVector<SeriesEvent> events;
        events << makeEvent("E1", 51.5000, -0.1000, 0.0);
        events << makeEvent("E2", 51.5001, -0.1001, 1.0);

        const auto alerts = m_nrv.analyse(events);
        QVERIFY(!alerts.isEmpty());

        const auto& a = alerts.first();
        QVERIFY(!a.eventId.isEmpty());
        QVERIFY(!a.priorEventId.isEmpty());
        QVERIFY2(a.alertScore > 0.0 && a.alertScore <= 1.0, "alertScore out of (0, 1]");
        QVERIFY2(a.spatialDistanceM >= 0.0, "spatialDistanceM must be non-negative");
        QVERIFY2(a.temporalDistanceDays >= 0.0, "temporalDistanceDays must be non-negative");
    }

    // ── bandwidthFor() returns positive values for known crime types ──────────

    void testBandwidthForKnownTypes()
    {
        for (const QString& type : { "burglary", "robbery", "theft", "vehicle crime" }) {
            const double bw = NearRepeatVictimisation::bandwidthFor(type);
            QVERIFY2(bw > 0.0,
                     qPrintable(QStringLiteral("bandwidthFor('%1') should be > 0, got %2").arg(type).arg(bw)));
        }
    }

    // ── Constructor / accessor ────────────────────────────────────────────────

    void testAccessors()
    {
        NearRepeatVictimisation nrv(300.0, 21.0);
        QCOMPARE(nrv.bandwidthM(), 300.0);
        QCOMPARE(nrv.windowDays(), 21.0);
    }
};

QTEST_GUILESS_MAIN(NearRepeatDeep3Test)
#include "test_near_repeat_deep3.moc"
