// test_temporal_heatmap_deep3.cpp — Deep audit iteration 17: TemporalHeatmapWidget
// construct, setData (matrix + events), aggregation edge cases.

#include <QTest>
#include <QApplication>
#include <QDateTime>
#include <array>

#include "ui/TemporalHeatmapWidget.h"
#include "core/CrimeEvent.h"

class TestTemporalHeatmapDeep3 : public QObject {
    Q_OBJECT

    static CrimeEvent makeEvent(const QDateTime& dt)
    {
        CrimeEvent e;
        e.eventId    = QStringLiteral("deep3-%1").arg(dt.toSecsSinceEpoch());
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

    void testConstructSizeHints()
    {
        TemporalHeatmapWidget w;
        QCOMPARE(w.sizeHint(), QSize(840, 220));
        QCOMPARE(w.minimumSizeHint(), QSize(480, 140));
    }

    void testSetDataMatrixSingleCell()
    {
        std::array<std::array<int, 24>, 7> counts{};
        counts[2][9] = 7;  // Wed 09:00

        TemporalHeatmapWidget w;
        w.setData(counts);

        QCOMPARE(w.hourlyData()[9], 7);
        QCOMPARE(w.dailyData()[2], 7);

        int total = 0;
        for (int v : w.hourlyData())
            total += v;
        QCOMPARE(total, 7);
    }

    void testSetDataMatrixEmptyZerosAggregates()
    {
        std::array<std::array<int, 24>, 7> zeros{};

        TemporalHeatmapWidget w;
        w.setData(zeros);

        for (int h : w.hourlyData())
            QCOMPARE(h, 0);
        for (int d : w.dailyData())
            QCOMPARE(d, 0);
    }

    void testSetDataEventsPopulatesMonthly()
    {
        QVector<CrimeEvent> events;
        events.append(makeEvent(QDateTime(QDate(2024, 3, 15), QTime(10, 0), Qt::UTC)));
        events.append(makeEvent(QDateTime(QDate(2024, 3, 20), QTime(11, 0), Qt::UTC)));
        events.append(makeEvent(QDateTime(QDate(2024, 6, 1), QTime(12, 0), Qt::UTC)));

        TemporalHeatmapWidget w;
        w.setData(events);

        const QVector<int> monthly = w.monthlyData();
        QCOMPARE(monthly.size(), 12);
        QCOMPARE(monthly[2], 2);  // March
        QCOMPARE(monthly[5], 1);  // June
    }

    void testSetDataMatrixAfterEventsLeavesStaleMonthly()
    {
        // BUG: matrix overload does not reset m_monthly from prior events load.
        QVector<CrimeEvent> events;
        events.append(makeEvent(QDateTime(QDate(2024, 1, 10), QTime(8, 0), Qt::UTC)));

        TemporalHeatmapWidget w;
        w.setData(events);
        QVERIFY(w.monthlyData()[0] > 0);

        std::array<std::array<int, 24>, 7> zeros{};
        w.setData(zeros);

        int monthlySum = 0;
        for (int v : w.monthlyData())
            monthlySum += v;

        QVERIFY2(monthlySum == 0,
                 "matrix setData should reset monthly aggregates; stale data remains");
    }

    void testSetDataSkipsInvalidTimestamps()
    {
        CrimeEvent invalid;
        invalid.eventId = QStringLiteral("invalid-ts");
        invalid.crimeType = QStringLiteral("theft");
        // occurredAt unset; timestamp default-invalid

        TemporalHeatmapWidget w;
        w.setData(QVector<CrimeEvent>{invalid});

        int total = 0;
        for (int v : w.hourlyData())
            total += v;
        for (int v : w.dailyData())
            total += v;

        // Invalid timestamps should not contribute; if they do, bins get spurious counts.
        QCOMPARE(total, 0);
    }

    void testPaintAfterResizeNoCrash()
    {
        TemporalHeatmapWidget w;
        w.resize(640, 200);
        w.show();
        QApplication::processEvents();

        const QDateTime dt(QDate(2024, 1, 1), QTime(14, 0), Qt::UTC);
        w.setData(QVector<CrimeEvent>{makeEvent(dt)});

        w.resize(800, 240);
        QApplication::processEvents();
        QVERIFY(true);
    }
};

QTEST_MAIN(TestTemporalHeatmapDeep3)

#include "test_temporal_heatmap_deep3.moc"
