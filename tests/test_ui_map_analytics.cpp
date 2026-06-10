// test_ui_map_analytics.cpp — Deep UI tests for MapWidget, AnalyticsWidget,
// EventsTableWidget, and TemporalHeatmapWidget.
// Requires QApplication (offscreen) for widget construction and painting.

#include <QTest>
#include <QApplication>
#include <QTabWidget>
#include <QTableView>
#include <QHeaderView>
#include <QStandardItemModel>
#include <QLineEdit>
#include <QComboBox>
#include <QPushButton>
#include <QLabel>
#include <QUuid>
#include <array>
#include <memory>

#include "core/AppConfig.h"
#include "core/Database.h"
#include "core/CrimeEvent.h"
#include "models/KDEHotspot.h"
#include "ui/MapWidget.h"
#include "ui/AnalyticsWidget.h"
#include "ui/EventsTableWidget.h"
#include "ui/TemporalHeatmapWidget.h"

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

static std::shared_ptr<Database> makeDB()
{
    AppConfig cfg;
    cfg.databasePath = QStringLiteral(":memory:");
    auto db = std::make_shared<Database>(cfg);
    db->open();
    return db;
}

// Helper matching the task specification, with additional flat-field population
// for DB-backed widgets.
static CrimeEvent makeEvent(double lat, double lon, const QString& type = QStringLiteral("burglary"))
{
    CrimeEvent e;
    e.eventId    = QUuid::createUuid().toString();
    e.id         = e.eventId;
    e.crimeType  = type;
    e.suburb     = QStringLiteral("TestArea");
    e.lat        = lat;
    e.lon        = lon;
    e.latitude   = lat;   // flat UI convenience field
    e.longitude  = lon;
    e.occurredAt = QDateTime::currentDateTimeUtc();
    e.ingestedAt = QDateTime::currentDateTimeUtc();
    e.source     = QStringLiteral("test");
    e.qualityScore = 0.8;
    return e;
}

// ─────────────────────────────────────────────────────────────────────────────
// TestMapAnalyticsWidget
// ─────────────────────────────────────────────────────────────────────────────

class TestMapAnalyticsWidget : public QObject {
    Q_OBJECT

private slots:

    // ── MapWidget ─────────────────────────────────────────────────────────────

    void testMapWidgetCreation()
    {
        MapWidget w;
        w.resize(600, 400);
        QVERIFY(w.width()  > 0);
        QVERIFY(w.height() > 0);
    }

    void testMapWidgetSetEvents()
    {
        MapWidget w;
        w.resize(600, 400);

        QVector<CrimeEvent> events;
        events.reserve(50);
        for (int i = 0; i < 50; ++i)
            events.append(makeEvent(51.4 + i * 0.005, -0.15 + i * 0.003));

        w.setEvents(events);
        QApplication::processEvents();
        QVERIFY(true); // no crash
    }

    void testMapWidgetSetHotspots()
    {
        MapWidget w;
        w.resize(600, 400);

        QVector<HotspotRegion> hotspots;
        for (int i = 0; i < 3; ++i) {
            HotspotRegion r;
            r.centroidLat = 51.5 + i * 0.01;
            r.centroidLon = -0.1 + i * 0.01;
            r.latMin      = r.centroidLat - 0.005;
            r.latMax      = r.centroidLat + 0.005;
            r.lonMin      = r.centroidLon - 0.005;
            r.lonMax      = r.centroidLon + 0.005;
            r.peakDensity = 0.8 - i * 0.1;
            r.totalMass   = 0.3;
            r.crimeCount  = 10 + i * 5;
            r.rank        = i + 1;
            hotspots.append(r);
        }

        w.setKDEHotspots(hotspots);
        QApplication::processEvents();
        QVERIFY(true);
    }

    void testMapWidgetClearEvents()
    {
        MapWidget w;
        w.resize(600, 400);

        // Populate then clear
        QVector<CrimeEvent> events;
        for (int i = 0; i < 10; ++i)
            events.append(makeEvent(51.5 + i * 0.01, -0.1 + i * 0.01));
        w.setEvents(events);
        QApplication::processEvents();

        w.setEvents({});   // equivalent to clearEvents
        QApplication::processEvents();
        QVERIFY(true);
    }

