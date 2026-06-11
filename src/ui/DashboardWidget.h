#pragma once
#include <QWidget>
#include <QLabel>
#include <QGroupBox>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <memory>
#include "core/Database.h"
#include "core/AppConfig.h"
#include "models/RiskForecaster.h"
#include "models/BayesianHierarchical.h"

class DashboardWidget : public QWidget {
    Q_OBJECT
public:
    DashboardWidget(std::shared_ptr<Database> db, AppConfig& cfg, QWidget* parent = nullptr);
    void refresh();
    void setEventCount(int count);

private:
    void setupUI();
    void refreshRiskPanel(const QVector<CrimeEvent>& events);
    void refreshBayesPanel(const QVector<CrimeEvent>& events);
    QGroupBox* createStatCard(const QString& title, const QString& value,
                               const QString& subtitle = {}, const QString& color = "#e94560");

    std::shared_ptr<Database> m_db;
    AppConfig& m_cfg;

    QLabel* m_totalEventsLabel;
    QLabel* m_last24hLabel;
    QLabel* m_topCrimeTypeLabel;
    QLabel* m_dataQualityLabel;
    QTableWidget* m_recentEventsTable;
    QTableWidget* m_crimeTypeTable;
    QTableWidget* m_riskAlertsTable;   // zone risk forecast panel
    QTableWidget* m_bayesTable;        // Bayesian hierarchical zone priors
};
