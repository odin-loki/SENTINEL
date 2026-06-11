// Iteration 13 — RiskForecaster threshold boundary and multi-zone tests
#include <QtTest/QtTest>
#include "models/RiskForecaster.h"
#include "core/CrimeEvent.h"

class RiskForecasterDeep3Test : public QObject
{
    Q_OBJECT

    static CrimeEvent ev(const QString& id, const QString& zone, const QDateTime& dt)
    {
        CrimeEvent e;
        e.eventId   = id;
        e.suburb    = zone;
        e.timestamp = dt;
        e.occurredAt = dt;
        e.crimeType = QStringLiteral("burglary");
        return e;
    }

private slots:

    void testAlertLabelsAllUpperCase()
    {
        ZoneForecast zf;
        QCOMPARE(zf.alertLabel(), QStringLiteral("NORMAL"));

        zf.alertLevel = 1;
        QCOMPARE(zf.alertLabel(), QStringLiteral("ELEVATED"));

        zf.alertLevel = 2;
        QCOMPARE(zf.alertLabel(), QStringLiteral("HIGH"));

        zf.alertLevel = 3;
        QCOMPARE(zf.alertLabel(), QStringLiteral("CRITICAL"));
    }

    void testMultiZoneForecasts()
    {
        const QDateTime base(QDate(2024, 1, 1), QTime(0, 0), Qt::UTC);
        QVector<CrimeEvent> events;
        for (int i = 0; i < 30; ++i)
            events.append(ev(QStringLiteral("a%1").arg(i), QStringLiteral("North"), base.addDays(i)));
        for (int i = 0; i < 10; ++i)
            events.append(ev(QStringLiteral("b%1").arg(i), QStringLiteral("South"), base.addDays(i * 2)));

        RiskForecaster rf(7);
        rf.fit(events);

        const auto north = rf.forecastZone(QStringLiteral("North"), base.addDays(35));
        const auto south = rf.forecastZone(QStringLiteral("South"), base.addDays(35));

        QVERIFY(!north.days.isEmpty());
        QVERIFY(!south.days.isEmpty());
        QCOMPARE(north.zoneId, QStringLiteral("North"));
        QCOMPARE(south.zoneId, QStringLiteral("South"));
    }

    void testUnknownZoneReturnsValidForecast()
    {
        const QDateTime base(QDate(2024, 1, 1), QTime(0, 0), Qt::UTC);
        RiskForecaster rf(7);
        rf.fit({ev(QStringLiteral("x0"), QStringLiteral("Known"), base)});

        const auto zf = rf.forecastZone(QStringLiteral("UnknownZone"), base.addDays(10));
        QVERIFY(!zf.days.isEmpty());
        QCOMPARE(zf.zoneId, QStringLiteral("UnknownZone"));
        for (const auto& day : zf.days) {
            QVERIFY(day.riskScore >= 0.0);
            QVERIFY(day.riskScore <= 1.0);
        }
    }

    void testDailyRiskScoresInRange()
    {
        const QDateTime base(QDate(2024, 6, 1), QTime(0, 0), Qt::UTC);
        QVector<CrimeEvent> events;
        for (int i = 0; i < 90; ++i)
            events.append(ev(QStringLiteral("e%1").arg(i), QStringLiteral("Central"),
                             base.addDays(i)));

        RiskForecaster rf(14);
        rf.fit(events);
        const auto zf = rf.forecastZone(QStringLiteral("Central"), base.addDays(100));

        for (const auto& day : zf.days) {
            QVERIFY2(day.riskScore >= 0.0 && day.riskScore <= 1.0,
                     qPrintable(QStringLiteral("riskScore=%1 out of [0,1]").arg(day.riskScore)));
        }
    }

    void testPeakDayMatchesMaxRisk()
    {
        const QDateTime base(QDate(2024, 1, 1), QTime(0, 0), Qt::UTC);
        QVector<CrimeEvent> events;
        for (int i = 0; i < 60; ++i)
            events.append(ev(QStringLiteral("p%1").arg(i), QStringLiteral("PeakZone"),
                             base.addDays(i)));

        RiskForecaster rf(7);
        rf.fit(events);
        const auto zf = rf.forecastZone(QStringLiteral("PeakZone"), base.addDays(70));

        int maxIdx = 0;
        double maxRisk = -1.0;
        for (int i = 0; i < zf.days.size(); ++i) {
            if (zf.days[i].riskScore > maxRisk) {
                maxRisk = zf.days[i].riskScore;
                maxIdx = i;
            }
        }
        QCOMPARE(zf.peakDayIndex, maxIdx);
    }

    void testAlertThresholdOrdering()
    {
        RiskForecaster rf(7);
        rf.setAlertThresholds(0.3, 0.6, 0.9);
        // No crash; thresholds stored internally
        const QDateTime base(QDate(2024, 1, 1), QTime(0, 0), Qt::UTC);
        rf.fit({ev(QStringLiteral("t0"), QStringLiteral("T"), base)});
        const auto zf = rf.forecastZone(QStringLiteral("T"), base.addDays(5));
        QVERIFY(zf.alertLevel >= 0 && zf.alertLevel <= 3);
    }

    void testEmptyFitProducesDefaultForecast()
    {
        RiskForecaster rf(7);
        rf.fit({});
        const QDateTime dt(QDate(2024, 6, 1), QTime(0, 0), Qt::UTC);
        const auto zf = rf.forecastZone(QStringLiteral("Any"), dt);
        QVERIFY(!zf.days.isEmpty());
        QCOMPARE(zf.days.size(), 7);
    }

    void testHorizonOneDay()
    {
        const QDateTime base(QDate(2024, 1, 1), QTime(0, 0), Qt::UTC);
        RiskForecaster rf(1);
        rf.fit({ev(QStringLiteral("h0"), QStringLiteral("H"), base)});
        const auto zf = rf.forecastZone(QStringLiteral("H"), base.addDays(2));
        QCOMPARE(zf.days.size(), 1);
    }
};

QTEST_GUILESS_MAIN(RiskForecasterDeep3Test)
#include "test_risk_forecaster_deep3.moc"
