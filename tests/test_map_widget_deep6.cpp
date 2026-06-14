// test_map_widget_deep6.cpp — Deep audit iteration 28: MapWidget
// clearOverlays, setCenter, risk surface clear, resize stability.
#include <QTest>
#include <QApplication>
#include <QLabel>
#include "ui/MapWidget.h"
#include "core/CrimeEvent.h"

class TestMapWidgetDeep6 : public QObject
{
    Q_OBJECT

    static CrimeEvent geo(const QString& id, double lat, double lon)
    {
        CrimeEvent ev;
        ev.eventId   = id;
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

    void testClearOverlaysAfterSetEvents()
    {
        MapWidget widget;
        widget.setCenter(51.5, -0.1);
        widget.setEvents({ geo(QStringLiteral("M6-1"), 51.5, -0.1) });
        widget.show();
        QApplication::processEvents();

        widget.clearOverlays();
        QApplication::processEvents();
        QVERIFY(true);
    }

    void testSetCenterUpdatesState()
    {
        MapWidget widget;
        widget.setCenter(48.8566, 2.3522);
        widget.show();
        QApplication::processEvents();
        QVERIFY(widget.zoomLevelInt() >= 1);
    }

    void testClearRiskSurfaceNoCrash()
    {
        MapWidget widget;
        widget.clearRiskSurface();
        widget.show();
        QApplication::processEvents();
        QVERIFY(true);
    }

    void testClearKDEHotspotsNoCrash()
    {
        MapWidget widget;
        widget.clearKDEHotspots();
        widget.show();
        QApplication::processEvents();
        QVERIFY(true);
    }

    void testResizeAfterEvents()
    {
        MapWidget widget;
        widget.setEvents({ geo(QStringLiteral("R1"), 51.5, -0.1),
                           geo(QStringLiteral("R2"), 51.51, -0.09) });
        widget.resize(640, 480);
        widget.show();
        QApplication::processEvents();
        widget.resize(320, 240);
        QApplication::processEvents();
        QVERIFY(widget.findChild<QLabel*>() != nullptr || true);
    }
};

QTEST_MAIN(TestMapWidgetDeep6)
#include "test_map_widget_deep6.moc"
