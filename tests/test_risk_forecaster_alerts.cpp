// test_risk_forecaster_alerts.cpp — Alert-level, threshold, horizon and escalation tests
// for RiskForecaster.

#include <QTest>
#include <QCoreApplication>
#include <QDateTime>
#include <QtMath>
#include "models/RiskForecaster.h"
#include "core/CrimeEvent.h"

class TestRiskForecasterAlerts : public QObject {
    Q_OBJECT

    // ── Helpers ──────────────────────────────────────────────────────────────

    static CrimeEvent makeEvent(const QString& id, const QString& suburb,
                                 const QDateTime& dt,
                                 const QString& type = "burglary") {
        CrimeEvent e;
        e.eventId   = id;
        e.id        = id;
        e.suburb    = suburb;
        e.timestamp = dt;
        e.crimeType = type;
        return e;
    }

    static QVector<CrimeEvent> makeZoneEvents(const QString& zone,
                                               const QDateTime& base,
                                               int n,
                                               const QString& prefix = "e") {
        QVector<CrimeEvent> events;
        events.reserve(n);
        for (int i = 0; i < n; ++i)
            events.append(makeEvent(prefix + QString::number(i), zone,
                                    base.addDays(i)));
        return events;
    }

private slots:

    // ── 1. testAlertLevelNormal ───────────────────────────────────────────────
    // Set thresholds so high that any normal zone's weeklyRisk stays below
    // elevated → alertLevel 0.

    void testAlertLevelNormal() {
        RiskForecaster rf;
        rf.setAlertThresholds(0.9, 0.95, 0.99);   // bar extremely high

        const QDateTime base(QDate(2024,1,1), QTime(0,0,0), Qt::UTC);
        rf.fit(makeZoneEvents("zone_low", base, 60));
        const auto zf = rf.forecastZone("zone_low", base.addDays(61));

        QCOMPARE(zf.alertLevel, 0);
    }

    // ── 2. testAlertLevelElevated ────────────────────────────────────────────
    // elevated=0 so any non-zero weekly risk triggers Elevated;
    // high/critical set impossibly high so we stay at level 1.

    void testAlertLevelElevated() {
        RiskForecaster rf;
        rf.setAlertThresholds(0.0, 0.9, 0.99);

        const QDateTime base(QDate(2024,1,1), QTime(0,0,0), Qt::UTC);
        rf.fit(makeZoneEvents("zone_el", base, 60));
        const auto zf = rf.forecastZone("zone_el", base.addDays(61));

        if (zf.weeklyRisk <= 0.0)
            QSKIP("weeklyRisk is zero — cannot exercise Elevated threshold");

        QCOMPARE(zf.alertLevel, 1);
    }

    // ── 3. testAlertLevelHigh ────────────────────────────────────────────────
    // elevated=0, high=0 → any non-zero risk → level 2;
    // critical stays impossibly high.

    void testAlertLevelHigh() {
        RiskForecaster rf;
        rf.setAlertThresholds(0.0, 0.0, 0.99);

        const QDateTime base(QDate(2024,1,1), QTime(0,0,0), Qt::UTC);
        rf.fit(makeZoneEvents("zone_hi", base, 60));
        const auto zf = rf.forecastZone("zone_hi", base.addDays(61));

        if (zf.weeklyRisk <= 0.0)
            QSKIP("weeklyRisk is zero — cannot exercise High threshold");

        QCOMPARE(zf.alertLevel, 2);
    }

    // ── 4. testAlertLevelCritical ────────────────────────────────────────────
    // All thresholds 0 → any non-zero risk → Critical.

    void testAlertLevelCritical() {
        RiskForecaster rf;
        rf.setAlertThresholds(0.0, 0.0, 0.0);

        const QDateTime base(QDate(2024,1,1), QTime(0,0,0), Qt::UTC);
        rf.fit(makeZoneEvents("zone_cr", base, 60));
        const auto zf = rf.forecastZone("zone_cr", base.addDays(61));

        if (zf.weeklyRisk <= 0.0)
            QSKIP("weeklyRisk is zero — cannot exercise Critical threshold");

        QCOMPARE(zf.alertLevel, 3);
    }

    // ── 5. testAlertLabelStrings ─────────────────────────────────────────────
    // alertLabel() must return a non-empty string for every alert level 0–3.

    void testAlertLabelStrings() {
        ZoneForecast zf;
        for (int level = 0; level <= 3; ++level) {
            zf.alertLevel = level;
            QVERIFY2(!zf.alertLabel().isEmpty(),
                     qPrintable(QStringLiteral("alertLabel() empty for level %1").arg(level)));
        }
    }

