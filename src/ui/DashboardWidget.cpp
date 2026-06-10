#include "ui/DashboardWidget.h"

#include <QScrollArea>
#include <QDateTime>
#include <QHeaderView>
#include <QFont>
#include <QFrame>
#include <QSizePolicy>
#include <QColor>
#include <algorithm>

static constexpr int CARD_MIN_WIDTH   = 220;
static constexpr int RECENT_ROWS      = 10;
static constexpr int RISK_ALERT_LIMIT = 8;

// ─────────────────────────────────────────────────────────────────────────────
DashboardWidget::DashboardWidget(std::shared_ptr<Database> db, AppConfig& cfg, QWidget* parent)
    : QWidget(parent)
    , m_db(std::move(db))
    , m_cfg(cfg)
    , m_totalEventsLabel(nullptr)
    , m_last24hLabel(nullptr)
    , m_topCrimeTypeLabel(nullptr)
    , m_dataQualityLabel(nullptr)
    , m_recentEventsTable(nullptr)
    , m_crimeTypeTable(nullptr)
    , m_riskAlertsTable(nullptr)
    , m_bayesTable(nullptr)
{
    setupUI();
}

// ─────────────────────────────────────────────────────────────────────────────
void DashboardWidget::setupUI()
{
    setStyleSheet(R"(
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
            text-transform: uppercase;
        }
        QTableWidget {
            background-color: #16213e;
            gridline-color: #1a2a4a;
            color: #eaeaea;
            border: none;
            selection-background-color: #1a2a4a;
        }
        QHeaderView::section {
            background-color: #0f3460;
            color: #a0a8b8;
            padding: 6px 10px;
            border: none;
            font-size: 11px;
            letter-spacing: 1px;
        }
        QScrollArea { border: none; background: transparent; }
    )");

    auto* outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(20, 20, 20, 20);
    outerLayout->setSpacing(20);

    // ── Page title ───────────────────────────────────────────────────────────
    auto* titleLbl = new QLabel("Dashboard Overview", this);
    titleLbl->setStyleSheet("color: #eaeaea; font-size: 22px; font-weight: bold;");
    outerLayout->addWidget(titleLbl);

    // ── Stat cards row ───────────────────────────────────────────────────────
    auto* cardsRow = new QHBoxLayout();
    cardsRow->setSpacing(16);

    // Create the four stat cards, capturing label pointers
    auto* totalCard = createStatCard("TOTAL EVENTS", "—", "All time", "#e94560");
    m_totalEventsLabel = totalCard->findChild<QLabel*>("valueLabel");
    cardsRow->addWidget(totalCard);

    auto* last24hCard = createStatCard("LAST 24 HOURS", "—", "New events", "#4fc3f7");
    m_last24hLabel = last24hCard->findChild<QLabel*>("valueLabel");
    cardsRow->addWidget(last24hCard);

    auto* topTypeCard = createStatCard("MOST COMMON", "—", "Crime type", "#81c784");
    m_topCrimeTypeLabel = topTypeCard->findChild<QLabel*>("valueLabel");
    cardsRow->addWidget(topTypeCard);

    auto* qualityCard = createStatCard("AVG QUALITY", "—", "Data quality score", "#ffb74d");
    m_dataQualityLabel = qualityCard->findChild<QLabel*>("valueLabel");
    cardsRow->addWidget(qualityCard);

    outerLayout->addLayout(cardsRow);

    // ── Lower panels: Recent Events | Crime Type Distribution ─────────────────
    auto* lowerRow = new QHBoxLayout();
    lowerRow->setSpacing(16);

    // Recent events
    auto* recentBox = new QGroupBox("Recent Events", this);
    auto* recentLayout = new QVBoxLayout(recentBox);
    recentLayout->setContentsMargins(0, 8, 0, 0);

    m_recentEventsTable = new QTableWidget(0, 5, this);
    m_recentEventsTable->setHorizontalHeaderLabels({"Date", "Type", "Location", "Suburb", "Quality"});
    m_recentEventsTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_recentEventsTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_recentEventsTable->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    m_recentEventsTable->verticalHeader()->setVisible(false);
    m_recentEventsTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_recentEventsTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_recentEventsTable->setAlternatingRowColors(true);
    m_recentEventsTable->setStyleSheet(
        "QTableWidget { alternate-background-color: #1a2035; }");
    recentLayout->addWidget(m_recentEventsTable);
    lowerRow->addWidget(recentBox, 3);

    // Crime type distribution
    auto* typeBox = new QGroupBox("Crime Type Distribution", this);
    auto* typeLayout = new QVBoxLayout(typeBox);
    typeLayout->setContentsMargins(0, 8, 0, 0);

    m_crimeTypeTable = new QTableWidget(0, 3, this);
    m_crimeTypeTable->setHorizontalHeaderLabels({"Crime Type", "Count", "Share"});
    m_crimeTypeTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_crimeTypeTable->verticalHeader()->setVisible(false);
    m_crimeTypeTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_crimeTypeTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_crimeTypeTable->setAlternatingRowColors(true);
    m_crimeTypeTable->setStyleSheet(
        "QTableWidget { alternate-background-color: #1a2035; }");
    typeLayout->addWidget(m_crimeTypeTable);
    lowerRow->addWidget(typeBox, 2);

    outerLayout->addLayout(lowerRow, 1);

    // ── Risk Forecast Alerts ─────────────────────────────────────────────────
    auto* riskBox = new QGroupBox("7-Day Zone Risk Forecast", this);
    riskBox->setStyleSheet(R"(
        QGroupBox {
            border: 1px solid #e94560;
            border-left: 4px solid #e94560;
            border-radius: 8px;
            background-color: #16213e;
            margin-top: 6px;
        }
        QGroupBox::title {
            color: #e94560;
            subcontrol-origin: margin;
            left: 14px;
            font-size: 10px;
            letter-spacing: 2px;
            font-weight: bold;
        }
    )");
    auto* riskLayout = new QVBoxLayout(riskBox);
    riskLayout->setContentsMargins(0, 8, 0, 0);

    m_riskAlertsTable = new QTableWidget(0, 5, this);
    m_riskAlertsTable->setHorizontalHeaderLabels(
        {"Zone", "Alert Level", "Weekly Risk", "Peak Day", "Peak Score"});
    m_riskAlertsTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_riskAlertsTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_riskAlertsTable->verticalHeader()->setVisible(false);
    m_riskAlertsTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_riskAlertsTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_riskAlertsTable->setAlternatingRowColors(true);
    m_riskAlertsTable->setMaximumHeight(220);
    m_riskAlertsTable->setStyleSheet(
        "QTableWidget { alternate-background-color: #1a2035; }");
    riskLayout->addWidget(m_riskAlertsTable);
    outerLayout->addWidget(riskBox);

    // ── Bayesian Hierarchical Zone Priors ────────────────────────────────────
    auto* bayesBox = new QGroupBox("Bayesian Zone Crime Rate Priors", this);
    bayesBox->setStyleSheet(R"(
        QGroupBox {
            border: 1px solid #4fc3f7;
            border-left: 4px solid #4fc3f7;
            border-radius: 8px;
            background-color: #16213e;
            margin-top: 6px;
        }
        QGroupBox::title {
            color: #4fc3f7;
            subcontrol-origin: margin;
            left: 14px;
            font-size: 10px;
            letter-spacing: 2px;
            font-weight: bold;
        }
    )");
    auto* bayesLayout = new QVBoxLayout(bayesBox);
    bayesLayout->setContentsMargins(0, 8, 0, 0);

    m_bayesTable = new QTableWidget(0, 6, this);
    m_bayesTable->setHorizontalHeaderLabels(
        {"Zone", "Posterior Mean", "95% CI Low", "95% CI High",
         "Shrinkage Est.", "Obs. Count"});
    m_bayesTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_bayesTable->verticalHeader()->setVisible(false);
    m_bayesTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_bayesTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_bayesTable->setAlternatingRowColors(true);
    m_bayesTable->setMaximumHeight(200);
    m_bayesTable->setStyleSheet(
        "QTableWidget { alternate-background-color: #1a2035; }");
    bayesLayout->addWidget(m_bayesTable);
    outerLayout->addWidget(bayesBox);
}