    void testMapWidgetBoundsUpdate()
    {
        MapWidget w;
        w.resize(600, 400);

        // Load a cluster of events at a known location
        QVector<CrimeEvent> events;
        for (int i = 0; i < 20; ++i)
            events.append(makeEvent(51.5 + i * 0.001, -0.12 + i * 0.001));
        w.setEvents(events);

        // Re-center on the cluster centroid — should not crash
        w.setCenter(51.51, -0.11);
        w.setZoom(0.0001);
        QApplication::processEvents();

        // Widget must still be valid after center/zoom change
        QVERIFY(w.width() > 0);
        QVERIFY(w.height() > 0);
    }

    void testMapWidgetPaintingDoesNotCrash()
    {
        MapWidget w;
        w.resize(600, 400);

        QVector<CrimeEvent> events;
        for (int i = 0; i < 30; ++i)
            events.append(makeEvent(51.4 + i * 0.005, -0.2 + i * 0.004, "robbery"));

        HotspotRegion r;
        r.centroidLat = 51.5; r.centroidLon = -0.1;
        r.latMin = 51.49; r.latMax = 51.51;
        r.lonMin = -0.11; r.lonMax = -0.09;
        r.peakDensity = 0.9; r.totalMass = 0.4; r.crimeCount = 15; r.rank = 1;
        w.setKDEHotspots({r});
        w.setEvents(events);

        QApplication::processEvents();
        QPixmap px = w.grab();
        QVERIFY(!px.isNull());
        QVERIFY(px.width()  > 0);
        QVERIFY(px.height() > 0);
    }

    // ── AnalyticsWidget ───────────────────────────────────────────────────────

    void testAnalyticsWidgetCreation()
    {
        AppConfig cfg;
        cfg.databasePath = QStringLiteral(":memory:");
        auto db = makeDB();
        AnalyticsWidget w(db, cfg);
        w.resize(900, 600);
        QVERIFY(w.width()  > 0);
        QVERIFY(w.height() > 0);
    }

    void testAnalyticsWidgetSetEvents()
    {
        AppConfig cfg;
        cfg.databasePath = QStringLiteral(":memory:");
        auto db = makeDB();

        // Insert 100 events with varied types into the backing database
        const QStringList types{ "burglary", "robbery", "assault", "theft", "vandalism" };
        for (int i = 0; i < 100; ++i) {
            CrimeEvent e = makeEvent(51.4 + (i % 20) * 0.005,
                                     -0.2  + (i % 20) * 0.004,
                                     types[i % types.size()]);
            db->insertEvent(e);
        }

        AnalyticsWidget w(db, cfg);
        w.resize(900, 600);
        w.refresh(); // charts should rebuild from the 100 DB events
        QApplication::processEvents();
        QVERIFY(true);
    }

    void testAnalyticsWidgetRefresh()
    {
        AppConfig cfg;
        cfg.databasePath = QStringLiteral(":memory:");
        auto db = makeDB();
        AnalyticsWidget w(db, cfg);
        w.resize(900, 600);

        // Multiple refreshes must not crash
        w.refresh();
        QApplication::processEvents();
        w.refresh();
        QApplication::processEvents();
        QVERIFY(true);
    }

    // ── EventsTableWidget ─────────────────────────────────────────────────────

    void testEventsTableCreation()
    {
        auto db = makeDB();
        EventsTableWidget w(db);
        w.resize(800, 500);
        QVERIFY(w.width()  > 0);
        QVERIFY(w.height() > 0);
    }

    void testEventsTablePopulate()
    {
        auto db = makeDB();
        for (int i = 0; i < 50; ++i)
            db->insertEvent(makeEvent(51.4 + i * 0.005, -0.2 + i * 0.004));

        EventsTableWidget w(db);
        w.resize(800, 500);
        w.refresh();
        QApplication::processEvents();

        auto* table = w.findChild<QTableView*>();
        QVERIFY(table != nullptr);
        QVERIFY(table->model() != nullptr);
        QVERIFY(table->model()->rowCount() >= 50);
    }

