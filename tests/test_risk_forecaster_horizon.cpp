// test_risk_forecaster_horizon.cpp
// Multi-horizon accuracy and boundary tests for RiskForecaster.
#include <QTest>
#include "models/RiskForecaster.h"
#include "core/CrimeEvent.h"

static CrimeEvent makeEvent(const QString& suburb, const QDate& date,
                              const QString& type = "burglary")
{
    CrimeEvent ev;
    ev.eventId   = suburb + QStringLiteral("_") + date.toString(Qt::ISODate);
    ev.crimeType = type;
    ev.suburb    = suburb;
    ev.lat       = 51.50;
    ev.lon       = -0.10;
    const QDateTime dt(date, QTime(12, 0, 0), QTimeZone::utc());
    ev.occurredAt = dt;
    ev.timestamp  = dt;   // RiskForecaster reads e.timestamp
    return ev;
}

class TestRiskForecasterHorizon : public QObject
{
    Q_OBJECT

private:
    QVector<CrimeEvent> makeHistory(const QString& zone, int nEvents,
                                     const QDate& startDate)
    {
        QVector<CrimeEvent> evs;
        for (int i = 0; i < nEvents; ++i)
            evs.append(makeEvent(zone, startDate.addDays(-i)));
        return evs;
    }

private slots:

    // ── 1. Forecast produces one ForecastDay per horizon day per zone ─────────
    void testHorizonDayCount()
    {
        constexpr int HORIZON = 14;
        RiskForecaster forecaster(HORIZON);

        QVector<CrimeEvent> evs;
        for (int i = 0; i < 30; ++i)
            evs.append(makeEvent(QStringLiteral("Z1"), QDate(2024, 1, 1).addDays(-i)));

        forecaster.fit(evs);
        const auto forecast = forecaster.forecast(
            QDateTime(QDate(2024, 1, 2), QTime(0, 0, 0), QTimeZone::utc()));

        QVERIFY2(!forecast.isEmpty(), "Forecast should produce at least 1 zone");
        for (const auto& zf : forecast)
            QCOMPARE(zf.days.size(), HORIZON);
    }

    // ── 2. Risk scores are in [0, 1] ──────────────────────────────────────────
    void testRiskScoresInRange()
    {
        RiskForecaster forecaster(7);
        QVector<CrimeEvent> evs;
        for (int i = 0; i < 20; ++i)
            evs.append(makeEvent(QStringLiteral("ZA"), QDate(2024, 3, 1).addDays(-i)));

        forecaster.fit(evs);
        const auto forecast = forecaster.forecast(
            QDateTime(QDate(2024, 3, 2), QTime(0, 0, 0), QTimeZone::utc()));

        for (const auto& zf : forecast)
            for (const auto& day : zf.days) {
                QVERIFY2(day.riskScore >= 0.0,
                         qPrintable(QStringLiteral("Risk score %1 < 0").arg(day.riskScore)));
                QVERIFY2(day.riskScore <= 1.0 + 1e-9,
                         qPrintable(QStringLiteral("Risk score %1 > 1").arg(day.riskScore)));
            }
    }

    // ── 3. High-activity zone gets higher risk than low-activity zone ─────────
    void testHighActivityZoneHigherRisk()
    {
        RiskForecaster forecaster(7);
        QVector<CrimeEvent> evs;

        // Zone HIGH: 50 recent events
        for (int i = 0; i < 50; ++i)
            evs.append(makeEvent(QStringLiteral("HIGH"), QDate(2024, 5, 1).addDays(-i % 14)));
        // Zone LOW: 3 events spread over 60 days
        for (int i = 0; i < 3; ++i)
            evs.append(makeEvent(QStringLiteral("LOW"), QDate(2024, 5, 1).addDays(-i * 20)));

        forecaster.fit(evs);
        const auto forecast = forecaster.forecast(
            QDateTime(QDate(2024, 5, 2), QTime(0, 0, 0), QTimeZone::utc()));

        double highWeekly = 0.0, lowWeekly = 0.0;
        for (const auto& zf : forecast) {
            if (zf.zoneId == QStringLiteral("HIGH")) highWeekly = zf.weeklyRisk;
            if (zf.zoneId == QStringLiteral("LOW"))  lowWeekly  = zf.weeklyRisk;
        }

        QVERIFY2(highWeekly >= lowWeekly,
                 qPrintable(QStringLiteral(
                    "HIGH zone weekly risk (%1) should be >= LOW zone (%2)")
                    .arg(highWeekly).arg(lowWeekly)));
    }

    // ── 4. Alert levels are valid ─────────────────────────────────────────────
    void testAlertLevelsValid()
    {
        RiskForecaster forecaster(7);
        QVector<CrimeEvent> evs;
        for (int i = 0; i < 100; ++i)
            evs.append(makeEvent(QStringLiteral("BUSY"), QDate(2024, 6, 1).addDays(-i % 7)));

        forecaster.fit(evs);
        const auto forecast = forecaster.forecast(
            QDateTime(QDate(2024, 6, 2), QTime(0, 0, 0), QTimeZone::utc()));

        for (const auto& zf : forecast) {
            QVERIFY2(zf.alertLevel >= 0 && zf.alertLevel <= 3,
                     qPrintable(QStringLiteral("alertLevel %1 out of [0,3]").arg(zf.alertLevel)));
            const QString lbl = zf.alertLabel();
            QVERIFY2(!lbl.isEmpty(), "alertLabel() should return non-empty string");
        }
    }

