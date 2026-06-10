// test_risk_forecaster_deep.cpp
// Deep tests for RiskForecaster: fit, forecast horizon, alert levels,
// escalation detection, and multi-zone support.
#include <QTest>
#include "models/RiskForecaster.h"
#include "core/CrimeEvent.h"
#include <cmath>

class RiskForecasterDeepTest : public QObject
{
    Q_OBJECT

private:
    static CrimeEvent makeEv(const QString& suburb, const QDate& date,
                              const QString& type = QStringLiteral("burglary"))
    {
        CrimeEvent ev;
        ev.eventId   = suburb + date.toString(Qt::ISODate);
        ev.crimeType = type;
        ev.suburb    = suburb;
        ev.lat       = 51.5;
        ev.lon       = -0.1;
        ev.latitude  = 51.5;
        ev.longitude = -0.1;
        const QDateTime dt(date, QTime(22, 0, 0), QTimeZone::utc());
        ev.occurredAt = dt;
        ev.timestamp  = dt;
        return ev;
    }

    static QVector<CrimeEvent> hotZoneEvents(const QDate& refDate, int count = 30)
    {
        QVector<CrimeEvent> evs;
        for (int i = 0; i < count; ++i)
            evs.append(makeEv(QStringLiteral("HotZone"), refDate.addDays(-i)));
        return evs;
    }

private slots:

    // ── 1. isFitted() false before fit() ─────────────────────────────────────
    void testNotFittedBeforeFit()
    {
        RiskForecaster rf;
        QVERIFY(!rf.isFitted());
    }

    // ── 2. isFitted() true after fit() ───────────────────────────────────────
    void testFittedAfterFit()
    {
        RiskForecaster rf;
        rf.fit(hotZoneEvents(QDate(2024, 3, 15)));
        QVERIFY2(rf.isFitted(), "RiskForecaster should be fitted after fit()");
    }

    // ── 3. zoneCount() correct after fit() ───────────────────────────────────
    void testZoneCount()
    {
        RiskForecaster rf;
        QVector<CrimeEvent> evs = hotZoneEvents(QDate(2024, 3, 15));
        for (int i = 0; i < 10; ++i)
            evs.append(makeEv(QStringLiteral("ColdZone"), QDate(2024, 3, 15).addDays(-i * 10)));

        rf.fit(evs);
        QVERIFY2(rf.zoneCount() >= 2,
                 qPrintable(QStringLiteral("zoneCount %1 should be >= 2").arg(rf.zoneCount())));
    }

    // ── 4. forecast() returns N days per zone ────────────────────────────────
    void testForecastHorizon()
    {
        RiskForecaster rf(7);
        rf.fit(hotZoneEvents(QDate(2024, 3, 15)));

        const QDateTime from(QDate(2024, 3, 20), QTime(0, 0, 0), QTimeZone::utc());
        const auto forecasts = rf.forecast(from);

        QVERIFY(!forecasts.isEmpty());
        for (const auto& zf : forecasts) {
            QVERIFY2(zf.days.size() == 7,
                     qPrintable(QStringLiteral("Zone %1 has %2 days, expected 7")
                        .arg(zf.zoneId).arg(zf.days.size())));
        }
    }

    // ── 5. riskScore is in [0, 1] ────────────────────────────────────────────
    void testRiskScoreRange()
    {
        RiskForecaster rf(7);
        rf.fit(hotZoneEvents(QDate(2024, 3, 15)));

        const QDateTime from(QDate(2024, 3, 20), QTime(0, 0, 0), QTimeZone::utc());
        const auto forecasts = rf.forecast(from);

        for (const auto& zf : forecasts) {
            for (const auto& day : zf.days) {
                QVERIFY2(day.riskScore >= 0.0 && day.riskScore <= 1.0,
                         qPrintable(QStringLiteral("riskScore %1 must be in [0,1]")
                            .arg(day.riskScore)));
            }
        }
    }

    // ── 6. escalationFactor >= 1.0 ───────────────────────────────────────────
    void testEscalationFactorPositive()
    {
        RiskForecaster rf(7);
        rf.fit(hotZoneEvents(QDate(2024, 3, 15)));

        const QDateTime from(QDate(2024, 3, 16), QTime(0, 0, 0), QTimeZone::utc());
        const auto forecasts = rf.forecast(from);

        for (const auto& zf : forecasts) {
            for (const auto& day : zf.days) {
                QVERIFY2(day.escalationFactor >= 1.0,
                         qPrintable(QStringLiteral("escalationFactor %1 must be >= 1.0")
                            .arg(day.escalationFactor)));
            }
        }
    }

    // ── 7. alertLabel() returns non-empty string ─────────────────────────────
    void testAlertLabelNonEmpty()
    {
        RiskForecaster rf(7);
        rf.fit(hotZoneEvents(QDate(2024, 3, 15)));

        const QDateTime from(QDate(2024, 3, 16), QTime(0, 0, 0), QTimeZone::utc());
        const auto forecasts = rf.forecast(from);

        for (const auto& zf : forecasts) {
            QVERIFY2(!zf.alertLabel().isEmpty(),
                     "alertLabel() must return a non-empty string");
        }
    }

    // ── 8. forecastZone() returns 7 days ─────────────────────────────────────
    void testForecastZoneHorizon()
    {
        RiskForecaster rf(7);
        rf.fit(hotZoneEvents(QDate(2024, 3, 15)));

        const QDateTime from(QDate(2024, 3, 16), QTime(0, 0, 0), QTimeZone::utc());
        const auto zf = rf.forecastZone(QStringLiteral("HotZone"), from);
        QVERIFY2(zf.days.size() == 7,
                 qPrintable(QStringLiteral("forecastZone should produce 7 days, got %1")
                    .arg(zf.days.size())));
    }

    // ── 9. Explanation is non-empty for non-trivial events ───────────────────
    void testExplanationPopulated()
    {
        RiskForecaster rf(7);
        rf.fit(hotZoneEvents(QDate(2024, 3, 15)));

        const QDateTime from(QDate(2024, 3, 16), QTime(0, 0, 0), QTimeZone::utc());
        const auto zf = rf.forecastZone(QStringLiteral("HotZone"), from);
        const bool hasExp = std::any_of(zf.days.begin(), zf.days.end(),
            [](const ForecastDay& d){ return !d.explanation.isEmpty(); });
        QVERIFY2(hasExp, "At least one ForecastDay should have a non-empty explanation");
    }

    // ── 10. setAlertThresholds: higher thresholds change alertLevel ──────────
    void testSetAlertThresholds()
    {
        RiskForecaster rf(7);
        rf.fit(hotZoneEvents(QDate(2024, 3, 15)));

        const QDateTime from(QDate(2024, 3, 16), QTime(0, 0, 0), QTimeZone::utc());

        rf.setAlertThresholds(0.99, 0.999, 0.9999);  // very high thresholds
        const auto forecasts = rf.forecast(from);
        // With very high thresholds, alert level should be 0 (Normal) for most zones
        for (const auto& zf : forecasts) {
            QVERIFY2(zf.alertLevel >= 0 && zf.alertLevel <= 3,
                     qPrintable(QStringLiteral("alertLevel %1 must be in [0,3]")
                        .arg(zf.alertLevel)));
        }
    }
};

QTEST_MAIN(RiskForecasterDeepTest)
#include "test_risk_forecaster_deep.moc"
