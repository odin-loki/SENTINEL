// test_hawkes_ensemble_deep.cpp â€” iteration-5 deep validation for HawkesProcess,
// EnsemblePredictor, and RiskForecaster.
#include <QTest>
#include <QDateTime>
#include <QTimeZone>
#include <QUuid>
#include <cmath>
#include <algorithm>

#include "models/HawkesProcess.h"
#include "models/EnsemblePredictor.h"
#include "models/PoissonBaseline.h"
#include "models/RiskForecaster.h"
#include "core/CrimeEvent.h"

class HawkesEnsembleDeepTest : public QObject
{
    Q_OBJECT

private:
    static SpatiotemporalEvent spatEvent(double tDays, double lat = 51.5, double lon = -0.1)
    {
        SpatiotemporalEvent e;
        e.tDays     = tDays;
        e.lat       = lat;
        e.lon       = lon;
        e.crimeType = QStringLiteral("burglary");
        return e;
    }

    static QDateTime baseDt()
    {
        return QDateTime(QDate(2024, 6, 15), QTime(12, 0, 0), QTimeZone::utc());
    }

    static double baseTDays()
    {
        return static_cast<double>(QDateTime::fromSecsSinceEpoch(0, QTimeZone::utc())
                                       .daysTo(baseDt()));
    }

    static CrimeEvent crimeEvent(const QString& zone, const QDate& date,
                                 const QString& type = QStringLiteral("burglary"))
    {
        CrimeEvent ev;
        ev.eventId   = QUuid::createUuid().toString(QUuid::WithoutBraces);
        ev.id        = ev.eventId;
        ev.suburb    = zone;
        ev.crimeType = type;
        ev.latitude  = 51.5;
        ev.longitude = -0.1;
        ev.lat       = 51.5;
        ev.lon       = -0.1;
        const QDateTime dt(date, QTime(12, 0, 0), QTimeZone::utc());
        ev.timestamp  = dt;
        ev.occurredAt = dt;
        return ev;
    }

    static QVector<PoissonBaseline::EventRecord> poissonRecords(const QString& zone, int n)
    {
        QVector<PoissonBaseline::EventRecord> recs;
        recs.reserve(n);
        const QDateTime base(QDate(2023, 1, 2), QTime(12, 0, 0), QTimeZone::utc());
        for (int i = 0; i < n; ++i) {
            PoissonBaseline::EventRecord r;
            r.zoneId     = zone;
            r.crimeType  = QStringLiteral("burglary");
            r.occurredAt = base.addDays(i * 7);
            recs.append(r);
        }
        return recs;
    }

    static PoissonBaseline fittedPoisson(const QString& zone, int n = 20)
    {
        PoissonBaseline p;
        p.fit(poissonRecords(zone, n));
        return p;
    }

    static HawkesProcess fittedHawkes(int n = 20)
    {
        HawkesProcess h;
        QVector<SpatiotemporalEvent> events;
        events.reserve(n);
        const double tb = baseTDays();
        for (int i = 0; i < n; ++i)
            events.append(spatEvent(tb - n + i + 1.0));
        h.fit(events, 8);
        return h;
    }

    static QVector<QPair<double, double>> perfectCalibrationData(int n = 120)
    {
        QVector<QPair<double, double>> data;
        data.reserve(n);
        for (int i = 0; i < n; ++i) {
            const double p = static_cast<double>(i) / static_cast<double>(n - 1);
            data.append({p, p});
        }
        return data;
    }

private slots:

    // â”€â”€ HawkesProcess â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

    void testHawkesNLLWithSingleEvent()
    {
        HawkesProcess hp;
        QVERIFY(!hp.isFitted());

        const QVector<SpatiotemporalEvent> events = { spatEvent(5.0) };
        QVERIFY(hp.fit(events, 5));
        QVERIFY(hp.isFitted());
        QVERIFY2(hp.params().mu > 0.0,
                 qPrintable(QStringLiteral("Single-event fit mu=%1 must be > 0")
                                .arg(hp.params().mu)));
    }

    void testHawkesIntensityAtLeastMu()
    {
        auto hp = fittedHawkes(30);
        QVERIFY(hp.isFitted());
        const double mu = hp.params().mu;

        const QList<double> times = {0.0, 1.0, 5.0, 15.0, 50.0};
        for (double t : times) {
            const double lam = hp.intensity(t, 51.5, -0.1);
            QVERIFY2(lam >= mu - 1e-9,
                     qPrintable(QStringLiteral("intensity(%1)=%2 < mu=%3")
                                    .arg(t).arg(lam).arg(mu)));
        }
    }

