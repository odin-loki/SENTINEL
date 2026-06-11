#include "ui/SettingsWidget.h"

#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QFormLayout>
#include <QScrollArea>
#include <QFileDialog>
#include <QMessageBox>
#include <QFrame>
#include <QFont>
#include <QSizePolicy>
#include <QComboBox>
#include <QDir>
#include <QProcessEnvironment>

namespace {

bool headlessTestMode()
{
    return QProcessEnvironment::systemEnvironment()
        .contains(QStringLiteral("SENTINEL_HEADLESS_TEST"));
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
SettingsWidget::SettingsWidget(AppConfig& cfg, QWidget* parent)
    : QWidget(parent)
    , m_cfg(cfg)
    , m_openWeatherKeyEdit(nullptr)
    , m_socrataTokenEdit(nullptr)
    , m_defaultLatSpin(nullptr)
    , m_defaultLonSpin(nullptr)
    , m_defaultRadiusSpin(nullptr)
    , m_hawkesHistorySpin(nullptr)
    , m_seriesMinEventsSpin(nullptr)
    , m_seriesEpsKmSpin(nullptr)
    , m_seriesEpsDaysSpin(nullptr)
    , m_qualityThresholdSpin(nullptr)
    , m_forecastHorizonSpin(nullptr)
    , m_alertElevatedSpin(nullptr)
    , m_alertHighSpin(nullptr)
    , m_alertCriticalSpin(nullptr)
    , m_gpSigma2Spin(nullptr)
    , m_gpLengthscaleSpin(nullptr)
    , m_gpNoiseSpin(nullptr)
    , m_ensemblePoissonSpin(nullptr)
    , m_ensembleHawkesSpin(nullptr)
    , m_autoRefreshCheck(nullptr)
    , m_refreshIntervalSpin(nullptr)
    , m_databasePathEdit(nullptr)
    , m_browseDbBtn(nullptr)
    , m_themeCombo(nullptr)
    , m_mapZoomSpin(nullptr)
    , m_exportDirEdit(nullptr)
    , m_browseExportBtn(nullptr)
    , m_maxLeadCountSpin(nullptr)
    , m_saveBtn(nullptr)
    , m_resetBtn(nullptr)
    , m_resetDefaultsBtn(nullptr)
{
    setupUI();
    loadFromConfig();
}

// ─────────────────────────────────────────────────────────────────────────────
static QGroupBox* makeGroup(const QString& title)
{
    auto* box = new QGroupBox(title);
    box->setStyleSheet(R"(
        QGroupBox {
            border: 1px solid #1a2a4a;
            border-radius: 8px;
            margin-top: 10px;
            padding: 10px 16px 14px 16px;
            background-color: #16213e;
        }
        QGroupBox::title {
            subcontrol-origin: margin;
            left: 14px;
            color: #e94560;
            font-size: 11px;
            letter-spacing: 2px;
            font-weight: bold;
        }
    )");
    return box;
}

// ─────────────────────────────────────────────────────────────────────────────
void SettingsWidget::setupUI()
{
    setStyleSheet(R"(
        QWidget { background-color: #0d1117; color: #eaeaea; }
        QLineEdit, QDoubleSpinBox, QSpinBox {
            background-color: #1a2035;
            color: #eaeaea;
            border: 1px solid #0f3460;
            border-radius: 4px;
            padding: 5px 10px;
            min-width: 240px;
        }
        QLineEdit:focus, QDoubleSpinBox:focus, QSpinBox:focus {
            border-color: #e94560;
        }
        QCheckBox { color: #eaeaea; spacing: 8px; }
        QCheckBox::indicator {
            width: 16px; height: 16px;
            border-radius: 3px;
            border: 1px solid #0f3460;
            background: #1a2035;
        }
        QCheckBox::indicator:checked { background: #e94560; border-color: #e94560; }
        QLabel { color: #a0a8b8; }
        QPushButton {
            background-color: #0f3460;
            color: #eaeaea;
            border: none;
            border-radius: 4px;
            padding: 8px 20px;
            font-size: 13px;
        }
        QPushButton:hover { background-color: #1a4a7a; }
        QScrollArea { border: none; }
    )");

    // Outer layout holds title + scrollable content + save bar
    auto* outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(20, 20, 20, 16);
    outerLayout->setSpacing(12);

    auto* titleLbl = new QLabel("Settings", this);
    titleLbl->setStyleSheet("color: #eaeaea; font-size: 22px; font-weight: bold;");
    outerLayout->addWidget(titleLbl);

    // Scroll area for groups
    auto* scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    auto* scrollWidget = new QWidget(scrollArea);
    auto* scrollLayout = new QVBoxLayout(scrollWidget);
    scrollLayout->setContentsMargins(0, 0, 12, 0);
    scrollLayout->setSpacing(16);

    // ── API Keys ─────────────────────────────────────────────────────────────
    {
        auto* box    = makeGroup("API KEYS");
        auto* form   = new QFormLayout(box);
        form->setSpacing(10);
        form->setLabelAlignment(Qt::AlignRight);

        m_openWeatherKeyEdit = new QLineEdit(box);
        m_openWeatherKeyEdit->setEchoMode(QLineEdit::Password);
        m_openWeatherKeyEdit->setPlaceholderText("OpenWeatherMap API key");

        m_socrataTokenEdit = new QLineEdit(box);
        m_socrataTokenEdit->setEchoMode(QLineEdit::Password);
        m_socrataTokenEdit->setPlaceholderText("Socrata app token");

        form->addRow("OpenWeather Key:", m_openWeatherKeyEdit);
        form->addRow("Socrata Token:",   m_socrataTokenEdit);
        scrollLayout->addWidget(box);
    }

    // ── Default Location ─────────────────────────────────────────────────────
    {
        auto* box  = makeGroup("DEFAULT LOCATION");
        auto* form = new QFormLayout(box);
        form->setSpacing(10);
        form->setLabelAlignment(Qt::AlignRight);

        m_defaultLatSpin = new QDoubleSpinBox(box);
        m_defaultLatSpin->setRange(-90.0, 90.0);
        m_defaultLatSpin->setDecimals(6);
        m_defaultLatSpin->setSingleStep(0.001);
        m_defaultLatSpin->setSuffix("°");

        m_defaultLonSpin = new QDoubleSpinBox(box);
        m_defaultLonSpin->setRange(-180.0, 180.0);
        m_defaultLonSpin->setDecimals(6);
        m_defaultLonSpin->setSingleStep(0.001);
        m_defaultLonSpin->setSuffix("°");

        m_defaultRadiusSpin = new QDoubleSpinBox(box);
        m_defaultRadiusSpin->setRange(0.1, 100.0);
        m_defaultRadiusSpin->setDecimals(1);
        m_defaultRadiusSpin->setSingleStep(0.5);
        m_defaultRadiusSpin->setSuffix(" km");

        form->addRow("Latitude:",  m_defaultLatSpin);
        form->addRow("Longitude:", m_defaultLonSpin);
        form->addRow("Radius:",    m_defaultRadiusSpin);

        auto* hint = new QLabel("Used as the default map centre and UK Police API fetch area.", box);
        hint->setStyleSheet("color: #4a5568; font-size: 11px; padding-top: 4px;");
        hint->setWordWrap(true);
        auto* boxLayout = qobject_cast<QVBoxLayout*>(box->layout());
        if (!boxLayout) {
            boxLayout = new QVBoxLayout();
            boxLayout->addLayout(form);
            box->setLayout(boxLayout);
        }
        scrollLayout->addWidget(box);
    }

    // ── Model Parameters ─────────────────────────────────────────────────────
    {
        auto* box  = makeGroup("MODEL PARAMETERS");
        auto* form = new QFormLayout(box);
        form->setSpacing(10);
        form->setLabelAlignment(Qt::AlignRight);

        m_hawkesHistorySpin = new QSpinBox(box);
        m_hawkesHistorySpin->setObjectName(QStringLiteral("hawkesHistorySpin"));
        m_hawkesHistorySpin->setRange(7, 365);
        m_hawkesHistorySpin->setSuffix(" days");
        m_hawkesHistorySpin->setToolTip("Look-back window for Hawkes process training");

        m_seriesMinEventsSpin = new QSpinBox(box);
        m_seriesMinEventsSpin->setRange(2, 50);
        m_seriesMinEventsSpin->setSuffix(" events");
        m_seriesMinEventsSpin->setToolTip("Minimum cluster size to recognise as a series");

        m_seriesEpsKmSpin = new QDoubleSpinBox(box);
        m_seriesEpsKmSpin->setRange(0.1, 50.0);
        m_seriesEpsKmSpin->setDecimals(2);
        m_seriesEpsKmSpin->setSingleStep(0.1);
        m_seriesEpsKmSpin->setSuffix(" km");
        m_seriesEpsKmSpin->setToolTip("DBSCAN spatial epsilon for series detection");

        m_seriesEpsDaysSpin = new QDoubleSpinBox(box);
        m_seriesEpsDaysSpin->setRange(0.5, 60.0);
        m_seriesEpsDaysSpin->setDecimals(1);
        m_seriesEpsDaysSpin->setSingleStep(0.5);
        m_seriesEpsDaysSpin->setSuffix(" days");
        m_seriesEpsDaysSpin->setToolTip("DBSCAN temporal epsilon for series detection");

        m_qualityThresholdSpin = new QDoubleSpinBox(box);
        m_qualityThresholdSpin->setRange(0.0, 1.0);
        m_qualityThresholdSpin->setDecimals(2);
        m_qualityThresholdSpin->setSingleStep(0.05);
        m_qualityThresholdSpin->setToolTip("Minimum quality score to include event in analysis");

        m_forecastHorizonSpin = new QSpinBox(box);
        m_forecastHorizonSpin->setRange(1, 30);
        m_forecastHorizonSpin->setSuffix(" days");
        m_forecastHorizonSpin->setToolTip("Risk forecast horizon");

        form->addRow("Hawkes History:",         m_hawkesHistorySpin);
        form->addRow("Series Min Events:",      m_seriesMinEventsSpin);
        form->addRow("Series Spatial Eps:",     m_seriesEpsKmSpin);
        form->addRow("Series Temporal Eps:",    m_seriesEpsDaysSpin);
        form->addRow("Quality Threshold:",      m_qualityThresholdSpin);
        form->addRow("Forecast Horizon:",       m_forecastHorizonSpin);
        scrollLayout->addWidget(box);
    }

    // ── Alert Thresholds ─────────────────────────────────────────────────────
    {
        auto* box  = makeGroup("RISK ALERT THRESHOLDS");
        auto* form = new QFormLayout(box);
        form->setSpacing(10);
        form->setLabelAlignment(Qt::AlignRight);

        auto makePct = [&](QWidget* parent) {
            auto* s = new QDoubleSpinBox(parent);
            s->setRange(0.0, 1.0);
            s->setDecimals(2);
            s->setSingleStep(0.05);
            s->setSuffix("  (0–1)");
            return s;
        };

        m_alertElevatedSpin = makePct(box);
        m_alertElevatedSpin->setToolTip("Weekly risk score ≥ this → Elevated alert");

        m_alertHighSpin = makePct(box);
        m_alertHighSpin->setToolTip("Weekly risk score ≥ this → High alert");

        m_alertCriticalSpin = makePct(box);
        m_alertCriticalSpin->setToolTip("Weekly risk score ≥ this → Critical alert");

        form->addRow("Elevated (🟡):",  m_alertElevatedSpin);
        form->addRow("High (🟠):",      m_alertHighSpin);
        form->addRow("Critical (🔴):", m_alertCriticalSpin);

        auto* hint = new QLabel(
            "Thresholds for the zone risk forecast panel on the Dashboard.",
            box);
        hint->setStyleSheet("color: #4a5568; font-size: 11px; padding-top: 4px;");
        hint->setWordWrap(true);
        form->addRow(hint);
        scrollLayout->addWidget(box);
    }

    // ── GP Regression Hyperparameters ─────────────────────────────────────────
    {
        auto* box  = makeGroup("GP REGRESSION HYPERPARAMETERS");
        auto* form = new QFormLayout(box);
        form->setSpacing(10);
        form->setLabelAlignment(Qt::AlignRight);

        m_gpSigma2Spin = new QDoubleSpinBox(box);
        m_gpSigma2Spin->setRange(0.01, 100.0);
        m_gpSigma2Spin->setDecimals(3);
        m_gpSigma2Spin->setSingleStep(0.1);
        m_gpSigma2Spin->setToolTip("GP signal variance σ²");

        m_gpLengthscaleSpin = new QDoubleSpinBox(box);
        m_gpLengthscaleSpin->setRange(0.001, 10.0);
        m_gpLengthscaleSpin->setDecimals(3);
        m_gpLengthscaleSpin->setSingleStep(0.05);
        m_gpLengthscaleSpin->setSuffix(" °");
        m_gpLengthscaleSpin->setToolTip("RBF kernel length-scale ℓ (degrees)");

        m_gpNoiseSpin = new QDoubleSpinBox(box);
        m_gpNoiseSpin->setRange(0.001, 10.0);
        m_gpNoiseSpin->setDecimals(3);
        m_gpNoiseSpin->setSingleStep(0.01);
        m_gpNoiseSpin->setToolTip("GP observation noise σ_n²");

        form->addRow("Signal Variance σ²:", m_gpSigma2Spin);
        form->addRow("Length-scale ℓ:",     m_gpLengthscaleSpin);
        form->addRow("Noise Variance σ_n²:", m_gpNoiseSpin);
        scrollLayout->addWidget(box);
    }

    // ── Ensemble Weights ─────────────────────────────────────────────────────
    {
        auto* box  = makeGroup("ENSEMBLE MODEL WEIGHTS");
        auto* form = new QFormLayout(box);
        form->setSpacing(10);
        form->setLabelAlignment(Qt::AlignRight);

        m_ensemblePoissonSpin = new QDoubleSpinBox(box);
        m_ensemblePoissonSpin->setRange(0.0, 1.0);
        m_ensemblePoissonSpin->setDecimals(2);
        m_ensemblePoissonSpin->setSingleStep(0.05);
        m_ensemblePoissonSpin->setToolTip("Weight for Poisson baseline model");

        m_ensembleHawkesSpin = new QDoubleSpinBox(box);
        m_ensembleHawkesSpin->setRange(0.0, 1.0);
        m_ensembleHawkesSpin->setDecimals(2);
        m_ensembleHawkesSpin->setSingleStep(0.05);
        m_ensembleHawkesSpin->setToolTip("Weight for Hawkes process model");

        form->addRow("Poisson Weight:", m_ensemblePoissonSpin);
        form->addRow("Hawkes Weight:",  m_ensembleHawkesSpin);

        auto* hint = new QLabel(
            "Weights are normalised automatically. Both can sum to any positive value.",
            box);
        hint->setStyleSheet("color: #4a5568; font-size: 11px; padding-top: 4px;");
        hint->setWordWrap(true);
        form->addRow(hint);
        scrollLayout->addWidget(box);
    }

    // ── Auto-Refresh ─────────────────────────────────────────────────────────
    {
        auto* box    = makeGroup("AUTO-REFRESH");
        auto* layout = new QVBoxLayout(box);
        layout->setSpacing(10);

        m_autoRefreshCheck = new QCheckBox("Enable automatic data refresh", box);

        auto* intervalRow = new QHBoxLayout();
        auto* intervalLbl = new QLabel("Refresh interval:", box);
        m_refreshIntervalSpin = new QSpinBox(box);
        m_refreshIntervalSpin->setRange(10, 3600);
        m_refreshIntervalSpin->setSingleStep(10);
        m_refreshIntervalSpin->setSuffix(" sec");
        m_refreshIntervalSpin->setFixedWidth(120);
        intervalRow->addWidget(intervalLbl);
        intervalRow->addWidget(m_refreshIntervalSpin);
        intervalRow->addStretch();

        layout->addWidget(m_autoRefreshCheck);
        layout->addLayout(intervalRow);

        connect(m_autoRefreshCheck, &QCheckBox::toggled,
                m_refreshIntervalSpin, &QSpinBox::setEnabled);

        scrollLayout->addWidget(box);
    }

    // ── Database ─────────────────────────────────────────────────────────────
    {
        auto* box    = makeGroup("DATABASE");
        auto* layout = new QVBoxLayout(box);
        layout->setSpacing(10);

        auto* pathRow = new QHBoxLayout();
        auto* pathLbl = new QLabel("SQLite file:", box);
        m_databasePathEdit = new QLineEdit(box);
        m_databasePathEdit->setPlaceholderText("/path/to/sentinel.db");
        m_databasePathEdit->setMinimumWidth(300);
        m_browseDbBtn = new QPushButton("Browse…", box);
        m_browseDbBtn->setFixedWidth(80);

        pathRow->addWidget(pathLbl);
        pathRow->addWidget(m_databasePathEdit, 1);
        pathRow->addWidget(m_browseDbBtn);
        layout->addLayout(pathRow);

        auto* hint = new QLabel("Changes to the database path require a restart to take effect.", box);
        hint->setStyleSheet("color: #4a5568; font-size: 11px;");
        layout->addWidget(hint);
        scrollLayout->addWidget(box);
    }

    // ── UI Preferences ───────────────────────────────────────────────────────
    {
        auto* box  = makeGroup("UI PREFERENCES");
        auto* form = new QFormLayout(box);
        form->setSpacing(10);
        form->setLabelAlignment(Qt::AlignRight);

        m_themeCombo = new QComboBox(box);
        m_themeCombo->addItem("Dark",  "dark");
        m_themeCombo->addItem("Light", "light");
        m_themeCombo->setStyleSheet(
            "QComboBox { background-color: #1a2035; color: #eaeaea; border: 1px solid #0f3460;"
            " border-radius: 4px; padding: 5px 10px; min-width: 120px; }"
            "QComboBox::drop-down { border: none; }"
            "QComboBox QAbstractItemView { background-color: #1a2035; color: #eaeaea; "
            "selection-background-color: #0f3460; }");

        m_mapZoomSpin = new QDoubleSpinBox(box);
        m_mapZoomSpin->setRange(1.0, 20.0);
        m_mapZoomSpin->setDecimals(1);
        m_mapZoomSpin->setSingleStep(0.5);
        m_mapZoomSpin->setToolTip("Initial map zoom level (1 = world, 20 = street)");

        auto* exportRow = new QHBoxLayout();
        m_exportDirEdit = new QLineEdit(box);
        m_exportDirEdit->setPlaceholderText("Select export directory…");
        m_browseExportBtn = new QPushButton("Browse…", box);
        m_browseExportBtn->setFixedWidth(80);
        exportRow->addWidget(m_exportDirEdit, 1);
        exportRow->addWidget(m_browseExportBtn);

        m_maxLeadCountSpin = new QSpinBox(box);
        m_maxLeadCountSpin->setRange(1, 10000);
        m_maxLeadCountSpin->setSingleStep(10);
        m_maxLeadCountSpin->setToolTip("Maximum number of leads shown in the leads panel");

        form->addRow("Theme:",            m_themeCombo);
        form->addRow("Map Zoom Level:",   m_mapZoomSpin);
        form->addRow("Export Directory:", exportRow);
        form->addRow("Max Lead Count:",   m_maxLeadCountSpin);

        scrollLayout->addWidget(box);
    }

    scrollLayout->addStretch();
    scrollArea->setWidget(scrollWidget);
    outerLayout->addWidget(scrollArea, 1);

    // ── Save / Reset bar ─────────────────────────────────────────────────────
    auto* btnBar = new QHBoxLayout();
    btnBar->addStretch();

    m_resetDefaultsBtn = new QPushButton("Reset to Defaults", this);
    m_resetDefaultsBtn->setStyleSheet(
        "QPushButton { background-color: #1a2035; color: #e94560; border: 1px solid #e94560; border-radius: 4px; padding: 8px 14px; }"
        "QPushButton:hover { background-color: #2a1020; }");

    m_resetBtn = new QPushButton("Reset to Saved", this);
    m_resetBtn->setStyleSheet("QPushButton { background-color: #1a2035; color: #a0a8b8; }"
                               "QPushButton:hover { color: #eaeaea; }");

    m_saveBtn = new QPushButton("Save Settings", this);
    m_saveBtn->setObjectName(QStringLiteral("saveSettingsBtn"));
    m_saveBtn->setStyleSheet("QPushButton { background-color: #e94560; color: #eaeaea; font-weight: bold; }"
                              "QPushButton:hover { background-color: #c62828; }");
    m_saveBtn->setMinimumWidth(140);

    btnBar->addWidget(m_resetDefaultsBtn);
    btnBar->addSpacing(8);
    btnBar->addWidget(m_resetBtn);
    btnBar->addSpacing(10);
    btnBar->addWidget(m_saveBtn);
    outerLayout->addLayout(btnBar);

    // ── Connections ───────────────────────────────────────────────────────────
    connect(m_saveBtn,         &QPushButton::clicked, this, &SettingsWidget::onSave);
    connect(m_resetBtn,        &QPushButton::clicked, this, &SettingsWidget::onReset);
    connect(m_resetDefaultsBtn,&QPushButton::clicked, this, &SettingsWidget::onResetToDefaults);
    connect(m_browseDbBtn,     &QPushButton::clicked, this, &SettingsWidget::onBrowseDatabase);
    connect(m_browseExportBtn, &QPushButton::clicked, this, &SettingsWidget::onBrowseExport);

    // Auto-save connections — save silently on every change
    connect(m_themeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &SettingsWidget::onAutoSave);
    connect(m_mapZoomSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &SettingsWidget::onAutoSave);
    connect(m_exportDirEdit,    &QLineEdit::editingFinished, this, &SettingsWidget::onAutoSave);
    connect(m_maxLeadCountSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &SettingsWidget::onAutoSave);
    connect(m_alertElevatedSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &SettingsWidget::onAutoSave);
    connect(m_alertHighSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &SettingsWidget::onAutoSave);
    connect(m_alertCriticalSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &SettingsWidget::onAutoSave);
    connect(m_autoRefreshCheck, &QCheckBox::toggled, this, &SettingsWidget::onAutoSave);
    connect(m_refreshIntervalSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &SettingsWidget::onAutoSave);
}

// ─────────────────────────────────────────────────────────────────────────────
void SettingsWidget::loadFromConfig()
{
    m_openWeatherKeyEdit->setText(m_cfg.openWeatherKey);
    m_socrataTokenEdit->setText(m_cfg.socrataToken);

    m_defaultLatSpin->setValue(m_cfg.defaultLat);
    m_defaultLonSpin->setValue(m_cfg.defaultLon);
    m_defaultRadiusSpin->setValue(m_cfg.defaultRadius);

    m_hawkesHistorySpin->setValue(m_cfg.hawkesHistoryDays);
    m_seriesMinEventsSpin->setValue(m_cfg.seriesMinEvents);
    m_seriesEpsKmSpin->setValue(m_cfg.seriesEpsKm);
    m_seriesEpsDaysSpin->setValue(m_cfg.seriesEpsDays);
    m_qualityThresholdSpin->setValue(m_cfg.qualityThreshold);
    m_forecastHorizonSpin->setValue(m_cfg.forecastHorizonDays);

    m_alertElevatedSpin->setValue(m_cfg.alertElevated);
    m_alertHighSpin->setValue(m_cfg.alertHigh);
    m_alertCriticalSpin->setValue(m_cfg.alertCritical);

    m_gpSigma2Spin->setValue(m_cfg.gpSigma2);
    m_gpLengthscaleSpin->setValue(m_cfg.gpLengthscale);
    m_gpNoiseSpin->setValue(m_cfg.gpNoiseSigma2);

    m_ensemblePoissonSpin->setValue(m_cfg.ensemblePoissonWeight);
    m_ensembleHawkesSpin->setValue(m_cfg.ensembleHawkesWeight);

    m_autoRefreshCheck->setChecked(m_cfg.autoRefreshEnabled);
    m_refreshIntervalSpin->setValue(m_cfg.refreshIntervalSeconds);
    m_refreshIntervalSpin->setEnabled(m_cfg.autoRefreshEnabled);

    m_databasePathEdit->setText(m_cfg.databasePath);

    // UI preferences
    const int themeIdx = (m_cfg.theme == "light") ? 1 : 0;
    m_themeCombo->setCurrentIndex(themeIdx);
    m_mapZoomSpin->setValue(m_cfg.mapZoomLevel);
    m_exportDirEdit->setText(m_cfg.exportDirectory);
    m_maxLeadCountSpin->setValue(m_cfg.maxLeadCount);
}

// ─────────────────────────────────────────────────────────────────────────────
void SettingsWidget::applyToConfig()
{
    m_cfg.openWeatherKey  = m_openWeatherKeyEdit->text().trimmed();
    m_cfg.socrataToken    = m_socrataTokenEdit->text().trimmed();

    m_cfg.defaultLat    = m_defaultLatSpin->value();
    m_cfg.defaultLon    = m_defaultLonSpin->value();
    m_cfg.defaultRadius = m_defaultRadiusSpin->value();

    m_cfg.hawkesHistoryDays   = m_hawkesHistorySpin->value();
    m_cfg.seriesMinEvents     = m_seriesMinEventsSpin->value();
    m_cfg.seriesEpsKm         = m_seriesEpsKmSpin->value();
    m_cfg.seriesEpsDays       = m_seriesEpsDaysSpin->value();
    m_cfg.qualityThreshold    = m_qualityThresholdSpin->value();
    m_cfg.forecastHorizonDays = m_forecastHorizonSpin->value();

    m_cfg.alertElevated  = m_alertElevatedSpin->value();
    m_cfg.alertHigh      = m_alertHighSpin->value();
    m_cfg.alertCritical  = m_alertCriticalSpin->value();

    m_cfg.gpSigma2       = m_gpSigma2Spin->value();
    m_cfg.gpLengthscale  = m_gpLengthscaleSpin->value();
    m_cfg.gpNoiseSigma2  = m_gpNoiseSpin->value();

    m_cfg.ensemblePoissonWeight = m_ensemblePoissonSpin->value();
    m_cfg.ensembleHawkesWeight  = m_ensembleHawkesSpin->value();

    m_cfg.autoRefreshEnabled     = m_autoRefreshCheck->isChecked();
    m_cfg.refreshIntervalSeconds = m_refreshIntervalSpin->value();

    m_cfg.databasePath = m_databasePathEdit->text().trimmed();

    // UI preferences
    m_cfg.theme          = (m_themeCombo->currentIndex() == 1) ? "light" : "dark";
    m_cfg.mapZoomLevel   = m_mapZoomSpin->value();
    m_cfg.exportDirectory = m_exportDirEdit->text().trimmed();
    m_cfg.maxLeadCount   = m_maxLeadCountSpin->value();
}

// ─────────────────────────────────────────────────────────────────────────────
void SettingsWidget::onSave()
{
    // Basic validation
    if (m_databasePathEdit->text().trimmed().isEmpty()) {
        if (!headlessTestMode()) {
            QMessageBox::warning(this, "Validation Error", "Database path must not be empty.");
        }
        return;
    }

    applyToConfig();
    emit settingsSaved(m_cfg);

    if (headlessTestMode())
        return;

    try {
        m_cfg.save();
        QMessageBox::information(this, "Settings Saved", "Configuration saved successfully.");
    } catch (const std::exception& ex) {
        QMessageBox::critical(this, "Save Error",
            QString("Could not save settings:\n%1").arg(ex.what()));
    }
}

// ─────────────────────────────────────────────────────────────────────────────
void SettingsWidget::onAutoSave()
{
    applyToConfig();
    try {
        m_cfg.save();
        emit settingsSaved(m_cfg);
    } catch (...) {}
}

// ─────────────────────────────────────────────────────────────────────────────
void SettingsWidget::onReset()
{
    const auto reply = QMessageBox::question(this, "Reset Settings",
        "Reload settings from the last saved configuration?",
        QMessageBox::Yes | QMessageBox::Cancel);

    if (reply == QMessageBox::Yes) {
        try {
            m_cfg = AppConfig::load();
        } catch (...) {}
        loadFromConfig();
    }
}

// ─────────────────────────────────────────────────────────────────────────────
void SettingsWidget::onBrowseDatabase()
{
    const QString path = QFileDialog::getSaveFileName(
        this, "Select Database File",
        m_databasePathEdit->text().isEmpty() ? "sentinel.db" : m_databasePathEdit->text(),
        "SQLite Database (*.db);;All Files (*)");

    if (!path.isEmpty())
        m_databasePathEdit->setText(path);
}

// ─────────────────────────────────────────────────────────────────────────────
void SettingsWidget::onBrowseExport()
{
    const QString dir = QFileDialog::getExistingDirectory(
        this, "Select Export Directory",
        m_exportDirEdit->text().isEmpty() ? QDir::homePath() : m_exportDirEdit->text());

    if (!dir.isEmpty()) {
        m_exportDirEdit->setText(dir);
        onAutoSave();
    }
}

// ─────────────────────────────────────────────────────────────────────────────
void SettingsWidget::onResetToDefaults()
{
    const auto reply = QMessageBox::question(this, "Reset to Defaults",
        "Reset all settings to their factory defaults?\nThis will overwrite your current configuration.",
        QMessageBox::Yes | QMessageBox::Cancel);

    if (reply == QMessageBox::Yes) {
        m_cfg.resetToDefaults();
        loadFromConfig();
        try { m_cfg.save(); } catch (...) {}
        emit settingsSaved(m_cfg);
    }
}
