// test_temporal_heatmap_data.cpp — TemporalHeatmapWidget data-model tests
// Tests data/model logic of TemporalHeatmapWidget (no rendering assertions).
// Runs headless via -platform offscreen.

#include <QTest>
#include <QApplication>
#include <array>

#include "ui/TemporalHeatmapWidget.h"

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

static std::array<std::array<int,24>,7> makeZeroData()
{
    std::array<std::array<int,24>,7> d{};
    for (auto& row : d) row.fill(0);
    return d;
}

static std::array<std::array<int,24>,7> makeUniformData(int value)
{
    std::array<std::array<int,24>,7> d{};
    for (auto& row : d) row.fill(value);
    return d;
}

static std::array<std::array<int,24>,7> makePeakData(int day, int hour, int peak, int bg = 1)
{
    auto d = makeUniformData(bg);
    d[day][hour] = peak;
    return d;
}

// ─────────────────────────────────────────────────────────────────────────────

class TestTemporalHeatmapData : public QObject {
    Q_OBJECT

private slots:

    // 1. Widget constructs without crashing
    void testConstruction()
    {
        TemporalHeatmapWidget w;
        QVERIFY(true);
    }

    // 2. sizeHint returns the documented 840×220 default
    void testSizeHint()
    {
        TemporalHeatmapWidget w;
        QSize hint = w.sizeHint();
        QCOMPARE(hint.width(),  840);
        QCOMPARE(hint.height(), 220);
    }

    // 3. minimumSizeHint returns at least 480×140
    void testMinimumSizeHint()
    {
        TemporalHeatmapWidget w;
        QSize minHint = w.minimumSizeHint();
        QVERIFY(minHint.width()  >= 480);
        QVERIFY(minHint.height() >= 140);
    }

    // 4. setData() with all-zero matrix does not crash
    void testSetDataAllZeros()
    {
        TemporalHeatmapWidget w;
        w.setData(makeZeroData());
        QVERIFY(true);
    }

    // 5. setData() with uniform positive values does not crash
    void testSetDataUniform()
    {
        TemporalHeatmapWidget w;
        w.setData(makeUniformData(42));
        QVERIFY(true);
    }

    // 6. setData() can be called multiple times without crash
    void testSetDataMultipleTimes()
    {
        TemporalHeatmapWidget w;
        for (int v = 0; v <= 10; ++v)
            w.setData(makeUniformData(v * 10));
        QVERIFY(true);
    }

    // 7. setData() with a single peak cell does not crash
    void testSetDataWithPeak()
    {
        TemporalHeatmapWidget w;
        w.setData(makePeakData(0, 0,  999));
        w.setData(makePeakData(6, 23, 999));
        QVERIFY(true);
    }

    // 8. setData() with large values (stress) does not crash
    void testSetDataLargeValues()
    {
        TemporalHeatmapWidget w;
        w.setData(makeUniformData(std::numeric_limits<int>::max() / 2));
        QVERIFY(true);
    }

    // 9. Alternating zero→populated→zero calls remain stable
    void testSetDataAlternatingEmptyPopulated()
    {
        TemporalHeatmapWidget w;
        for (int i = 0; i < 5; ++i) {
            w.setData(makeZeroData());
            w.setData(makeUniformData(100));
        }
        QVERIFY(true);
    }

    // 10. sizeHint and minimumSizeHint are consistent (min ≤ hint)
    void testSizeHintConsistency()
    {
        TemporalHeatmapWidget w;
        QSize hint    = w.sizeHint();
        QSize minHint = w.minimumSizeHint();
        QVERIFY(minHint.width()  <= hint.width());
        QVERIFY(minHint.height() <= hint.height());
    }
};

// ─────────────────────────────────────────────────────────────────────────────

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    app.setAttribute(Qt::AA_Use96Dpi);
    TestTemporalHeatmapData tc;
    return QTest::qExec(&tc, argc, argv);
}

#include "test_temporal_heatmap_data.moc"