    // ── 5. forecastZone matches global forecast for that zone ────────────────
    void testForecastZoneConsistency()
    {
        RiskForecaster forecaster(7);
        QVector<CrimeEvent> evs;
        for (int i = 0; i < 20; ++i)
            evs.append(makeEvent(QStringLiteral("XZ"), QDate(2024, 4, 1).addDays(-i)));

        forecaster.fit(evs);
        const QDateTime from(QDate(2024, 4, 2), QTime(0, 0, 0), QTimeZone::utc());

        const auto globalForecast = forecaster.forecast(from);
        const auto zoneOnly       = forecaster.forecastZone(QStringLiteral("XZ"), from);

        // Find the XZ zone in global forecast
        ZoneForecast globalXZ;
        bool found = false;
        for (const auto& zf : globalForecast) {
            if (zf.zoneId == QStringLiteral("XZ")) {
                globalXZ = zf;
                found = true;
            }
        }

        QVERIFY2(found, "Zone XZ not found in global forecast");
        QCOMPARE(zoneOnly.days.size(), globalXZ.days.size());
        for (int i = 0; i < zoneOnly.days.size(); ++i) {
            QVERIFY2(std::abs(zoneOnly.days[i].riskScore - globalXZ.days[i].riskScore) < 1e-9,
                     qPrintable(QStringLiteral(
                        "Day %1 risk mismatch: zone=%2 global=%3")
                        .arg(i).arg(zoneOnly.days[i].riskScore).arg(globalXZ.days[i].riskScore)));
        }
    }

    // ── 6. Empty fit → isFitted returns false → no crash ─────────────────────
    void testEmptyFit()
    {
        RiskForecaster forecaster(7);
        forecaster.fit({});
        QVERIFY(!forecaster.isFitted());
        const auto forecast = forecaster.forecast(QDateTime::currentDateTimeUtc());
        QVERIFY(forecast.isEmpty());
    }

    // ── 7. Custom alert thresholds respected: elevated threshold at 0 = all elevated ─
    void testCustomAlertThresholds()
    {
        RiskForecaster forecaster(7);
        // Set thresholds so low that any positive activity triggers elevated (>= 1)
        forecaster.setAlertThresholds(0.0, 100.0, 200.0);

        QVector<CrimeEvent> evs;
        for (int i = 0; i < 20; ++i)
            evs.append(makeEvent(QStringLiteral("HOT"), QDate(2024, 7, 1).addDays(-i)));

        forecaster.fit(evs);
        const auto forecast = forecaster.forecast(
            QDateTime(QDate(2024, 7, 2), QTime(0, 0, 0), QTimeZone::utc()));

        // With threshold at 0 and any weekly risk > 0, should get >= Elevated (1)
        bool hasElevated = false;
        for (const auto& zf : forecast)
            if (zf.alertLevel >= 1) hasElevated = true;

        QVERIFY2(hasElevated, "With threshold=0 and any activity, expect Elevated or higher alerts");
    }

    // ── 8. Multiple zones → correct zone count ────────────────────────────────
    void testMultipleZones()
    {
        RiskForecaster forecaster(7);
        QVector<CrimeEvent> evs;
        const QStringList zones = { "A", "B", "C", "D", "E" };
        for (const auto& z : zones)
            for (int i = 0; i < 10; ++i)
                evs.append(makeEvent(z, QDate(2024, 2, 1).addDays(-i)));

        forecaster.fit(evs);
        QCOMPARE(forecaster.zoneCount(), 5);

        const auto forecast = forecaster.forecast(
            QDateTime(QDate(2024, 2, 2), QTime(0, 0, 0), QTimeZone::utc()));
        QCOMPARE(forecast.size(), 5);
    }

    // ── 9. Day dates in forecast are sequential ───────────────────────────────
    void testForecastDatesSequential()
    {
        RiskForecaster forecaster(7);
        QVector<CrimeEvent> evs;
        for (int i = 0; i < 10; ++i)
            evs.append(makeEvent(QStringLiteral("SEQ"), QDate(2024, 3, 1).addDays(-i)));

        forecaster.fit(evs);
        const QDate startDate(2024, 3, 2);
        const auto forecast = forecaster.forecast(
            QDateTime(startDate, QTime(0, 0, 0), QTimeZone::utc()));

        for (const auto& zf : forecast) {
            for (int d = 0; d < zf.days.size(); ++d) {
                QCOMPARE(zf.days[d].date, startDate.addDays(d));
            }
        }
    }

    // ── 10. ForecastDay explanation is non-empty ──────────────────────────────
    void testExplanationNonEmpty()
    {
        RiskForecaster forecaster(3);
        QVector<CrimeEvent> evs;
        for (int i = 0; i < 10; ++i)
            evs.append(makeEvent(QStringLiteral("EXP"), QDate(2024, 8, 1).addDays(-i)));

        forecaster.fit(evs);
        const auto forecast = forecaster.forecast(
            QDateTime(QDate(2024, 8, 2), QTime(0, 0, 0), QTimeZone::utc()));

        for (const auto& zf : forecast)
            for (const auto& day : zf.days)
                QVERIFY2(!day.explanation.isEmpty(),
                         "ForecastDay.explanation should be non-empty");
    }
};

QTEST_MAIN(TestRiskForecasterHorizon)
#include "test_risk_forecaster_horizon.moc"
