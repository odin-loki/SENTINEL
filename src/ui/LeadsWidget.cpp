#include "ui/LeadsWidget.h"

#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QGridLayout>
#include <QFormLayout>
#include <QFileDialog>
#include <QFile>
#include <QTextStream>
#include <QMessageBox>
#include <QScrollArea>
#include <QFrame>
#include <QFont>
#include <QHeaderView>
#include <QDateTime>
#include <QTimeZone>
#include <algorithm>

// The 18 evidence types shown in the scorer panel (display labels)
static const QStringList EVIDENCE_TYPES = {
    "Physical Evidence at Scene",
    "Witness Testimony",
    "CCTV / Camera Footage",
    "Digital / Electronic Evidence",
    "DNA Match",
    "Fingerprint Match",
    "Vehicle Match",
    "MO Similarity",
    "Series Linkage",
    "Geographic Proximity",
    "Temporal Proximity",
    "Prior Offences Match",
    "Associate Network Link",
    "Communications Record",
    "Financial Transaction Link",
    "Alibi Contradiction",
    "Expert Analysis Report",
    "Statistical Anomaly Flag"
};

// Parallel EvidenceScorer LR-table keys for each checkbox (same order as EVIDENCE_TYPES)
static const QStringList EVIDENCE_SCORER_KEYS = {
    QStringLiteral("tool_mark_match"),
    QStringLiteral("eyewitness_identification_ideal"),
    QStringLiteral("cctv_clear_face"),
    QStringLiteral("digital_device_at_scene"),
    QStringLiteral("dna_match_full_profile"),
    QStringLiteral("fingerprint_match_10pt"),
    QStringLiteral("vehicle_at_scene"),
    QStringLiteral("modus_operandi_match_high"),
    QStringLiteral("network_link_direct"),
    QStringLiteral("geographic_profile_in_peak_zone"),
    QStringLiteral("phone_records_at_scene"),
    QStringLiteral("prior_conviction_same_type"),
    QStringLiteral("network_link_direct"),
    QStringLiteral("phone_records_at_scene"),
    QStringLiteral("informant_tip_reliable"),
    QStringLiteral("no_alibi"),
    QStringLiteral("informant_tip_reliable"),
    QStringLiteral("blood_type_match"),
};

// ─────────────────────────────────────────────────────────────────────────────
LeadsWidget::LeadsWidget(std::shared_ptr<Database> db, QWidget* parent)
    : QWidget(parent)
    , m_db(std::move(db))
    , m_leadsList(nullptr)
    , m_detailPanel(nullptr)
    , m_evidenceBox(nullptr)
    , m_priorSpin(nullptr)
    , m_evidenceOutput(nullptr)
    , m_runEvidenceBtn(nullptr)
    , m_exportBtn(nullptr)
    , m_countLabel(nullptr)
    , m_seriesTable(nullptr)
{
    setupUI();
}

// ─────────────────────────────────────────────────────────────────────────────
QString LeadsWidget::categoryIcon(const QString& category) const
{
    if (category == "series_linkage")      return "🔗";
    if (category == "mo_similarity")       return "🔎";
    if (category == "geographic_profile")  return "📍";
    if (category == "statistical_anomaly") return "⚠";
    if (category == "network_link")        return "🕸";
    return "•";
}

QString LeadsWidget::confidenceBar(double confidence) const
{
    const int filled = static_cast<int>(confidence * 8.0 + 0.5);
    const int empty  = 8 - filled;
    return QString("█").repeated(filled) + QString("░").repeated(empty);
}

