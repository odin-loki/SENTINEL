// test_map_widget.cpp
// Focused unit tests for MapWidget (offscreen / headless).
// Run with: test_map_widget.exe -platform offscreen
#include <QTest>
#include <QApplication>
#include <QSignalSpy>
#include <QVector>
#include "ui/MapWidget.h"
#include "core/CrimeEvent.h"
#include "models/KDEHotspot.h"

// ---------------------------------------------------------------------------
// Helper: build a minimal CrimeEvent at (lat, lon)
// ---------------------------------------------------------------------------
static CrimeEvent makeEvent(double lat, double lon, const QString& id = "E1")
{
    CrimeEvent ev;
    ev.eventId   = id;
    ev.id        = id;
    ev.lat       = lat;
    ev.lon       = lon;
    ev.latitude  = lat;
    ev.longitude = lon;
    ev.crimeType = "Theft";
    ev.source    = "test";
    ev.ingestedAt = QDateTime::currentDateTimeUtc();
    return ev;
}

// ---------------------------------------------------------------------------
// Test class
// ---------------------------------------------------------------------------
class TestMapWidget : public QObject {
    Q_OBJECT

private slots:

    // 1 ── Widget constructs without crash
    void testConstruct()
    {
        MapWidget w;
        QVERIFY(true);
    }

    // 2 ── setCenter() doesn't crash
    void testSetCenter()
    {
        MapWidget w;
        w.setCenter(51.5074, -0.1278);   // London
        w.setCenter(-33.8688, 151.2093); // Sydney
        w.setCenter(0.0, 0.0);
        QVERIFY(true);
    }

    // 3 ── setZoom() doesn't crash
    void testSetZoom()
    {
        MapWidget w;
        w.setZoom(0.0001);
        w.setZoom(0.01);
        w.setZoom(0.1);
        QVERIFY(true);
    }

    // 4 ── setEvents() with empty vector doesn't crash
    void testSetEventsEmpty()
    {
        MapWidget w;
        QVector<CrimeEvent> empty;
        w.setEvents(empty);
        QVERIFY(true);
    }

    // 5 ── setEvents() with 5 events doesn't crash
    void testSetEventsFive()
    {
        MapWidget w;
        QVector<CrimeEvent> events;
        for (int i = 0; i < 5; ++i) {
            double lat = 51.5074 + i * 0.001;
            double lon = -0.1278 + i * 0.001;
            events.append(makeEvent(lat, lon, QStringLiteral("E%1").arg(i + 1)));
        }
        w.setEvents(events);
        QVERIFY(true);
    }

    // 6 ── clearOverlays() doesn't crash (also when called on empty widget)
    void testClearOverlays()
    {
        MapWidget w;
        w.clearOverlays();   // empty state

        QVector<CrimeEvent> events;
        events.append(makeEvent(51.5, -0.1));
        w.setEvents(events);
        w.clearOverlays();   // with data
        QVERIFY(true);
    }

    // 7 ── setRiskSurface() with empty GeographicProfile doesn't crash
    void testSetRiskSurfaceEmpty()
    {
        MapWidget w;
        GeographicProfile profile;   // all defaults / empty vectors
        w.setRiskSurface(profile);
        QVERIFY(true);
    }

    // 8 ── setKDEHotspots() with empty vector doesn't crash
    void testSetKDEHotspotsEmpty()
    {
        MapWidget w;
        QVector<HotspotRegion> hotspots;
        w.setKDEHotspots(hotspots);
        QVERIFY(true);
    }

    // 9 ── zoomLevelInt() returns a non-negative value after construction
    void testZoomLevelInt()
    {
        MapWidget w;
        int z = w.zoomLevelInt();
        QVERIFY(z >= 0);
    }

    // 10 ── locationClicked signal connects and delivers lat/lon on emit
    void testLocationClickedSignal()
    {
        MapWidget w;
        QSignalSpy spy(&w, &MapWidget::locationClicked);
        QVERIFY(spy.isValid());

        // Emit the signal directly (the real trigger is a mouse click which
        // requires a paint surface; this verifies the connection works).
        emit w.locationClicked(51.5074, -0.1278);

        QCOMPARE(spy.count(), 1);
        const QList<QVariant>& args = spy.at(0);
        QCOMPARE(args.size(), 2);
        QVERIFY(qAbs(args.at(0).toDouble() - 51.5074) < 1e-6);
        QVERIFY(qAbs(args.at(1).toDouble() - (-0.1278)) < 1e-6);
    }
};

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    TestMapWidget t;
    return QTest::qExec(&t, argc, argv);
}

#include "test_map_widget.moc"
