// test_risk_forecaster_deep2.cpp — Deep audit of RiskForecaster horizon,
// peakDayIndex, weeklyRisk, alertLevel, and alertLabel.
#include <QTest>
#include <QtMath>
#include "models/RiskForecaster.h"
#include "core/CrimeEvent.h"

class TestRiskForecasterDeep2 : public QObject {
    Q_OBJECT

    static CrimeEvent makeEvent(const QString& id, const QString& suburb,
                                 const QDateTime& dt)
    {
        CrimeEvent e;
        e.eventId   = id;
        e.id        = id;
        e.suburb    = suburb;
        e.timestamp = dt;
        e.crimeType = QStringLiteral("burglary");
        return e;
    }

    static QVector<CrimeEvent> makeZoneEvents(const QString& zone,
                                               const QDateTime& base, int n)
    {
        QVector<CrimeEvent> events;
        events.reserve(n);
        for (int i = 0; i < n; ++i)
            events.append(makeEvent(QStringLiteral("e%1").arg(i), zone,
                                    base.addDays(i)));
        return events;
    }

private slots:

    void testForecastZoneReturnsHorizonDays()
    {
        const QDateTime base(QDate(2024, 1, 1), QTime(0, 0, 0), Qt::UTC);
        RiskForecaster rf(7);
        rf.fit(makeZoneEvents(QStringLiteral("zone_a"), base, 60));
        const auto zf = rf.forecastZone(QStringLiteral("zone_a"), base.addDays(61));
        QCOMPARE(zf.days.size(), 7);
    }

    void testForecastZoneReturnsExactCustomHorizon()
    {
        const QDateTime base(QDate(2024, 1, 1), QTime(0, 0, 0), Qt::UTC);
        for (int h : {1, 3, 14, 30}) {
            RiskForecaster rf(h);
            rf.fit(makeZoneEvents(QStringLiteral("zone_h%1").arg(h), base, 60));
            const auto zf = rf.forecastZone(
                QStringLiteral("zone_h%1").arg(h), base.addDays(61));
            QCOMPARE(zf.days.size(), h);
        }
    }

    void testPeakDayIndexInRange()
    {
        const QDateTime base(QDate(2024, 1, 1), QTime(0, 0, 0), Qt::UTC);
        RiskForecaster rf(7);
        rf.fit(makeZoneEvents(QStringLiteral("zone_b"), base, 60));
        const auto zf = rf.forecastZone(QStringLiteral("zone_b"), base.addDays(61));
        QVERIFY(!zf.days.isEmpty());
        QVERIFY(zf.peakDayIndex >= 0);
        QVERIFY(zf.peakDayIndex < zf.days.size());
    }

    void testWeeklyRiskEqualsMeanDailyRisk()
    {
        const QDateTime base(QDate(2024, 1, 1), QTime(0, 0, 0), Qt::UTC);
        RiskForecaster rf(7);
        rf.fit(makeZoneEvents(QStringLiteral("zone_c"), base, 60));
        const auto zf = rf.forecastZone(QStringLiteral("zone_c"), base.addDays(61));
        QVERIFY(!zf.days.isEmpty());

        double sum = 0.0;
        for (const auto& day : zf.days)
            sum += day.riskScore;
        const double mean = sum / zf.days.size();

        QVERIFY2(qAbs(zf.weeklyRisk - mean) < 1e-9,
                 qPrintable(QStringLiteral("weeklyRisk=%1, mean daily=%2")
                            .arg(zf.weeklyRisk).arg(mean)));
    }

    void testAlertLabelNormal()
    {
        ZoneForecast zf;
        zf.alertLevel = 0;
        QCOMPARE(zf.alertLabel(), QStringLiteral("NORMAL"));
    }

    void testAlertLabelElevated()
    {
        ZoneForecast zf;
        zf.alertLevel = 1;
        QCOMPARE(zf.alertLabel(), QStringLiteral("ELEVATED"));
    }

    void testAlertLabelHigh()
    {
        ZoneForecast zf;
        zf.alertLevel = 2;
        QCOMPARE(zf.alertLabel(), QStringLiteral("HIGH"));
    }

    void testAlertLabelCritical()
    {
        ZoneForecast zf;
        zf.alertLevel = 3;
        QCOMPARE(zf.alertLabel(), QStringLiteral("CRITICAL"));
    }

    void testAlertLevelZeroForLowRisk()
    {
        const QDateTime base(QDate(2024, 1, 1), QTime(0, 0, 0), Qt::UTC);
        RiskForecaster rf(7);
        rf.setAlertThresholds(0.99, 0.995, 0.999);
        rf.fit(makeZoneEvents(QStringLiteral("zone_low"), base, 60));
        const auto zf = rf.forecastZone(QStringLiteral("zone_low"), base.addDays(61));
        QCOMPARE(zf.alertLevel, 0);
    }

    void testAlertLevelCriticalForVeryHighRisk()
    {
        const QDateTime base(QDate(2024, 1, 1), QTime(0, 0, 0), Qt::UTC);
        RiskForecaster rf(7);
        rf.setAlertThresholds(0.0, 0.0, 0.0);
        rf.fit(makeZoneEvents(QStringLiteral("zone_cr"), base, 60));
        const auto zf = rf.forecastZone(QStringLiteral("zone_cr"), base.addDays(61));

        if (zf.weeklyRisk <= 0.0)
            QSKIP("weeklyRisk is zero -- cannot exercise Critical threshold");

        QCOMPARE(zf.alertLevel, 3);
    }
};

QTEST_GUILESS_MAIN(TestRiskForecasterDeep2)
#include "test_risk_forecaster_deep2.moc"