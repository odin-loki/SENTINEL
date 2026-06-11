// test_temporal_heatmap_deep2.cpp — Deep audit of TemporalHeatmapWidget
// data model: setData(QVector<CrimeEvent>), hourlyData(), dailyData().
// Requires QApplication (widget test). Run with -platform offscreen.
#include <QTest>
#include <QApplication>
#include <QDateTime>
#include "ui/TemporalHeatmapWidget.h"
#include "core/CrimeEvent.h"

class TestTemporalHeatmapDeep2 : public QObject {
    Q_OBJECT

    static CrimeEvent makeEvent(const QDateTime& dt)
    {
        CrimeEvent e;
        e.eventId    = QStringLiteral("test");
        e.timestamp  = dt;
        e.occurredAt = dt;
        e.crimeType  = QStringLiteral("burglary");
        return e;
    }

private slots:

    void testSetDataEmptyVectorNoCrash()
    {
        TemporalHeatmapWidget w;
        w.setData(QVector<CrimeEvent>{});
        QVERIFY(true);
    }

    void testHourlyDataReturns24Elements()
    {
        TemporalHeatmapWidget w;
        w.setData(QVector<CrimeEvent>{});
        QCOMPARE(w.hourlyData().size(), 24);
    }

    void testDailyDataReturns7Elements()
    {
        TemporalHeatmapWidget w;
        w.setData(QVector<CrimeEvent>{});
        QCOMPARE(w.dailyData().size(), 7);
    }

    void testMonthlyDataReturns12Elements()
    {
        TemporalHeatmapWidget w;
        w.setData(QVector<CrimeEvent>{});
        QCOMPARE(w.monthlyData().size(), 12);
    }

    void testEventAtHour14MondayIncrementsCorrectBins()
    {
        // 2024-01-01 is a Monday (dayOfWeek() == 1)
        const QDateTime dt(QDate(2024, 1, 1), QTime(14, 0, 0), Qt::UTC);
        QCOMPARE(dt.date().dayOfWeek(), 1);  // sanity check: Monday

        TemporalHeatmapWidget w;
        w.setData(QVector<CrimeEvent>{makeEvent(dt)});

        const QVector<int> hourly = w.hourlyData();
        const QVector<int> daily  = w.dailyData();

        QVERIFY2(hourly[14] > 0,
                 "hour 14 bin must be incremented for an event at 14:00");
        QVERIFY2(daily[0] > 0,
                 "day 0 (Monday) bin must be incremented for a Monday event");
    }

    void testTotalCountMatchesEventCount()
    {
        QVector<CrimeEvent> events;
        const QDateTime base(QDate(2024, 1, 1), QTime(12, 0, 0), Qt::UTC);
        for (int i = 0; i < 10; ++i)
            events.append(makeEvent(base.addDays(i)));

        TemporalHeatmapWidget w;
        w.setData(events);

        int total = 0;
        for (int v : w.hourlyData())
            total += v;

        QCOMPARE(total, static_cast<int>(events.size()));
    }

    void testDailyTotalAlsoMatchesEventCount()
    {
        QVector<CrimeEvent> events;
        const QDateTime base(QDate(2024, 1, 1), QTime(8, 0, 0), Qt::UTC);
        for (int i = 0; i < 7; ++i)
            events.append(makeEvent(base.addDays(i)));

        TemporalHeatmapWidget w;
        w.setData(events);

        int total = 0;
        for (int v : w.dailyData())
            total += v;

        QCOMPARE(total, static_cast<int>(events.size()));
    }
};

QTEST_MAIN(TestTemporalHeatmapDeep2)
#include "test_temporal_heatmap_deep2.moc"
