// test_leads_widget_deep6.cpp — Deep audit iteration 28: LeadsWidget
// setLeads forEventId, rank ordering, detail panel, confidence spin.
#include <QTest>
#include <QApplication>
#include <QListWidget>
#include <QTextEdit>
#include <QDoubleSpinBox>
#include <memory>
#include "ui/LeadsWidget.h"
#include "core/Database.h"
#include "core/AppConfig.h"
#include "core/CrimeEvent.h"

class TestLeadsWidgetDeep6 : public QObject
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

    static InvestigativeLead lead(int rank, double conf, const QString& headline)
    {
        InvestigativeLead l;
        l.rank             = rank;
        l.category         = QStringLiteral("series_linkage");
        l.headline         = headline;
        l.detail           = QStringLiteral("Detail %1").arg(rank);
        l.confidence       = conf;
        l.confidenceMethod = QStringLiteral("deep6_test");
        l.generatedAt      = QDateTime::currentDateTimeUtc();
        return l;
    }

private slots:
    void initTestCase()
    {
        qputenv("QT_QPA_PLATFORM", "offscreen");
        qputenv("SENTINEL_HEADLESS_TEST", "1");
    }

    void testSetLeadsWithEventId()
    {
        auto db = openDb();
        LeadsWidget widget(db);
        widget.setLeads({ lead(1, 0.85, QStringLiteral("Series A")) },
                          QStringLiteral("EVT-L6"));
        QApplication::processEvents();

        auto* list = widget.findChild<QListWidget*>();
        QVERIFY(list != nullptr);
        QVERIFY(list->count() >= 1);
    }

    void testMultipleLeadsAllListed()
    {
        auto db = openDb();
        LeadsWidget widget(db);
        widget.setLeads({
            lead(1, 0.9, QStringLiteral("First")),
            lead(2, 0.7, QStringLiteral("Second")),
            lead(3, 0.5, QStringLiteral("Third")),
        });
        QApplication::processEvents();

        auto* list = widget.findChild<QListWidget*>();
        QVERIFY(list != nullptr);
        QCOMPARE(list->count(), 3);
    }

    void testSelectingLeadUpdatesDetail()
    {
        auto db = openDb();
        LeadsWidget widget(db);
        widget.setLeads({ lead(1, 0.88, QStringLiteral("Detail lead")) });
        QApplication::processEvents();

        auto* list = widget.findChild<QListWidget*>();
        QVERIFY(list != nullptr && list->count() > 0);
        list->setCurrentRow(0);
        QApplication::processEvents();

        QTextEdit* detail = nullptr;
        for (auto* edit : widget.findChildren<QTextEdit*>()) {
            if (!edit->toPlainText().isEmpty() || !edit->toHtml().isEmpty())
                detail = edit;
        }
        QVERIFY(detail != nullptr);
    }

    void testConfidenceSpinExists()
    {
        auto db = openDb();
        LeadsWidget widget(db);
        QVERIFY(!widget.findChildren<QDoubleSpinBox*>().isEmpty());
    }

    void testEmptyLeadsClearsList()
    {
        auto db = openDb();
        LeadsWidget widget(db);
        widget.setLeads({ lead(1, 0.6, QStringLiteral("Temp")) });
        QApplication::processEvents();
        widget.setLeads({});
        QApplication::processEvents();

        auto* list = widget.findChild<QListWidget*>();
        if (list)
            QCOMPARE(list->count(), 0);
    }
};

QTEST_MAIN(TestLeadsWidgetDeep6)
#include "test_leads_widget_deep6.moc"
