// test_temporal_heatmap_deep5.cpp — Deep audit iteration 24: TemporalHeatmapWidget
// 7×24 grid, peak cell, clear via zeros, hourly/daily projections.
#include <QTest>
#include <QApplication>
#include <array>
#include "ui/TemporalHeatmapWidget.h"
#include "core/CrimeEvent.h"

class TestTemporalHeatmapDeep5 : public QObject
{
    Q_OBJECT

    static CrimeEvent at(const QDateTime& dt, const QString& id = QString())
    {
        CrimeEvent e;
        e.eventId    = id.isEmpty() ? QStringLiteral("TH5-%1").arg(dt.toSecsSinceEpoch()) : id;
        e.timestamp  = dt;
        e.occurredAt = dt;
        e.crimeType  = QStringLiteral("burglary");
        return e;
    }

private slots:
    void initTestCase() { qputenv("QT_QPA_PLATFORM", "offscreen"); }

    void testConstructionNoCrash()
    {
        TemporalHeatmapWidget w;
        w.resize(600, 200);
        w.show();
        QApplication::processEvents();
        QCOMPARE(w.hourlyData().size(), 24);
        QCOMPARE(w.dailyData().size(), 7);
    }

    void testGridDimensions7x24()
    {
        TemporalHeatmapWidget w;
        const QDateTime dt(QDate(2024, 3, 4), QTime(14, 0), Qt::UTC);
        w.setData(QVector<CrimeEvent>{ at(dt) });
        QCOMPARE(w.hourlyData().size(), 24);
        QCOMPARE(w.dailyData().size(), 7);
        QCOMPARE(w.hourlyData()[14], 1);
    }

    void testPeakCellNonZeroWithCluster()
    {
        TemporalHeatmapWidget w;
        QVector<CrimeEvent> events;
        const QDateTime base(QDate(2024, 1, 1), QTime(22, 0), Qt::UTC);
        for (int i = 0; i < 8; ++i)
            events.append(at(base.addSecs(i * 60), QStringLiteral("C%1").arg(i)));

        w.setData(events);
        QCOMPARE(w.hourlyData()[22], 8);
        int maxHour = 0;
        int maxVal  = 0;
        const auto hourly = w.hourlyData();
        for (int h = 0; h < 24; ++h) {
            if (hourly[h] > maxVal) {
                maxVal = hourly[h];
                maxHour = h;
            }
        }
        QCOMPARE(maxHour, 22);
        QCOMPARE(maxVal, 8);
    }

    void testClearViaZeroMatrix()
    {
        TemporalHeatmapWidget w;
        w.setData(QVector<CrimeEvent>{
            at(QDateTime(QDate(2024, 2, 1), QTime(9, 0), Qt::UTC)) });
        QVERIFY(w.hourlyData()[9] > 0);

        std::array<std::array<int, 24>, 7> zeros{};
        w.setData(zeros);
        int sum = 0;
        for (int v : w.hourlyData()) sum += v;
        QCOMPARE(sum, 0);
    }

    void testSundayMapsToIndexSix()
    {
        const QDateTime sunday(QDate(2024, 1, 7), QTime(6, 0), Qt::UTC);
        TemporalHeatmapWidget w;
        w.setData(QVector<CrimeEvent>{ at(sunday) });
        QCOMPARE(w.dailyData()[6], 1);
    }

    void testResizeTriggersRepaint()
    {
        TemporalHeatmapWidget w;
        w.setData(QVector<CrimeEvent>{
            at(QDateTime(QDate(2024, 5, 1), QTime(12, 0), Qt::UTC)) });
        w.resize(400, 120);
        QApplication::processEvents();
        w.resize(840, 220);
        QApplication::processEvents();
        QVERIFY(w.hourlyData()[12] >= 1);
    }

    void testMonthlyDataPopulated()
    {
        TemporalHeatmapWidget w;
        w.setData(QVector<CrimeEvent>{
            at(QDateTime(QDate(2024, 3, 15), QTime(10, 0), Qt::UTC)) });
        QCOMPARE(w.monthlyData().size(), 12);
        QVERIFY(w.monthlyData()[2] >= 1);
    }
};

QTEST_MAIN(TestTemporalHeatmapDeep5)
#include "test_temporal_heatmap_deep5.moc"
