// test_leads_widget_deep3.cpp - Deep audit iteration 19: LeadsWidget evidence
// scorer panel, Detected Series tab, refresh-from-DB, rich lead detail.

#include <QTest>
#include <QApplication>
#include <QListWidget>
#include <QLabel>
#include <QTextEdit>
#include <QTabWidget>
#include <QTableWidget>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QPushButton>
#include <QRegularExpression>
#include <cmath>
#include <memory>

#include "ui/LeadsWidget.h"
#include "core/Database.h"
#include "core/AppConfig.h"
#include "core/CrimeEvent.h"
#include "inference/EvidenceScorer.h"

class TestLeadsWidgetDeep3 : public QObject {
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
        lead.confidenceMethod = QStringLiteral("deep3_test");
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
        for (auto* edit : widget.findChildren<QTextEdit*>()) {
            if (edit->placeholderText().contains(QStringLiteral("Select a lead"),
                                                 Qt::CaseInsensitive))
                return edit;
        }
        return nullptr;
    }

    static QTextEdit* evidenceOutput(LeadsWidget& widget)
    {
        for (auto* edit : widget.findChildren<QTextEdit*>()) {
            if (edit->placeholderText().contains(QStringLiteral("Evidence scoring"),
                                                 Qt::CaseInsensitive))
                return edit;
        }
        return nullptr;
    }

    static QPushButton* runEvidenceButton(LeadsWidget& widget)
    {
        for (auto* btn : widget.findChildren<QPushButton*>()) {
            if (btn->text().contains(QStringLiteral("Run Evidence Scorer"),
                                     Qt::CaseInsensitive))
                return btn;
        }
        return nullptr;
    }

