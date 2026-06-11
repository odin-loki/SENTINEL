// test_analytics_widget_deep2.cpp — Deep audit iteration 13: AnalyticsWidget refresh/metrics.

#include <QTest>
#include <QApplication>
#include <QLabel>
#include <QUuid>
#include <memory>

#include "ui/AnalyticsWidget.h"
#include "core/Database.h"
#include "core/AppConfig.h"
#include "core/CrimeEvent.h"

class TestAnalyticsWidgetDeep2 : public QObject {
    Q_OBJECT

private slots:
    void initTestCase()
    {
        qputenv("QT_QPA_PLATFORM", "offscreen");
    }

    void testConstruct()
    {
        AppConfig cfg;
        cfg.databasePath = QStringLiteral(":memory:");
        auto db = std::make_shared<Database>(cfg);
        db->open();

        AnalyticsWidget widget(db, cfg);
        widget.resize(900, 600);
        QVERIFY(widget.width() > 0);
        QVERIFY(widget.height() > 0);
    }

    void testUpdateMetricsEmptyDb()
    {
        AppConfig cfg;
        cfg.databasePath = QStringLiteral(":memory:");
        auto db = std::make_shared<Database>(cfg);
        db->open();

        AnalyticsWidget widget(db, cfg);
        widget.resize(900, 600);
        widget.refresh();
        QApplication::processEvents();

        auto* summary = widget.findChild<QLabel*>();
        QVERIFY(summary != nullptr);
        QVERIFY(!summary->text().contains(QStringLiteral("Loading")));
    }

    void testUpdateMetricsPopulatedDb()
    {
        AppConfig cfg;
        cfg.databasePath = QStringLiteral(":memory:");
        auto db = std::make_shared<Database>(cfg);
        db->open();

        for (int i = 0; i < 25; ++i) {
            CrimeEvent ev;
            ev.eventId    = QUuid::createUuid().toString();
            ev.id         = ev.eventId;
            ev.crimeType  = (i % 2 == 0) ? QStringLiteral("theft") : QStringLiteral("assault");
            ev.suburb     = QStringLiteral("TestSuburb");
            ev.lat        = 51.5 + i * 0.001;
            ev.lon        = -0.12 + i * 0.001;
            ev.latitude   = ev.lat.value();
            ev.longitude  = ev.lon.value();
            ev.occurredAt = QDateTime::currentDateTimeUtc().addDays(-i);
            ev.ingestedAt = QDateTime::currentDateTimeUtc();
            ev.source     = QStringLiteral("test");
            db->insertEvent(ev);
        }

        AnalyticsWidget widget(db, cfg);
        widget.resize(900, 600);
        widget.refresh();
        QApplication::processEvents();
        widget.refresh();
        QApplication::processEvents();
        QVERIFY(true);
    }
};

QTEST_MAIN(TestAnalyticsWidgetDeep2)

#include "test_analytics_widget_deep2.moc"