    void testHawkesPredictProbInRange()
    {
        HawkesProcess hawkes = fittedHawkes(15);
        EnsemblePredictor ep;
        ep.setHawkes(&hawkes);

        const auto pred = ep.predict(QStringLiteral("zone"), baseDt(),
                                     QStringLiteral("burglary"), 51.5, -0.1);
        QVERIFY2(pred.probCrime >= 0.0 && pred.probCrime <= 1.0,
                 qPrintable(QStringLiteral("Hawkes predict probCrime=%1 out of [0,1]")
                                .arg(pred.probCrime)));
    }

    void testHawkesHighAlphaSelfExcitation()
    {
        HawkesParams p;
        p.mu    = 0.05;
        p.alpha = 0.95;
        p.beta  = 3.0;
        p.sigma = 0.05;

        HawkesProcess hp;
        hp.setParams(p);
        hp.setHistory({ spatEvent(10.0, 51.5, -0.1) });

        const double before = hp.intensity(9.9, 51.5, -0.1);
        const double after  = hp.intensity(10.01, 51.5, -0.1);

        QVERIFY2(after > before * 2.0,
                 qPrintable(QStringLiteral("High-alpha spike: before=%1 after=%2")
                                .arg(before).arg(after)));
        QVERIFY2(after > p.mu,
                 qPrintable(QStringLiteral("Post-event intensity %1 should exceed mu %2")
                                .arg(after).arg(p.mu)));
    }

    void testHawkesTemporalDecayVerified()
    {
        HawkesParams p;
        p.mu    = 0.08;
        p.alpha = 0.7;
        p.beta  = 2.0;
        p.sigma = 0.04;

        HawkesProcess hp;
        hp.setParams(p);
        hp.setHistory({ spatEvent(20.0, 51.5, -0.1) });

        const double near = hp.intensity(20.1, 51.5, -0.1);
        const double far  = hp.intensity(30.0, 51.5, -0.1);

        QVERIFY2(near > far,
                 qPrintable(QStringLiteral("Decay: t+0.1d=%1 should exceed t+10d=%2")
                                .arg(near).arg(far)));
    }

    void testHawkesFitConvergence()
    {
        QVector<SpatiotemporalEvent> events;
        events.reserve(50);
        for (int i = 0; i < 50; ++i)
            events.append(spatEvent(static_cast<double>(i) * 0.5));

        HawkesProcess hp;
        QVERIFY(hp.fit(events, 10));
        QVERIFY(hp.isFitted());
        QVERIFY(std::isfinite(hp.params().logLik));
        QVERIFY(hp.params().mu > 0.0);
    }

    void testHawkesIdenticalTimestamps()
    {
        QVector<SpatiotemporalEvent> events;
        for (int i = 0; i < 5; ++i)
            events.append(spatEvent(3.0, 51.5 + i * 0.001, -0.1 + i * 0.001));

        HawkesProcess hp;
        QVERIFY(hp.fit(events, 5));
        QVERIFY(hp.isFitted());

        const double lam = hp.intensity(3.5, 51.5, -0.1);
        QVERIFY(std::isfinite(lam));
        QVERIFY(lam > 0.0);
    }

    // â”€â”€ EnsemblePredictor â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

    void testEnsemblePoissonOnlyDominant()
    {
        PoissonBaseline poisson = fittedPoisson(QStringLiteral("POnly"), 15);
        EnsemblePredictor ep;
        ep.setPoisson(&poisson);

        const auto pred = ep.predict(QStringLiteral("POnly"), baseDt(),
                                     QStringLiteral("burglary"), 51.5, -0.1);
        QCOMPARE(pred.dominantModel, QStringLiteral("poisson"));
    }

    void testEnsembleHawkesOnlyDominant()
    {
        HawkesProcess hawkes = fittedHawkes(15);
        EnsemblePredictor ep;
        ep.setHawkes(&hawkes);

        const auto pred = ep.predict(QStringLiteral("HOnly"), baseDt(),
                                     QStringLiteral("burglary"), 51.5, -0.1);
        QCOMPARE(pred.dominantModel, QStringLiteral("hawkes"));
    }

