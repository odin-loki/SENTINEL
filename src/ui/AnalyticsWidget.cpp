#include "ui/AnalyticsWidget.h"
#include "ui/TemporalHeatmapWidget.h"

#include <QPainter>
#include <QTimeZone>
#include <QScrollArea>
#include <QtCharts/QBarSet>
#include <QtCharts/QBarCategoryAxis>
#include <QtCharts/QValueAxis>
#include <QtCharts/QPieSlice>
#include <QtCharts/QDateTimeAxis>
#include <QtCharts/QLineSeries>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QFont>
#include <QFrame>
#include <QDateTime>
#include <QHeaderView>
#include <QTableWidgetItem>
#include <algorithm>

// ─────────────────────────────────────────────────────────────────────────────
static const QColor SENTINEL_RED    = QColor("#e94560");
static const QColor SENTINEL_BLUE   = QColor("#0f3460");
static const QColor SENTINEL_PANEL  = QColor("#16213e");
static const QColor SENTINEL_BG     = QColor("#0d1117");
static const QColor SENTINEL_GRID   = QColor("#1a2a4a");
static const QColor SENTINEL_TEXT   = QColor("#eaeaea");
static const QColor SENTINEL_MUTED  = QColor("#a0a8b8");

static const QStringList CHART_PALETTE = {
    "#e94560", "#4fc3f7", "#81c784", "#ffb74d",
    "#ce93d8", "#f48fb1", "#80cbc4", "#a5d6a7",
    "#fff176", "#90caf9", "#ef9a9a", "#b39ddb"
};

// ─────────────────────────────────────────────────────────────────────────────
AnalyticsWidget::AnalyticsWidget(std::shared_ptr<Database> db, AppConfig& cfg, QWidget* parent)
    : QWidget(parent)
    , m_db(std::move(db))
    , m_cfg(cfg)
    , m_tabWidget(nullptr)
    , m_hourlyView(nullptr)
    , m_typeView(nullptr)
    , m_trendView(nullptr)
    , m_periodCombo(nullptr)
    , m_summaryLabel(nullptr)
{
    setupUI();
}

