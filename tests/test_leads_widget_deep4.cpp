// test_leads_widget_deep4.cpp — Deep audit iteration 21: LeadsWidget
// evidence-key mapping bugs, series tab refresh, confidence tiers, empty state.

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

class TestLeadsWidgetDeep4 : public QObject {
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
        lead.headline         = QStringLiteral("Lead %1").arg(rank);
        lead.detail           = QStringLiteral("Detail %1").arg(rank);
        lead.confidence       = confidence;
        lead.confidenceMethod = QStringLiteral("deep4_test");
        lead.generatedAt      = QDateTime::currentDateTimeUtc();
        return lead;
    }

    static CrimeEvent clusterEvent(int index, double latOffset = 0.0, int dayOffset = 0)
    {
        CrimeEvent ev;
        ev.eventId    = QStringLiteral("LW4-%1").arg(index, 3, 10, QChar('0'));
        ev.id         = ev.eventId;
        ev.crimeType  = QStringLiteral("burglary");
        ev.suburb     = QStringLiteral("ClusterZone");
        ev.lat        = 51.5074 + latOffset;
        ev.lon        = -0.1278;
        ev.latitude   = ev.lat.value();
        ev.longitude  = ev.lon.value();
        ev.source     = QStringLiteral("deep4_test");
        ev.occurredAt = QDateTime::currentDateTimeUtc().addDays(-dayOffset);
        ev.timestamp  = ev.occurredAt.value();
        ev.ingestedAt = QDateTime::currentDateTimeUtc();
        ev.narrative  = QStringLiteral("forced entry residential");
        return ev;
    }

    static void seedCluster(const std::shared_ptr<Database>& db, int count)
    {
        for (int i = 0; i < count; ++i)
            QVERIFY2(db->insertEvent(clusterEvent(i, i * 0.0001, i % 2)),
                     qPrintable(db->lastError()));
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

    static QCheckBox* checkboxByLabel(LeadsWidget& widget, const QString& label)
    {
        for (auto* cb : widget.findChildren<QCheckBox*>()) {
            if (cb->text() == label)
                return cb;
        }
        return nullptr;
    }

    static double posteriorFromOutput(QTextEdit* out)
    {
        const QString html = out->toHtml();
        QRegularExpression postRe(QStringLiteral("Posterior:\\s*([0-9.]+)"));
        const auto match = postRe.match(html);
        Q_ASSERT(match.hasMatch());
        return match.captured(1).toDouble();
    }

private slots:
    void initTestCase()
    {
        qputenv("QT_QPA_PLATFORM", "offscreen");
    }

    void testTemporalProximityUsesPhoneRecordsKey()
    {
        // BUG LeadsWidget.cpp:41-60 — "Temporal Proximity" maps to phone_records_at_scene.
        auto db = openDb();
        LeadsWidget widget(db);

        auto* temporal = checkboxByLabel(widget, QStringLiteral("Temporal Proximity"));
        QVERIFY(temporal != nullptr);
        temporal->setChecked(true);

        widget.findChild<QDoubleSpinBox*>()->setValue(0.10);
        runEvidenceButton(widget)->click();
        QApplication::processEvents();

        EvidenceScorer scorer;
        QMap<QString, bool> mapped;
        mapped[QStringLiteral("phone_records_at_scene")] = true;
        const double expected = scorer.score(0.10, mapped).posteriorProbability;

        const double uiPosterior = posteriorFromOutput(evidenceOutput(widget));
        QVERIFY2(std::abs(uiPosterior - expected) < 1e-3,
                 qPrintable(QStringLiteral(
                     "Temporal Proximity uses phone_records_at_scene; UI=%1 expected=%2")
                     .arg(uiPosterior).arg(expected)));
    }

    void testDuplicateEvidenceKeysLastCheckboxWins()
    {
        // BUG LeadsWidget.cpp:41-60 — duplicate keys in EVIDENCE_SCORER_KEYS:
        // network_link_direct (Series Linkage + Associate Network Link),
        // phone_records_at_scene (Temporal Proximity + Communications Record),
        // informant_tip_reliable (Financial Transaction + Expert Analysis).
        auto db = openDb();
        LeadsWidget widget(db);

        auto* seriesLink = checkboxByLabel(widget, QStringLiteral("Series Linkage"));
        auto* associate  = checkboxByLabel(widget, QStringLiteral("Associate Network Link"));
        QVERIFY(seriesLink != nullptr);
        QVERIFY(associate != nullptr);

        seriesLink->setChecked(true);
        associate->setChecked(true);
        widget.findChild<QDoubleSpinBox*>()->setValue(0.10);
        runEvidenceButton(widget)->click();
        QApplication::processEvents();

        EvidenceScorer scorer;
        QMap<QString, bool> once;
        once[QStringLiteral("network_link_direct")] = true;
        const double expectedOnce = scorer.score(0.10, once).posteriorProbability;

        const double uiPosterior = posteriorFromOutput(evidenceOutput(widget));
        QVERIFY2(std::abs(uiPosterior - expectedOnce) < 1e-3,
                 qPrintable(QStringLiteral(
                     "Duplicate network_link_direct keys should not double-apply LR; got %1 vs %2")
                     .arg(uiPosterior).arg(expectedOnce)));
    }

    void testStatisticalAnomalyMapsToBloodTypeMatch()
    {
        auto db = openDb();
        LeadsWidget widget(db);

        auto* anomaly = checkboxByLabel(widget, QStringLiteral("Statistical Anomaly Flag"));
        QVERIFY(anomaly != nullptr);
        anomaly->setChecked(true);

        widget.findChild<QDoubleSpinBox*>()->setValue(0.10);
        runEvidenceButton(widget)->click();
        QApplication::processEvents();

        EvidenceScorer scorer;
        QMap<QString, bool> mapped;
        mapped[QStringLiteral("blood_type_match")] = true;
        const double expected = scorer.score(0.10, mapped).posteriorProbability;

        const double uiPosterior = posteriorFromOutput(evidenceOutput(widget));
        QVERIFY2(std::abs(uiPosterior - expected) < 1e-6,
                 qPrintable(QStringLiteral(
                     "Statistical Anomaly Flag maps to blood_type_match; UI=%1 expected=%2")
                     .arg(uiPosterior).arg(expected)));
    }

    void testSeriesRefreshPopulatesDetectedSeriesTab()
    {
        auto db = openDb();
        seedCluster(db, 4);

        LeadsWidget widget(db);
        widget.refresh();
        QApplication::processEvents();

        auto* tabs = widget.findChild<QTabWidget*>();
        QVERIFY(tabs != nullptr);
        tabs->setCurrentIndex(1);
        QApplication::processEvents();

        auto* table = widget.findChild<QTableWidget*>();
        QVERIFY(table != nullptr);
        QVERIFY2(table->rowCount() >= 1,
                 qPrintable(QStringLiteral("Expected at least one detected series, got %1 rows")
                                .arg(table->rowCount())));

        bool foundActive = false;
        for (int row = 0; row < table->rowCount(); ++row) {
            if (auto* status = table->item(row, 6)) {
                if (status->text() == QStringLiteral("ACTIVE")
                    || status->text() == QStringLiteral("HIGH")
                    || status->text() == QStringLiteral("CRITICAL"))
                    foundActive = true;
            }
        }
        QVERIFY(foundActive);
    }

    void testSeriesStatusCriticalAtTenMembers()
    {
        auto db = openDb();
        seedCluster(db, 10);

        LeadsWidget widget(db);
        widget.refresh();
        QApplication::processEvents();

        auto* table = widget.findChild<QTableWidget*>();
        QVERIFY(table != nullptr);

        bool foundCritical = false;
        for (int row = 0; row < table->rowCount(); ++row) {
            if (auto* status = table->item(row, 6)) {
                if (status->text() == QStringLiteral("CRITICAL"))
                    foundCritical = true;
            }
        }
        QVERIFY2(foundCritical,
                 qPrintable(QStringLiteral("10-member cluster should yield CRITICAL status")));
    }

    void testEmptyLeadsClearsDetailPanel()
    {
        auto db = openDb();
        LeadsWidget widget(db);

        widget.setLeads({ makeLead(QStringLiteral("mo_similarity"), 0.6, 1) });
        widget.setLeads({});
        QApplication::processEvents();

        QCOMPARE(widget.findChild<QListWidget*>()->count(), 0);
        QVERIFY(detailPanel(widget)->toPlainText().trimmed().isEmpty()
                || detailPanel(widget)->toHtml().trimmed().isEmpty());
    }

    void testConfidenceTierColorsInList()
    {
        auto db = openDb();
        LeadsWidget widget(db);
        widget.setLeads({
            makeLead(QStringLiteral("series_linkage"), 0.85, 1),
            makeLead(QStringLiteral("mo_similarity"), 0.55, 2),
            makeLead(QStringLiteral("network_link"), 0.25, 3),
        });

        auto* list = widget.findChild<QListWidget*>();
        QVERIFY(list != nullptr);
        QCOMPARE(list->count(), 3);

        QCOMPARE(list->item(0)->foreground().color(), QColor(QStringLiteral("#81c784")));
        QCOMPARE(list->item(1)->foreground().color(), QColor(QStringLiteral("#ffb74d")));
        QCOMPARE(list->item(2)->foreground().color(), QColor(QStringLiteral("#e57373")));
    }

    void testExportButtonPresentAndLeadsCountUpdates()
    {
        auto db = openDb();
        LeadsWidget widget(db);

        QPushButton* exportBtn = nullptr;
        for (auto* btn : widget.findChildren<QPushButton*>()) {
            if (btn->text().contains(QStringLiteral("Export Report"), Qt::CaseInsensitive)) {
                exportBtn = btn;
                break;
            }
        }
        QVERIFY(exportBtn != nullptr);

        widget.setLeads({
            makeLead(QStringLiteral("geographic_profile"), 0.7, 1),
            makeLead(QStringLiteral("statistical_anomaly"), 0.4, 2),
        });
        QCOMPARE(countLabel(widget)->text(), QStringLiteral("2 leads"));
    }
};

QTEST_MAIN(TestLeadsWidgetDeep4)

#include "test_leads_widget_deep4.moc"
