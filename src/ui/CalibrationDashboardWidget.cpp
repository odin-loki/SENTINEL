#include "ui/CalibrationDashboardWidget.h"

#include <QPainter>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QtCharts/QChart>
#include <QtCharts/QLineSeries>
#include <QtCharts/QValueAxis>
#include <algorithm>
#include "models/PoissonBaseline.h"

namespace {

static const QColor SENTINEL_RED   = QColor("#e94560");
static const QColor SENTINEL_PANEL = QColor("#16213e");
static const QColor SENTINEL_BG    = QColor("#0d1117");
static const QColor SENTINEL_GRID  = QColor("#1a2a4a");
static const QColor SENTINEL_TEXT  = QColor("#eaeaea");
static const QColor SENTINEL_MUTED = QColor("#a0a8b8");

void styleChart(QChart* chart)
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

struct MonthHoldoutSplit {
    QVector<CrimeEvent> train;
    QVector<CrimeEvent> test;
    int skippedNoDate = 0;
};

MonthHoldoutSplit splitByMonth(const QVector<CrimeEvent>& events)
{
    MonthHoldoutSplit split;
    for (const CrimeEvent& ev : events) {
        if (!ev.occurredAt) {
            ++split.skippedNoDate;
            continue;
        }
        const int month = ev.occurredAt->date().month();
        if (month <= 4)
            split.train.append(ev);
        else
            split.test.append(ev);
    }
    return split;
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────

CalibrationDashboardWidget::CalibrationDashboardWidget(std::shared_ptr<Database> db,
                                                       AppConfig& cfg,
                                                       QWidget* parent)
    : QWidget(parent)
    , m_db(std::move(db))
    , m_cfg(cfg)
{
    setupUI();
}

void CalibrationDashboardWidget::setupUI()
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(10);

    auto* headerRow = new QHBoxLayout();
    auto* titleLbl  = new QLabel(QStringLiteral("Live Calibration Dashboard"), this);
    titleLbl->setStyleSheet("color: #eaeaea; font-size: 15px; font-weight: bold;");

    m_runBtn = new QPushButton(QStringLiteral("Run Holdout Analysis"), this);
    m_runBtn->setMinimumWidth(200);
    m_runBtn->setStyleSheet(
        "QPushButton { background-color: #0f3460; color: #eaeaea; border: 1px solid #1a4a80; "
        "border-radius: 4px; padding: 6px 14px; font-weight: bold; }"
        "QPushButton:hover { background-color: #1a4a80; }"
        "QPushButton:disabled { background-color: #4a4a5a; color: #888; }");

    headerRow->addWidget(titleLbl);
    headerRow->addStretch();
    headerRow->addWidget(m_runBtn);
    layout->addLayout(headerRow);

    auto* descLbl = new QLabel(
        QStringLiteral(
            "Reliability diagram on held-out predictions using the same temporal split as "
            "benchmark export: Jan–Apr train, May+ test. Poisson model fit on train months; "
            "calibration measured on test events."),
        this);
    descLbl->setStyleSheet("color: #a0a8b8; font-size: 12px;");
    descLbl->setWordWrap(true);
    layout->addWidget(descLbl);

    m_summaryLabel = new QLabel(
        QStringLiteral("Run analysis to populate reliability diagram."), this);
    m_summaryLabel->setStyleSheet("color: #a0a8b8; font-size: 12px; padding: 4px 0;");
    m_summaryLabel->setTextFormat(Qt::RichText);
    layout->addWidget(m_summaryLabel);

    m_chartView = new QChartView(this);
    m_chartView->setRenderHint(QPainter::Antialiasing);
    m_chartView->setMinimumHeight(320);

    auto* chart = new QChart();
    chart->setTitle(QStringLiteral("Reliability Diagram (month holdout)"));
    styleChart(chart);
    m_chartView->setChart(chart);

    layout->addWidget(m_chartView, 1);

    connect(m_runBtn, &QPushButton::clicked, this, &CalibrationDashboardWidget::runAnalysis);
}

void CalibrationDashboardWidget::refresh()
{
    runAnalysis();
}

void CalibrationDashboardWidget::runAnalysis()
{
    if (!m_runBtn || !m_chartView || !m_summaryLabel)
        return;

    m_runBtn->setEnabled(false);

    const QVector<CrimeEvent> events = m_db->getAllEvents();
    const MonthHoldoutSplit split    = splitByMonth(events);

    if (split.train.size() < 20 || split.test.size() < 10) {
        m_summaryLabel->setText(
            QStringLiteral(
                "<span style='color:#ef5350;'>Insufficient data for month holdout — need "
                "≥20 train (Jan–Apr) and ≥10 test (May+) dated events.<br>"
                "Train: %1, test: %2, skipped (no date): %3</span>")
                .arg(split.train.size())
                .arg(split.test.size())
                .arg(split.skippedNoDate));
        m_runBtn->setEnabled(true);
        return;
    }

    QVector<PoissonBaseline::EventRecord> trainRecs;
    trainRecs.reserve(split.train.size());
    for (const CrimeEvent& ev : split.train) {
        PoissonBaseline::EventRecord r;
        r.zoneId     = ev.suburb.isEmpty() ? QStringLiteral("unknown") : ev.suburb;
        r.occurredAt = ev.occurredAt.value_or(ev.ingestedAt);
        r.crimeType  = ev.crimeType.isEmpty() ? QStringLiteral("unknown") : ev.crimeType;
        trainRecs.append(r);
    }

    PoissonBaseline model;
    model.fit(trainRecs);

    QVector<QPair<double, double>> predActual;
    predActual.reserve(split.test.size() * 2);

    for (const CrimeEvent& ev : split.test) {
        const QString zone  = ev.suburb.isEmpty()    ? QStringLiteral("unknown") : ev.suburb;
        const QDateTime dt  = ev.occurredAt.value_or(ev.ingestedAt);
        const QString ctype = ev.crimeType.isEmpty() ? QStringLiteral("unknown") : ev.crimeType;
        predActual.append({ model.predict(zone, dt, ctype).probAtLeastOne, 1.0 });
        predActual.append({ model.predict(zone, dt.addDays(-365), ctype).probAtLeastOne, 0.0 });
    }

    CalibrationAnalyser analyser(10);
    const CalibrationResult result     = analyser.analyse(predActual);
    const auto diagramPoints           = analyser.reliabilityDiagram(predActual);
    renderChart(result, diagramPoints);

    const QString statusColour = (result.status() == QStringLiteral("EXCELLENT")) ? "#66bb6a"
                               : (result.status() == QStringLiteral("GOOD"))      ? "#4fc3f7"
                               : (result.status() == QStringLiteral("FAIR"))      ? "#ffa726"
                               :                                                    "#ef5350";

    m_summaryLabel->setText(
        QStringLiteral(
            "Month holdout: %1 train / %2 test events  |  "
            "ECE: <b>%3</b>  |  MCE: <b>%4</b>  |  ACE: <b>%5</b>  |  "
            "Brier: <b>%6</b>  |  Status: <b><span style='color:%7;'>%8</span></b>")
            .arg(split.train.size())
            .arg(split.test.size())
            .arg(result.ece, 0, 'f', 4)
            .arg(result.mce, 0, 'f', 4)
            .arg(result.ace, 0, 'f', 4)
            .arg(result.brierScore, 0, 'f', 4)
            .arg(statusColour)
            .arg(result.status()));

    m_runBtn->setEnabled(true);
}

void CalibrationDashboardWidget::renderChart(
    const CalibrationResult& result,
    const QVector<QPair<double, double>>& diagramPoints)
{
    Q_UNUSED(result);

    auto* reliabilitySeries = new QLineSeries();
    reliabilitySeries->setName(QStringLiteral("Model"));
    reliabilitySeries->setColor(SENTINEL_RED);
    QPen reliaPen(SENTINEL_RED);
    reliaPen.setWidth(2);
    reliabilitySeries->setPen(reliaPen);
    for (const auto& pt : diagramPoints)
        reliabilitySeries->append(pt.first, pt.second);

    auto* diagonalSeries = new QLineSeries();
    diagonalSeries->setName(QStringLiteral("Perfect Calibration"));
    diagonalSeries->setColor(QColor("#4fc3f7"));
    QPen diagPen(QColor("#4fc3f7"));
    diagPen.setStyle(Qt::DashLine);
    diagPen.setWidth(1);
    diagonalSeries->setPen(diagPen);
    diagonalSeries->append(0.0, 0.0);
    diagonalSeries->append(1.0, 1.0);

    auto* chart = new QChart();
    chart->addSeries(diagonalSeries);
    chart->addSeries(reliabilitySeries);
    chart->setTitle(QStringLiteral("Reliability Diagram — Month Holdout"));
    styleChart(chart);

    auto* axisX = new QValueAxis();
    axisX->setTitleText(QStringLiteral("Mean Predicted Probability"));
    axisX->setRange(0.0, 1.0);
    axisX->setTickCount(6);
    axisX->setLabelFormat("%.1f");
    axisX->setTitleBrush(QBrush(SENTINEL_TEXT));
    axisX->setLabelsBrush(QBrush(SENTINEL_MUTED));
    axisX->setGridLineColor(SENTINEL_GRID);
    chart->addAxis(axisX, Qt::AlignBottom);
    reliabilitySeries->attachAxis(axisX);
    diagonalSeries->attachAxis(axisX);

    auto* axisY = new QValueAxis();
    axisY->setTitleText(QStringLiteral("Fraction of Positives (Observed Rate)"));
    axisY->setRange(0.0, 1.0);
    axisY->setTickCount(6);
    axisY->setLabelFormat("%.1f");
    axisY->setTitleBrush(QBrush(SENTINEL_TEXT));
    axisY->setLabelsBrush(QBrush(SENTINEL_MUTED));
    axisY->setGridLineColor(SENTINEL_GRID);
    chart->addAxis(axisY, Qt::AlignLeft);
    reliabilitySeries->attachAxis(axisY);
    diagonalSeries->attachAxis(axisY);

    if (m_chartView->chart())
        m_chartView->chart()->deleteLater();
    m_chartView->setChart(chart);
}
