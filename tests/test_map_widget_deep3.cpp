// test_map_widget_deep3.cpp — Deep audit iteration 16: MapWidget construct,
// setEvents empty/populated, overlay lifecycle, offscreen rendering.

#include <QTest>
#include <QApplication>
#include <QLabel>
#include <QVector>

#include "ui/MapWidget.h"
#include "core/CrimeEvent.h"

class TestMapWidgetDeep3 : public QObject {
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

private slots:
    void initTestCase()
    {
        qputenv("QT_QPA_PLATFORM", "offscreen");
    }

    void testConstruct()
    {
        MapWidget widget;
        widget.resize(640, 480);
        widget.show();
        QApplication::processEvents();

        QVERIFY(widget.width() >= 400);
        QVERIFY(widget.height() >= 300);

        auto* zoomLbl = widget.findChild<QLabel*>();
        QVERIFY2(zoomLbl != nullptr, "Zoom label should exist after construction");
        QVERIFY(zoomLbl->text().contains(QStringLiteral("Zoom:")));
    }

    void testSetEventsEmpty()
    {
        MapWidget widget;
        widget.resize(640, 480);

        QVector<CrimeEvent> seeded;
        seeded.append(makeGeoEvent(QStringLiteral("pre-1"), QStringLiteral("theft"),
                                   51.5074, -0.1278));
        widget.setEvents(seeded);
        QApplication::processEvents();

        widget.setEvents({});
        QApplication::processEvents();
        widget.update();
        QApplication::processEvents();

        const QPixmap px = widget.grab();
        QVERIFY(!px.isNull());
    }

    void testSetEventsPopulated()
    {
        MapWidget widget;
        widget.resize(640, 480);
        widget.setCenter(51.5074, -0.1278);

        QVector<CrimeEvent> events;
        const QStringList types = {
            QStringLiteral("assault"), QStringLiteral("burglary"),
            QStringLiteral("theft"),   QStringLiteral("robbery"),
            QStringLiteral("vehicle_crime"), QStringLiteral("drugs")
        };
        for (int i = 0; i < types.size(); ++i) {
            events.append(makeGeoEvent(QStringLiteral("MW3-%1").arg(i),
                                       types[i],
                                       51.5074 + i * 0.002,
                                       -0.1278 + i * 0.002));
        }

        widget.setEvents(events);
        QApplication::processEvents();
        widget.update();
        QApplication::processEvents();

        const QPixmap px = widget.grab();
        QVERIFY(!px.isNull());
        QVERIFY(px.width() > 0);
        QVERIFY(px.height() > 0);
    }

    void testSetEventsOptionalLatLonSync()
    {
        MapWidget widget;
        widget.resize(640, 480);
        widget.setCenter(51.5074, -0.1278);

        CrimeEvent ev;
        ev.eventId   = QStringLiteral("opt-sync");
        ev.id        = ev.eventId;
        ev.crimeType = QStringLiteral("burglary");
        ev.lat       = 51.5074;
        ev.lon       = -0.1278;

        widget.setEvents({ev});
        QApplication::processEvents();
        widget.update();
        QApplication::processEvents();

        const QPixmap px = widget.grab();
        QVERIFY(!px.isNull());
    }

    void testSetEventsReplacesPrevious()
    {
        MapWidget widget;
        widget.resize(640, 480);
        widget.setCenter(51.5074, -0.1278);

        QVector<CrimeEvent> first;
        for (int i = 0; i < 8; ++i)
            first.append(makeGeoEvent(QStringLiteral("A-%1").arg(i),
                                      QStringLiteral("theft"),
                                      51.50 + i * 0.01, -0.12));
        widget.setEvents(first);

        QVector<CrimeEvent> second;
        second.append(makeGeoEvent(QStringLiteral("B-only"),
                                   QStringLiteral("assault"),
                                   51.5074, -0.1278));
        widget.setEvents(second);

        QApplication::processEvents();
        widget.update();
        QApplication::processEvents();
        QVERIFY(!widget.grab().isNull());
    }

    void testClearOverlaysAfterSetEvents()
    {
        MapWidget widget;
        widget.resize(640, 480);
        widget.setCenter(51.5074, -0.1278);

        QVector<CrimeEvent> events;
        events.append(makeGeoEvent(QStringLiteral("clr-1"), QStringLiteral("theft"),
                                   51.5074, -0.1278));
        widget.setEvents(events);
        QApplication::processEvents();

        widget.clearOverlays();
        QApplication::processEvents();
        widget.update();
        QApplication::processEvents();

        const QPixmap px = widget.grab();
        QVERIFY(!px.isNull());
    }
};

QTEST_MAIN(TestMapWidgetDeep3)

#include "test_map_widget_deep3.moc"