private slots:
    void initTestCase()
    {
        qputenv("QT_QPA_PLATFORM", "offscreen");
    }

    void testEvidenceScorerPanelHasEighteenCheckboxes()
    {
        auto db = openDb();
        LeadsWidget widget(db);
        widget.show();
        QApplication::processEvents();

        const auto checks = widget.findChildren<QCheckBox*>();
        QCOMPARE(checks.size(), 18);

        auto* prior = widget.findChild<QDoubleSpinBox*>();
        QVERIFY(prior != nullptr);
        QCOMPARE(prior->value(), 0.10);
        QVERIFY(runEvidenceButton(widget) != nullptr);
    }

    void testRunEvidenceScorerWritesOutput()
    {
        auto db = openDb();
        LeadsWidget widget(db);

        auto* btn = runEvidenceButton(widget);
        auto* out = evidenceOutput(widget);
        QVERIFY(btn != nullptr);
        QVERIFY(out != nullptr);

        btn->click();
        QApplication::processEvents();

        QVERIFY(out->toHtml().contains(QStringLiteral("Prior:")));
        QVERIFY(out->toHtml().contains(QStringLiteral("Posterior:")));
    }

    void testEvidenceCheckboxLabelsMismatchScorerKeys()
    {
        // BUG: LeadsWidget EVIDENCE_TYPES use display names ("DNA Match") but
        // EvidenceScorer LR table keys are snake_case ("dna_match_full_profile").
        auto db = openDb();
        LeadsWidget widget(db);

        QCheckBox* dnaBox = nullptr;
        for (auto* cb : widget.findChildren<QCheckBox*>()) {
            if (cb->text() == QStringLiteral("DNA Match")) {
                dnaBox = cb;
                break;
            }
        }
        QVERIFY2(dnaBox != nullptr, "DNA Match checkbox not found");

        auto* prior = widget.findChild<QDoubleSpinBox*>();
        QVERIFY(prior != nullptr);
        prior->setValue(0.10);
        dnaBox->setChecked(true);

        runEvidenceButton(widget)->click();
        QApplication::processEvents();

        EvidenceScorer scorer;
        QMap<QString, bool> mapped;
        mapped[QStringLiteral("dna_match_full_profile")] = true;
        const double expectedPosterior = scorer.score(0.10, mapped).posteriorProbability;

        const QString html = evidenceOutput(widget)->toHtml();
        QRegularExpression postRe(QStringLiteral("Posterior:\\s*([0-9.]+)"));
        const auto match = postRe.match(html);
        QVERIFY2(match.hasMatch(), qPrintable(QStringLiteral("Posterior not found in: %1").arg(html)));

        const double uiPosterior = match.captured(1).toDouble();
        QVERIFY2(std::abs(uiPosterior - expectedPosterior) < 1e-6,
                 qPrintable(QStringLiteral(
                     "UI posterior %1 should match scorer key 'dna_match_full_profile' (%2); "
                     "checkbox label mismatch leaves posterior near prior")
                     .arg(uiPosterior).arg(expectedPosterior)));
    }

    void testDetectedSeriesTabAndTableColumns()
    {
        auto db = openDb();
        LeadsWidget widget(db);

        auto* tabs = widget.findChild<QTabWidget*>();
        QVERIFY(tabs != nullptr);
        QCOMPARE(tabs->count(), 2);
        QCOMPARE(tabs->tabText(0), QStringLiteral("Investigative Leads"));
        QCOMPARE(tabs->tabText(1), QStringLiteral("Detected Series"));

        tabs->setCurrentIndex(1);
        QApplication::processEvents();

        auto* table = widget.findChild<QTableWidget*>();
        QVERIFY(table != nullptr);
        QCOMPARE(table->columnCount(), 7);
        QCOMPARE(table->horizontalHeaderItem(0)->text(), QStringLiteral("Series ID"));
        QCOMPARE(table->horizontalHeaderItem(6)->text(), QStringLiteral("Status"));
    }

    void testRefreshLoadsLeadsFromDatabase()
    {
        auto db = openDb();
        InvestigativeLead lead = makeLead(QStringLiteral("series_linkage"), 0.85, 1);
        lead.provenance = { QStringLiteral("DB ingest"), QStringLiteral("Rule engine") };
        QVERIFY(db->insertLead(lead, QStringLiteral("EVT-DEEP3-001")));

        LeadsWidget widget(db);
        widget.setLeads({}, QStringLiteral("EVT-DEEP3-001"));
        QCOMPARE(widget.findChild<QListWidget*>()->count(), 0);

        widget.refresh();
        QApplication::processEvents();

        auto* list = widget.findChild<QListWidget*>();
        QVERIFY(list != nullptr);
        QCOMPARE(list->count(), 1);
        QCOMPARE(countLabel(widget)->text(), QStringLiteral("1 lead"));
    }

    void testSingleLeadCountLabelGrammar()
    {
        auto db = openDb();
        LeadsWidget widget(db);

        widget.setLeads({ makeLead(QStringLiteral("mo_similarity"), 0.6, 1) });
        QApplication::processEvents();

        QCOMPARE(countLabel(widget)->text(), QStringLiteral("1 lead"));
    }

    void testLeadDetailShowsProvenanceAndSupportingData()
    {
        auto db = openDb();
        LeadsWidget widget(db);

        InvestigativeLead lead = makeLead(QStringLiteral("network_link"), 0.77, 1);
        lead.provenance = { QStringLiteral("Source A"), QStringLiteral("Inference B") };
        lead.supportingData.insert(QStringLiteral("match_score"), 0.91);
        lead.relatedEventIds = { QStringLiteral("REL-001"), QStringLiteral("REL-002") };

        widget.setLeads({ lead });
        auto* list = widget.findChild<QListWidget*>();
        emit list->itemClicked(list->item(0));
        QApplication::processEvents();

        const QString html = detailPanel(widget)->toHtml();
        QVERIFY(html.contains(QStringLiteral("Provenance")));
        QVERIFY(html.contains(QStringLiteral("Source A")));
        QVERIFY(html.contains(QStringLiteral("Supporting Data")));
        QVERIFY(html.contains(QStringLiteral("match_score")));
        QVERIFY(html.contains(QStringLiteral("REL-001")));
    }

    void testTabSwitchDoesNotLoseLeadList()
    {
        auto db = openDb();
        LeadsWidget widget(db);
        widget.setLeads({
            makeLead(QStringLiteral("statistical_anomaly"), 0.5, 1),
            makeLead(QStringLiteral("geographic_profile"), 0.62, 2),
        });

        auto* tabs = widget.findChild<QTabWidget*>();
        tabs->setCurrentIndex(1);
        QApplication::processEvents();
        tabs->setCurrentIndex(0);
        QApplication::processEvents();

        QCOMPARE(widget.findChild<QListWidget*>()->count(), 2);
    }
};

QTEST_MAIN(TestLeadsWidgetDeep3)

#include "test_leads_widget_deep3.moc"
