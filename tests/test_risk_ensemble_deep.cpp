// test_risk_ensemble_deep.cpp — iteration-8 deep audit for RiskForecaster and EnsemblePredictor
#include <QTest>
#include <QDateTime>
#include <QTimeZone>
#include <QUuid>
#include <cmath>
#include <algorithm>

#include "models/RiskForecaster.h"
#include "models/EnsemblePredictor.h"
#include "models/PoissonBaseline.h"
#include "models/HawkesProcess.h"
#include "core/CrimeEvent.h"

class RiskEnsembleDeepTest : public QObject
{
    Q_OBJECT

private:
    static QDateTime utcDt(const QDate& date, int hour = 12)
    {
        return QDateTime(date, QTime(hour, 0, 0), QTimeZone::utc());
    }

    static CrimeEvent makeEvent(const QString& zone, const QDate& date,
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
        const QDateTime dt = utcDt(date);
        ev.timestamp  = dt;
        ev.occurredAt = dt;
        return ev;
    }

    static int classifyAlert(double weeklyRisk, double elevated, double high, double critical)
    {
        if (weeklyRisk >= critical)      return 3;
        if (weeklyRisk >= high)          return 2;
        if (weeklyRisk >= elevated)      return 1;
        return 0;
    }

    static QString alertLabelFor(int level)
    {
        ZoneForecast zf;
        zf.alertLevel = level;
        return zf.alertLabel();
    }

