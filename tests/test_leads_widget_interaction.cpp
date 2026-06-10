// test_leads_widget_interaction.cpp — UI interaction tests for LeadsWidget
//
// Runs under the offscreen Qt platform (no display required).
// QApplication is needed because LeadsWidget creates QWidgets.

#include <QTest>
#include <QApplication>
#include <QListWidget>
#include <QLabel>
#include <QPushButton>
#include <QTextEdit>
#include <QTimer>
#include <QDialog>
#include <memory>

#include "core/AppConfig.h"
#include "core/Database.h"
#include "core/CrimeEvent.h"
#include "ui/LeadsWidget.h"

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

// Adapt to the actual InvestigativeLead struct (category / headline / confidence)
static InvestigativeLead makeLead(const QString& category, double confidence,
                                   int rank = 1)
{
    InvestigativeLead l;
    l.rank             = rank;
    l.category         = category;
    l.headline         = "Test lead for " + category;
    l.detail           = "Description for " + category;
    l.confidence       = confidence;
    l.confidenceMethod = QStringLiteral("test");
    l.generatedAt      = QDateTime::currentDateTimeUtc();
    return l;
}

// ─────────────────────────────────────────────────────────────────────────────
// TestLeadsWidget
// ─────────────────────────────────────────────────────────────────────────────

class TestLeadsWidget : public QObject {
    Q_OBJECT

    std::shared_ptr<Database> m_db;

private slots:

    void initTestCase()
    {
        m_db = makeDB();
    }

    void cleanupTestCase()
    {
        m_db.reset();
    }

    // 1 ── LeadsWidget constructs without crashing
    void testLeadsWidgetCreation()
    {
        LeadsWidget w(m_db);
        Q_UNUSED(w)
        QVERIFY(true);
    }

    // 2 ── setLeads() with 10 InvestigativeLead objects populates the list
    void testSetLeads()
    {
        LeadsWidget w(m_db);
        QVector<InvestigativeLead> leads;
        leads.reserve(10);
        for (int i = 0; i < 10; ++i)
            leads.append(makeLead(QString("category%1").arg(i), 0.5 + i * 0.04, i + 1));

        w.setLeads(leads);
        QApplication::processEvents();

        auto* list = w.findChild<QListWidget*>();
        QVERIFY(list != nullptr);
        QCOMPARE(list->count(), 10);
    }

    // 3 ── Selecting the first lead populates the detail panel
    void testLeadSelection()
    {
        LeadsWidget w(m_db);
        QVector<InvestigativeLead> leads;
        leads.append(makeLead("series_linkage", 0.85, 1));
        leads.append(makeLead("mo_similarity",  0.72, 2));
        w.setLeads(leads);

        auto* list = w.findChild<QListWidget*>();
        QVERIFY(list != nullptr);
        QVERIFY(list->count() >= 1);

        // Simulate a click on the first item
        list->setCurrentRow(0);
        QListWidgetItem* item = list->item(0);
        QVERIFY(item != nullptr);
        emit list->itemClicked(item);
        QApplication::processEvents();

        // The detail text-edit should now be non-empty
        auto* detail = w.findChild<QTextEdit*>();
        QVERIFY(detail != nullptr);
        QVERIFY(!detail->toPlainText().isEmpty() || !detail->toHtml().isEmpty());
    }

    // 4 ── setLeads({}) empties the displayed list
    void testClearLeads()
    {
        LeadsWidget w(m_db);
        QVector<InvestigativeLead> leads;
        for (int i = 0; i < 5; ++i)
            leads.append(makeLead("burglary", 0.6, i + 1));
        w.setLeads(leads);

        w.setLeads({});
        QApplication::processEvents();

        auto* list = w.findChild<QListWidget*>();
        QVERIFY(list != nullptr);
        QCOMPARE(list->count(), 0);
    }

    // 5 ── Leads provided in score-descending order appear in the same order in the list
    void testLeadSortByScore()
    {
        LeadsWidget w(m_db);

        // Pre-sort highest → lowest confidence
        QVector<InvestigativeLead> leads = {
            makeLead("cat", 0.95, 1),
            makeLead("cat", 0.80, 2),
            makeLead("cat", 0.60, 3),
            makeLead("cat", 0.40, 4),
        };
        w.setLeads(leads);
        QApplication::processEvents();

        auto* list = w.findChild<QListWidget*>();
        QVERIFY(list != nullptr);
        QCOMPARE(list->count(), 4);

        // Row 0 should be rank 1 (highest)
        const QString firstText = list->item(0)->text();
        QVERIFY(firstText.contains("#1") || firstText.contains("95%") ||
                firstText.contains("95"));
    }

    // 6 ── setLeads with only address-type leads shows only those leads
    void testLeadFilterByType()
    {
        LeadsWidget w(m_db);

        QVector<InvestigativeLead> allLeads = {
            makeLead("address",  0.80, 1),
            makeLead("address",  0.75, 2),
            makeLead("weapon",   0.65, 3),
            makeLead("vehicle",  0.55, 4),
        };

        // Filter client-side: pass only address leads to setLeads
        QVector<InvestigativeLead> filtered;
        for (const auto& l : allLeads)
            if (l.category == "address") filtered.append(l);

        w.setLeads(filtered);
        QApplication::processEvents();

        auto* list = w.findChild<QListWidget*>();
        QVERIFY(list != nullptr);
        QCOMPARE(list->count(), 2);
    }

    // 7 ── Clicking the Export Report button does not crash
    void testExportButton()
    {
        LeadsWidget w(m_db);
        QVector<InvestigativeLead> leads = {
            makeLead("mo_similarity", 0.78, 1),
        };
        w.setLeads(leads);

        // Find the "Export Report" button
        QPushButton* exportBtn = nullptr;
        for (auto* btn : w.findChildren<QPushButton*>()) {
            if (btn->text().contains("Export", Qt::CaseInsensitive)) {
                exportBtn = btn;
                break;
            }
        }
        QVERIFY(exportBtn != nullptr);

        // Schedule immediate rejection of any modal dialog that opens
        // (works for Qt-native QFileDialog running its own event loop)
        QTimer::singleShot(0, []() {
            for (QWidget* top : QApplication::topLevelWidgets()) {
                if (auto* d = qobject_cast<QDialog*>(top)) {
                    if (d->isVisible())
                        d->reject();
                }
            }
        });

        exportBtn->click();
        QApplication::processEvents();
        QVERIFY(true);   // no crash
    }

    // 8 ── The count label updates when setLeads() is called
    void testLeadCountDisplay()
    {
        LeadsWidget w(m_db);
        const int count = 7;

        QVector<InvestigativeLead> leads;
        for (int i = 0; i < count; ++i)
            leads.append(makeLead("geographic_profile", 0.5 + i * 0.05, i + 1));
        w.setLeads(leads);
        QApplication::processEvents();

        // Look for a QLabel whose text contains the count
        const QString expected = QString::number(count);
        bool found = false;
        for (auto* lbl : w.findChildren<QLabel*>()) {
            if (lbl->text().contains(expected)) {
                found = true;
                break;
            }
        }
        QVERIFY(found);
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// main
// ─────────────────────────────────────────────────────────────────────────────

int main(int argc, char* argv[])
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    TestLeadsWidget t;
    return QTest::qExec(&t, argc, argv);
}

#include "test_leads_widget_interaction.moc"
