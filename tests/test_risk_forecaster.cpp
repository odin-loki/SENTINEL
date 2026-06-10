// test_risk_forecaster.cpp — RiskForecaster unit tests
#include <QTest>
#include <QCoreApplication>
#include <QDateTime>
#include "models/RiskForecaster.h"
#include "core/CrimeEvent.h"

class TestRiskForecaster : public QObject {
    Q_OBJECT

    static CrimeEvent makeEvent(const QString& id, const QString& suburb,
                                 const QDateTime& dt,
                                 const QString& type = "burglary") {
        CrimeEvent e;
        e.eventId = e.id = id;
        e.suburb = suburb;
        e.timestamp = dt;
        e.crimeType = type;
        return e;
    }

    // 60 events over 60 days in "zone_A" (one per day)
    static QVector<CrimeEvent> regularEvents(const QDateTime& start, int n = 60) {
        QVector<CrimeEvent> events;
        for (int i = 0; i < n; ++i)
            events.append(makeEvent(QString("e%1").arg(i), "zone_A",
                                    start.addDays(i), "burglary"));
        return events;
    }

private slots:

    void testNotFittedBeforeFit() {
        RiskForecaster rf;
        QVERIFY(!rf.isFitted());
        QCOMPARE(rf.zoneCount(), 0);
    }

    void testFitSetsState() {
        RiskForecaster rf;
        const QDateTime start(QDate(2024,1,1), QTime(0,0,0), Qt::UTC);
        rf.fit(regularEvents(start));
        QVERIFY(rf.isFitted());
        QVERIFY(rf.zoneCount() > 0);
    }

    void testFitEmptyEvents() {
        RiskForecaster rf;
        rf.fit({});
        QVERIFY(true);   // must not crash
    }

    void testForecastProducesCorrectDayCount() {
        const int horizon = 7;
        RiskForecaster rf(horizon);
        const QDateTime start(QDate(2024,1,1), QTime(0,0,0), Qt::UTC);
        rf.fit(regularEvents(start));
        const auto zf = rf.forecastZone("zone_A", start.addDays(61));
        QCOMPARE(zf.days.size(), horizon);
    }

    void testForecastRiskInRange() {
        RiskForecaster rf;
        const QDateTime start(QDate(2024,1,1), QTime(0,0,0), Qt::UTC);
        rf.fit(regularEvents(start));
        const auto zf = rf.forecastZone("zone_A", start.addDays(61));
        for (const auto& day : zf.days) {
            QVERIFY(day.riskScore >= 0.0);
            QVERIFY(day.riskScore <= 1.0);
        }
    }

    void testWeeklyRiskNonNegative() {
        RiskForecaster rf;
        const QDateTime start(QDate(2024,1,1), QTime(0,0,0), Qt::UTC);
        rf.fit(regularEvents(start));
        const auto zf = rf.forecastZone("zone_A", start.addDays(61));
        QVERIFY(zf.weeklyRisk >= 0.0);
        QVERIFY(zf.weeklyRisk <= 1.0);
    }

    void testAlertLevelAssigned() {
        RiskForecaster rf;
        const QDateTime start(QDate(2024,1,1), QTime(0,0,0), Qt::UTC);
        rf.fit(regularEvents(start));
        const auto zf = rf.forecastZone("zone_A", start.addDays(61));
        QVERIFY(zf.alertLevel >= 0 && zf.alertLevel <= 3);
    }

    void testAlertLabelNonEmpty() {
        RiskForecaster rf;
        const QDateTime start(QDate(2024,1,1), QTime(0,0,0), Qt::UTC);
        rf.fit(regularEvents(start));
        const auto zf = rf.forecastZone("zone_A", start.addDays(61));
        QVERIFY(!zf.alertLabel().isEmpty());
    }

    void testAlertThresholds() {
        RiskForecaster rf;
        rf.setAlertThresholds(0.0, 0.0, 0.0);  // Everything should be CRITICAL
        const QDateTime start(QDate(2024,1,1), QTime(0,0,0), Qt::UTC);
        rf.fit(regularEvents(start));
        const auto zf = rf.forecastZone("zone_A", start.addDays(61));
        if (zf.weeklyRisk > 0.0) QCOMPARE(zf.alertLevel, 3);
    }

    void testForecastAllZones() {
        RiskForecaster rf;
        const QDateTime start(QDate(2024,1,1), QTime(0,0,0), Qt::UTC);
        QVector<CrimeEvent> events = regularEvents(start);
        // Add a second zone
        for (int i = 0; i < 30; ++i)
            events.append(makeEvent(QString("b%1").arg(i), "zone_B",
                                    start.addDays(i), "theft"));
        rf.fit(events);

        const auto all = rf.forecast(start.addDays(61));
        QVERIFY(all.size() >= 2);
    }

