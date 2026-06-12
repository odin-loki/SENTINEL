// test_map_widget_deep4.cpp — Deep audit iteration 20: MapWidget zoom/wheel,
// risk surface & KDE overlays, click-vs-drag signals, overlay lifecycle gaps.

#include <QTest>
#include <QApplication>
#include <QLabel>
#include <QSignalSpy>
#include <QWheelEvent>
#include <QVector>
#include <cmath>

#include "ui/MapWidget.h"
#include "core/CrimeEvent.h"

class TestMapWidgetDeep4 : public QObject {
    Q_OBJECT

private:
    static CrimeEvent makeGeoEvent(const QString& id, const QString& type,
                                   double lat, double lon)
    {
        CrimeEvent ev;
        ev.eventId   = id;
        ev.id        = id;
        ev.crimeType = type;
        ev.lat       = lat;
        ev.lon       = lon;
        ev.latitude  = lat;
        ev.longitude = lon;
        ev.timestamp = QDateTime::currentDateTimeUtc();
        return ev;
    }

    static GeographicProfile makeRiskProfile(double centerLat, double centerLon)
    {
        GeographicProfile prof;
        prof.gridLats = { centerLat - 0.01, centerLat + 0.01 };
        prof.gridLons = { centerLon - 0.01, centerLon + 0.01 };
        prof.probabilitySurface = {
            { 0.2, 0.8 },
            { 0.3, 0.9 },
        };
        return prof;
    }

    static HotspotRegion makeHotspot(double lat, double lon, int rank)
    {
        HotspotRegion hs;
        hs.centroidLat = lat;
        hs.centroidLon = lon;
        hs.latMin      = lat - 0.02;
        hs.latMax      = lat + 0.02;
        hs.lonMin      = lon - 0.02;
        hs.lonMax      = lon + 0.02;
        hs.peakDensity = 1.0 / rank;
        hs.rank        = rank;
        return hs;
    }

private slots:
    void initTestCase()
    {
        qputenv("QT_QPA_PLATFORM", "offscreen");
    }

    void testSetZoomClampsAndUpdatesLabel()
    {
        MapWidget widget;
        widget.resize(640, 480);
        widget.show();
        QApplication::processEvents();

        QSignalSpy spy(&widget, &MapWidget::zoomChanged);

        widget.setZoom(2.0);
        QCOMPARE(widget.zoomLevelInt(), 1);

        widget.setZoom(1e-8);
        QVERIFY(widget.zoomLevelInt() >= 1);
        QVERIFY(widget.zoomLevelInt() <= 20);

        auto* zoomLbl = widget.findChild<QLabel*>();
        QVERIFY(zoomLbl != nullptr);
        QVERIFY(zoomLbl->text().contains(QString::number(widget.zoomLevelInt())));
        QVERIFY(spy.count() >= 2);
    }

    void testWheelZoomInChangesZoomLevel()
    {
        MapWidget widget;
        widget.resize(640, 480);
        widget.show();
        QApplication::processEvents();

        const int before = widget.zoomLevelInt();

        for (int i = 0; i < 6; ++i) {
            QWheelEvent wheel(
                widget.rect().center(),
                widget.mapToGlobal(widget.rect().center()),
                QPoint(0, 0),
                QPoint(0, 120),
                Qt::NoButton,
                Qt::NoModifier,
                Qt::ScrollPhase::ScrollUpdate,
                false);
            QApplication::sendEvent(&widget, &wheel);
        }
        QApplication::processEvents();

        QVERIFY2(widget.zoomLevelInt() > before,
                 "wheel scroll up should increase zoom level after repeated steps");
    }

    void testClickWithoutDragEmitsLocationSignals()
    {
        MapWidget widget;
        widget.resize(640, 480);
        widget.setCenter(51.5074, -0.1278);
        widget.show();
        QApplication::processEvents();

        QSignalSpy clickSpy(&widget, &MapWidget::locationClicked);
        QSignalSpy regionSpy(&widget, &MapWidget::regionClicked);

        const QPoint center = widget.rect().center();
        QTest::mousePress(&widget, Qt::LeftButton, {}, center);
        QTest::mouseRelease(&widget, Qt::LeftButton, {}, center);
        QApplication::processEvents();

        QCOMPARE(clickSpy.count(), 1);
        QCOMPARE(regionSpy.count(), 1);
        QVERIFY(std::isfinite(clickSpy.at(0).at(0).toDouble()));
        QVERIFY(std::isfinite(clickSpy.at(0).at(1).toDouble()));
    }

    void testDragSuppressesClickSignals()
    {
        MapWidget widget;
        widget.resize(640, 480);
        widget.setCenter(51.5074, -0.1278);
        widget.show();
        QApplication::processEvents();

        QSignalSpy clickSpy(&widget, &MapWidget::locationClicked);

        const QPoint start(120, 120);
        const QPoint end(220, 180);
        QTest::mousePress(&widget, Qt::LeftButton, {}, start);
        QTest::mouseMove(&widget, end);
        QTest::mouseRelease(&widget, Qt::LeftButton, {}, end);
        QApplication::processEvents();

        QCOMPARE(clickSpy.count(), 0);
    }

