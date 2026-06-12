// test_temporal_heatmap_deep4.cpp — Deep audit iteration 20: TemporalHeatmapWidget
// aggregation invariants, timestamp fallbacks, matrix/event transitions, paint.

#include <QTest>
#include <QApplication>
#include <QDateTime>
#include <array>

#include "ui/TemporalHeatmapWidget.h"
#include "core/CrimeEvent.h"

class TestTemporalHeatmapDeep4 : public QObject {
    Q_OBJECT

    static CrimeEvent makeEvent(const QDateTime& dt, const QString& id = QString())
    {
        CrimeEvent e;
        e.eventId    = id.isEmpty()
            ? QStringLiteral("deep4-%1").arg(dt.toSecsSinceEpoch())
            : id;
        e.timestamp  = dt;
        e.occurredAt = dt;
        e.crimeType  = QStringLiteral("burglary");
        return e;
    }

private slots:
    void initTestCase()
    {
        qputenv("QT_QPA_PLATFORM", "offscreen");
    }

    void testMatrixSetDataResetsMonthlyAggregates()
    {
        TemporalHeatmapWidget w;
        w.setData(QVector<CrimeEvent>{
            makeEvent(QDateTime(QDate(2024, 2, 5), QTime(9, 0), Qt::UTC)) });
        QVERIFY(w.monthlyData()[1] > 0);

        std::array<std::array<int, 24>, 7> zeros{};
        w.setData(zeros);

        int monthlySum = 0;
        for (int v : w.monthlyData())
            monthlySum += v;
        QCOMPARE(monthlySum, 0);
    }

    void testTimestampFallbackWhenOccurredAtUnset()
    {
        CrimeEvent viaTimestamp;
        viaTimestamp.eventId   = QStringLiteral("ts-only");
        viaTimestamp.timestamp = QDateTime(QDate(2024, 1, 3), QTime(16, 0), Qt::UTC);
        viaTimestamp.crimeType = QStringLiteral("theft");

        TemporalHeatmapWidget w;
        w.setData(QVector<CrimeEvent>{ viaTimestamp });

        QCOMPARE(w.hourlyData()[16], 1);
        QCOMPARE(w.dailyData()[2], 1);  // 2024-01-03 is Wednesday
    }

    void testMultipleEventsSameBinAccumulate()
    {
        const QDateTime dt(QDate(2024, 1, 1), QTime(22, 30), Qt::UTC);
        QVector<CrimeEvent> events;
        for (int i = 0; i < 5; ++i)
            events.append(makeEvent(dt, QStringLiteral("same-bin-%1").arg(i)));

        TemporalHeatmapWidget w;
        w.setData(events);

        QCOMPARE(w.hourlyData()[22], 5);
        QCOMPARE(w.dailyData()[0], 5);
    }

    void testSundayMapsToIndexSix()
    {
        // 2024-01-07 is a Sunday (dayOfWeek == 7 → index 6)
        const QDateTime sunday(QDate(2024, 1, 7), QTime(6, 0), Qt::UTC);
        QCOMPARE(sunday.date().dayOfWeek(), 7);

        TemporalHeatmapWidget w;
        w.setData(QVector<CrimeEvent>{ makeEvent(sunday) });

        QCOMPARE(w.dailyData()[6], 1);
        QCOMPARE(w.hourlyData()[6], 1);
    }

    void testHourlyAndDailyTotalsMatchEventCount()
    {
        QVector<CrimeEvent> events;
        const QDateTime base(QDate(2024, 4, 1), QTime(8, 0), Qt::UTC);
        for (int i = 0; i < 11; ++i)
            events.append(makeEvent(base.addDays(i % 7).addSecs(i * 1800)));

        TemporalHeatmapWidget w;
        w.setData(events);

        int hourlyTotal = 0;
        for (int v : w.hourlyData())
            hourlyTotal += v;
        int dailyTotal = 0;
        for (int v : w.dailyData())
            dailyTotal += v;

        QCOMPARE(hourlyTotal, static_cast<int>(events.size()));
        QCOMPARE(dailyTotal, static_cast<int>(events.size()));
    }

    void testMatrixPeakCellDrivesMaxCount()
    {
        std::array<std::array<int, 24>, 7> counts{};
        counts[4][18] = 42;
        counts[1][3]  = 7;

        TemporalHeatmapWidget w;
        w.setData(counts);

        QCOMPARE(w.hourlyData()[18], 42);
        QCOMPARE(w.dailyData()[4], 42);

        w.resize(720, 220);
        w.show();
        QApplication::processEvents();
        QVERIFY(!w.grab().isNull());
    }

    void testZeroSizePaintIsSafe()
    {
        TemporalHeatmapWidget w;
        w.resize(40, 20);
        w.show();
        QApplication::processEvents();

        std::array<std::array<int, 24>, 7> counts{};
        counts[0][0] = 3;
        w.setData(counts);
        w.resize(720, 220);
        QApplication::processEvents();

        QVERIFY(!w.grab().isNull());
    }

    void testEventsThenMatrixPreservesHourlyButClearsMonthly()
    {
        TemporalHeatmapWidget w;
        w.setData(QVector<CrimeEvent>{
            makeEvent(QDateTime(QDate(2024, 5, 10), QTime(12, 0), Qt::UTC)) });
        QVERIFY(w.monthlyData()[4] > 0);

        std::array<std::array<int, 24>, 7> matrix{};
        matrix[2][12] = 9;
        w.setData(matrix);

        QCOMPARE(w.hourlyData()[12], 9);
        QCOMPARE(w.dailyData()[2], 9);
        QCOMPARE(w.monthlyData()[4], 0);
    }
};

QTEST_MAIN(TestTemporalHeatmapDeep4)

#include "test_temporal_heatmap_deep4.moc"
