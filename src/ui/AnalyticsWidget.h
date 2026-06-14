#pragma once
#include <QWidget>
#include <QtCharts/QChartView>
#include <QtCharts/QChart>
#include <QtCharts/QBarSeries>
#include <QtCharts/QPieSeries>
#include <QtCharts/QLineSeries>
#include <QComboBox>
#include <QPushButton>
#include <QTabWidget>
#include <QLabel>
#include <QTableWidget>
#include <memory>
#include "core/Database.h"
#include "core/AppConfig.h"
#include "benchmark/BenchmarkMetrics.h"
#include "models/PoissonBaseline.h"
#include "ui/TemporalHeatmapWidget.h"
#include "ui/MapWidget.h"
#include "ui/CalibrationDashboardWidget.h"
#include "models/KDEHotspot.h"

class AnalyticsWidget : public QWidget {
    Q_OBJECT
public:
    AnalyticsWidget(std::shared_ptr<Database> db, AppConfig& cfg, QWidget* parent = nullptr);
    void refresh();

private slots:
    void onChartTypeChanged(int index);
    void runBenchmark();

private:
    void setupUI();
    void buildHourlyChart();
    void buildCrimeTypeChart();
    void buildTemporalTrendChart();
    void buildBenchmarkTab();
    void buildCalibrationTab();
    void buildHeatmapTab();
    void buildMapTab();
    void refreshHeatmap();
    void refreshMapView();
    void populateBenchmarkTable(const BenchmarkReport& report);
    void styleChart(QChart* chart);

    std::shared_ptr<Database> m_db;
    AppConfig& m_cfg;

    QTabWidget*   m_tabWidget;
    QChartView*   m_hourlyView;
    QChartView*   m_typeView;
    QChartView*   m_trendView;
    QComboBox*    m_periodCombo;
    QLabel*       m_summaryLabel;

    // Benchmark tab
    QTableWidget* m_benchmarkTable  = nullptr;
    QPushButton*  m_runBenchmarkBtn = nullptr;

    // Calibration tab
    CalibrationDashboardWidget* m_calibDashboard = nullptr;

    // Heatmap tab
    TemporalHeatmapWidget* m_heatmap = nullptr;

    // Map tab
    MapWidget* m_mapView = nullptr;
};