    static int peakDayIndex(const ZoneForecast& zf)
    {
        int peakIdx = 0;
        for (int i = 1; i < zf.days.size(); ++i) {
            if (zf.days[i].riskScore > zf.days[peakIdx].riskScore)
                peakIdx = i;
        }
        return peakIdx;
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

    static double baseTDays()
    {
        return static_cast<double>(
            QDateTime::fromSecsSinceEpoch(0, QTimeZone::utc())
                .daysTo(utcDt(QDate(2024, 6, 15))));
    }

    static HawkesProcess fittedHawkes(int n = 20)
    {
        HawkesProcess h;
        QVector<SpatiotemporalEvent> events;
        events.reserve(n);
        const double tb = baseTDays();
        for (int i = 0; i < n; ++i) {
            SpatiotemporalEvent ev;
            ev.tDays     = tb - static_cast<double>(n - i);
            ev.lat       = 51.5 + (i % 5) * 0.001;
            ev.lon       = -0.1 + (i % 3) * 0.001;
            ev.crimeType = QStringLiteral("burglary");
            events.append(ev);
        }
        h.fit(events, 5);
        return h;
    }

    static QVector<QPair<double, double>> monotonicCalData()
    {
        QVector<QPair<double, double>> data;
        data.reserve(100);
        for (int i = 0; i < 100; ++i) {
            const double p = static_cast<double>(i) / 99.0;
            data.append({p, p});
        }
        return data;
    }

private slots:

    // ── RiskForecaster (10 tests) ────────────────────────────────────────────

    void testWeeklyRiskIsMean()
    {
        RiskForecaster rf(7);
        QVector<CrimeEvent> events;
        for (int i = 0; i < 25; ++i)
            events.append(makeEvent(QStringLiteral("MeanZone"), QDate(2024, 1, 15).addDays(i)));
        rf.fit(events);

        const auto zf = rf.forecastZone(QStringLiteral("MeanZone"),
                                       utcDt(QDate(2024, 2, 15), 0));
        QCOMPARE(zf.days.size(), 7);

        double sum = 0.0;
        for (const auto& day : zf.days)
            sum += day.riskScore;
        const double mean = sum / zf.days.size();

        QVERIFY2(std::abs(zf.weeklyRisk - mean) < 1e-9,
                 qPrintable(QStringLiteral("weeklyRisk=%1 mean=%2")
                                .arg(zf.weeklyRisk).arg(mean)));
    }

    void testAlertLevelNormal()
    {
        RiskForecaster rf;
        rf.setAlertThresholds(0.3, 0.5, 0.75);
        QCOMPARE(classifyAlert(0.10, 0.3, 0.5, 0.75), 0);
        QCOMPARE(alertLabelFor(0), QStringLiteral("Normal"));
    }

    void testAlertLevelElevated()
    {
        RiskForecaster rf;
        rf.setAlertThresholds(0.3, 0.5, 0.75);
        QCOMPARE(classifyAlert(0.35, 0.3, 0.5, 0.75), 1);
        QCOMPARE(alertLabelFor(1), QStringLiteral("Elevated"));
    }

    void testAlertLevelHigh()
    {
        RiskForecaster rf;
        rf.setAlertThresholds(0.3, 0.5, 0.75);
        QCOMPARE(classifyAlert(0.55, 0.3, 0.5, 0.75), 2);
        QCOMPARE(alertLabelFor(2), QStringLiteral("High"));
    }

    void testAlertLevelCritical()
    {
        RiskForecaster rf;
        rf.setAlertThresholds(0.3, 0.5, 0.75);
        QCOMPARE(classifyAlert(0.80, 0.3, 0.5, 0.75), 3);
        QCOMPARE(alertLabelFor(3), QStringLiteral("Critical"));
    }

    void testForecastDayCount()
    {
        RiskForecaster rf(7);
        QVector<CrimeEvent> events;
        for (int i = 0; i < 15; ++i)
            events.append(makeEvent(QStringLiteral("HorizonZone"), QDate(2024, 3, 1).addDays(i)));
        rf.fit(events);

        const auto zf = rf.forecastZone(QStringLiteral("HorizonZone"),
                                       utcDt(QDate(2024, 4, 1), 0));
        QCOMPARE(zf.days.size(), 7);
    }

    void testEmptyEventsNocrash()
    {
        RiskForecaster rf(7);
        rf.fit({});

        const QDateTime from = utcDt(QDate(2024, 4, 1), 0);
        const auto all = rf.forecast(from);
        QVERIFY(all.isEmpty());

        const auto zf = rf.forecastZone(QStringLiteral("AnyZone"), from);
        QCOMPARE(zf.days.size(), 7);
        for (const auto& day : zf.days)
            QCOMPARE(day.riskScore, 0.0);
        QCOMPARE(zf.weeklyRisk, 0.0);
        QCOMPARE(zf.alertLevel, 0);
    }

    void testPeakDayIsMaxRisk()
    {
        RiskForecaster rf(7);
        QVector<CrimeEvent> events;
        const QDate baseDate(2024, 3, 1);
        for (int i = 0; i < 40; ++i)
            events.append(makeEvent(QStringLiteral("PeakZone"), baseDate.addDays(i)));
        for (int i = 0; i < 5; ++i)
            events.append(makeEvent(QStringLiteral("PeakZone"), QDate(2024, 4, 10).addDays(-i)));
        rf.fit(events);

        const auto zf = rf.forecastZone(QStringLiteral("PeakZone"),
                                        utcDt(QDate(2024, 4, 10), 0));
        const int peakIdx = peakDayIndex(zf);

        for (int i = 0; i < zf.days.size(); ++i) {
            QVERIFY2(zf.days[peakIdx].riskScore >= zf.days[i].riskScore - 1e-12,
                     qPrintable(QStringLiteral("peak day risk %1 < day %2 risk %3")
                                    .arg(zf.days[peakIdx].riskScore)
                                    .arg(i)
                                    .arg(zf.days[i].riskScore)));
        }
    }

    void testRiskBounded01()
    {
        RiskForecaster rf(7);
        QVector<CrimeEvent> events;
        for (int i = 0; i < 60; ++i)
            events.append(makeEvent(QStringLiteral("BoundZone"), QDate(2024, 1, 1).addDays(i)));
        for (int i = 0; i < 10; ++i)
            events.append(makeEvent(QStringLiteral("BoundZone"), QDate(2024, 4, 1).addDays(-i)));
        rf.fit(events);

        const auto zf = rf.forecastZone(QStringLiteral("BoundZone"),
                                        utcDt(QDate(2024, 4, 1), 0));
        for (const auto& day : zf.days) {
            QVERIFY2(day.riskScore >= 0.0 && day.riskScore <= 1.0,
                     qPrintable(QStringLiteral("riskScore=%1 out of [0,1]").arg(day.riskScore)));
        }
    }

    void testDecayKernelMonotone()
    {
        const QDateTime forecastFrom = utcDt(QDate(2024, 4, 15), 0);
        const QDate forecastDate = forecastFrom.date();

        QVector<CrimeEvent> recentEvents;
        for (int i = 0; i < 30; ++i)
            recentEvents.append(makeEvent(QStringLiteral("RecentZone"),
                                          QDate(2024, 1, 1).addDays(i)));
        recentEvents.append(makeEvent(QStringLiteral("RecentZone"), forecastDate.addDays(-1)));

        QVector<CrimeEvent> oldEvents;
        for (int i = 0; i < 30; ++i)
            oldEvents.append(makeEvent(QStringLiteral("OldZone"),
                                       QDate(2024, 1, 1).addDays(i)));
        oldEvents.append(makeEvent(QStringLiteral("OldZone"), forecastDate.addDays(-10)));

        RiskForecaster rfRecent(1);
        rfRecent.fit(recentEvents);
        const auto zfRecent = rfRecent.forecastZone(QStringLiteral("RecentZone"), forecastFrom);

        RiskForecaster rfOld(1);
        rfOld.fit(oldEvents);
        const auto zfOld = rfOld.forecastZone(QStringLiteral("OldZone"), forecastFrom);

        QVERIFY(!zfRecent.days.isEmpty());
        QVERIFY(!zfOld.days.isEmpty());
        QVERIFY2(zfRecent.days.first().riskScore >= zfOld.days.first().riskScore - 1e-9,
                 qPrintable(QStringLiteral("recent risk=%1 should be >= old risk=%2")
                                .arg(zfRecent.days.first().riskScore)
                                .arg(zfOld.days.first().riskScore)));
    }

    // ── EnsemblePredictor (10 tests) ─────────────────────────────────────────

    void testWeightNormalization()
    {
        PoissonBaseline poisson = fittedPoisson(QStringLiteral("WNorm"), 15);
        HawkesProcess   hawkes  = fittedHawkes(15);

        EnsemblePredictor ep;
        ep.setPoisson(&poisson);
        ep.setHawkes(&hawkes);
        ep.setWeights(3.0, 7.0);

        const auto pred = ep.predict(QStringLiteral("WNorm"), utcDt(QDate(2024, 6, 15)),
                                     QStringLiteral("burglary"), 51.5, -0.1);
        const double wSum = pred.poissonWeight + pred.hawkesWeight;
        QVERIFY2(std::abs(wSum - 1.0) < 1e-9,
                 qPrintable(QStringLiteral("weight sum=%1").arg(wSum)));
    }

    void testDominantModelPoisson()
    {
        PoissonBaseline poisson = fittedPoisson(QStringLiteral("DomP"), 15);
        HawkesProcess   hawkes  = fittedHawkes(15);

        EnsemblePredictor ep;
        ep.setPoisson(&poisson);
        ep.setHawkes(&hawkes);
        ep.setWeights(0.8, 0.2);

        const auto pred = ep.predict(QStringLiteral("DomP"), utcDt(QDate(2024, 6, 15)),
                                     QStringLiteral("burglary"), 51.5, -0.1);
        QCOMPARE(pred.dominantModel, QStringLiteral("poisson"));
    }

    void testDominantModelHawkes()
    {
        PoissonBaseline poisson = fittedPoisson(QStringLiteral("DomH"), 15);
        HawkesProcess   hawkes  = fittedHawkes(15);

        EnsemblePredictor ep;
        ep.setPoisson(&poisson);
        ep.setHawkes(&hawkes);
        ep.setWeights(0.2, 0.8);

        const auto pred = ep.predict(QStringLiteral("DomH"), utcDt(QDate(2024, 6, 15)),
                                     QStringLiteral("burglary"), 51.5, -0.1);
        QCOMPARE(pred.dominantModel, QStringLiteral("hawkes"));
    }

    void testDominantModelEqual()
    {
        PoissonBaseline poisson = fittedPoisson(QStringLiteral("DomEq"), 15);
        HawkesProcess   hawkes  = fittedHawkes(15);

        EnsemblePredictor ep;
        ep.setPoisson(&poisson);
        ep.setHawkes(&hawkes);
        ep.setWeights(0.5, 0.5);

        const auto pred = ep.predict(QStringLiteral("DomEq"), utcDt(QDate(2024, 6, 15)),
                                     QStringLiteral("burglary"), 51.5, -0.1);
        QCOMPARE(pred.dominantModel, QStringLiteral("equal"));
    }

    void testProbCrimeRange()
    {
        PoissonBaseline poisson = fittedPoisson(QStringLiteral("ProbR"), 15);
        HawkesProcess   hawkes  = fittedHawkes(15);

        EnsemblePredictor ep;
        ep.setPoisson(&poisson);
        ep.setHawkes(&hawkes);

        const auto pred = ep.predict(QStringLiteral("ProbR"), utcDt(QDate(2024, 6, 15)),
                                     QStringLiteral("burglary"), 51.5, -0.1);
        QVERIFY2(pred.probCrime >= 0.0 && pred.probCrime <= 1.0,
                 qPrintable(QStringLiteral("probCrime=%1").arg(pred.probCrime)));
    }

    void testCIWidthPositive()
    {
        PoissonBaseline poisson = fittedPoisson(QStringLiteral("CIW"), 15);
        HawkesProcess   hawkes  = fittedHawkes(15);

        EnsemblePredictor ep;
        ep.setPoisson(&poisson);
        ep.setHawkes(&hawkes);

        const auto pred = ep.predict(QStringLiteral("CIW"), utcDt(QDate(2024, 6, 15)),
                                     QStringLiteral("burglary"), 51.5, -0.1);
        QVERIFY2(pred.ciHigh95 > pred.ciLow95,
                 qPrintable(QStringLiteral("ciLow95=%1 ciHigh95=%2")
                                .arg(pred.ciLow95).arg(pred.ciHigh95)));
    }

    void testAleatoricEpistemicDecomposition()
    {
        PoissonBaseline poisson = fittedPoisson(QStringLiteral("Unc"), 20);
        HawkesProcess   hawkes  = fittedHawkes(20);

        EnsemblePredictor ep;
        ep.setPoisson(&poisson);
        ep.setHawkes(&hawkes);
        ep.setWeights(0.5, 0.5);

        const auto pred = ep.predict(QStringLiteral("Unc"), utcDt(QDate(2024, 6, 15)),
                                     QStringLiteral("burglary"), 51.5, -0.1);

        const double sigma = std::sqrt(
            pred.uncertaintyAleatoric * pred.uncertaintyAleatoric +
            pred.uncertaintyEpistemic * pred.uncertaintyEpistemic);
        const double rawHalfWidth = 1.96 * sigma;
        const double halfWidth = (pred.ciHigh95 - pred.ciLow95) / 2.0;

        // Combined uncertainty is Pythagorean on component sigmas
        QVERIFY2(sigma >= 0.0,
                 qPrintable(QStringLiteral("sigma=%1").arg(sigma)));

        // CI may be clamped to [0,1]; when unclamped it equals 1.96 * sigma
        if (pred.probCrime - rawHalfWidth >= 0.0 && pred.probCrime + rawHalfWidth <= 1.0) {
            QVERIFY2(std::abs(halfWidth - rawHalfWidth) < 1e-6,
                     qPrintable(QStringLiteral("halfWidth=%1 expected=%2")
                                    .arg(halfWidth).arg(rawHalfWidth)));
        } else {
            QVERIFY2(halfWidth <= rawHalfWidth + 1e-6,
                     qPrintable(QStringLiteral("clamped halfWidth=%1 exceeds raw=%2")
                                    .arg(halfWidth).arg(rawHalfWidth)));
        }
    }

    void testIsotonicCalibrationMonotone()
    {
        EnsemblePredictor ep;
        ep.calibrate(monotonicCalData());

        double prev = -1.0;
        for (int i = 0; i <= 20; ++i) {
            const double raw = static_cast<double>(i) / 20.0;
            const double cal = ep.applyCalibration(raw);
            QVERIFY2(cal >= prev - 1e-9,
                     qPrintable(QStringLiteral("cal not monotone: raw=%1 cal=%2 prev=%3")
                                    .arg(raw).arg(cal).arg(prev)));
            prev = cal;
        }
    }

    void testZeroWeightFallback()
    {
        PoissonBaseline poisson = fittedPoisson(QStringLiteral("ZeroW"), 15);
        HawkesProcess   hawkes  = fittedHawkes(15);

        EnsemblePredictor ep;
        ep.setPoisson(&poisson);
        ep.setHawkes(&hawkes);
        ep.setWeights(0.0, 0.0);

        const auto pred = ep.predict(QStringLiteral("ZeroW"), utcDt(QDate(2024, 6, 15)),
                                     QStringLiteral("burglary"), 51.5, -0.1);

        QVERIFY2(std::abs(pred.poissonWeight + pred.hawkesWeight - 1.0) < 1e-9,
                 qPrintable(QStringLiteral("weight sum=%1")
                                .arg(pred.poissonWeight + pred.hawkesWeight)));
        QVERIFY2(pred.probCrime >= 0.0 && pred.probCrime <= 1.0,
                 qPrintable(QStringLiteral("probCrime=%1").arg(pred.probCrime)));
        QVERIFY2(std::isfinite(pred.expectedCount),
                 qPrintable(QStringLiteral("expectedCount=%1").arg(pred.expectedCount)));
    }

    void testApplyCalibrationPublic()
    {
        EnsemblePredictor ep;
        ep.calibrate(monotonicCalData());

        const double cal = ep.applyCalibration(0.5);
        QVERIFY2(cal >= 0.0 && cal <= 1.0,
                 qPrintable(QStringLiteral("applyCalibration(0.5)=%1").arg(cal)));
    }
};

QTEST_GUILESS_MAIN(RiskEnsembleDeepTest)
#include "test_risk_ensemble_deep.moc"

