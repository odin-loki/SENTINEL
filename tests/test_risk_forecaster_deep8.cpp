// test_risk_forecaster_deep8.cpp — Deep audit iteration 26: RiskForecaster
// peakDayIndex, alertLabel strings, zoneCount, explanation field.
#include <QtTest/QtTest>
#include "models/RiskForecaster.h"
#include "core/CrimeEvent.h"

class RiskForecasterDeep8Test : public QObject
{
    Q_OBJECT

    static CrimeEvent ev(const QString& zone, const QDateTime& dt)
    {
        CrimeEvent e;
        e.eventId    = zone + dt.toString(QStringLiteral("yyyyMMddhh"));
        e.suburb     = zone;
        e.timestamp  = dt;
        e.occurredAt = dt;
        e.crimeType  = QStringLiteral("burglary");
        return e;
    }

private slots:

    void testPeakDayIndexPointsToHighestRisk()
    {
        const QDateTime base(QDate(2024, 8, 1), QTime(0, 0), Qt::UTC);
        QVector<CrimeEvent> events;
        for (int i = 0; i < 20; ++i)
            events.append(ev(QStringLiteral("PeakZ"), base.addDays(-i)));

        RiskForecaster rf(5);
        rf.fit(events);
        const auto zf = rf.forecastZone(QStringLiteral("PeakZ"), base);

        QVERIFY(zf.peakDayIndex >= 0 && zf.peakDayIndex < zf.days.size());
        double maxRisk = -1.0;
        int maxIdx = -1;
        for (int i = 0; i < zf.days.size(); ++i) {
            if (zf.days[i].riskScore > maxRisk) {
                maxRisk = zf.days[i].riskScore;
                maxIdx = i;
            }
        }
        QCOMPARE(zf.peakDayIndex, maxIdx);
    }

    void testAlertLabelMatchesLevel()
    {
        RiskForecaster rf(1);
        rf.setAlertThresholds(0.1, 0.2, 0.3);

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

    void testZoneCountAfterFit()
    {
        const QDateTime base(QDate(2024, 9, 1), QTime(12, 0), Qt::UTC);
        QVector<CrimeEvent> events;
        events.append(ev(QStringLiteral("A"), base));
        events.append(ev(QStringLiteral("B"), base.addDays(-1)));
        events.append(ev(QStringLiteral("A"), base.addDays(-2)));

        RiskForecaster rf(3);
        rf.fit(events);
        QCOMPARE(rf.zoneCount(), 2);
    }

    void testForecastDayHasExplanation()
    {
        const QDateTime base(QDate(2024, 10, 1), QTime(0, 0), Qt::UTC);
        RiskForecaster rf(2);
        rf.fit({ ev(QStringLiteral("Expl"), base) });

        const auto zf = rf.forecastZone(QStringLiteral("Expl"), base);
        QVERIFY(!zf.days.isEmpty());
        QVERIFY(!zf.days.first().explanation.isEmpty());
    }

    void testUnfittedZoneReturnsZeroRiskDays()
    {
        RiskForecaster rf(3);
        const auto zf = rf.forecastZone(QStringLiteral("NONE"),
                                        QDateTime(QDate(2024, 1, 1), QTime(0, 0), Qt::UTC));
        QCOMPARE(zf.days.size(), 3);
        QCOMPARE(zf.weeklyRisk, 0.0);
        for (const auto& day : zf.days)
            QCOMPARE(day.riskScore, 0.0);
    }
};

QTEST_GUILESS_MAIN(RiskForecasterDeep8Test)
#include "test_risk_forecaster_deep8.moc"
