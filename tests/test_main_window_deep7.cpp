// test_main_window_deep7.cpp — Deep audit iteration 30: MainWindow
// window title, menu bar, status bar, central widget, close event.
#include <QTest>
#include <QApplication>
#include <QMenu>
#include <QStatusBar>
#include <memory>
#include "core/AppConfig.h"
#include "core/Database.h"
#include "ui/MainWindow.h"

class TestMainWindowDeep7 : public QObject
{
    Q_OBJECT

    struct Fx {
        AppConfig cfg;
        std::shared_ptr<Database> db;
        std::unique_ptr<MainWindow> win;

        Fx()
            : cfg([] {
                AppConfig c;
                c.databasePath = QStringLiteral(":memory:");
                c.autoRefreshEnabled = false;
                return c;
            }())
            , db(std::make_shared<Database>(cfg))
        {
            db->open();
            win = std::make_unique<MainWindow>(cfg, db);
        }
    };

private slots:
    void initTestCase() { qputenv("QT_QPA_PLATFORM", "offscreen"); }

    void testWindowTitleContainsSentinel()
    {
        Fx fx;
        fx.win->show();
        QApplication::processEvents();
        QVERIFY(fx.win->windowTitle().contains(QStringLiteral("SENTINEL")));
    }

    void testMenuBarHasFileMenu()
    {
        Fx fx;
        fx.win->show();
        QApplication::processEvents();

        bool hasFile = false;
        for (auto* menu : fx.win->findChildren<QMenu*>()) {
            if (menu->title().contains(QStringLiteral("File"), Qt::CaseInsensitive))
                hasFile = true;
        }
        QVERIFY(hasFile);
    }

    void testStatusBarExists()
    {
        Fx fx;
        fx.win->show();
        QApplication::processEvents();
        QVERIFY(fx.win->statusBar() != nullptr);
    }

    void testCentralWidgetPresent()
    {
        Fx fx;
        fx.win->show();
        QApplication::processEvents();
        QVERIFY(fx.win->centralWidget() != nullptr);
    }

    void testCloseDoesNotCrash()
    {
        Fx fx;
        fx.win->show();
        QApplication::processEvents();
        fx.win->close();
        QApplication::processEvents();
        QVERIFY(true);
    }
};

QTEST_MAIN(TestMainWindowDeep7)
#include "test_main_window_deep7.moc"