// ─────────────────────────────────────────────────────────────────────────────
void LeadsWidget::setupUI()
{
    setStyleSheet(R"(
        QListWidget {
            background-color: #16213e;
            border: none;
            outline: none;
        }
        QListWidget::item {
            color: #eaeaea;
            padding: 10px 12px;
            border-bottom: 1px solid #1a2a4a;
        }
        QListWidget::item:selected {
            background-color: #1a2a4a;
            color: #e94560;
        }
        QListWidget::item:hover:!selected { background-color: #1a2035; }
        QGroupBox {
            border: 1px solid #1a2a4a;
            border-radius: 8px;
            margin-top: 8px;
            padding: 8px;
            background-color: #16213e;
        }
        QGroupBox::title {
            color: #a0a8b8;
            subcontrol-origin: margin;
            left: 12px;
            font-size: 11px;
            letter-spacing: 1px;
        }
        QTextEdit {
            background-color: #16213e;
            color: #eaeaea;
            border: none;
        }
        QDoubleSpinBox, QSpinBox {
            background-color: #1a2035;
            color: #eaeaea;
            border: 1px solid #0f3460;
            border-radius: 4px;
            padding: 3px 6px;
        }
        QPushButton {
            background-color: #0f3460;
            color: #eaeaea;
            border: none;
            border-radius: 4px;
            padding: 6px 16px;
        }
        QPushButton:hover { background-color: #e94560; }
        QCheckBox { color: #eaeaea; spacing: 6px; }
        QCheckBox::indicator { width: 14px; height: 14px; border-radius: 3px;
                               border: 1px solid #0f3460; background: #1a2035; }
        QCheckBox::indicator:checked { background: #e94560; border-color: #e94560; }
    )");

    auto* outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(16, 16, 16, 16);
    outerLayout->setSpacing(12);

    // ── Page title + count + export ──────────────────────────────────────────
    auto* titleRow = new QHBoxLayout();
    auto* titleLbl = new QLabel("Investigative Leads", this);
    titleLbl->setStyleSheet("color: #eaeaea; font-size: 22px; font-weight: bold;");
    m_countLabel = new QLabel("0 leads", this);
    m_countLabel->setStyleSheet("color: #4a5568; font-size: 12px;");
    m_exportBtn = new QPushButton("Export Report", this);
    m_exportBtn->setStyleSheet(
        "QPushButton { background-color: #1a3a2a; color: #81c784; }"
        "QPushButton:hover { background-color: #2e7d32; }");
    titleRow->addWidget(titleLbl);
    titleRow->addSpacing(16);
    titleRow->addWidget(m_countLabel);
    titleRow->addStretch();
    titleRow->addWidget(m_exportBtn);
    outerLayout->addLayout(titleRow);

    // ── Main horizontal split: leads list | right panel ─────────────────────
    auto* mainSplit = new QSplitter(Qt::Horizontal, this);
    mainSplit->setHandleWidth(4);
    mainSplit->setStyleSheet("QSplitter::handle { background: #0f3460; }");

    // ── Leads list (left) ────────────────────────────────────────────────────
    m_leadsList = new QListWidget(this);
    m_leadsList->setMinimumWidth(260);
    m_leadsList->setMaximumWidth(360);
    mainSplit->addWidget(m_leadsList);

    // ── Right panel: vertical splitter (detail + evidence scorer) ────────────
    auto* rightSplit = new QSplitter(Qt::Vertical, this);
    rightSplit->setHandleWidth(4);
    rightSplit->setStyleSheet("QSplitter::handle { background: #0f3460; }");

    // Detail panel
    m_detailPanel = new QTextEdit(this);
    m_detailPanel->setReadOnly(true);
    m_detailPanel->setMinimumHeight(200);
    m_detailPanel->setPlaceholderText("Select a lead to view details…");
    rightSplit->addWidget(m_detailPanel);

    // ── Evidence scorer panel ────────────────────────────────────────────────
    m_evidenceBox = new QGroupBox("Evidence Scorer (Bayesian)", this);
    auto* evidLayout = new QVBoxLayout(m_evidenceBox);
    evidLayout->setSpacing(8);

    // Prior probability row
    auto* priorRow = new QHBoxLayout();
    auto* priorLbl = new QLabel("Prior Probability (P₀):", m_evidenceBox);
    priorLbl->setStyleSheet("color: #a0a8b8;");
    m_priorSpin = new QDoubleSpinBox(m_evidenceBox);
    m_priorSpin->setRange(0.01, 0.99);
    m_priorSpin->setSingleStep(0.01);
    m_priorSpin->setValue(0.10);
    m_priorSpin->setDecimals(2);
    m_priorSpin->setFixedWidth(80);
    priorRow->addWidget(priorLbl);
    priorRow->addWidget(m_priorSpin);
    priorRow->addStretch();
    evidLayout->addLayout(priorRow);

    // Evidence checkboxes: 3 columns × 6 rows
    auto* checkGrid = new QGridLayout();
    checkGrid->setSpacing(4);
    m_evidenceChecks.clear();
    for (int i = 0; i < EVIDENCE_TYPES.size(); ++i) {
        auto* cb = new QCheckBox(EVIDENCE_TYPES[i], m_evidenceBox);
        m_evidenceChecks.append(cb);
        checkGrid->addWidget(cb, i / 3, i % 3);
    }
    evidLayout->addLayout(checkGrid);

    // Run button + output
    auto* runRow = new QHBoxLayout();
    m_runEvidenceBtn = new QPushButton("Run Evidence Scorer", m_evidenceBox);
    m_runEvidenceBtn->setStyleSheet(
        "QPushButton { background-color: #3a1a4a; color: #ce93d8; font-weight: bold; }"
        "QPushButton:hover { background-color: #6a1b9a; }");
    runRow->addWidget(m_runEvidenceBtn);
    runRow->addStretch();
    evidLayout->addLayout(runRow);

    m_evidenceOutput = new QTextEdit(m_evidenceBox);
    m_evidenceOutput->setReadOnly(true);
    m_evidenceOutput->setMaximumHeight(120);
    m_evidenceOutput->setPlaceholderText("Evidence scoring results will appear here…");
    evidLayout->addWidget(m_evidenceOutput);

    rightSplit->addWidget(m_evidenceBox);
    rightSplit->setSizes({400, 280});

    mainSplit->addWidget(rightSplit);
    mainSplit->setSizes({280, 800});

    // ── Tab widget: Leads | Detected Series ──────────────────────────────────
    auto* tabWidget = new QTabWidget(this);
    tabWidget->setStyleSheet(R"(
        QTabWidget::pane { border: 1px solid #1a2a4a; background: #16213e; }
        QTabBar::tab {
            background: #1a2035; color: #a0a8b8;
            padding: 6px 20px; border: 1px solid #1a2a4a;
            border-bottom: none; border-radius: 4px 4px 0 0;
        }
        QTabBar::tab:selected { background: #16213e; color: #eaeaea; border-bottom: none; }
        QTabBar::tab:hover:!selected { background: #1a2a4a; }
    )");

    // First tab: existing leads content
    auto* leadsTabWidget = new QWidget(tabWidget);
    auto* leadsTabLayout = new QVBoxLayout(leadsTabWidget);
    leadsTabLayout->setContentsMargins(0, 8, 0, 0);
    leadsTabLayout->addWidget(mainSplit, 1);
    tabWidget->addTab(leadsTabWidget, "Investigative Leads");

    // Second tab: detected crime series
    auto* seriesTabWidget = new QWidget(tabWidget);
    auto* seriesTabLayout = new QVBoxLayout(seriesTabWidget);
    seriesTabLayout->setContentsMargins(0, 8, 0, 0);

    m_seriesTable = new QTableWidget(0, 7, seriesTabWidget);
    m_seriesTable->setHorizontalHeaderLabels({
        "Series ID", "Crime Type", "Event Count", "First Date", "Last Date", "Span (days)", "Status"
    });
    m_seriesTable->setStyleSheet(R"(
        QTableWidget {
            background-color: #16213e;
            color: #eaeaea;
            gridline-color: #1a2a4a;
            border: none;
        }
        QTableWidget::item { padding: 4px 8px; }
        QTableWidget::item:selected { background-color: #1a2a4a; color: #4fc3f7; }
        QHeaderView::section {
            background-color: #0f3460;
            color: #a0a8b8;
            padding: 6px 8px;
            border: none;
            font-size: 11px;
            letter-spacing: 1px;
        }
    )");
    m_seriesTable->horizontalHeader()->setStretchLastSection(true);
    m_seriesTable->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_seriesTable->horizontalHeader()->setSectionResizeMode(6, QHeaderView::Stretch);
    m_seriesTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_seriesTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_seriesTable->verticalHeader()->setVisible(false);
    m_seriesTable->setAlternatingRowColors(false);
    seriesTabLayout->addWidget(m_seriesTable);
    tabWidget->addTab(seriesTabWidget, "Detected Series");

    outerLayout->addWidget(tabWidget, 1);

    // Connections
    connect(m_leadsList,      &QListWidget::itemClicked, this, &LeadsWidget::onLeadSelected);
    connect(m_runEvidenceBtn, &QPushButton::clicked,     this, &LeadsWidget::onRunEvidenceScorer);
    connect(m_exportBtn,      &QPushButton::clicked,     this, &LeadsWidget::onExportReport);
}

// ─────────────────────────────────────────────────────────────────────────────
void LeadsWidget::setLeads(const QVector<InvestigativeLead>& leads, const QString& forEventId)
{
    m_leads = leads;
    m_currentEventId = forEventId;
    m_leadsList->clear();

    for (int i = 0; i < m_leads.size(); ++i) {
        const InvestigativeLead& lead = m_leads[i];
        const QString icon  = categoryIcon(lead.category);
        const QString bar   = confidenceBar(lead.confidence);
        const QString label = QString("#%1  %2  %3\n%4  %5%")
                                  .arg(lead.rank)
                                  .arg(icon)
                                  .arg(lead.headline)
                                  .arg(bar)
                                  .arg(static_cast<int>(lead.confidence * 100));

        auto* item = new QListWidgetItem(label, m_leadsList);
        item->setSizeHint(QSize(0, 60));

        // Color by confidence
        if (lead.confidence >= 0.7)
            item->setForeground(QColor("#81c784"));
        else if (lead.confidence >= 0.4)
            item->setForeground(QColor("#ffb74d"));
        else
            item->setForeground(QColor("#e57373"));

        item->setData(Qt::UserRole, i);
    }

    m_countLabel->setText(QString("%1 lead%2").arg(m_leads.size()).arg(m_leads.size() == 1 ? "" : "s"));

    if (m_leads.isEmpty() && m_detailPanel)
        m_detailPanel->clear();
}

// ─────────────────────────────────────────────────────────────────────────────
void LeadsWidget::refresh()
{
    try {
        const QVector<InvestigativeLead> leads = m_db->getLeads(m_currentEventId);
        setLeads(leads, m_currentEventId);
    } catch (...) {
        // No leads loaded; keep current state
    }

    // ── Crime series detection ────────────────────────────────────────────────
    try {
        const QVector<CrimeEvent> recentEvents = m_db->getRecentEvents(200);
        SeriesDetector detector(0.5, 3.0, 3);
        const QVector<CrimeSeries> series = detector.detect(recentEvents);

        m_seriesTable->setRowCount(0);

        // Reference epoch for converting days back to dates
        const QDateTime epoch = QDateTime(QDate(2000, 1, 1), QTime(0, 0), QTimeZone::utc());

        for (const CrimeSeries& s : series) {
            const int row = m_seriesTable->rowCount();
            m_seriesTable->insertRow(row);

            const int eventCount = s.members.size();
            const QDateTime firstDate = epoch.addSecs(static_cast<qint64>(s.firstDays * 86400.0));
            const QDateTime lastDate  = epoch.addSecs(static_cast<qint64>(s.lastDays  * 86400.0));
            const double spanDays = s.lastDays - s.firstDays;

            const QString status = eventCount >= 10 ? "CRITICAL"
                                 : eventCount >= 5  ? "HIGH"
                                 : "ACTIVE";

            m_seriesTable->setItem(row, 0, new QTableWidgetItem(s.seriesId));
            m_seriesTable->setItem(row, 1, new QTableWidgetItem(s.dominantCrimeType));
            m_seriesTable->setItem(row, 2, new QTableWidgetItem(QString::number(eventCount)));
            m_seriesTable->setItem(row, 3, new QTableWidgetItem(firstDate.toString("dd MMM yyyy")));
            m_seriesTable->setItem(row, 4, new QTableWidgetItem(lastDate.toString("dd MMM yyyy")));
            m_seriesTable->setItem(row, 5, new QTableWidgetItem(QString::number(spanDays, 'f', 1)));
            m_seriesTable->setItem(row, 6, new QTableWidgetItem(status));

            // Highlight rows by severity
            QColor rowColor;
            if (eventCount >= 10)
                rowColor = QColor("#4a1a1a"); // red tint
            else if (eventCount >= 5)
                rowColor = QColor("#3a2800"); // orange tint

            if (rowColor.isValid()) {
                for (int col = 0; col < m_seriesTable->columnCount(); ++col) {
                    if (auto* itm = m_seriesTable->item(row, col)) {
                        itm->setBackground(rowColor);
                        itm->setForeground(eventCount >= 10 ? QColor("#e57373") : QColor("#ffb74d"));
                    }
                }
            }
        }
    } catch (...) {
        // Series detection failed; leave table as-is
    }
}

// ─────────────────────────────────────────────────────────────────────────────
void LeadsWidget::onLeadSelected(QListWidgetItem* item)
{
    if (!item) return;
    const int idx = item->data(Qt::UserRole).toInt();
    if (idx < 0 || idx >= m_leads.size()) return;
    populateLeadDetail(m_leads[idx]);
}

// ─────────────────────────────────────────────────────────────────────────────
void LeadsWidget::populateLeadDetail(const InvestigativeLead& lead)
{
    QString html;
    html.reserve(4096);
    html += "<html><body style='font-family:\"Segoe UI\",Arial;color:#eaeaea;"
            "background:#16213e;padding:14px;'>";

    // Title row
    html += QString("<div style='display:flex;align-items:center;margin-bottom:10px;'>"
                    "<span style='font-size:28px;'>%1</span>"
                    "<div style='margin-left:12px;'>"
                    "<div style='font-size:16px;font-weight:bold;color:#4fc3f7;'>%2</div>"
                    "<div style='font-size:11px;color:#4a5568;text-transform:uppercase;"
                    "letter-spacing:2px;'>%3  ·  Rank #%4</div>"
                    "</div></div>")
                .arg(categoryIcon(lead.category))
                .arg(lead.headline)
                .arg(lead.category)
                .arg(lead.rank);

    // Confidence gauge
    const int pct = static_cast<int>(lead.confidence * 100);
    const QString barColor = lead.confidence >= 0.7 ? "#81c784"
                           : lead.confidence >= 0.4 ? "#ffb74d" : "#e57373";
    html += QString("<div style='margin-bottom:12px;'>"
                    "<span style='color:#a0a8b8;font-size:11px;'>CONFIDENCE</span>"
                    "<div style='height:8px;background:#1a2a4a;border-radius:4px;margin:4px 0;'>"
                    "<div style='height:8px;width:%1%;background:%2;border-radius:4px;'></div></div>"
                    "<span style='color:%2;font-size:18px;font-weight:bold;'>%3%</span>"
                    "</div>")
                .arg(pct).arg(barColor).arg(pct);

    // Description
    if (!lead.detail.isEmpty()) {
        html += QString("<p style='color:#a0a8b8;margin-bottom:10px;'>%1</p>")
                    .arg(lead.detail);
    }

    // Provenance chain
    if (!lead.provenance.empty()) {
        html += "<b style='color:#81c784;'>Provenance Chain</b><ol style='margin:6px 0 12px 18px;color:#c5cae9;'>";
        for (const QString& step : lead.provenance)
            html += QString("<li style='margin-bottom:3px;'>%1</li>").arg(step);
        html += "</ol>";
    }

    // Supporting data
    if (!lead.supportingData.isEmpty()) {
        html += "<b style='color:#ffb74d;'>Supporting Data</b>";
        html += "<table style='border-collapse:collapse;width:100%;margin-top:6px;'>";
        for (auto it = lead.supportingData.constBegin(); it != lead.supportingData.constEnd(); ++it) {
            html += QString("<tr>"
                            "<td style='color:#a0a8b8;width:180px;padding:3px 8px 3px 0;'>%1</td>"
                            "<td style='color:#eaeaea;'>%2</td>"
                            "</tr>").arg(it.key(), it.value().toString());
        }
        html += "</table>";
    }

    // Related events
    if (!lead.relatedEventIds.isEmpty()) {
        html += "<hr style='border-color:#1a2a4a;margin:10px 0;'/>";
        html += "<b style='color:#a0a8b8;'>Related Events</b> ";
        html += QString("<span style='color:#4a5568;'>(%1)</span><br>").arg(lead.relatedEventIds.size());
        html += "<div style='color:#7986cb;margin-top:4px;'>" + lead.relatedEventIds.join(" · ") + "</div>";
    }

    html += "</body></html>";
    m_detailPanel->setHtml(html);
}

// ─────────────────────────────────────────────────────────────────────────────
void LeadsWidget::onRunEvidenceScorer()
{
    // Build evidence map from checkboxes
    QMap<QString, bool> evidencePresence;
    for (int i = 0; i < m_evidenceChecks.size() && i < EVIDENCE_SCORER_KEYS.size(); ++i) {
        if (m_evidenceChecks[i]->isChecked())
            evidencePresence[EVIDENCE_SCORER_KEYS[i]] = true;
    }

    const double prior = m_priorSpin->value();

    EvidenceScorer::Result result;
    try {
        result = m_evidenceScorer.score(prior, evidencePresence);
    } catch (const std::exception& ex) {
        m_evidenceOutput->setPlainText(QString("Scorer error: %1").arg(ex.what()));
        return;
    }

    QString out;
    out.reserve(1024);
    out += "<html><body style='font-family:\"Segoe UI\",Arial;color:#eaeaea;background:#16213e;padding:8px;'>";
    out += QString("<b>Prior: %1</b>  →  <b style='font-size:15px;color:%2'>Posterior: %3</b>"
                   "  (LR = %4)<br><br>")
               .arg(prior, 0, 'f', 3)
               .arg(result.posteriorProbability >= 0.5 ? "#e94560" : "#81c784")
               .arg(result.posteriorProbability, 0, 'f', 4)
               .arg(result.overallLikelihoodRatio, 0, 'f', 3);

    // Per-evidence contributions
    out += "<table style='border-collapse:collapse;width:100%;'>";
    out += "<tr><th style='text-align:left;color:#a0a8b8;font-size:10px;letter-spacing:1px;'>EVIDENCE</th>"
           "<th style='text-align:left;color:#a0a8b8;font-size:10px;'>PRESENT</th>"
           "<th style='text-align:left;color:#a0a8b8;font-size:10px;'>LR</th></tr>";

    for (const auto& contrib : result.contributions) {
        const QString lrStr = QString::number(contrib.likelihoodRatio, 'f', 3);
        const QString color = contrib.wasPresent ? "#81c784" : "#4a5568";
        const QString present = contrib.wasPresent ? "✓" : "—";
        out += QString("<tr><td style='color:%1;padding:2px 8px 2px 0;font-size:11px;'>%2</td>"
                       "<td style='color:%1;'>%3</td>"
                       "<td style='color:%1;'>%4</td></tr>")
                   .arg(color, contrib.name, present, lrStr);
    }
    out += "</table></body></html>";

    m_evidenceOutput->setHtml(out);
}

// ─────────────────────────────────────────────────────────────────────────────
void LeadsWidget::onExportReport()
{
    const QString path = QFileDialog::getSaveFileName(
        this, "Export Leads Report", "leads_report.html",
        "HTML Files (*.html);;All Files (*)");

    if (path.isEmpty()) return;

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::critical(this, "Export Error", "Could not write to:\n" + path);
        return;
    }

    QTextStream out(&file);
    out << "<!DOCTYPE html>\n<html>\n<head>\n"
        << "<meta charset='UTF-8'/>\n"
        << "<title>SENTINEL Investigative Leads Report</title>\n"
        << "<style>"
        << "body{font-family:'Segoe UI',Arial;background:#0d1117;color:#eaeaea;padding:32px;}"
        << "h1{color:#e94560;letter-spacing:4px;}"
        << "h2{color:#4fc3f7;border-bottom:1px solid #1a2a4a;padding-bottom:6px;}"
        << ".lead{background:#16213e;border-left:4px solid #e94560;"
        << "border-radius:8px;padding:16px;margin-bottom:24px;}"
        << ".tag{display:inline-block;background:#0f3460;color:#a0a8b8;"
        << "padding:2px 8px;border-radius:12px;font-size:11px;margin-right:6px;}"
        << "table{border-collapse:collapse;width:100%;}"
        << "td{padding:4px 10px 4px 0;font-size:13px;}"
        << "</style>\n</head>\n<body>\n";

    out << "<h1>SENTINEL — Investigative Leads Report</h1>\n";
    out << QString("<p style='color:#4a5568;'>Generated: %1 | Total leads: %2</p>\n")
               .arg(QDateTime::currentDateTime().toString("dd MMM yyyy HH:mm"))
               .arg(m_leads.size());

    for (const InvestigativeLead& lead : m_leads) {
        out << "<div class='lead'>\n";
        out << QString("<h2>%1 %2  <span style='font-size:14px;color:#4a5568;'>#%3</span></h2>\n")
                   .arg(categoryIcon(lead.category))
                   .arg(lead.headline.toHtmlEscaped())
                   .arg(lead.rank);
        out << QString("<span class='tag'>%1</span>").arg(lead.category.toHtmlEscaped());
        out << QString("<span class='tag' style='color:#%1;'>Confidence: %2%</span><br><br>\n")
                   .arg(lead.confidence >= 0.7 ? "81c784" : lead.confidence >= 0.4 ? "ffb74d" : "e57373")
                   .arg(static_cast<int>(lead.confidence * 100));

        if (!lead.detail.isEmpty())
            out << "<p>" << lead.detail.toHtmlEscaped() << "</p>\n";

        if (!lead.provenance.empty()) {
            out << "<b>Provenance:</b><ol>\n";
            for (const QString& s : lead.provenance)
                out << "<li>" << s.toHtmlEscaped() << "</li>\n";
            out << "</ol>\n";
        }

        if (!lead.supportingData.isEmpty()) {
            out << "<b>Supporting Data:</b><table>\n";
            for (auto it = lead.supportingData.constBegin(); it != lead.supportingData.constEnd(); ++it)
                out << "<tr><td style='color:#a0a8b8;'>" << it.key().toHtmlEscaped()
                    << "</td><td>" << it.value().toString().toHtmlEscaped() << "</td></tr>\n";
            out << "</table>\n";
        }
        out << "</div>\n";
    }

    out << "</body>\n</html>\n";

    QMessageBox::information(this, "Export Complete",
        QString("Report saved to:<br>%1").arg(path));
}
