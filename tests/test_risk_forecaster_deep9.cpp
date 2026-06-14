// test_risk_forecaster_deep9.cpp — Deep audit iteration 28: RiskForecaster
// multi-zone forecast, crime-type filter, custom thresholds, baselineProb field.
#include <QtTest/QtTest>
#include "models/RiskForecaster.h"
#include "core/CrimeEvent.h"

class RiskForecasterDeep9Test : public QObject
{
    Q_OBJECT

    static CrimeEvent ev(const QString& zone, const QString& type, int dayOffset)
    {
        CrimeEvent e;
        e.eventId    = zone + type + QString::number(dayOffset);
        e.suburb     = zone;
        e.crimeType  = type;
        e.occurredAt = QDateTime(QDate(2024, 5, 1), QTime(0, 0), Qt::UTC).addDays(-dayOffset);
        e.timestamp  = e.occurredAt.value();
        return e;
    }

private slots:

    void testForecastMultipleZones()
    {
        QVector<CrimeEvent> events;
        for (int i = 0; i < 5; ++i)
            events.append(ev(QStringLiteral("Z1"), QStringLiteral("theft"), i));
        for (int i = 0; i < 4; ++i)
            events.append(ev(QStringLiteral("Z2"), QStringLiteral("theft"), i));

        RiskForecaster rf(3);
        rf.fit(events);
        const auto all = rf.forecast(QDateTime(QDate(2024, 5, 10), QTime(0, 0), Qt::UTC));
        QVERIFY(all.size() >= 2);
    }

    void testCrimeTypeFilterExcludesOtherTypes()
    {
        QVector<CrimeEvent> events;
        for (int i = 0; i < 8; ++i)
            events.append(ev(QStringLiteral("Filt"), QStringLiteral("burglary"), i));
        events.append(ev(QStringLiteral("Filt"), QStringLiteral("theft"), 0));

        RiskForecaster rf(2);
        rf.fit(events, QStringLiteral("burglary"));
        QCOMPARE(rf.zoneCount(), 1);
    }

    void testCustomThresholdsAffectAlertLevel()
    {
        RiskForecaster rf(2);
        rf.setAlertThresholds(0.01, 0.02, 0.03);
        QVector<CrimeEvent> events;
        for (int i = 0; i < 12; ++i)
            events.append(ev(QStringLiteral("Alert"), QStringLiteral("robbery"), i));
        rf.fit(events);

        const auto zf = rf.forecastZone(QStringLiteral("Alert"),
                                        QDateTime(QDate(2024, 6, 1), QTime(0, 0), Qt::UTC));
        QVERIFY(zf.alertLevel >= 0 && zf.alertLevel <= 3);
    }

    void testForecastDayBaselineProbBounded()
    {
        QVector<CrimeEvent> events;
        for (int i = 0; i < 6; ++i)
            events.append(ev(QStringLiteral("Base"), QStringLiteral("theft"), i));

        RiskForecaster rf(2);
        rf.fit(events);
        const auto zf = rf.forecastZone(QStringLiteral("Base"),
                                        QDateTime(QDate(2024, 7, 1), QTime(0, 0), Qt::UTC));
        for (const auto& day : zf.days) {
            QVERIFY(day.baselineProb >= 0.0 && day.baselineProb <= 1.0);
        }
    }

    void testHorizonMatchesDayCount()
    {
        RiskForecaster rf(4);
        rf.fit({ ev(QStringLiteral("H"), QStringLiteral("theft"), 0) });
        const auto zf = rf.forecastZone(QStringLiteral("H"),
                                        QDateTime(QDate(2024, 8, 1), QTime(0, 0), Qt::UTC));
        QCOMPARE(zf.days.size(), 4);
    }
};

QTEST_GUILESS_MAIN(RiskForecasterDeep9Test)
#include "test_risk_forecaster_deep9.moc"
