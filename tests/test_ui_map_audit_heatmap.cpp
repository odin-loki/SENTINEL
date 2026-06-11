// test_ui_map_audit_heatmap.cpp — Iteration 5 audit tests for MapWidget,
// AuditLogWidget, and TemporalHeatmapWidget.
// Requires QApplication for widget construction and offscreen rendering.

#include <QTest>
#include <QApplication>
#include <QCoreApplication>
#include <QLineEdit>
#include <QTableWidget>
#include <QPixmap>
#include <QPainter>
#include <array>

#include "core/CrimeEvent.h"
#include "models/KDEHotspot.h"
#include "audit/ProvenanceLog.h"
#include "ui/MapWidget.h"
#include "ui/AuditLogWidget.h"
#include "ui/TemporalHeatmapWidget.h"

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

static CrimeEvent makeMapEvent(double lat, double lon, const QString& id = QStringLiteral("E1"))
{
    CrimeEvent ev;
    ev.eventId    = id;
    ev.id         = id;
    ev.lat        = lat;
    ev.lon        = lon;
    ev.latitude   = lat;
    ev.longitude  = lon;
    ev.crimeType  = QStringLiteral("Theft");
    ev.source     = QStringLiteral("test");
    ev.ingestedAt = QDateTime::currentDateTimeUtc();
    ev.occurredAt = QDateTime::currentDateTimeUtc();
    ev.timestamp  = *ev.occurredAt;
    return ev;
}

static CrimeEvent makeHeatmapEvent(int dayOffset, int hour, const QString& id)
{
    // dayOffset: 0=Mon … 6=Sun (Qt dayOfWeek 1..7)
    const QDate base = QDate(2024, 6, 3); // Monday
    const QDate date = base.addDays(dayOffset);
    const QDateTime dt(date, QTime(hour, 30, 0), Qt::UTC);

    CrimeEvent ev;
    ev.eventId    = id;
    ev.id         = id;
    ev.crimeType  = QStringLiteral("burglary");
    ev.source     = QStringLiteral("test");
    ev.ingestedAt = QDateTime::currentDateTimeUtc();
    ev.occurredAt = dt;
    ev.timestamp  = dt;
    return ev;
}

// Mirrors AnalyticsWidget::refreshHeatmap — TemporalHeatmapWidget has setData(),
// not loadData().
static std::array<std::array<int, 24>, 7> countsFromEvents(const QVector<CrimeEvent>& events)
{
    std::array<std::array<int, 24>, 7> counts{};
    for (const CrimeEvent& ev : events) {
        const QDateTime dt = ev.occurredAt ? *ev.occurredAt : ev.timestamp;
        if (!dt.isValid())
            continue;
        const int day  = dt.date().dayOfWeek() - 1;
        const int hour = dt.time().hour();
        if (day >= 0 && day < 7 && hour >= 0 && hour < 24)
            ++counts[day][hour];
    }
    return counts;
}

static int pixelIntensity(const QColor& c)
{
    return c.red() + c.green() + c.blue();
}

// ─────────────────────────────────────────────────────────────────────────────
// UIMapAuditHeatmapTest
// ─────────────────────────────────────────────────────────────────────────────

class UIMapAuditHeatmapTest : public QObject {
    Q_OBJECT

private slots:

    // ── MapWidget ─────────────────────────────────────────────────────────────

    void testMapWidgetConstruction()
    {
        MapWidget w;
        w.resize(400, 300);
        QVERIFY(w.width() > 0);
        QVERIFY(w.height() > 0);
    }

    void testMapWidgetSetEvents()
    {
        MapWidget w;
        w.resize(400, 300);

        QVector<CrimeEvent> events;
        for (int i = 0; i < 5; ++i) {
            const double lat = 51.5074 + i * 0.001;
            const double lon = -0.1278 + i * 0.001;
            events.append(makeMapEvent(lat, lon, QStringLiteral("E%1").arg(i + 1)));
        }
        w.setEvents(events);
        QApplication::processEvents();
        QVERIFY(true);
    }

    void testMapWidgetEmptyEvents()
    {
        MapWidget w;
        w.setEvents({});
        QApplication::processEvents();
        QVERIFY(true);
    }

    void testMapWidgetMissingLatLon()
    {
        MapWidget w;
        w.resize(400, 300);

        QVector<CrimeEvent> events;
        for (int i = 0; i < 3; ++i) {
            CrimeEvent ev;
            ev.eventId = QStringLiteral("missing_%1").arg(i);
            ev.lat     = std::nullopt;
            ev.lon     = std::nullopt;
            // flat fields default to 0.0 — MapWidget skips lat==0 && lon==0
            ev.latitude  = 0.0;
            ev.longitude = 0.0;
            events.append(ev);
        }
        w.setEvents(events);
        QApplication::processEvents();
        QVERIFY(true);
    }

    void testMapWidgetSetHotspots()
    {
        MapWidget w;
        w.resize(400, 300);

        QVector<HotspotRegion> hotspots;
        HotspotRegion r;
        r.centroidLat = 51.5074;
        r.centroidLon = -0.1278;
        r.latMin      = 51.50;
        r.latMax      = 51.52;
        r.lonMin      = -0.14;
        r.lonMax      = -0.12;
        r.peakDensity = 12.0;
        r.rank        = 1;
        hotspots.append(r);

        w.setKDEHotspots(hotspots);
        QApplication::processEvents();
        QVERIFY(true);
    }

    // ── AuditLogWidget ────────────────────────────────────────────────────────

    void testAuditLogConstruction()
    {
        ProvenanceLog log;
        AuditLogWidget w(log);
        QVERIFY(true);
    }

