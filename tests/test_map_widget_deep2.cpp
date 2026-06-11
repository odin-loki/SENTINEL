// test_map_widget_deep2.cpp — Deep audit iteration 13: MapWidget setEvents & paint paths.

#include <QTest>
#include <QApplication>
#include <QVector>

#include "ui/MapWidget.h"
#include "core/CrimeEvent.h"

class TestMapWidgetDeep2 : public QObject {
    Q_OBJECT

private slots:
    void initTestCase()
    {
        qputenv("QT_QPA_PLATFORM", "offscreen");
    }

    void testConstruct()
    {
        MapWidget widget;
        widget.resize(640, 480);
        QVERIFY(widget.width() > 0);
        QVERIFY(widget.height() > 0);
    }

    void testSetEventsEmpty()
    {
        MapWidget widget;
        widget.resize(640, 480);
        widget.setEvents({});
        QApplication::processEvents();
        widget.update();
        QApplication::processEvents();
        QVERIFY(true);
    }

    void testSetEventsNonEmpty()
    {
        MapWidget widget;
        widget.resize(640, 480);

        QVector<CrimeEvent> events;
        for (int i = 0; i < 12; ++i) {
            CrimeEvent ev;
            ev.eventId   = QStringLiteral("E%1").arg(i);
            ev.id        = ev.eventId;
            ev.crimeType = QStringLiteral("theft");
            ev.lat       = 51.5074 + i * 0.001;
            ev.lon       = -0.1278 + i * 0.001;
            ev.latitude  = ev.lat.value();
            ev.longitude = ev.lon.value();
            events.append(ev);
        }

        widget.setEvents(events);
        QApplication::processEvents();
        widget.update();
        QApplication::processEvents();

        const QPixmap px = widget.grab();
        QVERIFY(!px.isNull());
    }

    void testSetEventsOptionalCoordsOnly()
    {
        MapWidget widget;
        widget.resize(640, 480);
        widget.setCenter(51.5074, -0.1278);

        CrimeEvent ev;
        ev.eventId  = QStringLiteral("opt-only");
        ev.id       = ev.eventId;
        ev.crimeType = QStringLiteral("burglary");
        ev.lat      = 51.5074;
        ev.lon      = -0.1278;

        widget.setEvents({ev});
        QApplication::processEvents();
        widget.update();
        QApplication::processEvents();
        QVERIFY(true);
    }
};

QTEST_MAIN(TestMapWidgetDeep2)

#include "test_map_widget_deep2.moc"
