#include "ui/MainWindow.h"
#include "ui/AuditLogWidget.h"
#include "ui/DashboardWidget.h"
#include "ui/EventsTableWidget.h"
#include "ui/AnalyticsWidget.h"
#include "ui/LeadsWidget.h"
#include "ui/CaseWorkspaceWidget.h"
#include "ui/CoOffendingGraphWidget.h"
#include "ui/SettingsWidget.h"
#include "ui/MapWidget.h"
#include "ui/DebugConsoleWidget.h"
#include "ingest/CsvImporter.h"
#include "ingest/IngestEnricher.h"
#include "ingest/UKPoliceSource.h"
#include "core/DataExporter.h"
#include "benchmark/BenchmarkMetrics.h"
#include "models/KDEHotspot.h"
#include "models/RiskForecaster.h"
#include "inference/HintEngine.h"

#include <algorithm>
#include <cmath>

#include <QApplication>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QMenuBar>
#include <QToolBar>
#include <QFileDialog>
#include <QFile>
#include <QInputDialog>
#include <QMessageBox>
#include <QAction>
#include <QFont>
#include <QIcon>
#include <QFrame>
#include <QSizePolicy>

namespace {

struct GridPaiInputs {
    QVector<double> yTrue;
    QVector<double> yPred;
    int trainCrimes = 0;
    int testCrimes  = 0;
};

GridPaiInputs buildKdeHoldoutPai(const QVector<CrimeEvent>& events, int gridN = 40)
{
    GridPaiInputs out;
    QVector<QPair<double, double>> trainLocs, testLocs;

    for (const CrimeEvent& e : events) {
        if (!e.lat || !e.lon || !e.occurredAt)
            continue;
        const auto loc = qMakePair(*e.lat, *e.lon);
        const int month = e.occurredAt->date().month();
        if (month <= 4)
            trainLocs.append(loc);
        else
            testLocs.append(loc);
    }

    out.trainCrimes = trainLocs.size();
    out.testCrimes  = testLocs.size();
    if (trainLocs.size() < 50 || testLocs.size() < 20)
        return out;

    double latMin = 90, latMax = -90, lonMin = 180, lonMax = -180;
    const auto expand = [&](const QVector<QPair<double, double>>& locs) {
        for (const auto& p : locs) {
            latMin = std::min(latMin, p.first);
            latMax = std::max(latMax, p.first);
            lonMin = std::min(lonMin, p.second);
            lonMax = std::max(lonMax, p.second);
        }
    };
    expand(trainLocs);
    expand(testLocs);

    KDEHotspot kde(gridN);
    const auto surface = kde.compute(trainLocs, latMin, latMax, lonMin, lonMax);

    const double latStep = (latMax - latMin) / gridN;
    const double lonStep = (lonMax - lonMin) / gridN;

    out.yTrue.reserve(gridN * gridN);
    out.yPred.reserve(gridN * gridN);

    for (int row = 0; row < gridN; ++row) {
        for (int col = 0; col < gridN; ++col) {
            const double cellLat = latMin + (row + 0.5) * latStep;
            const double cellLon = lonMin + (col + 0.5) * lonStep;
            const double halfLat = latStep * 0.5;
            const double halfLon = lonStep * 0.5;

            int crimesInCell = 0;
            for (const auto& p : testLocs) {
                if (std::abs(p.first - cellLat) <= halfLat
                    && std::abs(p.second - cellLon) <= halfLon)
                    ++crimesInCell;
            }

            out.yTrue.append(crimesInCell > 0 ? 1.0 : 0.0);
            out.yPred.append(surface[static_cast<size_t>(row)][static_cast<size_t>(col)]);
        }
    }
    return out;
}

} // namespace

static constexpr int NAV_WIDTH = 200;