    void testForecastSortedByRiskDescending() {
        RiskForecaster rf;
        const QDateTime start(QDate(2024,1,1), QTime(0,0,0), Qt::UTC);
        QVector<CrimeEvent> events;
        for (int i = 0; i < 60; ++i)
            events.append(makeEvent(QString("a%1").arg(i), "zone_A",
                                    start.addDays(i)));
        for (int i = 0; i < 10; ++i)   // much lower rate
            events.append(makeEvent(QString("b%1").arg(i), "zone_B",
                                    start.addDays(i * 5)));
        rf.fit(events);

        const auto all = rf.forecast(start.addDays(61));
        for (int i = 0; i + 1 < all.size(); ++i)
            QVERIFY(all[i].weeklyRisk >= all[i+1].weeklyRisk);
    }

    void testEscalationBoostedByRecent() {
        // A zone with MANY recent events should have a higher risk
        // than a zone with equally-sized historical base but fewer recent events
        const QDateTime now(QDate(2024,3,1), QTime(0,0,0), Qt::UTC);
        const QDateTime oldStart(QDate(2024,1,1), QTime(0,0,0), Qt::UTC);

        QVector<CrimeEvent> events;
        // zone_hot: steady 60 days + 5 very recent (last 14 days)
        for (int i = 0; i < 60; ++i)
            events.append(makeEvent(QString("h%1").arg(i), "zone_hot",
                                    oldStart.addDays(i)));
        for (int i = 0; i < 5; ++i)
            events.append(makeEvent(QString("hr%1").arg(i), "zone_hot",
                                    now.addDays(-i)));

        // zone_cold: same 60-day history but no recent events
        for (int i = 0; i < 60; ++i)
            events.append(makeEvent(QString("c%1").arg(i), "zone_cold",
                                    oldStart.addDays(i)));

        RiskForecaster rf;
        rf.fit(events);
        const auto hotFc  = rf.forecastZone("zone_hot",  now);
        const auto coldFc = rf.forecastZone("zone_cold", now);

        // Hot zone should have higher escalation → higher risk
        const double hotRisk  = hotFc.days.empty()  ? 0.0 : hotFc.days.first().riskScore;
        const double coldRisk = coldFc.days.empty() ? 0.0 : coldFc.days.first().riskScore;
        QVERIFY(hotRisk >= coldRisk);
    }

    void testExplanationNonEmpty() {
        RiskForecaster rf;
        const QDateTime start(QDate(2024,1,1), QTime(0,0,0), Qt::UTC);
        rf.fit(regularEvents(start));
        for (const auto& day : rf.forecastZone("zone_A", start.addDays(61)).days)
            QVERIFY(!day.explanation.isEmpty());
    }

    void testUnknownZoneReturnsDefaultForecast() {
        RiskForecaster rf;
        const QDateTime start(QDate(2024,1,1), QTime(0,0,0), Qt::UTC);
        rf.fit(regularEvents(start));
        const auto zf = rf.forecastZone("nonexistent_zone", start.addDays(61));
        QCOMPARE(zf.zoneId, QStringLiteral("nonexistent_zone"));
        QCOMPARE(zf.days.size(), 7);
    }

    void testHorizonOneDay() {
        RiskForecaster rf(1);
        const QDateTime start(QDate(2024,1,1), QTime(0,0,0), Qt::UTC);
        rf.fit(regularEvents(start));
        const auto zf = rf.forecastZone("zone_A", start.addDays(61));
        QCOMPARE(zf.days.size(), 1);
    }

    void testBaselineProbInRange() {
        RiskForecaster rf;
        const QDateTime start(QDate(2024,1,1), QTime(0,0,0), Qt::UTC);
        rf.fit(regularEvents(start));
        for (const auto& day : rf.forecastZone("zone_A", start.addDays(61)).days) {
            QVERIFY(day.baselineProb >= 0.0 && day.baselineProb <= 1.0);
        }
    }

    void testExpectedCountNonNegative() {
        RiskForecaster rf;
        const QDateTime start(QDate(2024,1,1), QTime(0,0,0), Qt::UTC);
        rf.fit(regularEvents(start));
        for (const auto& day : rf.forecastZone("zone_A", start.addDays(61)).days) {
            QVERIFY(day.expectedCount >= 0.0);
        }
    }
};

// ─── main ─────────────────────────────────────────────────────────────────────

static int runTest(QObject* obj, const char* logFile) {
    QStringList args = { "test", "-o", QString("%1,txt").arg(logFile) };
    return QTest::qExec(obj, args);
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    int r = 0;
    TestRiskForecaster t1; r |= runTest(&t1, "risk_forecaster.txt");
    return r;
}

#include "test_risk_forecaster.moc"