    // ── 6. testForecastHorizonDays ───────────────────────────────────────────
    // RiskForecaster(7) produces exactly 7 ForecastDays per zone.

    void testForecastHorizonDays() {
        const QDateTime base(QDate(2024,1,1), QTime(0,0,0), Qt::UTC);
        RiskForecaster rf(7);
        rf.fit(makeZoneEvents("zone_h7", base, 60));
        const auto zf = rf.forecastZone("zone_h7", base.addDays(61));
        QCOMPARE(zf.days.size(), 7);
    }

    // ── 7. testForecastHorizonCustom ─────────────────────────────────────────
    // RiskForecaster(14) produces exactly 14 ForecastDays per zone.

    void testForecastHorizonCustom() {
        const QDateTime base(QDate(2024,1,1), QTime(0,0,0), Qt::UTC);
        RiskForecaster rf(14);
        rf.fit(makeZoneEvents("zone_h14", base, 60));
        const auto zf = rf.forecastZone("zone_h14", base.addDays(61));
        QCOMPARE(zf.days.size(), 14);
    }

    // ── 8. testForecastRiskScoreRange ────────────────────────────────────────
    // Every day's riskScore must lie in [0, 1].

    void testForecastRiskScoreRange() {
        const QDateTime base(QDate(2024,1,1), QTime(0,0,0), Qt::UTC);
        RiskForecaster rf(7);
        rf.fit(makeZoneEvents("zone_r", base, 60));
        const auto zf = rf.forecastZone("zone_r", base.addDays(61));

        for (const auto& day : zf.days) {
            QVERIFY2(day.riskScore >= 0.0,
                     qPrintable(QStringLiteral("riskScore %1 < 0").arg(day.riskScore)));
            QVERIFY2(day.riskScore <= 1.0,
                     qPrintable(QStringLiteral("riskScore %1 > 1").arg(day.riskScore)));
        }
    }

    // ── 9. testForecastExpectedCountPositive ─────────────────────────────────
    // expectedCount must be >= 0 for every forecast day.

    void testForecastExpectedCountPositive() {
        const QDateTime base(QDate(2024,1,1), QTime(0,0,0), Qt::UTC);
        RiskForecaster rf(7);
        rf.fit(makeZoneEvents("zone_ec", base, 60));
        const auto zf = rf.forecastZone("zone_ec", base.addDays(61));

        for (const auto& day : zf.days)
            QVERIFY2(day.expectedCount >= 0.0,
                     qPrintable(QStringLiteral("expectedCount %1 < 0").arg(day.expectedCount)));
    }

    // ── 10. testForecastZoneCount ────────────────────────────────────────────
    // After fitting 5 distinct zones, forecast() returns exactly 5 ZoneForecasts.

    void testForecastZoneCount() {
        const QDateTime base(QDate(2024,1,1), QTime(0,0,0), Qt::UTC);
        QVector<CrimeEvent> events;
        for (int z = 0; z < 5; ++z) {
            const QString zoneId = QStringLiteral("zone_%1").arg(z);
            events += makeZoneEvents(zoneId, base, 10,
                                     QStringLiteral("z%1_").arg(z));
        }
        RiskForecaster rf;
        rf.fit(events);
        QCOMPARE(rf.zoneCount(), 5);

        const auto forecasts = rf.forecast(base.addDays(15));
        QCOMPARE(forecasts.size(), 5);
    }

    // ── 11. testForecastFromDate ──────────────────────────────────────────────
    // Forecast from a specific QDateTime must produce days starting from that date.

    void testForecastFromDate() {
        const QDateTime base(QDate(2024,1,1), QTime(0,0,0), Qt::UTC);
        RiskForecaster rf(7);
        rf.fit(makeZoneEvents("zone_fd", base, 60));

        const QDateTime from(QDate(2024,5,1), QTime(0,0,0), Qt::UTC);
        const auto zf = rf.forecastZone("zone_fd", from);

        QCOMPARE(zf.days.size(), 7);
        QCOMPARE(zf.days.first().date, from.date());
        for (int i = 0; i < 7; ++i)
            QCOMPARE(zf.days[i].date, from.addDays(i).date());
    }

    // ── 12. testRecentActivityBoostsRisk ─────────────────────────────────────
    // A zone with crimes in the last 7 days must have escalationFactor > 1.0
    // on the first forecast day.

