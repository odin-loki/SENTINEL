// test_main_window_deep3.cpp — Deep audit iteration 15: MainWindow headless
// construction, menu/tabs presence, and resize stability.

#include <QTest>
#include <QApplication>
#include <QListWidget>
#include <QStackedWidget>
#include <QMenuBar>
#include <QToolBar>
#include <QStatusBar>
#include <memory>

#include "core/AppConfig.h"
#include "core/Database.h"
#include "ui/MainWindow.h"

class TestMainWindowDeep3 : public QObject {
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

private slots:
    void initTestCase()
    {
        qputenv("QT_QPA_PLATFORM", "offscreen");
    }

    void testConstructHeadless()
    {
        auto cfg = headlessCfg();
        auto db  = std::make_shared<Database>(cfg);
        db->open();

        MainWindow window(cfg, db);
        QApplication::processEvents();
        QVERIFY(window.centralWidget() != nullptr);
    }

    void testMenuBarExists()
    {
        WindowFixture fx;
        fx.window->show();
        QApplication::processEvents();

        auto* bar = fx.window->menuBar();
        QVERIFY(bar != nullptr);

        const QStringList expected = {
            QStringLiteral("File"),
            QStringLiteral("View"),
            QStringLiteral("Debug"),
            QStringLiteral("Help"),
        };
        for (const auto& name : expected) {
            bool found = false;
            for (auto* action : bar->actions()) {
                if (action->text().contains(name, Qt::CaseInsensitive)) {
                    found = true;
                    break;
                }
            }
            QVERIFY2(found, qPrintable(QStringLiteral("Menu '%1' not found").arg(name)));
        }
    }

    void testNavTabsExist()
    {
        WindowFixture fx;
        fx.window->show();
        QApplication::processEvents();

        auto* nav   = fx.window->findChild<QListWidget*>();
        auto* stack = fx.window->findChild<QStackedWidget*>();
        QVERIFY(nav != nullptr);
        QVERIFY(stack != nullptr);

        QCOMPARE(nav->count(), 7);
        QCOMPARE(stack->count(), 7);
        QVERIFY(nav->count() == stack->count());
    }

    void testResizeDoesNotCrash()
    {
        WindowFixture fx;
        fx.window->show();
        QApplication::processEvents();

        const QList<QSize> sizes = {
            QSize(1280, 720),
            QSize(800, 600),
            QSize(1920, 1080),
            QSize(640, 480),
        };
        for (const auto& sz : sizes) {
            fx.window->resize(sz);
            QApplication::processEvents();
        }
        QVERIFY(true);
    }

    void testToolBarAndStatusBarExist()
    {
        WindowFixture fx;
        fx.window->show();
        QApplication::processEvents();

        QVERIFY(!fx.window->findChildren<QToolBar*>().isEmpty());
        QVERIFY(fx.window->statusBar() != nullptr);
    }

    void testTabSwitchAfterResize()
    {
        WindowFixture fx;
        fx.window->show();
        fx.window->resize(900, 650);
        QApplication::processEvents();

        auto* nav   = fx.window->findChild<QListWidget*>();
        auto* stack = fx.window->findChild<QStackedWidget*>();
        QVERIFY(nav != nullptr);
        QVERIFY(stack != nullptr);

        for (int i = 0; i < nav->count(); ++i) {
            nav->setCurrentRow(i);
            QApplication::processEvents();
            QCOMPARE(stack->currentIndex(), i);
        }
    }
};

QTEST_MAIN(TestMainWindowDeep3)

#include "test_main_window_deep3.moc"
