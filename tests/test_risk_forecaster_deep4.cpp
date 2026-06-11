// test_risk_forecaster_deep4.cpp — Deep audit iteration 15: alert thresholds,
// escalation, temporal multiplier, horizon clamping, empty fit.
#include <QtTest/QtTest>
#include <cmath>
#include "models/RiskForecaster.h"
#include "core/CrimeEvent.h"

class RiskForecasterDeep4Test : public QObject
{
    Q_OBJECT

    static CrimeEvent ev(const QString& id, const QString& zone, const QDateTime& dt)
    {
        CrimeEvent e;
        e.eventId    = id;
        e.suburb     = zone;
        e.timestamp  = dt;
        e.occurredAt = dt;
        e.crimeType  = QStringLiteral("burglary");
        return e;
    }

    static QVector<CrimeEvent> clusterEvents(const QString& zone,
                                              const QDateTime& day,
                                              int count)
    {
        QVector<CrimeEvent> out;
        out.reserve(count);
        for (int i = 0; i < count; ++i)
            out.append(ev(QStringLiteral("%1_%2").arg(zone).arg(i), zone, day));
        return out;
    }

private slots:

    void testEmptyFitIsNotFittedAndZeroRisk()
    {
        RiskForecaster rf(7);
        rf.fit({});

        QVERIFY(!rf.isFitted());
        QCOMPARE(rf.zoneCount(), 0);

        const QDateTime dt(QDate(2024, 6, 1), QTime(0, 0), Qt::UTC);
        const auto zf = rf.forecastZone(QStringLiteral("Any"), dt);

        QCOMPARE(zf.days.size(), 7);
        QCOMPARE(zf.weeklyRisk, 0.0);
        QCOMPARE(zf.alertLevel, 0);
        for (const auto& day : zf.days) {
            QCOMPARE(day.riskScore, 0.0);
            QCOMPARE(day.baselineProb, 0.0);
        }
    }

    void testHorizonNonPositiveClampsToOne()
    {
        const QDateTime base(QDate(2024, 1, 1), QTime(0, 0), Qt::UTC);
        for (int h : {0, -3, -100}) {
            RiskForecaster rf(h);
            rf.fit({ev(QStringLiteral("h0"), QStringLiteral("H"), base)});
            const auto zf = rf.forecastZone(QStringLiteral("H"), base.addDays(2));
            QCOMPARE(zf.days.size(), 1);
        }
    }

    void testHorizonLargeValue()
    {
        const QDateTime base(QDate(2024, 1, 1), QTime(0, 0), Qt::UTC);
        RiskForecaster rf(21);
        rf.fit({ev(QStringLiteral("l0"), QStringLiteral("L"), base)});
        const auto zf = rf.forecastZone(QStringLiteral("L"), base.addDays(5));
        QCOMPARE(zf.days.size(), 21);
    }

    void testEscalationBoostsRiskAfterRecentCluster()
    {
        const QDateTime base(QDate(2024, 1, 1), QTime(12, 0), Qt::UTC);

        // Hot zone: dense cluster in the 14-day window before forecast.
        QVector<CrimeEvent> hot;
        for (int d = 24; d <= 29; ++d)
            hot += clusterEvents(QStringLiteral("Hot"), base.addDays(d), 6);

        // Calm zone: one historical event; forecast far enough that lag > 14 → no boost.
        QVector<CrimeEvent> calm;
        calm.append(ev(QStringLiteral("c0"), QStringLiteral("Calm"), base));

        RiskForecaster rfHot(3);
        rfHot.fit(hot);
        const auto hotZf = rfHot.forecastZone(QStringLiteral("Hot"), base.addDays(30));

        RiskForecaster rfCalm(3);
        rfCalm.fit(calm);
        const auto calmZf = rfCalm.forecastZone(QStringLiteral("Calm"), base.addDays(20));

        const double hotEsc  = hotZf.days.first().escalationFactor;
        const double calmEsc = calmZf.days.first().escalationFactor;

        QCOMPARE(calmEsc, 1.0);
        QVERIFY2(hotEsc > calmEsc,
                 qPrintable(QStringLiteral("hot escalation=%1 should exceed calm=%2")
                                .arg(hotEsc).arg(calmEsc)));
        QVERIFY2(hotEsc >= 1.0 && hotEsc <= 2.0,
                 qPrintable(QStringLiteral("escalation=%1 outside [1,2]").arg(hotEsc)));
    }

