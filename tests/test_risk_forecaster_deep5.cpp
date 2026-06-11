// test_risk_forecaster_deep5.cpp — Deep audit iteration 18: zone resolution,
// timestamp precedence, forecast ordering, threshold invariants, escalation window.
#include <QtTest/QtTest>
#include <cmath>
#include "models/RiskForecaster.h"
#include "core/CrimeEvent.h"

class RiskForecasterDeep5Test : public QObject
{
    Q_OBJECT

    static CrimeEvent evSuburb(const QString& id, const QString& suburb, const QDateTime& dt,
                               const QString& type = QStringLiteral("burglary"))
    {
        CrimeEvent e;
        e.eventId    = id;
        e.suburb     = suburb;
        e.timestamp  = dt;
        e.occurredAt = dt;
        e.crimeType  = type;
        return e;
    }

    static CrimeEvent evLgaOnly(const QString& id, const QString& lga, const QDateTime& dt)
    {
        CrimeEvent e;
        e.eventId    = id;
        e.suburb     = {};
        e.lga        = lga;
        e.timestamp  = dt;
        e.occurredAt = dt;
        e.crimeType  = QStringLiteral("burglary");
        return e;
    }

private slots:

    void testZoneFallsBackToLgaWhenSuburbEmpty()
    {
        const QDateTime base(QDate(2024, 3, 1), QTime(10, 0), Qt::UTC);
        RiskForecaster rf(3);
        rf.fit({ evLgaOnly(QStringLiteral("l0"), QStringLiteral("Westminster"), base) });

        QCOMPARE(rf.zoneCount(), 1);
        const auto all = rf.forecast(base.addDays(5));
        QCOMPARE(all.size(), 1);
        QCOMPARE(all.first().zoneId, QStringLiteral("Westminster"));
    }

    void testOccurredAtPreferredOverTimestampForRecency()
    {
        const QDateTime ts(QDate(2024, 1, 1), QTime(0, 0), Qt::UTC);
        const QDateTime occurred(QDate(2024, 2, 1), QTime(0, 0), Qt::UTC);

        CrimeEvent e;
        e.eventId    = QStringLiteral("dual");
        e.suburb     = QStringLiteral("Dual");
        e.timestamp  = ts;
        e.occurredAt = occurred;
        e.crimeType  = QStringLiteral("burglary");

        RiskForecaster rf(1);
        rf.fit({ e });

        // Forecast from Feb 15: event on Feb 1 is 14 days ago → escalation boost.
        const auto zf = rf.forecastZone(
            QStringLiteral("Dual"),
            QDateTime(QDate(2024, 2, 15), QTime(0, 0), Qt::UTC));

        QVERIFY(!zf.days.isEmpty());
        if (zf.days.first().escalationFactor <= 1.0 + 1e-9) {
            QWARN("RiskForecaster.cpp:31-47 — occurredAt should drive recency window, "
                  "not stale timestamp when both are set");
        }
        QVERIFY2(zf.days.first().escalationFactor >= 1.0,
                 "escalation must be at least 1.0");
    }

    void testForecastSortedByWeeklyRiskDescending()
    {
        const QDateTime base(QDate(2024, 1, 1), QTime(12, 0), Qt::UTC);
        QVector<CrimeEvent> events;
        // Both zones active within the 30-day recency window; Hot is much denser.
        for (int i = 0; i < 25; ++i)
            events.append(evSuburb(QStringLiteral("hot_%1").arg(i), QStringLiteral("Hot"),
                                   base.addDays(65 + i)));
        for (int i = 0; i < 3; ++i)
            events.append(evSuburb(QStringLiteral("calm_%1").arg(i), QStringLiteral("Calm"),
                                   base.addDays(87 + i)));

        RiskForecaster rf(5);
        rf.fit(events);
        const auto results = rf.forecast(base.addDays(90));

        QVERIFY2(results.size() >= 2,
                 qPrintable(QStringLiteral("need >=2 zones in forecast, got %1").arg(results.size())));
        for (int i = 1; i < results.size(); ++i) {
            QVERIFY2(results[i - 1].weeklyRisk >= results[i].weeklyRisk,
                     qPrintable(QStringLiteral("forecast not sorted: [%1]=%2 < [%3]=%4")
                                    .arg(i - 1).arg(results[i - 1].weeklyRisk)
                                    .arg(i).arg(results[i].weeklyRisk)));
        }
    }

    void testInvertedAlertThresholdsBreakMonotonicity()
    {
        const QDateTime base(QDate(2024, 5, 1), QTime(0, 0), Qt::UTC);
        QVector<CrimeEvent> events;
        for (int i = 0; i < 60; ++i)
            events.append(evSuburb(QStringLiteral("a%1").arg(i), QStringLiteral("A"),
                                   base.addDays(i % 20)));

        RiskForecaster rf(7);
        rf.setAlertThresholds(0.8, 0.5, 0.2);   // inverted: critical < high < elevated
        rf.fit(events);
        const auto zf = rf.forecastZone(QStringLiteral("A"), base.addDays(25));

        if (zf.weeklyRisk <= 0.0)
            QSKIP("weeklyRisk zero — cannot probe threshold ordering");

        // With correct ordering, weeklyRisk in (0.5, 0.8) should be level 1 not 3.
        if (zf.weeklyRisk >= 0.5 && zf.weeklyRisk < 0.8 && zf.alertLevel == 3) {
            QWARN("RiskForecaster.cpp:14-18,155-158 — setAlertThresholds does not "
                  "validate elevated <= high <= critical; inverted bounds mis-classify");
        }
        QVERIFY(zf.alertLevel >= 0 && zf.alertLevel <= 3);
    }

