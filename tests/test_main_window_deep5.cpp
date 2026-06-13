// test_main_window_deep5.cpp — Deep audit iteration 22: MainWindow shell
// title, menu bar, status bar, central stack, headless construction.

#include <QTest>
#include <QApplication>
#include <QMenuBar>
#include <QStackedWidget>
#include <QStatusBar>
#include <memory>

#include "core/AppConfig.h"
#include "core/Database.h"
#include "ui/MainWindow.h"

class TestMainWindowDeep5 : public QObject {
    Q_OBJECT

private:
    static AppConfig headlessCfg()
    {
        AppConfig cfg;
        cfg.databasePath       = QStringLiteral(":memory:");
        cfg.autoRefreshEnabled = false;
        return cfg;
    }

    struct WindowFixture {
        AppConfig cfg;
        std::shared_ptr<Database> db;
        std::unique_ptr<MainWindow> window;

        WindowFixture()
            : cfg(headlessCfg())
            , db(std::make_shared<Database>(cfg))
        {
            db->open();
            window = std::make_unique<MainWindow>(cfg, db);
        }
    };

    static bool menuContains(MainWindow& window, const QString& name)
    {
        auto* bar = window.menuBar();
        if (!bar)
            return false;
        for (auto* action : bar->actions()) {
            if (action->text().contains(name, Qt::CaseInsensitive))
                return true;
        }
        return false;
    }

private slots:
    void initTestCase()
    {
        qputenv("QT_QPA_PLATFORM", "offscreen");
    }

    void testWindowTitleContainsSentinel()
    {
        WindowFixture fx;
        fx.window->show();
        QApplication::processEvents();

        QVERIFY(fx.window->windowTitle().contains(QStringLiteral("SENTINEL"),
                                                  Qt::CaseInsensitive));
    }

    void testMenuBarHasFileMenu()
    {
        WindowFixture fx;
        fx.window->show();
        QApplication::processEvents();

        QVERIFY(fx.window->menuBar() != nullptr);
        QVERIFY2(menuContains(*fx.window, QStringLiteral("File")),
                 "Menu bar should expose a File menu");
    }

    void testStatusBarExists()
    {
        WindowFixture fx;
        fx.window->show();
        QApplication::processEvents();

        QVERIFY(fx.window->statusBar() != nullptr);
        QVERIFY(fx.window->statusBar()->isVisible());
    }

    void testCentralStackWidgetExists()
    {
        WindowFixture fx;
        fx.window->show();
        QApplication::processEvents();

        auto* stack = fx.window->findChild<QStackedWidget*>();
        QVERIFY2(stack != nullptr, "MainWindow should host a QStackedWidget navigation stack");
        QVERIFY(stack->count() >= 1);
        QVERIFY(fx.window->centralWidget() != nullptr);
    }

    void testConstructHeadlessWithoutCrash()
    {
        auto cfg = headlessCfg();
        auto db  = std::make_shared<Database>(cfg);
        db->open();

        MainWindow window(cfg, db);
        QApplication::processEvents();

        QVERIFY(window.centralWidget() != nullptr);
        window.show();
        QApplication::processEvents();
        window.hide();
        QApplication::processEvents();
    }
};

QTEST_MAIN(TestMainWindowDeep5)

#include "test_main_window_deep5.moc"