    void testEnsembleCalibrationIdentity()
    {
        EnsemblePredictor ep;
        ep.calibrate(perfectCalibrationData());

        const QList<double> probes = {0.0, 0.1, 0.25, 0.5, 0.75, 0.9, 1.0};
        for (double raw : probes) {
            const double cal = ep.applyCalibration(raw);
            QVERIFY2(std::abs(cal - raw) < 0.08,
                     qPrintable(QStringLiteral("Identity cal: raw=%1 cal=%2")
                                    .arg(raw).arg(cal)));
        }
    }

    void testEnsembleCIConsistent()
    {
        PoissonBaseline poisson = fittedPoisson(QStringLiteral("CI"), 20);
        HawkesProcess   hawkes  = fittedHawkes(20);

        EnsemblePredictor ep;
        ep.setPoisson(&poisson);
        ep.setHawkes(&hawkes);
        ep.setWeights(0.5, 0.5);

        const auto pred = ep.predict(QStringLiteral("CI"), baseDt(),
                                     QStringLiteral("burglary"), 51.5, -0.1);
        QVERIFY2(pred.ciLow95 <= pred.probCrime + 1e-9,
                 qPrintable(QStringLiteral("ciLow95=%1 > probCrime=%2")
                                .arg(pred.ciLow95).arg(pred.probCrime)));
        QVERIFY2(pred.ciHigh95 >= pred.probCrime - 1e-9,
                 qPrintable(QStringLiteral("ciHigh95=%1 < probCrime=%2")
                                .arg(pred.ciHigh95).arg(pred.probCrime)));
    }

    void testEnsembleUncertaintyNonNegative()
    {
        PoissonBaseline poisson = fittedPoisson(QStringLiteral("Unc"), 15);
        HawkesProcess   hawkes  = fittedHawkes(15);

        EnsemblePredictor ep;
        ep.setPoisson(&poisson);
        ep.setHawkes(&hawkes);

        const auto pred = ep.predict(QStringLiteral("Unc"), baseDt(),
                                     QStringLiteral("burglary"), 51.5, -0.1);
        QVERIFY2(pred.uncertaintyAleatoric >= 0.0,
                 qPrintable(QStringLiteral("aleatoric=%1").arg(pred.uncertaintyAleatoric)));
        QVERIFY2(pred.uncertaintyEpistemic >= 0.0,
                 qPrintable(QStringLiteral("epistemic=%1").arg(pred.uncertaintyEpistemic)));
    }

    void testEnsembleCalibrationEdge()
    {
        EnsemblePredictor ep;
        ep.calibrate(perfectCalibrationData());

        QCOMPARE(ep.applyCalibration(0.0), 0.0);
        QCOMPARE(ep.applyCalibration(1.0), 1.0);
    }

    // â”€â”€ RiskForecaster â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

    void testRiskForecasterGenerates7Days()
    {
        RiskForecaster rf(7);
        QVector<CrimeEvent> events;
        for (int i = 0; i < 20; ++i)
            events.append(crimeEvent(QStringLiteral("SevenZone"), QDate(2024, 3, 1).addDays(i)));
        rf.fit(events);

        const QDateTime from(QDate(2024, 4, 1), QTime(0, 0, 0), QTimeZone::utc());
        const auto zf = rf.forecastZone(QStringLiteral("SevenZone"), from);
        QCOMPARE(zf.days.size(), 7);
    }

    void testRiskAlertLevelMonotone()
    {
        const QDateTime base(QDate(2024, 1, 1), QTime(0, 0, 0), QTimeZone::utc());

        QVector<CrimeEvent> lowEvents;
        for (int i = 0; i < 5; ++i)
            lowEvents.append(crimeEvent(QStringLiteral("LowZone"), base.date().addDays(i * 30)));

        QVector<CrimeEvent> highEvents;
        for (int i = 0; i < 60; ++i)
            highEvents.append(crimeEvent(QStringLiteral("HighZone"), base.date().addDays(i)));

        RiskForecaster rfLow(7);
        rfLow.setAlertThresholds(0.3, 0.5, 0.75);
        rfLow.fit(lowEvents);

        RiskForecaster rfHigh(7);
        rfHigh.setAlertThresholds(0.3, 0.5, 0.75);
        rfHigh.fit(highEvents);

        const auto zfLow  = rfLow.forecastZone(QStringLiteral("LowZone"), base.addDays(200));
        const auto zfHigh = rfHigh.forecastZone(QStringLiteral("HighZone"), base.addDays(70));

        QVERIFY2(zfHigh.weeklyRisk >= zfLow.weeklyRisk - 1e-9,
                 qPrintable(QStringLiteral("High weeklyRisk=%1 should be >= low=%2")
                                .arg(zfHigh.weeklyRisk).arg(zfLow.weeklyRisk)));
        QVERIFY2(zfHigh.alertLevel >= zfLow.alertLevel,
                 qPrintable(QStringLiteral("High alertLevel=%1 should be >= low=%2")
                                .arg(zfHigh.alertLevel).arg(zfLow.alertLevel)));
    }