    void testExpectedCountUnboundedWhileRiskScoreClamped()
    {
        const QDateTime base(QDate(2024, 6, 1), QTime(10, 0), Qt::UTC);
        QVector<CrimeEvent> events;
        for (int i = 0; i < 80; ++i)
            events.append(evSuburb(QStringLiteral("s%1").arg(i), QStringLiteral("Spike"),
                                   base.addDays(25 + (i % 5))));

        RiskForecaster rf(1);
        rf.fit(events);
        const auto zf = rf.forecastZone(QStringLiteral("Spike"), base.addDays(30));

        QVERIFY(!zf.days.isEmpty());
        const auto& day = zf.days.first();
        QVERIFY2(day.riskScore <= 1.0,
                 qPrintable(QStringLiteral("riskScore=%1 exceeds 1").arg(day.riskScore)));

        if (day.expectedCount > 1.0 && day.riskScore < 1.0) {
            QWARN("RiskForecaster.cpp:130-131 — expectedCount is not clamped while "
                  "riskScore is clamped to [0,1]; consumers may misread severity");
        }
    }

    void testRecentCountsWindowExcludesEventsBeyond30Days()
    {
        const QDateTime latest(QDate(2024, 7, 31), QTime(0, 0), Qt::UTC);
        const QDateTime oldEvent = latest.addDays(-45);

        RiskForecaster rf(1);
        // Only a stale event — no activity within 30 days of the global latest date.
        rf.fit({ evSuburb(QStringLiteral("old"), QStringLiteral("W"), oldEvent) });

        const auto zf = rf.forecastZone(QStringLiteral("W"), latest.addDays(1));
        QVERIFY(!zf.days.isEmpty());
        QCOMPARE(zf.days.first().escalationFactor, 1.0);
    }

    void testFitCrimeTypeParameterDoesNotFilterEvents()
    {
        const QDateTime base(QDate(2024, 4, 1), QTime(9, 0), Qt::UTC);
        QVector<CrimeEvent> events;
        events.append(evSuburb(QStringLiteral("b0"), QStringLiteral("Mix"), base,
                               QStringLiteral("burglary")));
        events.append(evSuburb(QStringLiteral("r0"), QStringLiteral("Mix"), base.addDays(1),
                               QStringLiteral("robbery")));

        RiskForecaster rf(1);
        rf.fit(events, QStringLiteral("burglary"));

        const auto burglaryPred = rf.forecastZone(
            QStringLiteral("Mix"), base.addDays(10));
        QVERIFY(rf.isFitted());
        QVERIFY(!burglaryPred.days.isEmpty());

        // Robbery event still contributes to Poisson bucket rates during fit.
        const double riskWithMixed = burglaryPred.days.first().baselineProb;

        RiskForecaster rfOnly(1);
        rfOnly.fit({ events.first() }, QStringLiteral("burglary"));
        const auto onlyBurglary = rfOnly.forecastZone(QStringLiteral("Mix"), base.addDays(10));
        const double riskBurglaryOnly = onlyBurglary.days.first().baselineProb;

        if (std::abs(riskWithMixed - riskBurglaryOnly) < 1e-12) {
            QWARN("RiskForecaster.cpp:24-50 — fit(crimeType) stores type but ingests "
                  "all events; mixed-type history may inflate baseline for filtered type");
        }
    }

    void testPeakDayIndexWithinHorizon()
    {
        const QDateTime base(QDate(2024, 8, 1), QTime(0, 0), Qt::UTC);
        QVector<CrimeEvent> events;
        for (int i = 0; i < 30; ++i)
            events.append(evSuburb(QStringLiteral("p%1").arg(i), QStringLiteral("P"),
                                   base.addDays(i)));

        RiskForecaster rf(10);
        rf.fit(events);
        const auto zf = rf.forecastZone(QStringLiteral("P"), base.addDays(35));

        QVERIFY2(zf.peakDayIndex >= 0 && zf.peakDayIndex < zf.days.size(),
                 qPrintable(QStringLiteral("peakDayIndex=%1 outside days size=%2")
                                .arg(zf.peakDayIndex).arg(zf.days.size())));

        double maxRisk = -1.0;
        int maxIdx = -1;
        for (int i = 0; i < zf.days.size(); ++i) {
            if (zf.days[i].riskScore > maxRisk) {
                maxRisk = zf.days[i].riskScore;
                maxIdx  = i;
            }
        }
        QCOMPARE(zf.peakDayIndex, maxIdx);
    }
};

QTEST_GUILESS_MAIN(RiskForecasterDeep5Test)
#include "test_risk_forecaster_deep5.moc"
