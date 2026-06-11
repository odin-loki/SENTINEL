// test_main_window_deep2.cpp — Deep audit iteration 12: MainWindow construction & navigation.

#include <QTest>
#include <QApplication>
#include <QListWidget>
#include <QStackedWidget>
#include <memory>

#include "core/AppConfig.h"
#include "core/Database.h"
#include "ui/MainWindow.h"

class TestMainWindowDeep2 : public QObject {
    Q_OBJECT

private slots:
    void initTestCase()
    {
        qputenv("QT_QPA_PLATFORM", "offscreen");
    }

    void testConstructsWithoutCrash()
    {
        AppConfig cfg;
        cfg.databasePath       = QStringLiteral(":memory:");
        cfg.autoRefreshEnabled = false;

        auto db = std::make_shared<Database>(cfg);
        db->open();

        MainWindow window(cfg, db);
        window.show();
        QApplication::processEvents();
        QVERIFY(true);
    }

    void testWindowTitleNonEmpty()
    {
        AppConfig cfg;
        cfg.databasePath       = QStringLiteral(":memory:");
        cfg.autoRefreshEnabled = false;

        auto db = std::make_shared<Database>(cfg);
        db->open();

        MainWindow window(cfg, db);
        QVERIFY(!window.windowTitle().isEmpty());
    }

    void testTabWidgetSwitching()
    {
        AppConfig cfg;
        cfg.databasePath       = QStringLiteral(":memory:");
        cfg.autoRefreshEnabled = false;

        auto db = std::make_shared<Database>(cfg);
        db->open();

        MainWindow window(cfg, db);
        window.show();
        QApplication::processEvents();

        auto* nav   = window.findChild<QListWidget*>();
        auto* stack = window.findChild<QStackedWidget*>();
        QVERIFY(nav != nullptr);
        QVERIFY(stack != nullptr);

        const int pages = qMin(nav->count(), stack->count());
        QVERIFY(pages > 1);

        for (int i = 0; i < pages; ++i) {
            nav->setCurrentRow(i);
            QApplication::processEvents();
            QCOMPARE(stack->currentIndex(), i);
        }
    }
};

QTEST_MAIN(TestMainWindowDeep2)

#include "test_main_window_deep2.moc"