    void testRecentActivityBoostsRisk() {
        const QDateTime now(QDate(2024,4,1), QTime(0,0,0), Qt::UTC);
        const QDateTime histStart(QDate(2024,1,1), QTime(0,0,0), Qt::UTC);

        QVector<CrimeEvent> events;
        // Historical baseline
        for (int i = 0; i < 60; ++i)
            events.append(makeEvent(QStringLiteral("h%1").arg(i), "hot_zone",
                                    histStart.addDays(i)));
        // Recent crimes within last 7 days of now (→ within escalation window)
        for (int i = 0; i < 5; ++i)
            events.append(makeEvent(QStringLiteral("r%1").arg(i), "hot_zone",
                                    now.addDays(-i)));

        RiskForecaster rf;
        rf.fit(events);
        const auto zf = rf.forecastZone("hot_zone", now);

        QVERIFY(!zf.days.isEmpty());
        QVERIFY2(zf.days.first().escalationFactor > 1.0,
                 "Zone with crimes in last 7 days should have escalationFactor > 1.0");
    }

    // ── 13. testNoRecentActivityNormalRisk ───────────────────────────────────
    // A zone with no crimes within 14 days of the forecast date must have
    // escalationFactor == 1.0 (no recency boost).

    void testNoRecentActivityNormalRisk() {
        // All events end on Feb 1 2024.  Forecast from Jun 1 2024:
        // the shortest lag (Feb 1 → Jun 1) = 121 days > 14 → no escalation.
        const QDateTime trainStart(QDate(2023,12,2), QTime(0,0,0), Qt::UTC);
        QVector<CrimeEvent> events;
        for (int i = 0; i < 60; ++i)
            events.append(makeEvent(QStringLiteral("c%1").arg(i), "cold_zone",
                                    trainStart.addDays(i)));

        RiskForecaster rf;
        rf.fit(events);

        const QDateTime forecastFrom(QDate(2024,6,1), QTime(0,0,0), Qt::UTC);
        const auto zf = rf.forecastZone("cold_zone", forecastFrom);

        QVERIFY(!zf.days.isEmpty());
        QVERIFY2(qFuzzyCompare(zf.days.first().escalationFactor, 1.0),
                 qPrintable(QStringLiteral("Expected escalationFactor=1.0, got %1")
                            .arg(zf.days.first().escalationFactor)));
    }

    // ── 14. testSetAlertThresholds ───────────────────────────────────────────
    // Changing thresholds after fit alters subsequent alert levels.

    void testSetAlertThresholds() {
        const QDateTime base(QDate(2024,1,1), QTime(0,0,0), Qt::UTC);
        RiskForecaster rf;
        rf.fit(makeZoneEvents("zone_th", base, 60));

        // Push critical threshold to 0 → any non-zero risk → Critical
        rf.setAlertThresholds(0.0, 0.0, 0.0);
        const auto zfCritical = rf.forecastZone("zone_th", base.addDays(61));
        if (zfCritical.weeklyRisk > 0.0)
            QCOMPARE(zfCritical.alertLevel, 3);

        // Push all thresholds to 1 → always Normal
        rf.setAlertThresholds(1.0, 1.0, 1.0);
        const auto zfNormal = rf.forecastZone("zone_th", base.addDays(61));
        QCOMPARE(zfNormal.alertLevel, 0);
    }

    // ── 15. testWeeklyRiskAggregation ────────────────────────────────────────
    // weeklyRisk must equal the mean of all daily riskScores in the forecast
    // (the implementation divides sum by days.size()).

    void testWeeklyRiskAggregation() {
        const QDateTime base(QDate(2024,1,1), QTime(0,0,0), Qt::UTC);
        RiskForecaster rf(7);
        rf.fit(makeZoneEvents("zone_wa", base, 60));
        const auto zf = rf.forecastZone("zone_wa", base.addDays(61));

        QVERIFY(!zf.days.isEmpty());
        double sum = 0.0;
        for (const auto& day : zf.days)
            sum += day.riskScore;
        const double meanRisk = sum / zf.days.size();

        QVERIFY2(qAbs(zf.weeklyRisk - meanRisk) < 1e-9,
                 qPrintable(QStringLiteral("weeklyRisk=%1 but mean daily risk=%2")
                            .arg(zf.weeklyRisk).arg(meanRisk)));
    }
};

// ─── main ─────────────────────────────────────────────────────────────────────

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    TestRiskForecasterAlerts t;
    return QTest::qExec(&t, argc, argv);
}

#include "test_risk_forecaster_alerts.moc"
