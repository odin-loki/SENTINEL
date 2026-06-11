// test_near_repeat_deep5.cpp — Deep audit iteration 17: NearRepeatVictimisation
// risk multiplier (via SeriesDetector calibration), distance decay, temporal window.

#include <QtTest>
#include <cmath>
#include "models/NearRepeatVictimisation.h"
#include "models/SeriesDetector.h"

class TestNearRepeatDeep5 : public QObject
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

    // ── Risk multiplier from published calibration table ─────────────────────

    void testRiskMultiplierBurglaryCalibrated()
    {
        const auto nr = SeriesDetector::nearRepeatFor(QStringLiteral("burglary"));
        QVERIFY2(nr.multiplier > 3.0,
                 qPrintable(QStringLiteral("burglary multiplier=%1 expected >3").arg(nr.multiplier)));
        QCOMPARE(nr.distM, 200.0);
        QCOMPARE(nr.days, 14.0);
    }

    void testRiskMultiplierRobberyUsesShorterWindow()
    {
        const auto robbery = SeriesDetector::nearRepeatFor(QStringLiteral("robbery"));
        const auto burglary = SeriesDetector::nearRepeatFor(QStringLiteral("burglary"));
        QVERIFY2(robbery.days < burglary.days,
                 "robbery temporal window should be shorter than burglary");
        QVERIFY2(robbery.multiplier > 2.0 && robbery.multiplier < 5.0,
                 qPrintable(QStringLiteral("robbery multiplier=%1 out of expected range")
                                .arg(robbery.multiplier)));
    }

    void testRiskMultiplierUnknownTypeFallsBackToDefault()
    {
        const auto unknown = SeriesDetector::nearRepeatFor(QStringLiteral("unknown_xyz"));
        QCOMPARE(unknown.multiplier, 3.0);
        QCOMPARE(unknown.distM, 300.0);
        QCOMPARE(unknown.days, 14.0);
    }

    void testLinkProbabilityScalesWithRiskMultiplier()
    {
        SeriesDetector det;
        CrimeSeries series;
        series.seriesId          = QStringLiteral("S1");
        series.dominantCrimeType = QStringLiteral("burglary");
        series.members << ev("M1", 51.5000, -0.1000, 0.0, QStringLiteral("burglary"));

        const SeriesEvent nearEv = ev("N1", 51.5001, -0.1001, 0.5, QStringLiteral("burglary"));
        const SeriesEvent farEv  = ev("F1", 51.5200, -0.1200, 50.0, QStringLiteral("burglary"));

        const SeriesMatch nearMatch = det.linkProbability(nearEv, series, 0.8);
        const SeriesMatch farMatch  = det.linkProbability(farEv, series, 0.8);

        const double nrMult = SeriesDetector::nearRepeatFor(QStringLiteral("burglary")).multiplier;
        QVERIFY2(nrMult > 1.0, "multiplier must amplify base rate");
        QVERIFY2(nearMatch.linkProbability > farMatch.linkProbability,
                 qPrintable(QStringLiteral("near event P=%1 should exceed far P=%2")
                                .arg(nearMatch.linkProbability).arg(farMatch.linkProbability)));
        QVERIFY2(nearMatch.linkProbability <= 0.95, "link probability capped at 0.95");
    }

    // ── Distance decay kernel ─────────────────────────────────────────────────

    void testDistanceDecayMonotoneWithinBandwidth()
    {
        NearRepeatVictimisation nrv(200.0, 14.0);
        double prev = nrv.alertScore(0.0, 1.0);
        for (double d : {25.0, 50.0, 100.0, 150.0, 200.0}) {
            const double curr = nrv.alertScore(d, 1.0);
            QVERIFY2(curr <= prev + 1e-12,
                     qPrintable(QStringLiteral("decay must decrease: d=%1 score=%2 prev=%3")
                                    .arg(d).arg(curr).arg(prev)));
            prev = curr;
        }
    }

    void testDistanceDecayZeroBeyondBandwidth()
    {
        NearRepeatVictimisation nrv(200.0, 14.0);
        QVERIFY2(nrv.alertScore(200.1, 0.0) < 1e-12, "just beyond bandwidth → 0");
        QVERIFY2(nrv.alertScore(500.0, 0.0) < 1e-12, "far beyond bandwidth → 0");
        const double atBw = nrv.alertScore(200.0, 0.0);
        const double expected = std::exp(-1.0);
        QVERIFY2(std::abs(atBw - expected) < 1e-9,
                 qPrintable(QStringLiteral("at bandwidth expected exp(-1)=%1 got %2")
                                .arg(expected).arg(atBw)));
    }

    // ── Temporal window kernel ────────────────────────────────────────────────

    void testTemporalWindowHardCutoff()
    {
        NearRepeatVictimisation nrv(200.0, 14.0);
        const double atWindow = nrv.alertScore(0.0, 14.0);
        const double expected = std::exp(-1.0);
        QVERIFY2(std::abs(atWindow - expected) < 1e-9,
                 qPrintable(QStringLiteral("at window edge expected exp(-1)=%1 got %2")
                                .arg(expected).arg(atWindow)));
        QVERIFY2(nrv.alertScore(0.0, 14.1) < 1e-12, "just beyond window → 0");
        QVERIFY2(nrv.alertScore(0.0, 30.0)  < 1e-12, "well beyond window → 0");
    }

    void testCrimeTypeUsesCalibratedTemporalWindow()
    {
        // robbery window = 7 days; burglary window = 14 days
        NearRepeatVictimisation nrv(400.0, 14.0); // defaults irrelevant when crimeType set
        const double dt = 10.0; // within burglary window, beyond robbery window
        const double burglaryScore = nrv.alertScore(50.0, dt, QStringLiteral("burglary"));
        const double robberyScore  = nrv.alertScore(50.0, dt, QStringLiteral("robbery"));
        QVERIFY2(burglaryScore > robberyScore,
                 qPrintable(QStringLiteral("dt=10: burglary=%1 should beat robbery=%2")
                                .arg(burglaryScore).arg(robberyScore)));
        QVERIFY2(robberyScore < 1e-12, "robbery 10-day gap exceeds 7-day window → 0");
    }
};

QTEST_GUILESS_MAIN(TestNearRepeatDeep5)
#include "test_near_repeat_deep5.moc"
