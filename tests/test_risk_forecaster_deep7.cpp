// test_risk_forecaster_deep7.cpp — Deep audit iteration 24: RiskForecaster
// alert tier boundaries, weeklyRisk, horizon clamp, crimeType filter, day probs.
#include <QtTest/QtTest>
#include <cmath>
#include "models/RiskForecaster.h"
#include "core/CrimeEvent.h"

class RiskForecasterDeep7Test : public QObject
{
    Q_OBJECT

    static CrimeEvent ev(const QString& zone, const QDateTime& dt,
                         const QString& type = QStringLiteral("burglary"))
    {
        CrimeEvent e;
        e.eventId    = zone + dt.toString(QStringLiteral("yyyyMMdd"));
        e.suburb     = zone;
        e.timestamp  = dt;
        e.occurredAt = dt;
        e.crimeType  = type;
        return e;
    }

private slots:

    void testAlertThresholdsDriveAlertLevel()
    {
        RiskForecaster rf(3);
        rf.setAlertThresholds(0.2, 0.4, 0.6);

        const QDateTime base(QDate(2024, 4, 1), QTime(10, 0), Qt::UTC);
        QVector<CrimeEvent> events;
        for (int i = 0; i < 30; ++i)
            events.append(ev(QStringLiteral("Hot"), base.addDays(-i)));

        rf.fit(events);
        QVERIFY(rf.isFitted());

        const auto forecasts = rf.forecast(base.addDays(1));
        QVERIFY(!forecasts.isEmpty());
        for (const auto& zf : forecasts) {
            QVERIFY(zf.alertLevel >= 0 && zf.alertLevel <= 3);
            if (zf.weeklyRisk >= 0.6)
                QCOMPARE(zf.alertLevel, 3);
            else if (zf.weeklyRisk >= 0.4)
                QVERIFY(zf.alertLevel >= 2);
        }
    }

    void testWeeklyRiskIsMeanOfDailyScores()
    {
        const QDateTime base(QDate(2024, 5, 1), QTime(0, 0), Qt::UTC);
        RiskForecaster rf(3);
        rf.fit({ ev(QStringLiteral("Avg"), base.addDays(-5)) });

        const auto zf = rf.forecastZone(QStringLiteral("Avg"), base);
        QCOMPARE(zf.days.size(), 3);
        QVERIFY(!zf.days.isEmpty());

        double sum = 0.0;
        for (const auto& d : zf.days)
            sum += d.riskScore;
        const double mean = sum / zf.days.size();
        QVERIFY2(std::abs(zf.weeklyRisk - mean) < 1e-6,
                 qPrintable(QStringLiteral("weeklyRisk=%1 mean=%2")
                                .arg(zf.weeklyRisk).arg(mean)));
    }

    void testHorizonZeroClampedToOneDay()
    {
        RiskForecaster rf(0);
        const QDateTime base(QDate(2024, 6, 1), QTime(0, 0), Qt::UTC);
        rf.fit({ ev(QStringLiteral("H0"), base) });
        const auto zf = rf.forecastZone(QStringLiteral("H0"), base);
        QCOMPARE(zf.days.size(), 1);
    }

    void testCrimeTypeFilterExcludesOtherTypes()
    {
        const QDateTime base(QDate(2024, 7, 1), QTime(12, 0), Qt::UTC);
        QVector<CrimeEvent> events;
        for (int i = 0; i < 10; ++i)
            events.append(ev(QStringLiteral("Z"), base.addDays(-i), QStringLiteral("theft")));
        for (int i = 0; i < 20; ++i)
            events.append(ev(QStringLiteral("Z"), base.addDays(-i), QStringLiteral("assault")));

        RiskForecaster rf(2);
        rf.fit(events, QStringLiteral("theft"));
        QVERIFY(rf.isFitted());
        QCOMPARE(rf.zoneCount(), 1);
    }

    void testIsFittedFalseBeforeFit()
    {
        RiskForecaster rf(5);
        QVERIFY(!rf.isFitted());
        QCOMPARE(rf.zoneCount(), 0);
    }

    void testDayProbabilitiesInUnitInterval()
    {
        const QDateTime base(QDate(2024, 8, 1), QTime(8, 0), Qt::UTC);
        QVector<CrimeEvent> events;
        for (int i = 0; i < 15; ++i)
            events.append(ev(QStringLiteral("Prob"), base.addDays(-i)));

        RiskForecaster rf(5);
        rf.fit(events);
        const auto zf = rf.forecastZone(QStringLiteral("Prob"), base.addDays(2));

        for (const auto& day : zf.days) {
            QVERIFY2(day.riskScore >= 0.0 && day.riskScore <= 1.0,
                     qPrintable(QStringLiteral("riskScore=%1").arg(day.riskScore)));
            QVERIFY2(day.baselineProb >= 0.0 && day.baselineProb <= 1.0,
                     qPrintable(QStringLiteral("baselineProb=%1").arg(day.baselineProb)));
        }
    }

    void testPeakDayIndexWithinBounds()
    {
        const QDateTime base(QDate(2024, 9, 1), QTime(0, 0), Qt::UTC);
        RiskForecaster rf(4);
        rf.fit({ ev(QStringLiteral("Peak"), base.addDays(-3)) });
        const auto zf = rf.forecastZone(QStringLiteral("Peak"), base);
        QVERIFY(zf.peakDayIndex >= 0);
        QVERIFY(zf.peakDayIndex < zf.days.size());
    }
};

QTEST_GUILESS_MAIN(RiskForecasterDeep7Test)
#include "test_risk_forecaster_deep7.moc"
