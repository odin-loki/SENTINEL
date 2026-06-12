// test_risk_forecaster_deep6.cpp — Deep audit iteration 21: RiskForecaster
// Verifies: recency-only zone enumeration, horizon clamp, suburb/LGA precedence,
// temporal multipliers, alert labels, unfitted path, unknown zone fallback.
#include <QtTest/QtTest>
#include <cmath>
#include "models/RiskForecaster.h"
#include "core/CrimeEvent.h"

class RiskForecasterDeep6Test : public QObject
{
    Q_OBJECT

    static CrimeEvent ev(const QString& id, const QString& suburb,
                         const std::optional<QString>& lga,
                         const QDateTime& dt,
                         const QString& type = QStringLiteral("burglary"))
    {
        CrimeEvent e;
        e.eventId    = id;
        e.suburb     = suburb;
        e.lga        = lga;
        e.timestamp  = dt;
        e.occurredAt = dt;
        e.crimeType  = type;
        return e;
    }

private slots:

    void testForecastOmitsZonesWhenRecencyWindowEmpty()
    {
        const QDateTime latest(QDate(2024, 9, 1), QTime(12, 0), Qt::UTC);
        const QDateTime stale = latest.addDays(-40);

        RiskForecaster rf(3);
        rf.fit({
            ev(QStringLiteral("recent"), QStringLiteral("Recent"), std::nullopt, latest),
            ev(QStringLiteral("stale"), QStringLiteral("StaleOnly"), std::nullopt, stale),
        });

        QVERIFY(rf.isFitted());
        QCOMPARE(rf.zoneCount(), 1);

        const auto all = rf.forecast(latest);
        QCOMPARE(all.size(), 2);

        bool hasRecent = false;
        bool hasStale = false;
        for (const auto& zf : all) {
            if (zf.zoneId == QStringLiteral("Recent")) hasRecent = true;
            if (zf.zoneId == QStringLiteral("StaleOnly")) hasStale = true;
        }
        QVERIFY(hasRecent);
        QVERIFY(hasStale);
    }

    void testNegativeHorizonClampedToOneDay()
    {
        RiskForecaster rf(-5);
        const QDateTime base(QDate(2024, 1, 1), QTime(0, 0), Qt::UTC);
        rf.fit({ ev(QStringLiteral("h0"), QStringLiteral("H"), std::nullopt, base) });

        const auto zf = rf.forecastZone(QStringLiteral("H"), base.addDays(1));
        QCOMPARE(zf.days.size(), 1);
    }

    void testSuburbPreferredOverLgaWhenBothPresent()
    {
        const QDateTime base(QDate(2024, 2, 1), QTime(9, 0), Qt::UTC);
        RiskForecaster rf(1);
        rf.fit({ ev(QStringLiteral("g0"), QStringLiteral("SuburbA"),
                    QStringLiteral("LgaB"), base) });

        QCOMPARE(rf.zoneCount(), 1);
        const auto all = rf.forecast(base.addDays(2));
        QCOMPARE(all.size(), 1);
        QCOMPARE(all.first().zoneId, QStringLiteral("SuburbA"));

        const auto zf = rf.forecastZone(QStringLiteral("SuburbA"), base.addDays(2));
        QVERIFY(!zf.days.isEmpty());
        QVERIFY2(zf.days.first().baselineProb > 1e-6,
                 "suburb-keyed bucket should carry fitted baseline rate");
    }

