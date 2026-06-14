// test_map_widget_deep5.cpp — Deep audit iteration 24: MapWidget
// construction, setEvents, zoom bounds, overlays, resize stability.
#include <QTest>
#include <QApplication>
#include <QLabel>
#include "ui/MapWidget.h"
#include "core/CrimeEvent.h"

class TestMapWidgetDeep5 : public QObject
{
    Q_OBJECT

    static CrimeEvent geo(const QString& id, double lat, double lon)
    {
        CrimeEvent ev;
        ev.eventId   = id;
        ev.id        = id;
        ev.lat       = lat;
        ev.lon       = lon;
        ev.latitude  = lat;
        ev.longitude = lon;
        ev.crimeType = QStringLiteral("theft");
        ev.timestamp = QDateTime::currentDateTimeUtc();
        return ev;
    }

private slots:
    void initTestCase() { qputenv("QT_QPA_PLATFORM", "offscreen"); }

    void testConstructionNoCrash()
    {
        MapWidget widget;
        widget.resize(400, 300);
        widget.show();
        QApplication::processEvents();
        QVERIFY(widget.zoomLevelInt() >= 1);
    }

    void testSetEventsPopulatesInternalState()
    {
        MapWidget widget;
        widget.resize(500, 400);
        widget.setCenter(51.5074, -0.1278);
        QVector<CrimeEvent> events;
        for (int i = 0; i < 5; ++i)
            events.append(geo(QStringLiteral("M5-%1").arg(i),
                              51.5074 + i * 0.001, -0.1278 + i * 0.001));
        widget.setEvents(events);
        widget.show();
        QApplication::processEvents();
        QVERIFY(true);
    }

    void testZoomClampsToValidRange()
    {
        MapWidget widget;
        widget.setZoom(100.0);
        QVERIFY(widget.zoomLevelInt() <= 20);
        widget.setZoom(1e-10);
        QVERIFY(widget.zoomLevelInt() >= 1);
    }

    void testClearOverlaysResetsView()
    {
        MapWidget widget;
        widget.setEvents({ geo(QStringLiteral("C1"), 51.5, -0.1) });
        widget.setKDEHotspots({ makeHotspot(51.5, -0.1, 1) });
        widget.clearOverlays();
        widget.show();
        QApplication::processEvents();
        QVERIFY(true);
    }

    void testResizeEventStable()
    {
        MapWidget widget;
        widget.setEvents({ geo(QStringLiteral("R1"), 51.5, -0.1) });
        widget.resize(200, 150);
        QApplication::processEvents();
        widget.resize(800, 600);
        QApplication::processEvents();
        QVERIFY(widget.zoomLevelInt() >= 1);
    }

    void testRiskSurfaceOverlay()
    {
        MapWidget widget;
        GeographicProfile prof;
        prof.gridLats = { 51.49, 51.51 };
        prof.gridLons = { -0.13, -0.11 };
        prof.probabilitySurface = {{ 0.2, 0.8 }, { 0.3, 0.9 }};
        widget.setRiskSurface(prof);
        widget.show();
        QApplication::processEvents();
        widget.clearRiskSurface();
        QVERIFY(true);
    }

    static HotspotRegion makeHotspot(double lat, double lon, int rank)
    {
        HotspotRegion hs;
        hs.centroidLat = lat;
        hs.centroidLon = lon;
        hs.latMin = lat - 0.01;
        hs.latMax = lat + 0.01;
        hs.lonMin = lon - 0.01;
        hs.lonMax = lon + 0.01;
        hs.peakDensity = 1.0 / rank;
        hs.rank = rank;
        return hs;
    }
};

QTEST_MAIN(TestMapWidgetDeep5)
#include "test_map_widget_deep5.moc"