    void testRiskEmptyZone()
    {
        RiskForecaster rf(7);
        QVector<CrimeEvent> events;
        for (int i = 0; i < 10; ++i)
            events.append(crimeEvent(QStringLiteral("KnownZone"), QDate(2024, 3, 1).addDays(i)));
        rf.fit(events);

        const QDateTime from(QDate(2024, 4, 1), QTime(0, 0, 0), QTimeZone::utc());
        const auto zf = rf.forecastZone(QStringLiteral("EmptyZone"), from);

        QCOMPARE(zf.days.size(), 7);
        QCOMPARE(zf.alertLevel, 0);
        QCOMPARE(zf.alertLabel(), QStringLiteral("NORMAL"));
    }

    void testRiskWeeklyAggregation()
    {
        RiskForecaster rf(7);
        QVector<CrimeEvent> events;
        for (int i = 0; i < 25; ++i)
            events.append(crimeEvent(QStringLiteral("AggZone"), QDate(2024, 1, 15).addDays(i)));
        rf.fit(events);

        const QDateTime from(QDate(2024, 2, 15), QTime(0, 0, 0), QTimeZone::utc());
        const auto zf = rf.forecastZone(QStringLiteral("AggZone"), from);

        double sum = 0.0;
        for (const auto& day : zf.days)
            sum += day.riskScore;
        const double mean = sum / zf.days.size();

        QVERIFY2(std::abs(zf.weeklyRisk - mean) < 1e-9,
                 qPrintable(QStringLiteral("weeklyRisk=%1 mean=%2")
                                .arg(zf.weeklyRisk).arg(mean)));
    }

    void testRiskEscalationAboveOneForSpike()
    {
        const QDateTime now(QDate(2024, 4, 1), QTime(0, 0, 0), QTimeZone::utc());
        const QDateTime histStart(QDate(2024, 1, 1), QTime(0, 0, 0), QTimeZone::utc());

        QVector<CrimeEvent> events;
        for (int i = 0; i < 40; ++i)
            events.append(crimeEvent(QStringLiteral("SpikeZone"), histStart.date().addDays(i)));
        for (int i = 0; i < 6; ++i)
            events.append(crimeEvent(QStringLiteral("SpikeZone"), now.date().addDays(-i)));

        RiskForecaster rf(7);
        rf.fit(events);

        const auto zf = rf.forecastZone(QStringLiteral("SpikeZone"), now);
        QVERIFY(!zf.days.isEmpty());
        QVERIFY2(zf.days.first().escalationFactor > 1.0,
                 qPrintable(QStringLiteral("escalationFactor=%1 expected > 1.0")
                                .arg(zf.days.first().escalationFactor)));
    }

    void testRiskTemporalFactorPositive()
    {
        RiskForecaster rf(7);
        QVector<CrimeEvent> events;
        for (int i = 0; i < 15; ++i)
            events.append(crimeEvent(QStringLiteral("TempZone"), QDate(2024, 2, 1).addDays(i)));
        rf.fit(events);

        const QDateTime from(QDate(2024, 3, 1), QTime(0, 0, 0), QTimeZone::utc());
        const auto zf = rf.forecastZone(QStringLiteral("TempZone"), from);

        for (const auto& day : zf.days) {
            QVERIFY2(day.temporalFactor > 0.0,
                     qPrintable(QStringLiteral("temporalFactor=%1 must be > 0")
                                    .arg(day.temporalFactor)));
        }
    }

    void testRiskForecastHorizonConfigurable()
    {
        const int horizon = 14;
        RiskForecaster rf(horizon);
        QVector<CrimeEvent> events;
        for (int i = 0; i < 20; ++i)
            events.append(crimeEvent(QStringLiteral("HorizonZone"), QDate(2024, 2, 1).addDays(i)));
        rf.fit(events);

        const QDateTime from(QDate(2024, 3, 1), QTime(0, 0, 0), QTimeZone::utc());
        const auto zf = rf.forecastZone(QStringLiteral("HorizonZone"), from);
        QCOMPARE(zf.days.size(), horizon);
    }
};

QTEST_MAIN(HawkesEnsembleDeepTest)
#include "test_hawkes_ensemble_deep.moc"

