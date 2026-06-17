// test_risk_forecaster_zones.cpp
// RiskForecaster multi-zone, alert levels, horizon, zone forecast
// and setAlertThresholds tests.
#include <QTest>
#include <QTimeZone>
#include "models/RiskForecaster.h"
#include "core/CrimeEvent.h"

class RiskForecasterZonesTest : public QObject
{
    Q_OBJECT

private:
    static CrimeEvent makeEvent(const QString& suburb, int daysAgo = 5,
                                 const QString& type = QStringLiteral("burglary"))
    {
        CrimeEvent ev;
        ev.id        = QUuid::createUuid().toString(QUuid::WithoutBraces);
        ev.crimeType = type;
        ev.suburb    = suburb;
        ev.latitude  = 51.5;
        ev.longitude = -0.1;
        const QDate d = QDate::currentDate().addDays(-daysAgo);
        ev.timestamp = QDateTime(d, QTime(10, 0), QTimeZone::utc());
        ev.occurredAt = ev.timestamp;
        return ev;
    }

    // 3 zones, 10 in Z1, 5 in Z2, 1 in Z3
    static QVector<CrimeEvent> threeZones()
    {
        QVector<CrimeEvent> evs;
        for (int i = 0; i < 10; ++i) evs.append(makeEvent(QStringLiteral("Z1"), i % 14 + 1));
        for (int i = 0; i < 5;  ++i) evs.append(makeEvent(QStringLiteral("Z2"), i % 14 + 1));
        for (int i = 0; i < 1;  ++i) evs.append(makeEvent(QStringLiteral("Z3"), i + 1));
        return evs;
    }

private slots:

    // 1. isFitted() false before fit
    void testNotFittedBeforeFit()
    {
        RiskForecaster rf;
        QVERIFY(!rf.isFitted());
    }

    // 2. isFitted() true after fit
    void testFittedAfterFit()
    {
        RiskForecaster rf;
        rf.fit(threeZones());
        QVERIFY(rf.isFitted());
    }

    // 3. zoneCount() == 3 for 3-zone data
    void testZoneCount()
    {
        RiskForecaster rf;
        rf.fit(threeZones());
        QCOMPARE(rf.zoneCount(), 3);
    }

    // 4. forecast() returns non-empty for all zones
    void testForecastNonEmpty()
    {
        RiskForecaster rf(7);
        rf.fit(threeZones());
        const auto forecasts = rf.forecast(QDateTime::currentDateTimeUtc());
        QVERIFY2(!forecasts.isEmpty(), "forecast() should return non-empty");
    }

    // 5. Each ZoneForecast has correct number of days
    void testForecastHorizonDays()
    {
        RiskForecaster rf(5);
        rf.fit(threeZones());
        const auto forecasts = rf.forecast(QDateTime::currentDateTimeUtc());
        for (const auto& zf : forecasts) {
            QVERIFY2(zf.days.size() == 5,
                     qPrintable(QStringLiteral("Zone %1 should have 5 forecast days, got %2")
                        .arg(zf.zoneId).arg(zf.days.size())));
        }
    }

    // 6. ForecastDay riskScore in [0, 1]
    void testForecastRiskScoreRange()
    {
        RiskForecaster rf;
        rf.fit(threeZones());
        const auto forecasts = rf.forecast(QDateTime::currentDateTimeUtc());
        for (const auto& zf : forecasts) {
            for (const auto& day : zf.days) {
                QVERIFY2(day.riskScore >= 0.0 && day.riskScore <= 1.0,
                         qPrintable(QStringLiteral("Zone %1 day riskScore %2 must be in [0,1]")
                            .arg(day.zoneId).arg(day.riskScore)));
            }
        }
    }

    // 7. alertLevel in [0, 3]
    void testAlertLevelRange()
    {
        RiskForecaster rf;
        rf.fit(threeZones());
        const auto forecasts = rf.forecast(QDateTime::currentDateTimeUtc());
        for (const auto& zf : forecasts) {
            QVERIFY2(zf.alertLevel >= 0 && zf.alertLevel <= 3,
                     qPrintable(QStringLiteral("Zone %1 alertLevel %2 must be in [0,3]")
                        .arg(zf.zoneId).arg(zf.alertLevel)));
        }
    }

    // 8. alertLabel() returns non-empty string
    void testAlertLabelNonEmpty()
    {
        RiskForecaster rf;
        rf.fit(threeZones());
        const auto forecasts = rf.forecast(QDateTime::currentDateTimeUtc());
        for (const auto& zf : forecasts) {
            QVERIFY2(!zf.alertLabel().isEmpty(),
                     qPrintable(QStringLiteral("Zone %1 alertLabel should be non-empty").arg(zf.zoneId)));
        }
    }

    // 9. setAlertThresholds: very low thresholds -> elevated alert
    void testLowThresholdsElevatedAlert()
    {
        RiskForecaster rf;
        rf.fit(threeZones());
        rf.setAlertThresholds(0.001, 0.002, 0.003);
        const auto forecasts = rf.forecast(QDateTime::currentDateTimeUtc());
        bool hasElevated = false;
        for (const auto& zf : forecasts)
            if (zf.alertLevel >= 1) hasElevated = true;
        QVERIFY2(hasElevated, "Very low thresholds should yield at least one elevated zone");
    }

    // 10. weeklyRisk >= 0 for all zones
    void testWeeklyRiskNonNegative()
    {
        RiskForecaster rf;
        rf.fit(threeZones());
        const auto forecasts = rf.forecast(QDateTime::currentDateTimeUtc());
        for (const auto& zf : forecasts) {
            QVERIFY2(zf.weeklyRisk >= 0.0,
                     qPrintable(QStringLiteral("Zone %1 weeklyRisk %2 must be >= 0")
                        .arg(zf.zoneId).arg(zf.weeklyRisk)));
        }
    }
};

QTEST_MAIN(RiskForecasterZonesTest)
#include "test_risk_forecaster_zones.moc"