// ─────────────────────────────────────────────────────────────────────────────
QGroupBox* DashboardWidget::createStatCard(const QString& title, const QString& value,
                                            const QString& subtitle, const QString& color)
{
    auto* box = new QGroupBox(title, this);
    box->setMinimumWidth(CARD_MIN_WIDTH);
    box->setStyleSheet(QString(R"(
        QGroupBox {
            border: 1px solid %1;
            border-left: 4px solid %1;
            border-radius: 8px;
            background-color: #16213e;
            margin-top: 6px;
        }
        QGroupBox::title {
            color: %1;
            subcontrol-origin: margin;
            left: 14px;
            font-size: 10px;
            letter-spacing: 2px;
            font-weight: bold;
        }
    )").arg(color));

    auto* layout = new QVBoxLayout(box);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(4);

    auto* valueLbl = new QLabel(value, box);
    valueLbl->setObjectName("valueLabel");
    valueLbl->setStyleSheet(QString("color: %1; font-size: 36px; font-weight: bold;").arg(color));
    valueLbl->setAlignment(Qt::AlignLeft);

    auto* subLbl = new QLabel(subtitle, box);
    subLbl->setStyleSheet("color: #4a5568; font-size: 11px;");

    layout->addWidget(valueLbl);
    layout->addWidget(subLbl);

    return box;
}

// ─────────────────────────────────────────────────────────────────────────────
void DashboardWidget::refresh()
{
    // ── Stat cards ───────────────────────────────────────────────────────────
    try {
        const int total = m_db->getTotalEventCount();
        if (m_totalEventsLabel)
            m_totalEventsLabel->setText(QString::number(total));
    } catch (...) {
        if (m_totalEventsLabel) m_totalEventsLabel->setText("—");
    }

    try {
        const QDateTime since = QDateTime::currentDateTime().addSecs(-86400);
        const int count24h = static_cast<int>(m_db->getEventsSince(since).size());
        if (m_last24hLabel)
            m_last24hLabel->setText(QString::number(count24h));
    } catch (...) {
        if (m_last24hLabel) m_last24hLabel->setText("—");
    }

    try {
        const double avgQuality = m_db->getAverageQualityScore();
        if (m_dataQualityLabel)
            m_dataQualityLabel->setText(QString::number(avgQuality, 'f', 2));
    } catch (...) {
        if (m_dataQualityLabel) m_dataQualityLabel->setText("—");
    }

    // ── Crime type counts ────────────────────────────────────────────────────
    QMap<QString, int> typeCounts;
    try {
        typeCounts = m_db->getCrimeTypeCounts();
    } catch (...) {}

    // Top crime type
    if (!typeCounts.isEmpty() && m_topCrimeTypeLabel) {
        // Find max manually:
        QString topType;
        int topCount = 0;
        for (auto jt = typeCounts.constBegin(); jt != typeCounts.constEnd(); ++jt) {
            if (jt.value() > topCount) {
                topCount = jt.value();
                topType  = jt.key();
            }
        }
        // Capitalise
        if (!topType.isEmpty())
            topType[0] = topType[0].toUpper();
        m_topCrimeTypeLabel->setText(topType.isEmpty() ? "—" : topType);
    }

    // ── Crime type distribution table ────────────────────────────────────────
    m_crimeTypeTable->setRowCount(0);
    if (!typeCounts.isEmpty()) {
        const int grandTotal = [&]{
            int s = 0;
            for (auto v : typeCounts) s += v;
            return s;
        }();

        // Sort descending by count
        QVector<QPair<QString, int>> sorted;
        for (auto it = typeCounts.constBegin(); it != typeCounts.constEnd(); ++it)
            sorted.append({it.key(), it.value()});
        std::sort(sorted.begin(), sorted.end(),
            [](const auto& a, const auto& b){ return a.second > b.second; });

        m_crimeTypeTable->setRowCount(sorted.size());
        for (int row = 0; row < sorted.size(); ++row) {
            const auto& [type, count] = sorted[row];
            const double pct = grandTotal > 0 ? (100.0 * count / grandTotal) : 0.0;

            // Bar representation using unicode blocks
            const int bars = static_cast<int>(pct / 100.0 * 12);
            const QString bar = QString("█").repeated(bars) + QString("░").repeated(12 - bars);

            auto* typeItem  = new QTableWidgetItem(type);
            auto* countItem = new QTableWidgetItem(QString::number(count));
            auto* barItem   = new QTableWidgetItem(QString("%1  %2%").arg(bar).arg(pct, 0, 'f', 1));

            countItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            barItem->setForeground(QColor("#e94560"));

            m_crimeTypeTable->setItem(row, 0, typeItem);
            m_crimeTypeTable->setItem(row, 1, countItem);
            m_crimeTypeTable->setItem(row, 2, barItem);
        }
    }

    // ── Risk forecast panel ──────────────────────────────────────────────────
    QVector<CrimeEvent> eventsFor90;
    try {
        const QDateTime since90 = QDateTime::currentDateTime().addDays(-90);
        eventsFor90 = m_db->getEventsSince(since90);
        refreshRiskPanel(eventsFor90);
    } catch (...) {
        if (m_riskAlertsTable) m_riskAlertsTable->setRowCount(0);
    }

    // ── Bayesian hierarchical panel ──────────────────────────────────────────
    try {
        if (eventsFor90.isEmpty()) {
            const QDateTime since90 = QDateTime::currentDateTime().addDays(-90);
            eventsFor90 = m_db->getEventsSince(since90);
        }
        refreshBayesPanel(eventsFor90);
    } catch (...) {
        if (m_bayesTable) m_bayesTable->setRowCount(0);
    }

    // ── Recent events table ──────────────────────────────────────────────────
    m_recentEventsTable->setRowCount(0);
    try {
        const QVector<CrimeEvent> recent = m_db->getRecentEvents(RECENT_ROWS);
        m_recentEventsTable->setRowCount(recent.size());

        for (int row = 0; row < recent.size(); ++row) {
            const CrimeEvent& ev = recent[row];

            auto* dateItem    = new QTableWidgetItem(ev.timestamp.toString("yyyy-MM-dd HH:mm"));
            auto* typeItem    = new QTableWidgetItem(ev.crimeType.isEmpty() ? "Unknown" : ev.crimeType);
            auto* locItem     = new QTableWidgetItem(ev.locationDescription);
            auto* suburbItem  = new QTableWidgetItem(ev.suburb.isEmpty() ? "—" : ev.suburb);

            // Quality badge
            QString badge;
            if (ev.qualityScore >= 0.7)      badge = "🟢 High";
            else if (ev.qualityScore >= 0.4) badge = "🟡 Med";
            else                              badge = "🔴 Low";
            auto* qualItem = new QTableWidgetItem(badge);

            m_recentEventsTable->setItem(row, 0, dateItem);
            m_recentEventsTable->setItem(row, 1, typeItem);
            m_recentEventsTable->setItem(row, 2, locItem);
            m_recentEventsTable->setItem(row, 3, suburbItem);
            m_recentEventsTable->setItem(row, 4, qualItem);
        }
    } catch (...) {
        // Leave table empty on DB error
    }
}

// ─────────────────────────────────────────────────────────────────────────────
void DashboardWidget::refreshRiskPanel(const QVector<CrimeEvent>& events)
{
    if (!m_riskAlertsTable) return;
    m_riskAlertsTable->setRowCount(0);

    if (events.isEmpty()) return;

    RiskForecaster forecaster(7);
    forecaster.fit(events);
    if (!forecaster.isFitted()) return;

    QVector<ZoneForecast> forecasts = forecaster.forecast(QDateTime::currentDateTime());
    // Sort by weeklyRisk descending
    std::sort(forecasts.begin(), forecasts.end(),
              [](const ZoneForecast& a, const ZoneForecast& b) {
                  return a.weeklyRisk > b.weeklyRisk;
              });

    const int limit = std::min(static_cast<int>(forecasts.size()), RISK_ALERT_LIMIT);
    m_riskAlertsTable->setRowCount(limit);

    // Alert-level colour map
    auto alertColor = [](int level) -> QColor {
        switch (level) {
            case 3: return QColor("#e94560");  // Critical — red
            case 2: return QColor("#ff9800");  // High — orange
            case 1: return QColor("#ffeb3b");  // Elevated — yellow
            default: return QColor("#81c784"); // Normal — green
        }
    };

    for (int row = 0; row < limit; ++row) {
        const ZoneForecast& zf = forecasts[row];

        // Peak day: ForecastDay with highest riskScore
        const ForecastDay* peak = nullptr;
        for (const auto& d : zf.days) {
            if (!peak || d.riskScore > peak->riskScore) peak = &d;
        }

        auto* zoneItem  = new QTableWidgetItem(zf.zoneId);
        auto* levelItem = new QTableWidgetItem(zf.alertLabel());
        auto* riskItem  = new QTableWidgetItem(
            QString::number(zf.weeklyRisk * 100.0, 'f', 1) + "%");
        auto* peakDayItem   = new QTableWidgetItem(
            peak ? peak->date.toString("ddd d MMM") : "—");
        auto* peakScoreItem = new QTableWidgetItem(
            peak ? QString::number(peak->riskScore * 100.0, 'f', 1) + "%" : "—");

        const QColor col = alertColor(zf.alertLevel);
        levelItem->setForeground(col);
        riskItem->setForeground(col);
        peakScoreItem->setForeground(col);
        riskItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        peakScoreItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);

        m_riskAlertsTable->setItem(row, 0, zoneItem);
        m_riskAlertsTable->setItem(row, 1, levelItem);
        m_riskAlertsTable->setItem(row, 2, riskItem);
        m_riskAlertsTable->setItem(row, 3, peakDayItem);
        m_riskAlertsTable->setItem(row, 4, peakScoreItem);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
void DashboardWidget::refreshBayesPanel(const QVector<CrimeEvent>& events)
{
    if (!m_bayesTable) return;
    m_bayesTable->setRowCount(0);
    if (events.isEmpty()) return;

    // Fit Bayesian hierarchical model on the last 90 days
    BayesianHierarchical bh;
    bh.fit(events, 90.0);
    if (!bh.isFitted()) return;

    const QVector<ZonePosterior> posts = bh.allPosteriors();
    const int limit = std::min(static_cast<int>(posts.size()), RISK_ALERT_LIMIT);
    m_bayesTable->setRowCount(limit);

    for (int row = 0; row < limit; ++row) {
        const ZonePosterior& zp = posts[row];
        const double shrink     = bh.shrinkageEstimate(zp.zoneId);

        // Colour by posterior mean relative to global
        const double globalMu = bh.globalMean();
        QColor rowColor;
        if (zp.posteriorMean > globalMu * 2.0)
            rowColor = QColor("#e94560");      // hot zone
        else if (zp.posteriorMean > globalMu * 1.3)
            rowColor = QColor("#ff9800");      // elevated
        else
            rowColor = QColor("#81c784");      // normal

        auto fmtRate = [](double v) {
            return QString::number(v, 'f', 3) + "/day";
        };

        auto* zoneItem    = new QTableWidgetItem(zp.zoneId);
        auto* meanItem    = new QTableWidgetItem(fmtRate(zp.posteriorMean));
        auto* lowItem     = new QTableWidgetItem(fmtRate(zp.credibleLow));
        auto* highItem    = new QTableWidgetItem(fmtRate(zp.credibleHigh));
        auto* shrinkItem  = new QTableWidgetItem(fmtRate(shrink));
        auto* countItem   = new QTableWidgetItem(QString::number(zp.observedCount));

        meanItem->setForeground(rowColor);
        shrinkItem->setForeground(rowColor);
        for (auto* item : {meanItem, lowItem, highItem, shrinkItem, countItem})
            item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);

        m_bayesTable->setItem(row, 0, zoneItem);
        m_bayesTable->setItem(row, 1, meanItem);
        m_bayesTable->setItem(row, 2, lowItem);
        m_bayesTable->setItem(row, 3, highItem);
        m_bayesTable->setItem(row, 4, shrinkItem);
        m_bayesTable->setItem(row, 5, countItem);
    }
}