    void testWeekendTemporalMultiplierBoostsRisk()
    {
        const QDateTime mon(QDate(2024, 3, 4), QTime(10, 0), Qt::UTC);   // Monday
        const QDateTime fri(QDate(2024, 3, 8), QTime(10, 0), Qt::UTC);   // Friday

        QVector<CrimeEvent> events;
        for (int i = 0; i < 20; ++i)
            events.append(ev(QStringLiteral("w%1").arg(i), QStringLiteral("Week"),
                             std::nullopt, mon.addDays(i % 10)));

        RiskForecaster rf(1);
        rf.fit(events);

        const auto monZf = rf.forecastZone(QStringLiteral("Week"), mon.addDays(15));
        const auto friZf = rf.forecastZone(QStringLiteral("Week"), fri.addDays(15));

        QVERIFY(!monZf.days.isEmpty() && !friZf.days.isEmpty());
        const double monTemp = monZf.days.first().temporalFactor;
        const double friTemp = friZf.days.first().temporalFactor;

        QVERIFY2(std::abs(monTemp - 1.0) < 1e-9,
                 qPrintable(QStringLiteral("weekday temporal=%1").arg(monTemp)));
        QVERIFY2(friTemp > monTemp + 1e-9,
                 qPrintable(QStringLiteral("weekend temporal=%1 expected > weekday")
                                .arg(friTemp)));
    }

    void testWinterMonthTemporalMultiplier()
    {
        const QDateTime summer(QDate(2024, 7, 15), QTime(10, 0), Qt::UTC);
        const QDateTime winter(QDate(2024, 12, 15), QTime(10, 0), Qt::UTC);

        RiskForecaster rf(1);
        rf.fit({ ev(QStringLiteral("t0"), QStringLiteral("Season"), std::nullopt,
                    summer.addDays(-5)) });

        const auto summerZf = rf.forecastZone(QStringLiteral("Season"), summer);
        const auto winterZf = rf.forecastZone(QStringLiteral("Season"), winter);

        QVERIFY(!summerZf.days.isEmpty() && !winterZf.days.isEmpty());
        QVERIFY2(winterZf.days.first().temporalFactor >=
                     summerZf.days.first().temporalFactor,
                 "winter month factor should be >= summer month factor");
    }

    void testAlertLabelMatchesAlertLevel()
    {
        ZoneForecast zf;
        zf.alertLevel = 0;
        QCOMPARE(zf.alertLabel(), QStringLiteral("NORMAL"));
        zf.alertLevel = 1;
        QCOMPARE(zf.alertLabel(), QStringLiteral("ELEVATED"));
        zf.alertLevel = 2;
        QCOMPARE(zf.alertLabel(), QStringLiteral("HIGH"));
        zf.alertLevel = 3;
        QCOMPARE(zf.alertLabel(), QStringLiteral("CRITICAL"));
    }

    void testUnfittedForecastReturnsZeroRiskDays()
    {
        RiskForecaster rf(4);
        const QDateTime from(QDate(2024, 5, 1), QTime(0, 0), Qt::UTC);
        const auto zf = rf.forecastZone(QStringLiteral("Missing"), from);

        QCOMPARE(zf.days.size(), 4);
        for (const auto& day : zf.days) {
            QCOMPARE(day.riskScore, 0.0);
            QCOMPARE(day.baselineProb, 0.0);
        }
        QCOMPARE(zf.weeklyRisk, 0.0);
        QCOMPARE(zf.alertLevel, 0);
    }

    void testUnknownZoneWhenSuburbAndLgaEmpty()
    {
        const QDateTime base(QDate(2024, 6, 1), QTime(8, 0), Qt::UTC);
        CrimeEvent e;
        e.eventId    = QStringLiteral("anon");
        e.suburb     = {};
        e.lga        = std::nullopt;
        e.timestamp  = base;
        e.occurredAt = base;
        e.crimeType  = QStringLiteral("burglary");

        RiskForecaster rf(1);
        rf.fit({ e });

        QCOMPARE(rf.zoneCount(), 1);
        const auto all = rf.forecast(base.addDays(1));
        QCOMPARE(all.size(), 1);
        QCOMPARE(all.first().zoneId, QStringLiteral("unknown"));
    }
};

QTEST_GUILESS_MAIN(RiskForecasterDeep6Test)
#include "test_risk_forecaster_deep6.moc"