// ─────────────────────────────────────────────────────────────────────────────
void AnalyticsWidget::setupUI()
{
    setStyleSheet(R"(
        QTabWidget::pane { border: none; background: #0d1117; }
        QTabBar::tab {
            background: #16213e;
            color: #a0a8b8;
            padding: 10px 24px;
            font-size: 13px;
            border: none;
            border-bottom: 2px solid transparent;
        }
        QTabBar::tab:selected {
            color: #e94560;
            border-bottom: 2px solid #e94560;
            background: #0d1117;
        }
        QTabBar::tab:hover:!selected { color: #eaeaea; }
        QComboBox {
            background-color: #1a2035;
            color: #eaeaea;
            border: 1px solid #0f3460;
            border-radius: 4px;
            padding: 4px 10px;
            min-width: 140px;
        }
        QComboBox::drop-down { border: none; }
        QComboBox QAbstractItemView {
            background: #16213e;
            color: #eaeaea;
            selection-background-color: #e94560;
        }
        QTableWidget {
            background-color: #0d1117;
            gridline-color: #1a2a4a;
            color: #eaeaea;
            border: 1px solid #0f3460;
            font-size: 13px;
            selection-background-color: #0f3460;
        }
        QTableWidget::item { padding: 6px 12px; }
        QHeaderView::section {
            background-color: #16213e;
            color: #a0a8b8;
            border: none;
            border-bottom: 1px solid #1a2a4a;
            padding: 8px 12px;
            font-weight: bold;
            font-size: 12px;
        }
        QPushButton#runBenchmarkBtn {
            background-color: #e94560;
            color: white;
            border: none;
            border-radius: 4px;
            padding: 8px 20px;
            font-size: 13px;
            font-weight: bold;
        }
        QPushButton#runBenchmarkBtn:hover  { background-color: #c73652; }
        QPushButton#runBenchmarkBtn:disabled { background-color: #4a4a5a; color: #888; }
    )");

    auto* outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(16, 16, 16, 16);
    outerLayout->setSpacing(12);

    // ── Page header ──────────────────────────────────────────────────────────
    auto* headerRow = new QHBoxLayout();
    auto* titleLbl  = new QLabel("Analytics", this);
    titleLbl->setStyleSheet("color: #eaeaea; font-size: 22px; font-weight: bold;");

    auto* periodLbl = new QLabel("Period:", this);
    periodLbl->setStyleSheet("color: #a0a8b8;");
    m_periodCombo = new QComboBox(this);
    m_periodCombo->addItem("Last 7 days",   7);
    m_periodCombo->addItem("Last 30 days",  30);
    m_periodCombo->addItem("Last 90 days",  90);
    m_periodCombo->addItem("All time",      0);

    headerRow->addWidget(titleLbl);
    headerRow->addStretch();
    headerRow->addWidget(periodLbl);
    headerRow->addWidget(m_periodCombo);
    outerLayout->addLayout(headerRow);

    // Summary label
    m_summaryLabel = new QLabel("Loading…", this);
    m_summaryLabel->setStyleSheet("color: #a0a8b8; font-size: 12px; padding: 4px 0;");
    outerLayout->addWidget(m_summaryLabel);

    // ── Chart tabs ───────────────────────────────────────────────────────────
    m_tabWidget = new QTabWidget(this);

    // Create placeholder chart views — charts are built in buildXXX()
    m_hourlyView = new QChartView(this);
    m_hourlyView->setRenderHint(QPainter::Antialiasing);
    m_tabWidget->addTab(m_hourlyView, "By Hour of Day");

    m_typeView = new QChartView(this);
    m_typeView->setRenderHint(QPainter::Antialiasing);
    m_tabWidget->addTab(m_typeView, "Crime Types");

    m_trendView = new QChartView(this);
    m_trendView->setRenderHint(QPainter::Antialiasing);
    m_tabWidget->addTab(m_trendView, "Temporal Trend");

    buildBenchmarkTab();
    buildCalibrationTab();
    buildHeatmapTab();
    buildMapTab();

    outerLayout->addWidget(m_tabWidget, 1);

    // Connections
    connect(m_periodCombo, &QComboBox::currentIndexChanged, this, &AnalyticsWidget::onChartTypeChanged);
    connect(m_tabWidget,   &QTabWidget::currentChanged,     this, &AnalyticsWidget::onChartTypeChanged);

    refresh();
}

// ─────────────────────────────────────────────────────────────────────────────
void AnalyticsWidget::styleChart(QChart* chart)
{
    chart->setBackgroundBrush(QBrush(SENTINEL_PANEL));
    chart->setBackgroundPen(Qt::NoPen);
    chart->setPlotAreaBackgroundBrush(QBrush(SENTINEL_BG));
    chart->setPlotAreaBackgroundVisible(true);

    QFont titleFont;
    titleFont.setFamily("Segoe UI");
    titleFont.setPointSize(13);
    titleFont.setBold(true);
    chart->setTitleFont(titleFont);
    chart->setTitleBrush(QBrush(SENTINEL_RED));

    if (chart->legend()) {
        chart->legend()->setLabelColor(SENTINEL_TEXT);
        chart->legend()->setBackgroundVisible(false);
        chart->legend()->setBorderColor(SENTINEL_GRID);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
void AnalyticsWidget::buildHourlyChart()
{
    const int days = m_periodCombo->currentData().toInt();
    QVector<int> hourlyCounts(24, 0);

    try {
        hourlyCounts = m_db->getHourlyCounts(days);
    } catch (...) {
        // Fallback: leave zeros
    }

    auto* set = new QBarSet("Crimes");
    set->setColor(SENTINEL_RED);
    set->setBorderColor(SENTINEL_RED.darker(130));

    int maxCount = 0;
    int peakHour = 0;
    for (int h = 0; h < 24; ++h) {
        *set << hourlyCounts[h];
        if (hourlyCounts[h] > maxCount) {
            maxCount = hourlyCounts[h];
            peakHour = h;
        }
    }

    auto* series = new QBarSeries();
    series->append(set);

    QStringList hourLabels;
    for (int h = 0; h < 24; ++h)
        hourLabels << QString::number(h);

    auto* axisX = new QBarCategoryAxis();
    axisX->append(hourLabels);
    axisX->setLabelsColor(SENTINEL_MUTED);
    axisX->setLinePen(QPen(SENTINEL_GRID));
    axisX->setGridLinePen(QPen(SENTINEL_GRID, 1, Qt::DotLine));

    auto* axisY = new QValueAxis();
    axisY->setRange(0, maxCount > 0 ? maxCount * 1.15 : 10);
    axisY->setLabelsColor(SENTINEL_MUTED);
    axisY->setLinePen(QPen(SENTINEL_GRID));
    axisY->setGridLinePen(QPen(SENTINEL_GRID, 1, Qt::DotLine));
    axisY->setTitleText("Count");
    axisY->setTitleBrush(QBrush(SENTINEL_MUTED));

    auto* chart = new QChart();
    chart->addSeries(series);
    chart->addAxis(axisX, Qt::AlignBottom);
    chart->addAxis(axisY, Qt::AlignLeft);
    series->attachAxis(axisX);
    series->attachAxis(axisY);
    chart->setTitle("Crimes by Hour of Day");
    chart->legend()->hide();
    styleChart(chart);

    if (m_hourlyView->chart()) m_hourlyView->chart()->deleteLater();
    m_hourlyView->setChart(chart);

    // Update summary label
    const int total = [&]{ int s=0; for (auto c : hourlyCounts) s+=c; return s; }();
    const QString peakLabel = peakHour < 12
        ? QString("%1am").arg(peakHour == 0 ? 12 : peakHour)
        : QString("%1pm").arg(peakHour == 12 ? 12 : peakHour - 12);
    m_summaryLabel->setText(
        QString("Total: <b>%1</b> events  |  Peak hour: <b>%2</b>  |  Max count/hour: <b>%3</b>")
            .arg(total).arg(peakLabel).arg(maxCount));
}

// ─────────────────────────────────────────────────────────────────────────────
void AnalyticsWidget::buildCrimeTypeChart()
{
    QMap<QString, int> typeCounts;
    try {
        typeCounts = m_db->getCrimeTypeCounts();
    } catch (...) {}

    auto* series = new QPieSeries();
    series->setHoleSize(0.35);

    int colorIdx = 0;
    int total = 0;
    for (auto v : typeCounts) total += v;

    QString topType;
    int topCount = 0;

    for (auto it = typeCounts.constBegin(); it != typeCounts.constEnd(); ++it) {
        if (it.value() > topCount) { topCount = it.value(); topType = it.key(); }

        const double pct = total > 0 ? (100.0 * it.value() / total) : 0.0;
        auto* slice = series->append(
            QString("%1  %2%").arg(it.key()).arg(pct, 0, 'f', 1),
            it.value());
        const QColor col(CHART_PALETTE[colorIdx % CHART_PALETTE.size()]);
        slice->setColor(col);
        slice->setBorderColor(SENTINEL_BG);
        slice->setBorderWidth(2);
        slice->setLabelColor(SENTINEL_TEXT);
        slice->setLabelVisible(pct > 3.0);   // only label slices > 3%
        ++colorIdx;
    }

    auto* chart = new QChart();
    chart->addSeries(series);
    chart->setTitle("Crime Type Distribution");
    chart->legend()->setAlignment(Qt::AlignRight);
    styleChart(chart);

    if (m_typeView->chart()) m_typeView->chart()->deleteLater();
    m_typeView->setChart(chart);

    m_summaryLabel->setText(
        QString("Total: <b>%1</b> events  |  Most common: <b>%2</b>  (%3 events, %4%)")
            .arg(total).arg(topType).arg(topCount)
            .arg(total > 0 ? 100.0 * topCount / total : 0.0, 0, 'f', 1));
}

// ─────────────────────────────────────────────────────────────────────────────
void AnalyticsWidget::buildTemporalTrendChart()
{
    const int days = m_periodCombo->currentData().toInt();

    QVector<QPair<QDate, int>> trend;
    try {
        trend = m_db->getDailyTrend(days);
    } catch (...) {}

    auto* series = new QLineSeries();
    series->setName("Daily Crime Count");
    series->setColor(SENTINEL_RED);
    QPen pen(SENTINEL_RED);
    pen.setWidth(2);
    series->setPen(pen);

    for (const auto& [date, count] : trend) {
        const qint64 ms = QDateTime(date, QTime(0,0), QTimeZone::utc()).toMSecsSinceEpoch();
        series->append(ms, count);
    }

    auto* axisX = new QDateTimeAxis();
    axisX->setFormat("dd MMM");
    axisX->setLabelsColor(SENTINEL_MUTED);
    axisX->setLinePen(QPen(SENTINEL_GRID));
    axisX->setGridLinePen(QPen(SENTINEL_GRID, 1, Qt::DotLine));
    axisX->setTitleText("Date");
    axisX->setTitleBrush(QBrush(SENTINEL_MUTED));

    int maxY = 0;
    for (const auto& [d, c] : trend) maxY = std::max(maxY, c);
    auto* axisY = new QValueAxis();
    axisY->setRange(0, maxY > 0 ? maxY * 1.15 : 10);
    axisY->setLabelsColor(SENTINEL_MUTED);
    axisY->setLinePen(QPen(SENTINEL_GRID));
    axisY->setGridLinePen(QPen(SENTINEL_GRID, 1, Qt::DotLine));
    axisY->setTitleText("Events");
    axisY->setTitleBrush(QBrush(SENTINEL_MUTED));

    auto* chart = new QChart();
    chart->addSeries(series);
    chart->addAxis(axisX, Qt::AlignBottom);
    chart->addAxis(axisY, Qt::AlignLeft);
    series->attachAxis(axisX);
    series->attachAxis(axisY);
    chart->setTitle("Crime Events — Temporal Trend");
    chart->legend()->hide();
    styleChart(chart);

    if (m_trendView->chart()) m_trendView->chart()->deleteLater();
    m_trendView->setChart(chart);

    const int total = [&]{ int s=0; for (const auto& [d,c] : trend) s+=c; return s; }();
    m_summaryLabel->setText(
        QString("Period total: <b>%1</b> events over <b>%2</b> days  |  Daily avg: <b>%3</b>")
            .arg(total).arg(trend.size())
            .arg(trend.isEmpty() ? 0.0 : static_cast<double>(total) / trend.size(), 0, 'f', 1));
}

// ─────────────────────────────────────────────────────────────────────────────
void AnalyticsWidget::refresh()
{
    onChartTypeChanged(m_tabWidget->currentIndex());
}

void AnalyticsWidget::onChartTypeChanged(int)
{
    const int tab = m_tabWidget->currentIndex();
    switch (tab) {
        case 0: buildHourlyChart();        break;
        case 1: buildCrimeTypeChart();     break;
        case 2: buildTemporalTrendChart(); break;
        case 3: /* Benchmark: driven by Run button */    break;
        case 4: /* Calibration: driven by Run button */  break;
        case 5: refreshHeatmap();  break;
        case 6: refreshMapView();  break;
        default: break;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
void AnalyticsWidget::buildBenchmarkTab()
{
    auto* container = new QWidget(this);
    auto* layout    = new QVBoxLayout(container);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(10);

    // ── Header row ────────────────────────────────────────────────────────────
    auto* headerRow = new QHBoxLayout();
    auto* titleLbl  = new QLabel("Model Performance Benchmark", container);
    titleLbl->setStyleSheet("color: #eaeaea; font-size: 15px; font-weight: bold;");

    auto* descLbl = new QLabel(
        "Evaluates Poisson predictions on an 80 / 20 train-test split of the last 500 events.",
        container);
    descLbl->setStyleSheet("color: #a0a8b8; font-size: 12px;");

    m_runBenchmarkBtn = new QPushButton("Run Benchmark", container);
    m_runBenchmarkBtn->setObjectName("runBenchmarkBtn");
    m_runBenchmarkBtn->setMinimumWidth(140);

    headerRow->addWidget(titleLbl);
    headerRow->addStretch();
    headerRow->addWidget(m_runBenchmarkBtn);
    layout->addLayout(headerRow);
    layout->addWidget(descLbl);

    // ── Metrics table ─────────────────────────────────────────────────────────
    static const QStringList kMetrics  = { "PAI @ 5%", "PAI @ 10%", "PAI @ 20%",
                                            "PEI @ 10%", "SER", "AUC-ROC", "Brier Score" };
    static const QStringList kTargets  = { "≥ 6.0", "≥ 4.5", "≥ 3.0",
                                            "≥ 0.6", "≥ 0.4", "≥ 0.85", "≤ 0.10" };

    m_benchmarkTable = new QTableWidget(kMetrics.size(), 4, container);
    m_benchmarkTable->setHorizontalHeaderLabels({ "Metric", "Value", "Target", "Status" });
    m_benchmarkTable->verticalHeader()->hide();
    m_benchmarkTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_benchmarkTable->setSelectionMode(QAbstractItemView::NoSelection);
    m_benchmarkTable->horizontalHeader()->setStretchLastSection(true);
    m_benchmarkTable->setShowGrid(true);
    m_benchmarkTable->setAlternatingRowColors(false);

    // Fixed column widths
    m_benchmarkTable->setColumnWidth(0, 140);
    m_benchmarkTable->setColumnWidth(1, 110);
    m_benchmarkTable->setColumnWidth(2, 100);

    for (int r = 0; r < kMetrics.size(); ++r) {
        auto mkItem = [&](const QString& text, Qt::Alignment align = Qt::AlignCenter) {
            auto* it = new QTableWidgetItem(text);
            it->setTextAlignment(align);
            it->setBackground(QColor("#16213e"));
            it->setForeground(QColor("#a0a8b8"));
            return it;
        };

        m_benchmarkTable->setItem(r, 0, mkItem(kMetrics[r], Qt::AlignVCenter | Qt::AlignLeft));
        m_benchmarkTable->setItem(r, 1, mkItem("—"));
        m_benchmarkTable->setItem(r, 2, mkItem(kTargets[r]));
        m_benchmarkTable->setItem(r, 3, mkItem("—"));
        m_benchmarkTable->setRowHeight(r, 36);
    }

    layout->addWidget(m_benchmarkTable, 1);
    m_tabWidget->addTab(container, "Benchmark");

    connect(m_runBenchmarkBtn, &QPushButton::clicked, this, &AnalyticsWidget::runBenchmark);
}

// ─────────────────────────────────────────────────────────────────────────────
void AnalyticsWidget::runBenchmark()
{
    m_runBenchmarkBtn->setEnabled(false);

    // ── Fetch & sort last 500 events ─────────────────────────────────────────
    auto events = m_db->getAllEvents();
    std::sort(events.begin(), events.end(), [](const CrimeEvent& a, const CrimeEvent& b) {
        return a.occurredAt.value_or(a.ingestedAt) < b.occurredAt.value_or(b.ingestedAt);
    });
    if (events.size() > 500)
        events = events.mid(events.size() - 500);

    if (events.size() < 10) {
        m_summaryLabel->setText(
            "Benchmark: insufficient data — need at least 10 events.");
        m_runBenchmarkBtn->setEnabled(true);
        return;
    }

    // ── Train / test split (80 / 20) ─────────────────────────────────────────
    const int trainSize = static_cast<int>(events.size() * 0.8);

    QVector<PoissonBaseline::EventRecord> trainRecs;
    trainRecs.reserve(trainSize);
    for (int i = 0; i < trainSize; ++i) {
        const auto& ev = events[i];
        PoissonBaseline::EventRecord r;
        r.zoneId     = ev.suburb.isEmpty() ? QStringLiteral("unknown") : ev.suburb;
        r.occurredAt = ev.occurredAt.value_or(ev.ingestedAt);
        r.crimeType  = ev.crimeType.isEmpty() ? QStringLiteral("unknown") : ev.crimeType;
        trainRecs.append(r);
    }

    PoissonBaseline model;
    model.fit(trainRecs);

    // ── Build yTrue / yPred ───────────────────────────────────────────────────
    QVector<double> yTrue, yPred;

    // Positive examples: actual test events (crime occurred → y=1)
    for (int i = trainSize; i < events.size(); ++i) {
        const auto& ev = events[i];
        const QString zone  = ev.suburb.isEmpty()    ? QStringLiteral("unknown") : ev.suburb;
        const QDateTime dt  = ev.occurredAt.value_or(ev.ingestedAt);
        const QString ctype = ev.crimeType.isEmpty() ? QStringLiteral("unknown") : ev.crimeType;
        yTrue.append(1.0);
        yPred.append(model.predict(zone, dt, ctype).probAtLeastOne);
    }

    // Negative examples: same zones shifted −365 days (background, no event → y=0)
    for (int i = trainSize; i < events.size(); ++i) {
        const auto& ev = events[i];
        const QString zone  = ev.suburb.isEmpty()    ? QStringLiteral("unknown") : ev.suburb;
        const QDateTime dt  = ev.occurredAt.value_or(ev.ingestedAt).addDays(-365);
        const QString ctype = ev.crimeType.isEmpty() ? QStringLiteral("unknown") : ev.crimeType;
        yTrue.append(0.0);
        yPred.append(model.predict(zone, dt, ctype).probAtLeastOne);
    }

    // ── Compute & display ─────────────────────────────────────────────────────
    const BenchmarkReport report = BenchmarkMetrics::fullReport(yTrue, yPred);
    populateBenchmarkTable(report);

    const int testSize = events.size() - trainSize;
    m_summaryLabel->setText(
        QString("Benchmark: %1 train / %2 test events  |  AUC-ROC: <b>%3</b>  |  Brier: <b>%4</b>")
            .arg(trainSize).arg(testSize)
            .arg(report.aucRoc,    0, 'f', 3)
            .arg(report.brierScore,0, 'f', 3));

    m_runBenchmarkBtn->setEnabled(true);
}

// ─────────────────────────────────────────────────────────────────────────────
void AnalyticsWidget::populateBenchmarkTable(const BenchmarkReport& report)
{
    struct Row {
        int     col;
        double  value;
        double  target;
        bool    lowerIsBetter;
    };

    const QVector<Row> rows = {
        { 0, report.pai5pct,    6.00, false },
        { 1, report.pai10pct,   4.50, false },
        { 2, report.pai20pct,   3.00, false },
        { 3, report.pei10pct,   0.60, false },
        { 4, report.ser,        0.40, false },
        { 5, report.aucRoc,     0.85, false },
        { 6, report.brierScore, 0.10, true  },
    };

    for (const auto& row : rows) {
        const int r = row.col;

        const bool meetsTarget = row.lowerIsBetter
            ? (row.value <= row.target)
            : (row.value >= row.target);
        const bool nearTarget = row.lowerIsBetter
            ? (row.value <= row.target * 1.20)
            : (row.value >= row.target * 0.80);

        const QColor bgPass  = QColor("#0d2b0d");
        const QColor bgWarn  = QColor("#2b2000");
        const QColor bgFail  = QColor("#2b0a0a");
        const QColor fgPass  = QColor("#66bb6a");
        const QColor fgWarn  = QColor("#ffa726");
        const QColor fgFail  = QColor("#ef5350");

        const QColor bg     = meetsTarget ? bgPass : (nearTarget ? bgWarn : bgFail);
        const QColor fg     = meetsTarget ? fgPass : (nearTarget ? fgWarn : fgFail);
        const QString status = meetsTarget ? "Pass" : (nearTarget ? "Warn" : "Fail");

        auto applyStyle = [&](int col, const QString& text) {
            auto* it = m_benchmarkTable->item(r, col);
            if (!it) { it = new QTableWidgetItem(); m_benchmarkTable->setItem(r, col, it); }
            it->setText(text);
            it->setBackground(bg);
            it->setForeground(fg);
            it->setTextAlignment(Qt::AlignCenter);
        };

        applyStyle(1, QString::number(row.value, 'f', 3));
        applyStyle(3, status);

        // Color the target cell the same way for visual coherence
        if (auto* tgt = m_benchmarkTable->item(r, 2)) {
            tgt->setBackground(bg);
            tgt->setForeground(fg.lighter(130));
        }
        // Keep metric name column neutral
        if (auto* name = m_benchmarkTable->item(r, 0)) {
            name->setBackground(QColor("#16213e"));
            name->setForeground(QColor("#eaeaea"));
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
void AnalyticsWidget::buildCalibrationTab()
{
    m_calibDashboard = new CalibrationDashboardWidget(m_db, m_cfg, this);
    m_tabWidget->addTab(m_calibDashboard, "Calibration");
}

// ─────────────────────────────────────────────────────────────────────────────
void AnalyticsWidget::buildHeatmapTab()
{
    auto* container = new QWidget(this);
    auto* layout    = new QVBoxLayout(container);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(10);

    auto* title = new QLabel(
        "<b style='color:#a0a8b8;font-size:11px;letter-spacing:2px;'>"
        "TEMPORAL HEATMAP — CRIME DENSITY BY DAY × HOUR</b>",
        container);
    title->setTextFormat(Qt::RichText);
    layout->addWidget(title);

    auto* desc = new QLabel(
        "Colour encodes the number of crimes per (day-of-week, hour-of-day) cell. "
        "Darker = fewer events; crimson = peak activity.",
        container);
    desc->setStyleSheet("color: #4a5568; font-size: 11px;");
    desc->setWordWrap(true);
    layout->addWidget(desc);

    auto* scroll = new QScrollArea(container);
    scroll->setWidgetResizable(true);
    scroll->setStyleSheet("QScrollArea { border: none; background: transparent; }");

    m_heatmap = new TemporalHeatmapWidget(scroll);
    scroll->setWidget(m_heatmap);
    layout->addWidget(scroll, 1);

    m_tabWidget->addTab(container, "Heat Map");
}

// ─────────────────────────────────────────────────────────────────────────────
void AnalyticsWidget::refreshHeatmap()
{
    if (!m_heatmap) return;

    const int periodDays = m_periodCombo->currentData().toInt();
    const QDateTime since = (periodDays > 0)
        ? QDateTime::currentDateTime().addDays(-periodDays)
        : QDateTime::fromSecsSinceEpoch(0);

    std::array<std::array<int,24>,7> counts{};

    try {
        const QVector<CrimeEvent> events = m_db->getEventsSince(since);
        for (const auto& ev : events) {
            const QDateTime dt = ev.occurredAt
                ? *ev.occurredAt
                : ev.timestamp;
            if (!dt.isValid()) continue;

            // Qt::Monday = 1 … Qt::Sunday = 7 → index 0..6
            const int day  = dt.date().dayOfWeek() - 1;  // 0=Mon..6=Sun
            const int hour = dt.time().hour();
            if (day >= 0 && day < 7 && hour >= 0 && hour < 24)
                ++counts[day][hour];
        }
    } catch (...) {
        // Leave counts at zero
    }

    m_heatmap->setData(counts);
}

// ─────────────────────────────────────────────────────────────────────────────
void AnalyticsWidget::buildMapTab()
{
    auto* container = new QWidget(this);
    auto* layout    = new QVBoxLayout(container);
    layout->setContentsMargins(0, 0, 0, 0);

    m_mapView = new MapWidget(container);
    layout->addWidget(m_mapView);

    m_tabWidget->addTab(container, "Map View");
}

// ─────────────────────────────────────────────────────────────────────────────
void AnalyticsWidget::refreshMapView()
{
    if (!m_mapView) return;

    const int periodDays = m_periodCombo->currentData().toInt();
    const QDateTime since = (periodDays > 0)
        ? QDateTime::currentDateTime().addDays(-periodDays)
        : QDateTime::fromSecsSinceEpoch(0);

    QVector<CrimeEvent> events;
    try {
        events = m_db->getEventsSince(since);
    } catch (...) {}

    m_mapView->setEvents(events);

    // Compute KDE hotspots
    if (events.isEmpty()) {
        m_mapView->clearKDEHotspots();
    } else {
        QVector<QPair<double,double>> points;
        for (const auto& ev : events) {
            const double lat = ev.lat.has_value() ? ev.lat.value() : ev.latitude;
            const double lon = ev.lon.has_value() ? ev.lon.value() : ev.longitude;
            if (ev.lat.has_value() || ev.lon.has_value()
                || lat != 0.0 || lon != 0.0)
                points.append({ lat, lon });
        }
        if (points.size() >= 3) {
            // Compute data bounds
            double latMin = 90, latMax = -90, lonMin = 180, lonMax = -180;
            for (const auto& pt : points) {
                if (pt.first  < latMin) latMin = pt.first;
                if (pt.first  > latMax) latMax = pt.first;
                if (pt.second < lonMin) lonMin = pt.second;
                if (pt.second > lonMax) lonMax = pt.second;
            }
            // Add 10% margin
            const double latMargin = std::max((latMax - latMin) * 0.1, 0.01);
            const double lonMargin = std::max((lonMax - lonMin) * 0.1, 0.01);
            latMin -= latMargin; latMax += latMargin;
            lonMin -= lonMargin; lonMax += lonMargin;

            KDEHotspot kde(40);
            const auto hotspots = kde.findHotspots(
                points, latMin, latMax, lonMin, lonMax, 8);
            m_mapView->setKDEHotspots(hotspots);

            // Auto-center on centroid of first hotspot or data mean
            if (!hotspots.isEmpty()) {
                m_mapView->setCenter(hotspots.first().centroidLat,
                                     hotspots.first().centroidLon);
            }
        } else {
            m_mapView->clearKDEHotspots();
        }
    }
}
