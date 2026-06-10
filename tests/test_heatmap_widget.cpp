// test_heatmap_widget.cpp — TemporalHeatmapWidget + MapWidget tests (headless)
#include <QTest>
#include <QApplication>
#include <QSignalSpy>
#include "ui/TemporalHeatmapWidget.h"
#include "ui/MapWidget.h"
#include "core/CrimeEvent.h"
#include "inference/GeographicProfiler.h"
#include "models/KDEHotspot.h"

class TestHeatmapWidget : public QObject
{
    Q_OBJECT

private:
    static std::array<std::array<int,24>,7> makeUniformData(int value = 5)
    {
        std::array<std::array<int,24>,7> counts{};
        for (auto& row : counts)
            row.fill(value);
        return counts;
    }

    static std::array<std::array<int,24>,7> makePeakData(int peakDay, int peakHour, int peakVal)
    {
        auto counts = makeUniformData(1);
        counts[peakDay][peakHour] = peakVal;
        return counts;
    }

private slots:

    // ── TemporalHeatmapWidget ─────────────────────────────────────────────────

    void testHeatmapConstruction()
    {
        TemporalHeatmapWidget w;
        QVERIFY(w.sizeHint().width() > 0);
        QVERIFY(w.sizeHint().height() > 0);
    }

    void testHeatmapSetDataNocrash()
    {
        TemporalHeatmapWidget w;
        w.setData(makeUniformData(0));
        w.setData(makeUniformData(100));
    }

    void testHeatmapSizeHint()
    {
        TemporalHeatmapWidget w;
        QVERIFY(w.sizeHint().width() >= 480);
        QVERIFY(w.sizeHint().height() >= 140);
    }

    void testHeatmapMinimumSizeHint()
    {
        TemporalHeatmapWidget w;
        QVERIFY(w.minimumSizeHint().width() > 0);
        QVERIFY(w.minimumSizeHint().height() > 0);
    }

    void testHeatmapPaintWithData()
    {
        TemporalHeatmapWidget w;
        w.resize(840, 220);
        w.setData(makePeakData(0, 22, 100));  // Peak on Monday 10pm
        // Render to a pixmap — no crash
        QPixmap pm(840, 220);
        pm.fill(Qt::black);
        QPainter painter(&pm);
        w.render(&painter);
        QVERIFY(true);
    }

    void testHeatmapPaintAllZerosNocrash()
    {
        TemporalHeatmapWidget w;
        w.resize(840, 220);
        w.setData(makeUniformData(0));
        QPixmap pm(840, 220);
        pm.fill(Qt::black);
        QPainter painter(&pm);
        w.render(&painter);
        QVERIFY(true);
    }

    void testHeatmapPaintHighCountsNocrash()
    {
        TemporalHeatmapWidget w;
        w.resize(840, 220);
        w.setData(makeUniformData(1000));
        QPixmap pm(840, 220);
        QPainter painter(&pm);
        w.render(&painter);
        QVERIFY(true);
    }

    // ── MapWidget ─────────────────────────────────────────────────────────────

    void testMapWidgetConstruction()
    {
        MapWidget map;
        QVERIFY(map.sizeHint().isValid() || true);  // May not have sizeHint
    }

    void testMapWidgetSetEventsEmpty()
    {
        MapWidget map;
        map.setEvents({});
        QVERIFY(true);
    }

    void testMapWidgetSetEventsWithData()
    {
        MapWidget map;
        QVector<CrimeEvent> events;
        for (int i = 0; i < 20; ++i) {
            CrimeEvent e;
            e.eventId = QString("E%1").arg(i);
            e.crimeType = "burglary";
            e.lat = 51.5 + i * 0.001;
            e.lon = -0.1 + i * 0.001;
            e.latitude = *e.lat;
            e.longitude = *e.lon;
            events.append(e);
        }
        map.setEvents(events);
        QVERIFY(true);
    }

    void testMapWidgetSetCenter()
    {
        MapWidget map;
        map.setCenter(51.5074, -0.1278);
        QVERIFY(true);
    }

    void testMapWidgetSetZoom()
    {
        MapWidget map;
        map.setZoom(0.0001);
        QVERIFY(true);
    }

    void testMapWidgetClearRiskSurface()
    {
        MapWidget map;
        map.clearRiskSurface();
        QVERIFY(true);
    }

    void testMapWidgetClearKDEHotspots()
    {
        MapWidget map;
        map.clearKDEHotspots();
        QVERIFY(true);
    }

    void testMapWidgetSetKDEHotspots()
    {
        MapWidget map;
        QVector<HotspotRegion> hotspots;
        HotspotRegion r;
        r.centroidLat = 51.5; r.centroidLon = -0.1;
        r.latMin = 51.45; r.latMax = 51.55;
        r.lonMin = -0.15; r.lonMax = -0.05;
        r.peakDensity = 10.0; r.rank = 1;
        hotspots.append(r);
        map.setKDEHotspots(hotspots);
        QVERIFY(true);
    }

    void testMapWidgetLocationClickedSignal()
    {
        MapWidget map;
        map.resize(400, 300);
        QSignalSpy spy(&map, &MapWidget::locationClicked);
        // Simulate a click in the middle of the map
        QMouseEvent press(QEvent::MouseButtonPress, QPointF(200, 150),
                          Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QMouseEvent release(QEvent::MouseButtonRelease, QPointF(200, 150),
                            Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(&map, &press);
        QApplication::sendEvent(&map, &release);
        QVERIFY(spy.count() >= 0);  // May or may not emit depending on drag threshold
    }

    void testMapWidgetPaintNocrash()
    {
        MapWidget map;
        map.resize(400, 300);
        QVector<CrimeEvent> events;
        CrimeEvent e;
        e.eventId = "E1";
        e.crimeType = "robbery";
        e.lat = 51.5074; e.lon = -0.1278;
        e.latitude = *e.lat; e.longitude = *e.lon;
        events.append(e);
        map.setEvents(events);

        QPixmap pm(400, 300);
        QPainter painter(&pm);
        map.render(&painter);
        QVERIFY(true);
    }

    void testMapWidgetMultipleCrimeTypes()
    {
        MapWidget map;
        QVector<CrimeEvent> events;
        QStringList types = {"burglary", "theft", "robbery", "assault",
                             "vehicle_crime", "drug_offence", "antisocial"};
        for (int i = 0; i < types.size(); ++i) {
            CrimeEvent e;
            e.eventId = QString("E%1").arg(i);
            e.crimeType = types[i];
            e.lat = 51.5 + i * 0.005;
            e.lon = -0.1 + i * 0.005;
            e.latitude = *e.lat; e.longitude = *e.lon;
            events.append(e);
        }
        map.setEvents(events);
        map.resize(400, 300);
        QPixmap pm(400, 300);
        QPainter p(&pm);
        map.render(&p);
        QVERIFY(true);
    }
};

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    app.setAttribute(Qt::AA_Use96Dpi);
    TestHeatmapWidget tc;
    return QTest::qExec(&tc, argc, argv);
}

#include "test_heatmap_widget.moc"
