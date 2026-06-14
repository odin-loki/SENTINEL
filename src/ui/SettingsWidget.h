#pragma once
#include <QWidget>
#include <QLineEdit>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QCheckBox>
#include <QPushButton>
#include <QGroupBox>
#include <QLabel>
#include <QComboBox>
#include "core/AppConfig.h"

class SettingsWidget : public QWidget {
    Q_OBJECT
public:
    explicit SettingsWidget(AppConfig& cfg, QWidget* parent = nullptr);

signals:
    void settingsSaved(const AppConfig& cfg);

private slots:
    void onSave();
    void onReset();
    void onResetToDefaults();
    void onBrowseDatabase();
    void onBrowseExport();
    void onAutoSave();
    void onCheckForUpdates();

private:
    void setupUI();
    void loadFromConfig();
    void applyToConfig();

    AppConfig& m_cfg;

    // API settings
    QLineEdit* m_openWeatherKeyEdit;
    QLineEdit* m_socrataTokenEdit;

    // Geographic defaults
    QDoubleSpinBox* m_defaultLatSpin;
    QDoubleSpinBox* m_defaultLonSpin;
    QDoubleSpinBox* m_defaultRadiusSpin;

    // Model parameters
    QSpinBox*       m_hawkesHistorySpin;
    QSpinBox*       m_seriesMinEventsSpin;
    QDoubleSpinBox* m_seriesEpsKmSpin;
    QDoubleSpinBox* m_seriesEpsDaysSpin;
    QDoubleSpinBox* m_seriesEpsBurglarySpin;
    QDoubleSpinBox* m_seriesEpsTheftSpin;
    QDoubleSpinBox* m_seriesEpsViolentSpin;
    QDoubleSpinBox* m_qualityThresholdSpin;
    QSpinBox*       m_forecastHorizonSpin;

    // Alert thresholds
    QDoubleSpinBox* m_alertElevatedSpin;
    QDoubleSpinBox* m_alertHighSpin;
    QDoubleSpinBox* m_alertCriticalSpin;

    // GP Regression hyperparameters
    QDoubleSpinBox* m_gpSigma2Spin;
    QDoubleSpinBox* m_gpLengthscaleSpin;
    QDoubleSpinBox* m_gpNoiseSpin;

    // Ensemble weights
    QDoubleSpinBox* m_ensemblePoissonSpin;
    QDoubleSpinBox* m_ensembleHawkesSpin;

    // Auto-refresh
    QCheckBox* m_autoRefreshCheck;
    QSpinBox*  m_refreshIntervalSpin;

    // Local REST API
    QCheckBox* m_enableLocalApiCheck;
    QSpinBox*  m_localApiPortSpin;

    // Database
    QLineEdit*   m_databasePathEdit;
    QPushButton* m_browseDbBtn;

    // UI preferences
    QComboBox*      m_themeCombo;
    QDoubleSpinBox* m_mapZoomSpin;
    QLineEdit*      m_exportDirEdit;
    QPushButton*    m_browseExportBtn;
    QSpinBox*       m_maxLeadCountSpin;

    QPushButton* m_saveBtn;
    QPushButton* m_resetBtn;
    QPushButton* m_resetDefaultsBtn;
    QPushButton* m_checkUpdatesBtn;
    bool         m_checkingUpdates = false;
};