    void testRiskSurfacePaintAndClear()
    {
        MapWidget widget;
        widget.resize(640, 480);
        widget.setCenter(51.5074, -0.1278);

        widget.setRiskSurface(makeRiskProfile(51.5074, -0.1278));
        QApplication::processEvents();
        const QPixmap withRisk = widget.grab();
        QVERIFY(!withRisk.isNull());

        widget.clearRiskSurface();
        QApplication::processEvents();
        const QPixmap cleared = widget.grab();
        QVERIFY(!cleared.isNull());
        QVERIFY(withRisk.toImage() != cleared.toImage());
    }

    void testKdeHotspotsAndSetHotspotsAlias()
    {
        MapWidget widget;
        widget.resize(640, 480);
        widget.setCenter(51.5074, -0.1278);

        QVector<HotspotRegion> hotspots;
        hotspots.append(makeHotspot(51.5074, -0.1278, 1));
        hotspots.append(makeHotspot(51.5174, -0.1178, 2));

        widget.setHotspots(hotspots);
        QApplication::processEvents();
        const QPixmap withHotspots = widget.grab();

        widget.clearKDEHotspots();
        QApplication::processEvents();
        const QPixmap cleared = widget.grab();

        QVERIFY(!withHotspots.isNull());
        QVERIFY(withHotspots.toImage() != cleared.toImage());
    }

    void testSetEventsEmptyLeavesKdeHotspots()
    {
        // BUG: setEvents({}) clears event dots but does not clear KDE hotspots.
        // AnalyticsWidget::refreshMapView relies on setEvents alone when the DB
        // filter returns zero rows, leaving stale hotspot circles on the map.
        MapWidget widget;
        widget.resize(640, 480);
        widget.setCenter(51.5074, -0.1278);

        widget.setKDEHotspots({ makeHotspot(51.5074, -0.1278, 1) });
        QApplication::processEvents();
        const QPixmap withHotspot = widget.grab();

        widget.setEvents({});
        QApplication::processEvents();
        const QPixmap afterClearEvents = widget.grab();

        widget.clearKDEHotspots();
        QApplication::processEvents();
        const QPixmap fullyCleared = widget.grab();

        QVERIFY2(afterClearEvents.toImage() != withHotspot.toImage(),
                 "setEvents({}) should clear KDE hotspot overlays");
        QVERIFY(withHotspot.toImage() != fullyCleared.toImage());
    }

    void testZeroCoordinateEventsNotDrawn()
    {
        // BUG: drawEvents skips events where latitude==0 && longitude==0, which
        // suppresses valid Gulf-of-Guinea coordinates as well as missing data.
        MapWidget widget;
        widget.resize(640, 480);
        widget.setCenter(0.0, 0.0);
        widget.setZoom(0.01);

        CrimeEvent atNullIsland;
        atNullIsland.eventId   = QStringLiteral("null-island");
        atNullIsland.id        = atNullIsland.eventId;
        atNullIsland.crimeType = QStringLiteral("theft");
        atNullIsland.latitude  = 0.0;
        atNullIsland.longitude = 0.0;
        atNullIsland.lat       = 0.0;
        atNullIsland.lon       = 0.0;

        widget.setEvents({ atNullIsland });
        QApplication::processEvents();

        const QPixmap emptyGrab = widget.grab();

        CrimeEvent offset;
        offset.eventId   = QStringLiteral("near-null");
        offset.id        = offset.eventId;
        offset.crimeType = QStringLiteral("theft");
        offset.latitude  = 0.001;
        offset.longitude = 0.001;
        offset.lat       = 0.001;
        offset.lon       = 0.001;

        widget.setEvents({ offset });
        QApplication::processEvents();
        const QPixmap withDot = widget.grab();

        QVERIFY2(emptyGrab.toImage() != withDot.toImage(),
                 "events at (0,0) are silently skipped by drawEvents");
    }

    void testClearOverlaysRemovesAllLayers()
    {
        MapWidget widget;
        widget.resize(640, 480);
        widget.setCenter(51.5074, -0.1278);

        widget.setEvents({ makeGeoEvent(QStringLiteral("e1"), QStringLiteral("theft"),
                                      51.5074, -0.1278) });
        widget.setRiskSurface(makeRiskProfile(51.5074, -0.1278));
        widget.setKDEHotspots({ makeHotspot(51.5074, -0.1278, 1) });
        QApplication::processEvents();
        const QPixmap layered = widget.grab();

        widget.clearOverlays();
        QApplication::processEvents();
        const QPixmap cleared = widget.grab();

        QVERIFY(!layered.isNull());
        QVERIFY(layered.toImage() != cleared.toImage());
    }
};

QTEST_MAIN(TestMapWidgetDeep4)

#include "test_map_widget_deep4.moc"
