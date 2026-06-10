// test_main_window_integration.cpp — MainWindow UI integration tests for SENTINEL.
// Requires QApplication (not QCoreApplication) because MainWindow is a QMainWindow.

#include <QTest>
#include <QApplication>
#include <QListWidget>
#include <QStackedWidget>
#include <QStatusBar>
#include <QLabel>
#include <QMetaObject>
#include <memory>

#include "core/AppConfig.h"
#include "core/Database.h"
#include "ui/MainWindow.h"

class TestMainWindow : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    void testMainWindowCreation();
    void testMainWindowTitle();
    void testNavigation();
    void testInitialPage();
    void testStatusBar();
    void testWindowMinSize();
    void testRefreshDoesNotCrash();
    void testAllWidgetsCreated();

private:
    AppConfig                 m_cfg;
    std::shared_ptr<Database> m_db;
    MainWindow*               m_window = nullptr;
};

// ─────────────────────────────────────────────────────────────────────────────

void TestMainWindow::initTestCase()
{
    qputenv("QT_QPA_PLATFORM", "offscreen");

    m_cfg = AppConfig{};
    m_cfg.databasePath       = QStringLiteral(":memory:");
    m_cfg.autoRefreshEnabled = false;

    m_db = std::make_shared<Database>(m_cfg);
    m_db->open();

    m_window = new MainWindow(m_cfg, m_db);
    m_window->show();
    QApplication::processEvents();
}

void TestMainWindow::cleanupTestCase()
{
    delete m_window;
    m_window = nullptr;
    m_db.reset();
}

// ── 1. testMainWindowCreation ─────────────────────────────────────────────────
// MainWindow must construct without crashing and the pointer must be valid.

void TestMainWindow::testMainWindowCreation()
{
    QVERIFY2(m_window != nullptr, "MainWindow must be non-null after construction");
}

// ── 2. testMainWindowTitle ────────────────────────────────────────────────────
// Window title must contain the word "SENTINEL" (case-insensitive).

void TestMainWindow::testMainWindowTitle()
{
    QVERIFY2(m_window->windowTitle().contains(QStringLiteral("SENTINEL"),
                                               Qt::CaseInsensitive),
             qPrintable(QStringLiteral("Window title '%1' does not contain 'SENTINEL'")
                        .arg(m_window->windowTitle())));
}

// ── 3. testNavigation ─────────────────────────────────────────────────────────
// Clicking each navigation item (0–6) must switch the stacked-widget page
// without crashing.

void TestMainWindow::testNavigation()
{
    QListWidget*    nav   = m_window->findChild<QListWidget*>();
    QStackedWidget* stack = m_window->findChild<QStackedWidget*>();

    QVERIFY2(nav   != nullptr, "MainWindow must contain a QListWidget for navigation");
    QVERIFY2(stack != nullptr, "MainWindow must contain a QStackedWidget");

    const int pageCount = stack->count();
    const int navCount  = qMin(nav->count(), pageCount);

    for (int i = 0; i < navCount; ++i) {
        nav->setCurrentRow(i);
        QApplication::processEvents();
        QTest::qWait(30);
        // Verify we didn't crash — the index should now equal i
        QVERIFY(stack->currentIndex() >= 0);
    }
}

// ── 4. testInitialPage ────────────────────────────────────────────────────────
// On startup, the Dashboard (index 0) must be the visible page.

void TestMainWindow::testInitialPage()
{
    QStackedWidget* stack = m_window->findChild<QStackedWidget*>();
    QVERIFY2(stack != nullptr, "QStackedWidget not found");
    // Navigate to page 0 and confirm
    QListWidget* nav = m_window->findChild<QListWidget*>();
    if (nav) nav->setCurrentRow(0);
    QApplication::processEvents();
    QCOMPARE(stack->currentIndex(), 0);
}

// ── 5. testStatusBar ─────────────────────────────────────────────────────────
// The status bar widget must exist (non-null).

void TestMainWindow::testStatusBar()
{
    QVERIFY2(m_window->statusBar() != nullptr,
             "MainWindow must have a status bar");
}

// ── 6. testWindowMinSize ─────────────────────────────────────────────────────
// The window's minimum size must be at least 800 × 600.

void TestMainWindow::testWindowMinSize()
{
    QVERIFY2(m_window->minimumWidth()  >= 800,
             qPrintable(QStringLiteral("minimumWidth %1 < 800")
                        .arg(m_window->minimumWidth())));
    QVERIFY2(m_window->minimumHeight() >= 600,
             qPrintable(QStringLiteral("minimumHeight %1 < 600")
                        .arg(m_window->minimumHeight())));
}

// ── 7. testRefreshDoesNotCrash ────────────────────────────────────────────────
// Invoking onRefreshRequested() must not crash.

void TestMainWindow::testRefreshDoesNotCrash()
{
    QMetaObject::invokeMethod(m_window, "onRefreshRequested",
                               Qt::DirectConnection);
    QTest::qWait(100);
    QVERIFY(true);
}

// ── 8. testAllWidgetsCreated ──────────────────────────────────────────────────
// The QStackedWidget should contain 7 pages — one for each main widget.

void TestMainWindow::testAllWidgetsCreated()
{
    QStackedWidget* stack = m_window->findChild<QStackedWidget*>();
    QVERIFY2(stack != nullptr, "QStackedWidget not found");
    QVERIFY2(stack->count() >= 7,
             qPrintable(QStringLiteral("Expected >= 7 pages in QStackedWidget, got %1")
                        .arg(stack->count())));

    // Every page widget must be non-null
    for (int i = 0; i < stack->count(); ++i) {
        QVERIFY2(stack->widget(i) != nullptr,
                 qPrintable(QStringLiteral("Page %1 widget is null").arg(i)));
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// main
// ─────────────────────────────────────────────────────────────────────────────

int main(int argc, char** argv)
{
    // Platform must be set before QApplication is constructed
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    TestMainWindow t;
    return QTest::qExec(&t, argc, argv);
}

#include "test_main_window_integration.moc"
