// test_main_window_deep6.cpp — Deep audit iteration 26: MainWindow
// toolbar, stacked pages, minimum size, close without crash.
#include <QTest>
#include <QApplication>
#include <QStackedWidget>
#include <QToolBar>
#include <memory>
#include "core/AppConfig.h"
#include "core/Database.h"
#include "ui/MainWindow.h"

class TestMainWindowDeep6 : public QObject
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

    void testHasCentralStack()
    {
        Fx fx;
        fx.win->show();
        QApplication::processEvents();
        QVERIFY(fx.win->findChild<QStackedWidget*>() != nullptr);
    }

    void testHasToolBar()
    {
        Fx fx;
        fx.win->show();
        QApplication::processEvents();
        QVERIFY(!fx.win->findChildren<QToolBar*>().isEmpty());
    }

    void testMinimumSizeReasonable()
    {
        Fx fx;
        fx.win->show();
        QApplication::processEvents();
        QVERIFY(fx.win->minimumWidth() >= 400);
        QVERIFY(fx.win->minimumHeight() >= 300);
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

QTEST_MAIN(TestMainWindowDeep6)
#include "test_main_window_deep6.moc"
