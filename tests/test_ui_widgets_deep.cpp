// test_ui_widgets_deep.cpp — Deep UI widget tests for SENTINEL
// Tests AnalyticsWidget, LeadsWidget, and EventsTableWidget.
// Requires QApplication for widget construction.

#include <QTest>
#include <QApplication>
#include <QTabWidget>
#include <QComboBox>
#include <QLineEdit>
#include <QListWidget>
#include <QTableView>
#include <QLabel>
#include <memory>

#include "core/AppConfig.h"
#include "core/Database.h"
#include "core/CrimeEvent.h"
#include "ui/AnalyticsWidget.h"
#include "ui/LeadsWidget.h"
#include "ui/EventsTableWidget.h"

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

static CrimeEvent makeEvent(const QString& id, const QString& type = "burglary")
{
    CrimeEvent e;
    e.eventId        = id;
    e.id             = id;
    e.crimeType      = type;
    e.suburb         = QStringLiteral("TestZone");
    e.lat            = 51.5;
    e.lon            = -0.1;
    e.latitude       = 51.5;
    e.longitude      = -0.1;
    e.occurredAt     = QDateTime::currentDateTimeUtc();
    e.qualityScore   = 0.8;
    return e;
}

static InvestigativeLead makeLead(int rank, double conf)
{
    InvestigativeLead l;
    l.rank               = rank;
    l.category           = QStringLiteral("MO");
    l.headline           = QString("Lead %1").arg(rank);
    l.confidence         = conf;
    l.confidenceMethod   = QStringLiteral("test");
    l.generatedAt        = QDateTime::currentDateTimeUtc();
    return l;
}

// ─────────────────────────────────────────────────────────────────────────────
// TestUIWidgetsDeep
// ─────────────────────────────────────────────────────────────────────────────

class TestUIWidgetsDeep : public QObject {
    Q_OBJECT

private slots:

    // ── AnalyticsWidget ───────────────────────────────────────────────────────

    void testAnalyticsWidgetConstruction()
    {
        AppConfig cfg;
        cfg.databasePath = QStringLiteral(":memory:");
        auto db = makeDB();
        AnalyticsWidget w(db, cfg);
        QVERIFY(true);
    }

    void testAnalyticsWidgetRefreshEmpty()
    {
        AppConfig cfg;
        cfg.databasePath = QStringLiteral(":memory:");
        auto db = makeDB();
        AnalyticsWidget w(db, cfg);
        w.refresh();
        QVERIFY(true);
    }

    void testAnalyticsWidgetRefreshWithEvents()
    {
        AppConfig cfg;
        cfg.databasePath = QStringLiteral(":memory:");
        auto db = makeDB();
        for (int i = 0; i < 10; ++i)
            db->insertEvent(makeEvent(QString("evt%1").arg(i)));
        AnalyticsWidget w(db, cfg);
        w.refresh();
        QVERIFY(true);
    }

    void testAnalyticsWidgetHasTabWidget()
    {
        AppConfig cfg;
        cfg.databasePath = QStringLiteral(":memory:");
        auto db = makeDB();
        AnalyticsWidget w(db, cfg);
        auto* tab = w.findChild<QTabWidget*>();
        QVERIFY(tab != nullptr);
    }

    void testAnalyticsWidgetSize()
    {
        AppConfig cfg;
        cfg.databasePath = QStringLiteral(":memory:");
        auto db = makeDB();
        AnalyticsWidget w(db, cfg);
        w.resize(800, 600);
        QVERIFY(w.width()  > 0);
        QVERIFY(w.height() > 0);
    }

    // ── LeadsWidget ───────────────────────────────────────────────────────────

    void testLeadsWidgetConstruction()
    {
        auto db = makeDB();
        LeadsWidget w(db);
        QVERIFY(true);
    }

    void testLeadsWidgetSetLeadsEmpty()
    {
        auto db = makeDB();
        LeadsWidget w(db);
        w.setLeads({});
        QVERIFY(true);
    }

    void testLeadsWidgetSetLeadsWithData()
    {
        auto db = makeDB();
        LeadsWidget w(db);

        QVector<InvestigativeLead> leads;
        for (int i = 1; i <= 5; ++i)
            leads.append(makeLead(i, 0.5 + i * 0.05));
        w.setLeads(leads);

        auto* list = w.findChild<QListWidget*>();
        QVERIFY(list != nullptr);
        QCOMPARE(list->count(), 5);
    }

    void testLeadsWidgetRefresh()
    {
        auto db = makeDB();
        LeadsWidget w(db);
        w.refresh();
        QVERIFY(true);
    }

    void testLeadsWidgetCountLabel()
    {
        auto db = makeDB();
        LeadsWidget w(db);

        QVector<InvestigativeLead> leads;
        for (int i = 1; i <= 3; ++i)
            leads.append(makeLead(i, 0.6));
        w.setLeads(leads);

        // At least one QLabel should contain a count/leads reference
        bool found = false;
        const auto labels = w.findChildren<QLabel*>();
        for (auto* lbl : labels) {
            if (lbl->text().contains("lead", Qt::CaseInsensitive) ||
                lbl->text().contains("3")) {
                found = true;
                break;
            }
        }
        QVERIFY(found);
    }

    // ── EventsTableWidget ─────────────────────────────────────────────────────

    void testEventsTableWidgetConstruction()
    {
        auto db = makeDB();
        EventsTableWidget w(db);
        QVERIFY(true);
    }

    void testEventsTableWidgetRefreshEmpty()
    {
        auto db = makeDB();
        EventsTableWidget w(db);
        w.refresh();
        QVERIFY(true);
    }

    void testEventsTableWidgetRefreshWithEvents()
    {
        auto db = makeDB();
        for (int i = 0; i < 5; ++i)
            db->insertEvent(makeEvent(QString("ev%1").arg(i)));

        EventsTableWidget w(db);
        w.refresh();

        auto* table = w.findChild<QTableView*>();
        QVERIFY(table != nullptr);
        QVERIFY(table->model() != nullptr);
        QVERIFY(table->model()->rowCount() >= 5);
    }

    void testEventsTableWidgetHasFilterControls()
    {
        auto db = makeDB();
        EventsTableWidget w(db);

        const bool hasCombo = (w.findChild<QComboBox*>() != nullptr);
        const bool hasEdit  = (w.findChild<QLineEdit*>() != nullptr);
        QVERIFY(hasCombo || hasEdit);
    }

    void testEventsTableWidgetColumnsVisible()
    {
        auto db = makeDB();
        EventsTableWidget w(db);
        w.refresh();

        auto* table = w.findChild<QTableView*>();
        QVERIFY(table != nullptr);
        QVERIFY(table->model() != nullptr);
        QVERIFY(table->model()->columnCount() > 3);
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// main
// ─────────────────────────────────────────────────────────────────────────────
int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    app.setAttribute(Qt::AA_Use96Dpi);
    TestUIWidgetsDeep tc;
    return QTest::qExec(&tc, argc, argv);
}

#include "test_ui_widgets_deep.moc"