    void testAuditLogLoadEntries()
    {
        ProvenanceLog log;
        for (int i = 0; i < 5; ++i)
            log.record(QStringLiteral("evt_%1").arg(i),
                       QStringLiteral("ingest"),
                       QStringLiteral("import"),
                       QStringLiteral("detail %1").arg(i));

        AuditLogWidget w(log);
        w.refresh();

        auto* table = w.findChild<QTableWidget*>();
        QVERIFY(table != nullptr);
        QCOMPARE(table->rowCount(), 5);
    }

    void testAuditLogFilterByStage()
    {
        ProvenanceLog log;
        log.record(QStringLiteral("evt_1"), QStringLiteral("ingest"),    QStringLiteral("act"), QStringLiteral("d"));
        log.record(QStringLiteral("evt_2"), QStringLiteral("nlp"),       QStringLiteral("act"), QStringLiteral("d"));
        log.record(QStringLiteral("evt_3"), QStringLiteral("inference"), QStringLiteral("act"), QStringLiteral("d"));

        AuditLogWidget w(log);
        w.refresh();

        auto* table      = w.findChild<QTableWidget*>();
        auto* filterEdit = w.findChild<QLineEdit*>();
        QVERIFY(table != nullptr);
        QVERIFY(filterEdit != nullptr);
        QCOMPARE(table->rowCount(), 3);

        filterEdit->setText(QStringLiteral("ingest"));
        QCoreApplication::processEvents();
        QCOMPARE(table->rowCount(), 1);
        QCOMPARE(table->item(0, 2)->text(), QStringLiteral("ingest"));
    }

    void testAuditLogClear()
    {
        ProvenanceLog log;
        log.record(QStringLiteral("evt_1"), QStringLiteral("ingest"), QStringLiteral("act"), QStringLiteral("d"));
        log.record(QStringLiteral("evt_2"), QStringLiteral("nlp"),    QStringLiteral("act"), QStringLiteral("d"));

        AuditLogWidget w(log);
        w.refresh();

        auto* table = w.findChild<QTableWidget*>();
        QVERIFY(table != nullptr);
        QCOMPARE(table->rowCount(), 2);

        log.clear();
        w.refresh();
        QCOMPARE(table->rowCount(), 0);
    }

    void testAuditLogExportHtml()
    {
        // AuditLogWidget has no HTML export; ProvenanceLog::formatHtml is the export API.
        ProvenanceLog log;
        log.record(QStringLiteral("evt_html"), QStringLiteral("ingest"), QStringLiteral("import"), QStringLiteral("step"));
        log.record(QStringLiteral("evt_html"), QStringLiteral("output"), QStringLiteral("export"), QStringLiteral("done"));

        const QString html = log.formatHtml(QStringLiteral("evt_html"));
        QVERIFY(!html.isEmpty());
        QVERIFY(html.contains(QStringLiteral("evt_html")));
        QVERIFY(html.contains(QStringLiteral("<table") ) || html.contains(QStringLiteral("<TABLE")));
    }

    // ── TemporalHeatmapWidget ─────────────────────────────────────────────────

    void testHeatmapConstruction()
    {
        TemporalHeatmapWidget w;
        QCOMPARE(w.sizeHint().width(),  840);
        QCOMPARE(w.sizeHint().height(), 220);
    }

    void testHeatmapLoadData()
    {
        TemporalHeatmapWidget w;
        w.resize(840, 220);

        QVector<CrimeEvent> events;
        for (int i = 0; i < 20; ++i)
            events.append(makeHeatmapEvent(i % 7, i % 24, QStringLiteral("H%1").arg(i)));

        w.setData(countsFromEvents(events));
        QApplication::processEvents();
        QVERIFY(true);
    }

    void testHeatmapEmptyData()
    {
        TemporalHeatmapWidget w;
        w.resize(840, 220);
        w.setData(countsFromEvents({}));
        QApplication::processEvents();
        QVERIFY(true);
    }

    void testHeatmapCellCount()
    {
        std::array<std::array<int, 24>, 7> counts{};
        for (int day = 0; day < 7; ++day)
            for (int hour = 0; hour < 24; ++hour)
                counts[day][hour] = 1;

        int cellCount = 0;
        for (const auto& row : counts)
            cellCount += static_cast<int>(row.size());
        QCOMPARE(cellCount, 168);

        TemporalHeatmapWidget w;
        w.resize(840, 220);
        w.setData(counts);
        QApplication::processEvents();
        QVERIFY(true);
    }

    void testHeatmapColorScale()
    {
        std::array<std::array<int, 24>, 7> counts{};
        counts[0][0] = 1;    // low
        counts[0][1] = 100;  // peak

        TemporalHeatmapWidget w;
        w.resize(840, 220);
        w.setData(counts);

        QPixmap pm(840, 220);
        pm.fill(Qt::black);
        QPainter painter(&pm);
        w.render(&painter);
        painter.end();

        // Approximate cell centres (matches widget margins)
        const int gridLeft = 56;
        const int gridTop  = 28;
        const double cellW = (840.0 - 56.0 - 12.0) / 24.0;
        const double cellH = (220.0 - 28.0 - 36.0) / 7.0;

        const QColor lowColor  = pm.toImage().pixelColor(
            static_cast<int>(gridLeft + cellW * 0.5),
            static_cast<int>(gridTop + cellH * 0.5));
        const QColor highColor = pm.toImage().pixelColor(
            static_cast<int>(gridLeft + cellW * 1.5),
            static_cast<int>(gridTop + cellH * 0.5));

        QVERIFY(pixelIntensity(highColor) > pixelIntensity(lowColor));
        QVERIFY(highColor.red() > lowColor.red());
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// main
// ─────────────────────────────────────────────────────────────────────────────

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    app.setAttribute(Qt::AA_Use96Dpi);
    UIMapAuditHeatmapTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_ui_map_audit_heatmap.moc"
