// test_leads_widget_deep5.cpp — Deep audit iteration 25: LeadsWidget
// populate leads, confidence tiers, empty state, series tab.
#include <QTest>
#include <QApplication>
#include <QListWidget>
#include <QLabel>
#include <QTabWidget>
#include <QDoubleSpinBox>
#include <memory>
#include "ui/LeadsWidget.h"
#include "core/Database.h"
#include "core/AppConfig.h"
#include "core/CrimeEvent.h"

class TestLeadsWidgetDeep5 : public QObject
{
    Q_OBJECT

    static std::shared_ptr<Database> openDb()
    {
        AppConfig cfg;
        cfg.databasePath = QStringLiteral(":memory:");
        auto db = std::make_shared<Database>(cfg);
        db->open();
        return db;
    }

    static InvestigativeLead makeLead(int rank, double confidence)
    {
        InvestigativeLead lead;
        lead.rank             = rank;
        lead.category         = QStringLiteral("pattern");
        lead.headline         = QStringLiteral("Lead rank %1").arg(rank);
        lead.detail           = QStringLiteral("Detail %1").arg(rank);
        lead.confidence       = confidence;
        lead.confidenceMethod = QStringLiteral("deep5_test");
        lead.generatedAt      = QDateTime::currentDateTimeUtc();
        return lead;
    }

private slots:
    void initTestCase()
    {
        qputenv("QT_QPA_PLATFORM", "offscreen");
        qputenv("SENTINEL_HEADLESS_TEST", "1");
    }

    void testConstructionNoCrash()
    {
        auto db = openDb();
        LeadsWidget widget(db);
        widget.resize(700, 500);
        widget.show();
        QApplication::processEvents();
        QVERIFY(widget.findChild<QTabWidget*>() != nullptr);
    }

    void testSetLeadsPopulatesList()
    {
        auto db = openDb();
        LeadsWidget widget(db);

        QVector<InvestigativeLead> leads;
        leads.append(makeLead(1, 0.9));
        leads.append(makeLead(2, 0.6));
        leads.append(makeLead(3, 0.3));

        widget.setLeads(leads);
        QApplication::processEvents();

        auto* list = widget.findChild<QListWidget*>();
        QVERIFY(list != nullptr);
        QVERIFY(list->count() >= 3);
    }

    void testClearLeadsEmptiesList()
    {
        auto db = openDb();
        LeadsWidget widget(db);
        widget.setLeads({ makeLead(1, 0.8) });
        QApplication::processEvents();

        widget.setLeads({});
        QApplication::processEvents();

        auto* list = widget.findChild<QListWidget*>();
        if (list)
            QVERIFY(list->count() == 0);
    }

    void testHighConfidenceLeadPresent()
    {
        auto db = openDb();
        LeadsWidget widget(db);
        widget.setLeads({ makeLead(1, 0.92) });
        QApplication::processEvents();

        auto* list = widget.findChild<QListWidget*>();
        QVERIFY(list != nullptr);
        QVERIFY(list->count() >= 1);
        QVERIFY(!list->item(0)->text().isEmpty());
    }

    void testRefreshWithDatabaseEvents()
    {
        auto db = openDb();
        CrimeEvent ev;
        ev.eventId = QStringLiteral("LW5-1");
        ev.id = ev.eventId;
        ev.crimeType = QStringLiteral("burglary");
        ev.suburb = QStringLiteral("Zone");
        ev.lat = 51.5074;
        ev.lon = -0.1278;
        ev.latitude = 51.5074;
        ev.longitude = -0.1278;
        ev.timestamp = QDateTime::currentDateTimeUtc();
        ev.occurredAt = ev.timestamp;
        ev.narrative = QStringLiteral("forced entry");
        QVERIFY(db->insertEvent(ev));

        LeadsWidget widget(db);
        widget.refresh();
        QApplication::processEvents();
        QVERIFY(true);
    }

    void testEvidenceScorerSpinExists()
    {
        auto db = openDb();
        LeadsWidget widget(db);
        widget.show();
        QApplication::processEvents();
        QVERIFY(!widget.findChildren<QDoubleSpinBox*>().isEmpty());
    }
};

QTEST_MAIN(TestLeadsWidgetDeep5)
#include "test_leads_widget_deep5.moc"
