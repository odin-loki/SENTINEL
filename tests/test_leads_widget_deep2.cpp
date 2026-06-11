// test_leads_widget_deep2.cpp — Deep audit iteration 16: LeadsWidget construct,
// setLeads populated/empty, count label and detail empty state.

#include <QTest>
#include <QApplication>
#include <QListWidget>
#include <QLabel>
#include <QTextEdit>
#include <QRegularExpression>
#include <memory>

#include "ui/LeadsWidget.h"
#include "core/Database.h"
#include "core/AppConfig.h"
#include "core/CrimeEvent.h"

class TestLeadsWidgetDeep2 : public QObject {
    Q_OBJECT

private:
    static std::shared_ptr<Database> openDb()
    {
        AppConfig cfg;
        cfg.databasePath = QStringLiteral(":memory:");
        auto db = std::make_shared<Database>(cfg);
        db->open();
        return db;
    }

    static InvestigativeLead makeLead(const QString& category, double confidence, int rank)
    {
        InvestigativeLead lead;
        lead.rank             = rank;
        lead.category         = category;
        lead.headline         = QStringLiteral("Lead headline %1").arg(rank);
        lead.detail           = QStringLiteral("Detail for rank %1").arg(rank);
        lead.confidence       = confidence;
        lead.confidenceMethod = QStringLiteral("deep2_test");
        lead.generatedAt      = QDateTime::currentDateTimeUtc();
        return lead;
    }

    static QLabel* countLabel(LeadsWidget& widget)
    {
        static const QRegularExpression kCountRe(QStringLiteral(R"(^\d+ leads?$)"));
        for (auto* lbl : widget.findChildren<QLabel*>()) {
            if (kCountRe.match(lbl->text()).hasMatch())
                return lbl;
        }
        return nullptr;
    }

    static QTextEdit* detailPanel(LeadsWidget& widget)
    {
        auto edits = widget.findChildren<QTextEdit*>();
        for (auto* edit : edits) {
            if (edit->placeholderText().contains(QStringLiteral("Select a lead"),
                                                 Qt::CaseInsensitive))
                return edit;
        }
        return edits.isEmpty() ? nullptr : edits.first();
    }

private slots:
    void initTestCase()
    {
        qputenv("QT_QPA_PLATFORM", "offscreen");
    }

    void testConstruct()
    {
        auto db = openDb();
        LeadsWidget widget(db);
        widget.resize(1024, 768);
        widget.show();
        QApplication::processEvents();

        QVERIFY(widget.width() > 0);
        QVERIFY(widget.findChild<QListWidget*>() != nullptr);
        QVERIFY(countLabel(widget) != nullptr);
        QVERIFY(detailPanel(widget) != nullptr);
    }

    void testSetLeadsEmpty()
    {
        auto db = openDb();
        LeadsWidget widget(db);

        widget.setLeads({});
        QApplication::processEvents();

        auto* list = widget.findChild<QListWidget*>();
        QVERIFY(list != nullptr);
        QCOMPARE(list->count(), 0);

        auto* lbl = countLabel(widget);
        QVERIFY(lbl != nullptr);
        QCOMPARE(lbl->text(), QStringLiteral("0 leads"));
    }

    void testSetLeadsPopulated()
    {
        auto db = openDb();
        LeadsWidget widget(db);

        QVector<InvestigativeLead> leads;
        leads.append(makeLead(QStringLiteral("series_linkage"), 0.88, 1));
        leads.append(makeLead(QStringLiteral("mo_similarity"), 0.72, 2));
        leads.append(makeLead(QStringLiteral("geographic_profile"), 0.55, 3));
        leads.append(makeLead(QStringLiteral("statistical_anomaly"), 0.41, 4));
        leads.append(makeLead(QStringLiteral("network_link"), 0.33, 5));

        widget.setLeads(leads, QStringLiteral("EVT-TEST-001"));
        QApplication::processEvents();

        auto* list = widget.findChild<QListWidget*>();
        QVERIFY(list != nullptr);
        QCOMPARE(list->count(), leads.size());

        auto* lbl = countLabel(widget);
        QVERIFY(lbl != nullptr);
        QCOMPARE(lbl->text(), QStringLiteral("5 leads"));

        QCOMPARE(list->item(0)->data(Qt::UserRole).toInt(), 0);
        QVERIFY(list->item(0)->text().contains(QStringLiteral("#1")));
    }

    void testEmptyStateAfterClear()
    {
        auto db = openDb();
        LeadsWidget widget(db);

        QVector<InvestigativeLead> leads;
        for (int i = 0; i < 3; ++i)
            leads.append(makeLead(QStringLiteral("mo_similarity"), 0.6 + i * 0.05, i + 1));
        widget.setLeads(leads);
        QCOMPARE(widget.findChild<QListWidget*>()->count(), 3);

        auto* list = widget.findChild<QListWidget*>();
        emit list->itemClicked(list->item(0));
        QApplication::processEvents();
        auto* detail = detailPanel(widget);
        QVERIFY(!detail->toHtml().isEmpty());

        widget.setLeads({});
        QApplication::processEvents();

        QCOMPARE(widget.findChild<QListWidget*>()->count(), 0);
        QCOMPARE(countLabel(widget)->text(), QStringLiteral("0 leads"));
        // setLeads({}) should reset the detail panel (currently stale — see bugs report)
        QVERIFY2(detail->toPlainText().trimmed().isEmpty()
                     && !detail->toHtml().contains(QStringLiteral("mo_similarity")),
                 "Detail panel should clear when leads are emptied");
    }

    void testDetailPanelEmptyState()
    {
        auto db = openDb();
        LeadsWidget widget(db);
        widget.setLeads({});
        QApplication::processEvents();

        auto* detail = detailPanel(widget);
        QVERIFY(detail != nullptr);
        QVERIFY(detail->toPlainText().isEmpty());
        QVERIFY(detail->toHtml().isEmpty() || detail->toHtml().contains(QStringLiteral("<html")));
    }

    void testLeadSelectionPopulatesDetail()
    {
        auto db = openDb();
        LeadsWidget widget(db);

        QVector<InvestigativeLead> leads;
        leads.append(makeLead(QStringLiteral("series_linkage"), 0.91, 1));
        widget.setLeads(leads);

        auto* list = widget.findChild<QListWidget*>();
        QVERIFY(list != nullptr);
        QVERIFY(list->count() >= 1);

        QListWidgetItem* item = list->item(0);
        QVERIFY(item != nullptr);
        emit list->itemClicked(item);
        QApplication::processEvents();

        auto* detail = detailPanel(widget);
        QVERIFY(detail != nullptr);
        QVERIFY(!detail->toHtml().isEmpty());
        QVERIFY(detail->toHtml().contains(QStringLiteral("series_linkage"))
               || detail->toHtml().contains(QStringLiteral("Lead headline")));
    }
};

QTEST_MAIN(TestLeadsWidgetDeep2)

#include "test_leads_widget_deep2.moc"
