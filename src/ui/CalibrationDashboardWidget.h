#pragma once
#include <QWidget>
#include <QtCharts/QChartView>
#include <QLabel>
#include <QPushButton>
#include <memory>
#include "core/Database.h"
#include "core/AppConfig.h"
#include "benchmark/CalibrationAnalyser.h"

class CalibrationDashboardWidget : public QWidget {
    Q_OBJECT
public:
    CalibrationDashboardWidget(std::shared_ptr<Database> db, AppConfig& cfg,
                               QWidget* parent = nullptr);

    void refresh();

private slots:
    void runAnalysis();

private:
    void setupUI();
    void renderChart(const CalibrationResult& result,
                     const QVector<QPair<double, double>>& diagramPoints);

    std::shared_ptr<Database> m_db;
    AppConfig&                m_cfg;

    QChartView*  m_chartView    = nullptr;
    QLabel*      m_summaryLabel = nullptr;
    QPushButton* m_runBtn       = nullptr;
};
