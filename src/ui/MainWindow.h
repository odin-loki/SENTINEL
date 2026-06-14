#pragma once
#include <QMainWindow>
#include <QStackedWidget>
#include <QListWidget>
#include <QSplitter>
#include <QStatusBar>
#include <QLabel>
#include <QTimer>
#include <QVector>
#include <memory>
#include "core/AppConfig.h"
#include "core/Database.h"
#include "core/CrimeEvent.h"
#include "audit/ProvenanceLog.h"
#include "api/LocalApiServer.h"

class DashboardWidget;
class EventsTableWidget;
class AnalyticsWidget;
class CaseWorkspaceWidget;
class CoOffendingGraphWidget;
class LeadsWidget;
class SettingsWidget;
class MapWidget;
class DebugConsoleWidget;
class AuditLogWidget;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow(AppConfig& cfg, std::shared_ptr<Database> db, QWidget* parent = nullptr);

private slots:
    void onNavItemSelected(int index);
    void onRefreshRequested();
    void onImportCsv();
    void onImportSampleData();
    void onFetchUKPolice();
    void onStatusMessage(const QString& msg);
    void onExportEventsCsv();
    void onExportEventsJson();
    void onExportForecastJson();
    void onExportBenchmarkMarkdown();

private:
    void setupUI();
    void setupMenuBar();
    void setupStatusBar();
    void setupNavigation();
    void setupToolBar();
    void syncLocalApi();

    int insertEnrichedEvents(QVector<CrimeEvent> events, const QString& sourceLabel);

    AppConfig& m_cfg;
    std::shared_ptr<Database> m_db;

    QWidget*        m_centralWidget;
    QSplitter*      m_splitter;
    QListWidget*    m_navList;
    QStackedWidget* m_stack;

    ProvenanceLog       m_provenanceLog;

    DashboardWidget*         m_dashboard;
    EventsTableWidget*       m_eventsTable;
    AnalyticsWidget*         m_analytics;
    CaseWorkspaceWidget*     m_caseWorkspace;
    CoOffendingGraphWidget*  m_coOffendingGraph;
    LeadsWidget*             m_leads;
    AuditLogWidget*     m_auditLog;
    SettingsWidget*     m_settings;
    DebugConsoleWidget* m_debugConsole;

    QLabel* m_statusLabel;
    QLabel* m_eventCountLabel;
    QTimer* m_autoRefreshTimer;
    std::unique_ptr<LocalApiServer> m_localApi;
};