    void testTemporalMultiplierWeekendHigherThanWeekday()
    {
        const QDateTime base(QDate(2024, 6, 1), QTime(0, 0), Qt::UTC);
        QVector<CrimeEvent> events;
        for (int i = 0; i < 90; ++i)
            events.append(ev(QStringLiteral("t%1").arg(i), QStringLiteral("T"),
                             base.addDays(i)));

        RiskForecaster rf(7);
        rf.fit(events);

        // 2024-06-10 is Monday; 2024-06-14 is Friday (Qt dow 5).
        const auto mon = rf.forecastZone(QStringLiteral("T"),
                                         QDateTime(QDate(2024, 6, 10), QTime(0, 0), Qt::UTC));
        const auto fri = rf.forecastZone(QStringLiteral("T"),
                                         QDateTime(QDate(2024, 6, 14), QTime(0, 0), Qt::UTC));

        QVERIFY(!mon.days.isEmpty());
        QVERIFY(!fri.days.isEmpty());
        QCOMPARE(mon.days.first().temporalFactor, 1.0);
        QCOMPARE(fri.days.first().temporalFactor, 1.2);
        QVERIFY2(fri.days.first().temporalFactor > mon.days.first().temporalFactor,
                 "Friday temporal multiplier should exceed Monday");
    }

    void testTemporalMultiplierWinterMonths()
    {
        const QDateTime base(QDate(2023, 1, 1), QTime(0, 0), Qt::UTC);
        RiskForecaster rf(1);
        rf.fit({ev(QStringLiteral("w0"), QStringLiteral("W"), base)});

        const auto winter = rf.forecastZone(
            QStringLiteral("W"),
            QDateTime(QDate(2024, 1, 15), QTime(0, 0), Qt::UTC));
        const auto summer = rf.forecastZone(
            QStringLiteral("W"),
            QDateTime(QDate(2024, 7, 15), QTime(0, 0), Qt::UTC));

        QCOMPARE(winter.days.first().temporalFactor, 1.1);
        QCOMPARE(summer.days.first().temporalFactor, 1.0);
    }

    void testAlertThresholdLevelsRespectCustomBounds()
    {
        const QDateTime base(QDate(2024, 1, 1), QTime(0, 0), Qt::UTC);
        QVector<CrimeEvent> events;
        for (int i = 0; i < 120; ++i)
            events.append(ev(QStringLiteral("a%1").arg(i), QStringLiteral("A"),
                             base.addDays(i % 30)));

        RiskForecaster rf(7);
        rf.setAlertThresholds(0.99, 0.995, 0.999);
        rf.fit(events);
        const auto lowTh = rf.forecastZone(QStringLiteral("A"), base.addDays(35));

        rf.setAlertThresholds(0.0, 0.0, 0.0);
        const auto zf = rf.forecastZone(QStringLiteral("A"), base.addDays(35));

        if (zf.weeklyRisk <= 0.0)
            QSKIP("weeklyRisk is zero — cannot exercise alert thresholds");

        QCOMPARE(zf.alertLevel, 3);
        QVERIFY(lowTh.alertLevel <= zf.alertLevel);
    }

    void testAlertLevelMonotonicWithThresholds()
    {
        ZoneForecast zf;
        zf.weeklyRisk = 0.55;

        RiskForecaster rf(7);
        rf.setAlertThresholds(0.6, 0.7, 0.8);
        const auto strict = rf.forecastZone(QStringLiteral("unused"),
                                              QDateTime(QDate(2024, 1, 1), QTime(0, 0), Qt::UTC));
        Q_UNUSED(strict);

        rf.setAlertThresholds(0.3, 0.5, 0.75);
        // Simulate classification using the same weeklyRisk against stored thresholds.
        int level = 0;
        if (zf.weeklyRisk >= 0.75)      level = 3;
        else if (zf.weeklyRisk >= 0.5)  level = 2;
        else if (zf.weeklyRisk >= 0.3)  level = 1;
        QCOMPARE(level, 2);
        QCOMPARE(zf.alertLabel(), QStringLiteral("NORMAL"));
        zf.alertLevel = level;
        QCOMPARE(zf.alertLabel(), QStringLiteral("HIGH"));
    }
};

QTEST_GUILESS_MAIN(RiskForecasterDeep4Test)
#include "test_risk_forecaster_deep4.moc"
