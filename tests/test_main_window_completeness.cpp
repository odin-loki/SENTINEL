// test_main_window_completeness.cpp — MainWindow structural completeness checks
#include <QTest>
#include <QApplication>
#include <QMenuBar>
#include <QStatusBar>
#include <QStackedWidget>
#include <QListWidget>
#include <QMetaObject>
#include <memory>

#include "core/AppConfig.h"
#include "core/Database.h"
#include "ui/MainWindow.h"
#include "ui/DashboardWidget.h"
#include "ui/MapWidget.h"
#include "ui/EventsTableWidget.h"
#include "ui/AnalyticsWidget.h"
#include "ui/LeadsWidget.h"
#include "ui/AuditLogWidget.h"
#include "ui/SettingsWidget.h"
#include "ui/DebugConsoleWidget.h"

class MainWindowCompletenessTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    void testMainWindowConstruction();
    void testMainWindowHasMenuBar();
    void testMainWindowHasStatusBar();
    void testMainWindowHasTabWidget();
    void testMainWindowHasCorrectTitle();
    void testMainWindowAllWidgetsPresent();
    void testMainWindowShow();
    void testMainWindowRefresh();

private:
    AppConfig                 m_cfg;
    std::shared_ptr<Database> m_db;
    MainWindow*               m_window = nullptr;
};

void MainWindowCompletenessTest::initTestCase()
{
    qputenv("QT_QPA_PLATFORM", "offscreen");

    m_cfg.databasePath       = QStringLiteral(":memory:");
    m_cfg.autoRefreshEnabled = false;

    m_db = std::make_shared<Database>(m_cfg);
    QVERIFY(m_db->open());

    m_window = new MainWindow(m_cfg, m_db);
}

void MainWindowCompletenessTest::cleanupTestCase()
{
    delete m_window;
    m_window = nullptr;
    m_db.reset();
}

void MainWindowCompletenessTest::testMainWindowConstruction()
{
    QVERIFY(m_window != nullptr);
}

void MainWindowCompletenessTest::testMainWindowHasMenuBar()
{
    QVERIFY(m_window->menuBar() != nullptr);
    QVERIFY(m_window->menuBar()->actions().size() >= 3);
}

void MainWindowCompletenessTest::testMainWindowHasStatusBar()
{
    QVERIFY(m_window->statusBar() != nullptr);
}

void MainWindowCompletenessTest::testMainWindowHasTabWidget()
{
    QStackedWidget* stack = m_window->findChild<QStackedWidget*>();
    QVERIFY2(stack != nullptr, "MainWindow must contain a QStackedWidget page stack");
    QVERIFY2(stack->count() >= 6,
             qPrintable(QStringLiteral("Expected >= 6 major views, got %1")
                            .arg(stack->count())));

    QListWidget* nav = m_window->findChild<QListWidget*>();
    QVERIFY2(nav != nullptr, "MainWindow must contain navigation list");
    QVERIFY2(nav->count() >= 6,
             qPrintable(QStringLiteral("Expected >= 6 nav entries, got %1").arg(nav->count())));
}

void MainWindowCompletenessTest::testMainWindowHasCorrectTitle()
{
    QVERIFY2(m_window->windowTitle().contains(QStringLiteral("SENTINEL"), Qt::CaseInsensitive),
             qPrintable(QStringLiteral("Title '%1' must contain SENTINEL")
                            .arg(m_window->windowTitle())));
}

void MainWindowCompletenessTest::testMainWindowAllWidgetsPresent()
{
    QVERIFY(m_window->findChild<DashboardWidget*>() != nullptr);
    QVERIFY(m_window->findChild<EventsTableWidget*>() != nullptr);
    QVERIFY(m_window->findChild<AnalyticsWidget*>() != nullptr);
    QVERIFY(m_window->findChild<LeadsWidget*>() != nullptr);
    QVERIFY(m_window->findChild<AuditLogWidget*>() != nullptr);
    QVERIFY(m_window->findChild<SettingsWidget*>() != nullptr);
    QVERIFY(m_window->findChild<DebugConsoleWidget*>() != nullptr);

    // MapWidget is embedded inside AnalyticsWidget
    QVERIFY(m_window->findChild<MapWidget*>() != nullptr);
}

void MainWindowCompletenessTest::testMainWindowShow()
{
    m_window->show();
    QApplication::processEvents();
    QVERIFY(m_window->isVisible());
}

void MainWindowCompletenessTest::testMainWindowRefresh()
{
    const bool invoked = QMetaObject::invokeMethod(
        m_window, "onRefreshRequested", Qt::DirectConnection);
    QVERIFY(invoked);
    QApplication::processEvents();
    QTest::qWait(50);
    QVERIFY(m_window->statusBar() != nullptr);
}

int main(int argc, char** argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    MainWindowCompletenessTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_main_window_completeness.moc"