// ─────────────────────────────────────────────────────────────────────────────
MainWindow::MainWindow(AppConfig& cfg, std::shared_ptr<Database> db, QWidget* parent)
    : QMainWindow(parent)
    , m_cfg(cfg)
    , m_db(std::move(db))
    , m_centralWidget(nullptr)
    , m_splitter(nullptr)
    , m_navList(nullptr)
    , m_stack(nullptr)
    , m_dashboard(nullptr)
    , m_eventsTable(nullptr)
    , m_analytics(nullptr)
    , m_caseWorkspace(nullptr)
    , m_coOffendingGraph(nullptr)
    , m_leads(nullptr)
    , m_auditLog(nullptr)
    , m_settings(nullptr)
    , m_debugConsole(nullptr)
    , m_statusLabel(nullptr)
    , m_eventCountLabel(nullptr)
    , m_autoRefreshTimer(nullptr)
{
    setWindowTitle("SENTINEL — Crime Analytics");
    setMinimumSize(1280, 720);
    resize(1440, 900);

    // Apply application-wide dark palette
    setStyleSheet(R"(
        QMainWindow, QWidget {
            background-color: #0d1117;
            color: #eaeaea;
            font-family: "Segoe UI", Arial, sans-serif;
            font-size: 13px;
        }
        QSplitter::handle { background: #1a2035; width: 2px; }
        QToolBar { background: #16213e; border-bottom: 1px solid #0f3460; spacing: 4px; padding: 4px; }
        QMenuBar { background: #16213e; color: #eaeaea; border-bottom: 1px solid #0f3460; }
        QMenuBar::item:selected { background: #0f3460; }
        QMenu { background: #16213e; color: #eaeaea; border: 1px solid #0f3460; }
        QMenu::item:selected { background: #e94560; }
        QStatusBar { background: #16213e; color: #a0a8b8; border-top: 1px solid #0f3460; }
    )");

    setupUI();
    setupMenuBar();
    setupToolBar();
    setupStatusBar();

    if (m_cfg.autoRefreshEnabled) {
        m_autoRefreshTimer = new QTimer(this);
        m_autoRefreshTimer->setInterval(m_cfg.refreshIntervalSeconds * 1000);
        connect(m_autoRefreshTimer, &QTimer::timeout, this, &MainWindow::onRefreshRequested);
        m_autoRefreshTimer->start();
    }

    syncLocalApi();

    // Initial data load
    onRefreshRequested();
}

// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::setupUI()
{
    m_centralWidget = new QWidget(this);
    setCentralWidget(m_centralWidget);

    auto* rootLayout = new QHBoxLayout(m_centralWidget);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    // ── Navigation sidebar ───────────────────────────────────────────────────
    m_navList = new QListWidget(this);
    m_navList->setFixedWidth(NAV_WIDTH);
    m_navList->setFrameShape(QFrame::NoFrame);
    m_navList->setStyleSheet(R"(
        QListWidget {
            background-color: #16213e;
            border-right: 1px solid #0f3460;
            outline: none;
        }
        QListWidget::item {
            color: #a0a8b8;
            padding: 14px 18px;
            font-size: 14px;
            border-left: 3px solid transparent;
        }
        QListWidget::item:selected {
            background-color: #1a2a4a;
            color: #e94560;
            border-left: 3px solid #e94560;
            font-weight: bold;
        }
        QListWidget::item:hover:!selected {
            background-color: #1a2035;
            color: #eaeaea;
        }
    )");

    setupNavigation();

    // ── Page stack ───────────────────────────────────────────────────────────
    m_stack = new QStackedWidget(this);

    m_dashboard       = new DashboardWidget(m_db, m_cfg, this);
    m_eventsTable     = new EventsTableWidget(m_db, this);
    m_analytics       = new AnalyticsWidget(m_db, m_cfg, this);
    m_caseWorkspace   = new CaseWorkspaceWidget(m_db, this);
    m_coOffendingGraph = new CoOffendingGraphWidget(m_db, this);
    m_leads           = new LeadsWidget(m_db, this);

    // Audit log
    m_auditLog = new AuditLogWidget(m_provenanceLog, this);

    m_settings     = new SettingsWidget(m_cfg, this);
    m_debugConsole = new DebugConsoleWidget(this);

    m_stack->addWidget(m_dashboard);         // 0
    m_stack->addWidget(m_eventsTable);       // 1
    m_stack->addWidget(m_analytics);         // 2
    m_stack->addWidget(m_caseWorkspace);     // 3
    m_stack->addWidget(m_coOffendingGraph);  // 4
    m_stack->addWidget(m_leads);             // 5
    m_stack->addWidget(m_auditLog);          // 6
    m_stack->addWidget(m_settings);          // 7
    m_stack->addWidget(m_debugConsole);      // 8

    // ── Splitter ─────────────────────────────────────────────────────────────
    m_splitter = new QSplitter(Qt::Horizontal, this);
    m_splitter->setHandleWidth(2);
    m_splitter->addWidget(m_navList);
    m_splitter->addWidget(m_stack);
    m_splitter->setStretchFactor(0, 0);
    m_splitter->setStretchFactor(1, 1);
    m_splitter->setSizes({NAV_WIDTH, 1240});

    rootLayout->addWidget(m_splitter);

    // Navigation signal
    connect(m_navList, &QListWidget::currentRowChanged, this, &MainWindow::onNavItemSelected);
    m_navList->setCurrentRow(0);

    // Propagate settings
    connect(m_settings, &SettingsWidget::settingsSaved, this, [this](const AppConfig&) {
        m_db->setQualityThreshold(m_cfg.qualityThreshold);
        syncLocalApi();
        onStatusMessage("Settings saved.");
        onRefreshRequested();
    });
}

// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::setupNavigation()
{
    struct NavEntry { QString label; };
    static const NavEntry entries[] = {
        { "🗺  Dashboard"           },
        { "📋  Crime Events"        },
        { "📊  Analytics"           },
        { "📁  Cases"               },
        { "🕸  Network"             },
        { "🔍  Investigative Leads" },
        { "📜  Audit Log"           },
        { "⚙  Settings"            },
        { "🐛  Debug Console"       },
    };

    for (const auto& e : entries) {
        auto* item = new QListWidgetItem(e.label, m_navList);
        item->setSizeHint(QSize(NAV_WIDTH, 48));
    }

    // Sentinel logo header above nav items
    auto* header = new QWidget(this);
    header->setFixedHeight(80);
    header->setStyleSheet("background-color: #16213e; border-bottom: 1px solid #0f3460;");
    {
        auto* l = new QVBoxLayout(header);
        l->setContentsMargins(18, 12, 18, 12);
        auto* title = new QLabel("SENTINEL", header);
        title->setStyleSheet("color: #e94560; font-size: 20px; font-weight: bold; letter-spacing: 4px;");
        auto* sub = new QLabel("Crime Analytics Platform", header);
        sub->setStyleSheet("color: #4a5568; font-size: 10px; letter-spacing: 1px;");
        l->addWidget(title);
        l->addWidget(sub);
    }
    // Insert as header via a container, but QListWidget doesn't natively support
    // custom headers — use a separate label above the list in setupUI instead.
    // (Header widget unused here; title is in the toolbar.)
    header->hide();
    header->deleteLater();
}

// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::setupMenuBar()
{
    // File
    QMenu* fileMenu = menuBar()->addMenu("&File");

    QAction* importAct = fileMenu->addAction("&Import CSV…");
    importAct->setShortcut(QKeySequence("Ctrl+I"));
    connect(importAct, &QAction::triggered, this, &MainWindow::onImportCsv);

    QAction* sampleAct = fileMenu->addAction("Import &Sample Data");
    sampleAct->setShortcut(QKeySequence("Ctrl+Shift+I"));
    connect(sampleAct, &QAction::triggered, this, &MainWindow::onImportSampleData);

    QAction* fetchAct = fileMenu->addAction("Fetch &UK Police Data…");
    fetchAct->setShortcut(QKeySequence("Ctrl+U"));
    connect(fetchAct, &QAction::triggered, this, &MainWindow::onFetchUKPolice);

    fileMenu->addSeparator();

    QMenu* exportMenu = fileMenu->addMenu("&Export…");
    QAction* expEvCsv  = exportMenu->addAction("Events as &CSV…");
    QAction* expEvJson = exportMenu->addAction("Events as &JSON…");
    exportMenu->addSeparator();
    QAction* expForecast  = exportMenu->addAction("&Risk Forecast (JSON)…");
    exportMenu->addSeparator();
    QAction* expBenchmark = exportMenu->addAction("&Benchmark Report (Markdown)…");
    connect(expEvCsv,    &QAction::triggered, this, &MainWindow::onExportEventsCsv);
    connect(expEvJson,   &QAction::triggered, this, &MainWindow::onExportEventsJson);
    connect(expForecast, &QAction::triggered, this, &MainWindow::onExportForecastJson);
    connect(expBenchmark,&QAction::triggered, this, &MainWindow::onExportBenchmarkMarkdown);

    fileMenu->addSeparator();

    QAction* quitAct = fileMenu->addAction("&Quit");
    quitAct->setShortcut(QKeySequence("Ctrl+Q"));
    connect(quitAct, &QAction::triggered, qApp, &QApplication::quit);

    // View
    QMenu* viewMenu = menuBar()->addMenu("&View");

    QAction* refreshAct = viewMenu->addAction("&Refresh");
    refreshAct->setShortcut(QKeySequence::Refresh);
    connect(refreshAct, &QAction::triggered, this, &MainWindow::onRefreshRequested);

    // Debug
    QMenu* debugMenu = menuBar()->addMenu("&Debug");

    QAction* consoleAct = debugMenu->addAction("Open Debug &Console");
    consoleAct->setShortcut(QKeySequence("Ctrl+Shift+D"));
    connect(consoleAct, &QAction::triggered, this, [this] {
        m_navList->setCurrentRow(8);
        m_stack->setCurrentIndex(8);
    });

    // Help
    QMenu* helpMenu = menuBar()->addMenu("&Help");

    QAction* aboutAct = helpMenu->addAction("&About SENTINEL");
    connect(aboutAct, &QAction::triggered, this, [this] {
        const QString version = QApplication::applicationVersion();
        QMessageBox::about(this,
            "About SENTINEL",
            QStringLiteral(
                "<h2 style='color:#e94560;'>SENTINEL</h2>"
                "<p><b>Crime Analytics Platform</b></p>"
                "<p>Version %1 — Built with C++23 &amp; Qt6</p>"
                "<p>Geospatial crime intelligence, NLP-driven event enrichment,<br>"
                "Hawkes process modelling, and Bayesian evidence scoring.</p>")
                .arg(version));
    });
}

// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::setupToolBar()
{
    QToolBar* tb = addToolBar("Main Toolbar");
    tb->setMovable(false);
    tb->setIconSize(QSize(18, 18));

    // App title in toolbar
    auto* titleLbl = new QLabel("  <b style='color:#e94560;font-size:16px;letter-spacing:3px;'>SENTINEL</b>"
                                 "<span style='color:#4a5568;font-size:10px;'> | Crime Analytics</span>  ", this);
    titleLbl->setTextFormat(Qt::RichText);
    tb->addWidget(titleLbl);

    tb->addSeparator();

    QAction* refreshAct = tb->addAction("⟳  Refresh");
    refreshAct->setToolTip("Refresh data (F5)");
    connect(refreshAct, &QAction::triggered, this, &MainWindow::onRefreshRequested);

    QAction* importAct = tb->addAction("⬆  Import CSV");
    importAct->setToolTip("Import crime events from CSV file");
    connect(importAct, &QAction::triggered, this, &MainWindow::onImportCsv);

    QAction* fetchAct = tb->addAction("🌐  UK Police API");
    fetchAct->setToolTip("Fetch events from UK Police open data API");
    connect(fetchAct, &QAction::triggered, this, &MainWindow::onFetchUKPolice);

    // Spacer
    auto* spacer = new QWidget(this);
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    tb->addWidget(spacer);

    m_eventCountLabel = new QLabel("Events: —", this);
    m_eventCountLabel->setStyleSheet("color: #a0a8b8; padding-right: 12px;");
    tb->addWidget(m_eventCountLabel);
}

// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::setupStatusBar()
{
    m_statusLabel = new QLabel("Ready", this);
    m_statusLabel->setStyleSheet("color: #a0a8b8; padding-left: 8px;");
    statusBar()->addWidget(m_statusLabel, 1);

    auto* versionLbl = new QLabel(
        QStringLiteral("SENTINEL v%1").arg(QApplication::applicationVersion()), this);
    versionLbl->setStyleSheet("color: #4a5568; padding-right: 8px;");
    statusBar()->addPermanentWidget(versionLbl);
}

void MainWindow::syncLocalApi()
{
    m_localApi.reset();
    if (!m_cfg.enableLocalApi)
        return;

    m_localApi = std::make_unique<LocalApiServer>(m_db, static_cast<quint16>(m_cfg.localApiPort), this);
    if (!m_localApi->start()) {
        onStatusMessage(QStringLiteral("Local API failed to start on port %1").arg(m_cfg.localApiPort));
        m_localApi.reset();
        return;
    }
    onStatusMessage(QStringLiteral("Local API listening on http://127.0.0.1:%1")
                        .arg(m_localApi->port()));
}

// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::onNavItemSelected(int index)
{
    if (index < 0 || index >= m_stack->count()) return;
    m_stack->setCurrentIndex(index);

    // Trigger lazy refresh when switching to heavy pages
    switch (index) {
        case 0: m_dashboard->refresh();         break;
        case 1: m_eventsTable->refresh();       break;
        case 2: m_analytics->refresh();         break;
        case 3: m_caseWorkspace->refresh();     break;
        case 4: m_coOffendingGraph->refresh();  break;
        case 5: m_leads->refresh();             break;
        case 6: m_auditLog->refresh();          break;
        default: break;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::onRefreshRequested()
{
    onStatusMessage("Refreshing…");

    // Refresh whichever page is currently visible
    const int idx = m_stack->currentIndex();
    switch (idx) {
        case 0: m_dashboard->refresh();         break;
        case 1: m_eventsTable->refresh();       break;
        case 2: m_analytics->refresh();         break;
        case 3: m_caseWorkspace->refresh();     break;
        case 4: m_coOffendingGraph->refresh();  break;
        case 5: m_leads->refresh();             break;
        case 6: m_auditLog->refresh();          break;
        default: break;
    }

    // Update event count in toolbar
    try {
        const int total = m_db->getTotalEventCount();
        m_eventCountLabel->setText(QString("Events: %1").arg(total));
    } catch (...) {
        m_eventCountLabel->setText("Events: —");
    }

    onStatusMessage("Ready");
}

// ─────────────────────────────────────────────────────────────────────────────
int MainWindow::insertEnrichedEvents(QVector<CrimeEvent> events, const QString& sourceLabel)
{
    IngestEnricher enricher(m_cfg);
    const ImportSummary summary = enricher.prepare(events);

    int inserted = 0;
    for (const auto& ev : events) {
        if (ev.qualityScore < m_cfg.qualityThreshold)
            continue;
        if (m_db->insertEvent(ev))
            ++inserted;
    }

    QMessageBox::information(this, QStringLiteral("Import Complete"),
        QStringLiteral("Source: <b>%1</b><br>"
                       "Parsed: <b>%2</b><br>"
                       "Avg quality: <b>%3</b><br>"
                       "Passed quality (≥ %4): <b>%5</b><br>"
                       "Quarantined: <b>%6</b><br>"
                       "Inserted: <b>%7</b>")
            .arg(sourceLabel)
            .arg(summary.totalParsed)
            .arg(summary.avgQuality, 0, 'f', 2)
            .arg(m_cfg.qualityThreshold, 0, 'f', 2)
            .arg(summary.passingCount)
            .arg(summary.quarantinedCount)
            .arg(inserted));

    onStatusMessage(QStringLiteral("Imported %1 / %2 events (%3 quarantined).")
                        .arg(inserted)
                        .arg(summary.totalParsed)
                        .arg(summary.quarantinedCount));

    if (inserted > 0) {
        constexpr int kAnalyticsPage = 2; // Dashboard=0, Events=1, Analytics=2, Cases=3, Network=4
        m_navList->setCurrentRow(kAnalyticsPage);
        m_stack->setCurrentIndex(kAnalyticsPage);
        m_analytics->refresh();
    }

    return inserted;
}

// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::onImportCsv()
{
    const QString path = QFileDialog::getOpenFileName(
        this, "Import Crime Events CSV", QString(),
        "CSV Files (*.csv);;All Files (*)");

    if (path.isEmpty()) return;

    onStatusMessage(QString("Importing %1…").arg(path));

    try {
        QVector<CrimeEvent> events = CsvImporter::importFile(path);
        insertEnrichedEvents(std::move(events), path);
        onRefreshRequested();
    } catch (const std::exception& ex) {
        onStatusMessage("Import failed.");
        QMessageBox::critical(this, "Import Error", QString("Failed to import CSV:\n%1").arg(ex.what()));
    }
}

// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::onImportSampleData()
{
    const QString path = IngestEnricher::defaultSampleCsv();
    if (!QFile::exists(path)) {
        QMessageBox::warning(this, QStringLiteral("Sample Data Missing"),
            QStringLiteral("Bundled sample CSV not found:<br>%1").arg(path));
        return;
    }

    onStatusMessage(QStringLiteral("Importing bundled sample data…"));

    try {
        QVector<CrimeEvent> events = CsvImporter::importFile(
            path, QStringLiteral("london_sample"));
        insertEnrichedEvents(std::move(events), path);
        onRefreshRequested();
    } catch (const std::exception& ex) {
        onStatusMessage(QStringLiteral("Sample import failed."));
        QMessageBox::critical(this, QStringLiteral("Import Error"),
            QStringLiteral("Failed to import sample CSV:\n%1").arg(ex.what()));
    }
}

// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::onFetchUKPolice()
{
    bool ok = false;
    const QString coords = QInputDialog::getText(
        this, "Fetch UK Police Data",
        "Enter latitude, longitude, radius (km):\n(e.g.  51.5074, -0.1278, 1.0)",
        QLineEdit::Normal, "51.5074, -0.1278, 1.0", &ok);

    if (!ok || coords.trimmed().isEmpty()) return;

    const QStringList parts = coords.split(',');
    if (parts.size() < 3) {
        QMessageBox::warning(this, "Input Error", "Please enter: lat, lon, radius");
        return;
    }

    bool latOk, lonOk, radOk;
    const double lat    = parts[0].trimmed().toDouble(&latOk);
    const double lon    = parts[1].trimmed().toDouble(&lonOk);
    const double radius = parts[2].trimmed().toDouble(&radOk);

    if (!latOk || !lonOk || !radOk) {
        QMessageBox::warning(this, "Input Error", "Could not parse numeric values.");
        return;
    }

    onStatusMessage(QString("Fetching UK Police data at (%1, %2) r=%3 km\xe2\x80\xa6")
                    .arg(lat, 0, 'f', 4).arg(lon, 0, 'f', 4).arg(radius, 0, 'f', 1));

    try {
        const QDateTime since = QDateTime::currentDateTimeUtc().addYears(-1);
        UKPoliceSource source(lat, lon, radius);
        QVector<CrimeEvent> events = source.fetchSync(since);

        insertEnrichedEvents(std::move(events), QStringLiteral("UK Police API"));
        onRefreshRequested();
    } catch (const std::exception& ex) {
        onStatusMessage("UK Police fetch failed.");
        QMessageBox::critical(this, "Fetch Error",
            QString("Failed to fetch from UK Police API:\n%1").arg(ex.what()));
    }
}

// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::onStatusMessage(const QString& msg)
{
    if (m_statusLabel) m_statusLabel->setText(msg);
    statusBar()->showMessage(msg, 0);
}

// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::onExportEventsCsv()
{
    const QString path = QFileDialog::getSaveFileName(
        this, "Export Events as CSV", "sentinel_events.csv",
        "CSV Files (*.csv);;All Files (*)");
    if (path.isEmpty()) return;

    try {
        const auto events = m_db->getEventsSince(
            QDateTime::fromSecsSinceEpoch(0));
        const QString csv = DataExporter::eventsToCsv(events);
        if (DataExporter::saveText(csv, path)) {
            onStatusMessage(QString("Exported %1 events to %2").arg(events.size()).arg(path));
            QMessageBox::information(this, "Export Complete",
                QString("Exported <b>%1</b> events to:<br>%2").arg(events.size()).arg(path));
        } else {
            QMessageBox::critical(this, "Export Error", DataExporter::lastError());
        }
    } catch (const std::exception& ex) {
        QMessageBox::critical(this, "Export Error", ex.what());
    }
}

// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::onExportEventsJson()
{
    const QString path = QFileDialog::getSaveFileName(
        this, "Export Events as JSON", "sentinel_events.json",
        "JSON Files (*.json);;All Files (*)");
    if (path.isEmpty()) return;

    try {
        const auto events = m_db->getEventsSince(
            QDateTime::fromSecsSinceEpoch(0));
        const auto arr = DataExporter::eventsToJson(events);
        if (DataExporter::saveJson(arr, path)) {
            onStatusMessage(QString("Exported %1 events to %2").arg(events.size()).arg(path));
            QMessageBox::information(this, "Export Complete",
                QString("Exported <b>%1</b> events to:<br>%2").arg(events.size()).arg(path));
        } else {
            QMessageBox::critical(this, "Export Error", DataExporter::lastError());
        }
    } catch (const std::exception& ex) {
        QMessageBox::critical(this, "Export Error", ex.what());
    }
}

// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::onExportForecastJson()
{
    const QString path = QFileDialog::getSaveFileName(
        this, "Export Risk Forecast as JSON", "sentinel_forecast.json",
        "JSON Files (*.json);;All Files (*)");
    if (path.isEmpty()) return;

    try {
        const QDateTime since90 = QDateTime::currentDateTime().addDays(-90);
        const auto events = m_db->getEventsSince(since90);

        RiskForecaster forecaster(7);
        forecaster.fit(events);
        if (!forecaster.isFitted()) {
            QMessageBox::information(this, "No Data",
                "Not enough data to produce a forecast.\nImport more crime events first.");
            return;
        }

        const auto forecasts = forecaster.forecast(QDateTime::currentDateTime());
        const auto arr = DataExporter::forecastsToJson(forecasts);
        if (DataExporter::saveJson(arr, path)) {
            onStatusMessage(QString("Exported forecast for %1 zones to %2")
                .arg(forecasts.size()).arg(path));
            QMessageBox::information(this, "Export Complete",
                QString("Exported forecast for <b>%1 zones</b> to:<br>%2")
                    .arg(forecasts.size()).arg(path));
        } else {
            QMessageBox::critical(this, "Export Error", DataExporter::lastError());
        }
    } catch (const std::exception& ex) {
        QMessageBox::critical(this, "Export Error", ex.what());
    }
}

// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::onExportBenchmarkMarkdown()
{
    const QString path = QFileDialog::getSaveFileName(
        this, "Export Benchmark Report", "sentinel_benchmark.md",
        "Markdown Files (*.md);;All Files (*)");
    if (path.isEmpty()) return;

    try {
        const auto events = m_db->getEventsSince(
            QDateTime::currentDateTime().addDays(-90));
        if (events.isEmpty()) {
            QMessageBox::information(this, "No Data",
                "No events in the last 90 days to benchmark against.");
            return;
        }

        const GridPaiInputs paiInputs = buildKdeHoldoutPai(events);
        if (paiInputs.yTrue.isEmpty()) {
            QMessageBox::information(this, QStringLiteral("Insufficient Data"),
                QStringLiteral("Need ≥50 train and ≥20 test geo-tagged events with dates "
                               "for KDE holdout (Jan–Apr train, May+ test).<br>"
                               "Train crimes: %1, test crimes: %2")
                    .arg(paiInputs.trainCrimes)
                    .arg(paiInputs.testCrimes));
            return;
        }

        const auto report = BenchmarkMetrics::fullReport(paiInputs.yTrue, paiInputs.yPred);
        const QString md  = DataExporter::benchmarkToMarkdown(report);
        if (DataExporter::saveText(md, path)) {
            onStatusMessage(QString("Benchmark report saved to %1").arg(path));
            QMessageBox::information(this, "Export Complete",
                QString("Benchmark report saved to:<br>%1").arg(path));
        } else {
            QMessageBox::critical(this, "Export Error", DataExporter::lastError());
        }
    } catch (const std::exception& ex) {
        QMessageBox::critical(this, "Export Error", ex.what());
    }
}