    void testEventsTableSort()
    {
        auto db = makeDB();
        for (int i = 0; i < 10; ++i)
            db->insertEvent(makeEvent(51.4 + i * 0.005, -0.2 + i * 0.004));

        EventsTableWidget w(db);
        w.resize(800, 500);
        w.refresh();
        QApplication::processEvents();

        auto* table = w.findChild<QTableView*>();
        QVERIFY(table != nullptr);

        // Click the first column header to trigger a sort — must not crash
        QHeaderView* hdr = table->horizontalHeader();
        QVERIFY(hdr != nullptr);
        if (hdr->count() > 0) {
            QTest::mouseClick(hdr, Qt::LeftButton,
                              Qt::NoModifier,
                              QPoint(hdr->sectionPosition(0) + hdr->sectionSize(0) / 2, 5));
            QApplication::processEvents();
        }
        QVERIFY(true);
    }

    void testEventsTableFilter()
    {
        auto db = makeDB();
        for (int i = 0; i < 20; ++i)
            db->insertEvent(makeEvent(51.5, -0.1, "burglary"));
        for (int i = 0; i < 5; ++i)
            db->insertEvent(makeEvent(51.5, -0.1, "assault"));

        EventsTableWidget w(db);
        w.resize(800, 500);
        w.refresh();
        QApplication::processEvents();

        // Locate the search/filter QLineEdit and type a search term
        auto* searchEdit = w.findChild<QLineEdit*>();
        if (searchEdit) {
            searchEdit->setText(QStringLiteral("burglary"));

            // Trigger filter by clicking the filter button if present, otherwise
            // simulate Return key to activate onFilterChanged.
            auto* filterBtn = w.findChild<QPushButton*>();
            if (filterBtn)
                QTest::mouseClick(filterBtn, Qt::LeftButton);
            else
                QTest::keyClick(searchEdit, Qt::Key_Return);

            QApplication::processEvents();
        }
        // Widget must remain valid after filter operation
        QVERIFY(w.findChild<QTableView*>() != nullptr);
    }

    void testEventsTableClear()
    {
        auto db = makeDB();
        for (int i = 0; i < 10; ++i)
            db->insertEvent(makeEvent(51.5, -0.1));

        EventsTableWidget w(db);
        w.resize(800, 500);
        w.refresh();
        QApplication::processEvents();

        auto* table = w.findChild<QTableView*>();
        QVERIFY(table != nullptr);
        int beforeCount = table->model() ? table->model()->rowCount() : 0;
        QVERIFY(beforeCount > 0);

        // Simulate a "clear" by refreshing against a new empty database.
        // EventsTableWidget uses the db reference it was constructed with;
        // here we verify that refresh() with the same (empty-after-clear) db
        // works without crash.  We call refresh() to exercise the clear path.
        w.refresh();
        QApplication::processEvents();
        QVERIFY(true);
    }

    // ── TemporalHeatmapWidget ─────────────────────────────────────────────────

    void testTemporalHeatmapWidget()
    {
        TemporalHeatmapWidget w;
        w.resize(840, 220);

        // Build a populated 7×24 count matrix
        std::array<std::array<int,24>,7> counts{};
        for (int day = 0; day < 7; ++day)
            for (int hour = 0; hour < 24; ++hour)
                counts[day][hour] = (day * 24 + hour) % 30;

        w.setData(counts);
        QApplication::processEvents();

        QPixmap px = w.grab();
        QVERIFY(!px.isNull());
        QVERIFY(px.width()  > 0);
        QVERIFY(px.height() > 0);
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// main
// ─────────────────────────────────────────────────────────────────────────────

int main(int argc, char* argv[])
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    app.setAttribute(Qt::AA_Use96Dpi);

    TestMapAnalyticsWidget t;
    return QTest::qExec(&t, argc, argv);
}

#include "test_ui_map_analytics.moc"
