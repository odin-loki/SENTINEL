// test_risk_forecaster_bounds.cpp — Deep bounds / invariant tests for RiskForecaster
#include <QTest>
#include <QCoreApplication>
#include <QDateTime>
#include <cmath>
#include "models/RiskForecaster.h"
#include "core/CrimeEvent.h"
#include "core/AppConfig.h"

class TestRiskForecasterBounds : public QObject {
    Q_OBJECT

    static QVector<CrimeEvent> makeZoneEvents(const QString& zone, int count, int daysAgo = 60) {
        QVector<CrimeEvent> events;
        for (int i = 0; i < count; ++i) {
            CrimeEvent e;
            e.eventId = e.id = QString("E_%1_%2").arg(zone).arg(i);
            e.crimeType = "burglary";
            e.suburb = zone;
            e.lat = 51.5 + i * 0.001; e.lon = -0.1;
            e.latitude = *e.lat; e.longitude = *e.lon;
            const QDateTime dt = QDateTime::currentDateTimeUtc().addDays(-(daysAgo - i));
            e.occurredAt = dt;
            e.timestamp  = dt;
            e.qualityScore = 0.8;
            events.append(e);
        }
        return events;
    }

private slots:

    void testProbabilityInUnitInterval() {
        RiskForecaster rf(7);
        rf.fit(makeZoneEvents("Z1", 20, 60));
        const auto zf = rf.forecastZone("Z1", QDateTime::currentDateTimeUtc());
        QVERIFY(!zf.days.isEmpty());
        for (const auto& day : zf.days)
            QVERIFY2(day.riskScore >= 0.0 && day.riskScore <= 1.0,
                     qPrintable(QString("riskScore %1 outside [0,1]").arg(day.riskScore)));
    }

    void testForecastHorizonDayCount() {
        RiskForecaster rf(7);
        rf.fit(makeZoneEvents("Z2", 20, 60));
        const auto zf = rf.forecastZone("Z2", QDateTime::currentDateTimeUtc());
        QCOMPARE(zf.days.size(), 7);
    }

    void testRiskScoreNonNegative() {
        RiskForecaster rf(7);
        rf.fit(makeZoneEvents("Z3", 20, 60));
        const auto zf = rf.forecastZone("Z3", QDateTime::currentDateTimeUtc());
        for (const auto& day : zf.days)
            QVERIFY(day.riskScore >= 0.0);
    }

    void testWeeklyRiskInUnitInterval() {
        RiskForecaster rf(7);
        rf.fit(makeZoneEvents("Z4", 20, 60));
        const auto zf = rf.forecastZone("Z4", QDateTime::currentDateTimeUtc());
        QVERIFY(zf.weeklyRisk >= 0.0);
        QVERIFY(zf.weeklyRisk <= 1.0);
    }

    void testAlertLevelCategorical() {
        RiskForecaster rf(7);
        rf.fit(makeZoneEvents("Z5", 20, 60));
        const auto zf = rf.forecastZone("Z5", QDateTime::currentDateTimeUtc());
        QVERIFY2(zf.alertLevel >= 0 && zf.alertLevel <= 3,
                 qPrintable(QString("alertLevel=%1").arg(zf.alertLevel)));
    }

    void testAlertLabelNonEmpty() {
        RiskForecaster rf(7);
        rf.fit(makeZoneEvents("Z6", 20, 60));
        const auto zf = rf.forecastZone("Z6", QDateTime::currentDateTimeUtc());
        QVERIFY(!zf.alertLabel().isEmpty());
    }

    void testHighActivityZoneHigherRisk() {
        RiskForecaster rf(7);
        auto events = makeZoneEvents("ActiveZone", 50, 60);
        events     += makeZoneEvents("QuietZone",   5, 60);
        rf.fit(events);
        const QDateTime now = QDateTime::currentDateTimeUtc();
        const auto active = rf.forecastZone("ActiveZone", now);
        const auto quiet  = rf.forecastZone("QuietZone",  now);
        QVERIFY(active.weeklyRisk >= quiet.weeklyRisk);
    }

    void testEscalationFactorPositive() {
        RiskForecaster rf(7);
        rf.fit(makeZoneEvents("Z7", 20, 60));
        const auto zf = rf.forecastZone("Z7", QDateTime::currentDateTimeUtc());
        for (const auto& day : zf.days)
            QVERIFY(day.escalationFactor >= 0.0);
    }

    void testTemporalFactorPositive() {
        RiskForecaster rf(7);
        rf.fit(makeZoneEvents("Z8", 20, 60));
        const auto zf = rf.forecastZone("Z8", QDateTime::currentDateTimeUtc());
        for (const auto& day : zf.days)
            QVERIFY(day.temporalFactor >= 0.0);
    }

    void testExpectedCountNonNegative() {
        RiskForecaster rf(7);
        rf.fit(makeZoneEvents("Z9", 20, 60));
        const auto zf = rf.forecastZone("Z9", QDateTime::currentDateTimeUtc());
        for (const auto& day : zf.days)
            QVERIFY(day.expectedCount >= 0.0);
    }

    void testExplanationNonEmpty() {
        RiskForecaster rf(7);
        rf.fit(makeZoneEvents("Z10", 20, 60));
        const auto zf = rf.forecastZone("Z10", QDateTime::currentDateTimeUtc());
        for (const auto& day : zf.days)
            QVERIFY(!day.explanation.isEmpty());
    }

    void testForecastForEmptyZoneReturnsDefault() {
        RiskForecaster rf(7);
        rf.fit(makeZoneEvents("KnownZone", 20, 60));
        const auto zf = rf.forecastZone("UnknownEmptyZone", QDateTime::currentDateTimeUtc());
        for (const auto& day : zf.days)
            QVERIFY2(day.riskScore < 0.02,
                     qPrintable(QString("Expected <0.02, got %1").arg(day.riskScore)));
    }

    void testForecastReturnsAllZones() {
        RiskForecaster rf(7);
        auto events  = makeZoneEvents("Alpha", 15, 25);
        events      += makeZoneEvents("Beta",  15, 25);
        events      += makeZoneEvents("Gamma", 15, 25);
        rf.fit(events);
        const auto forecasts = rf.forecast(QDateTime::currentDateTimeUtc());
        QCOMPARE(forecasts.size(), 3);
    }

    void testAlertThresholdsFromConfig() {
        RiskForecaster rf(7);
        rf.fit(makeZoneEvents("Z11", 30, 30));
        const QDateTime now = QDateTime::currentDateTimeUtc();
        rf.setAlertThresholds(0.0, 0.0, 0.0);
        const auto zfCritical = rf.forecastZone("Z11", now);
        QCOMPARE(zfCritical.alertLevel, 3);
        rf.setAlertThresholds(1.5, 2.0, 3.0);
        const auto zfNormal = rf.forecastZone("Z11", now);
        QCOMPARE(zfNormal.alertLevel, 0);
    }

    void testMultiDayForecastVariation() {
        RiskForecaster rf(7);
        rf.fit(makeZoneEvents("Z12", 15, 20));
        const auto zf = rf.forecastZone("Z12", QDateTime::currentDateTimeUtc());
        QVERIFY(zf.days.size() == 7);
        bool anyVariation = false;
        for (int i = 1; i < zf.days.size(); ++i) {
            if (std::abs(zf.days[i].riskScore - zf.days[0].riskScore) > 1e-9) {
                anyVariation = true;
                break;
            }
        }
        QVERIFY2(anyVariation, "Expected risk to vary across the 7-day horizon");
    }
};

QTEST_MAIN(TestRiskForecasterBounds)
#include "test_risk_forecaster_bounds.moc"
