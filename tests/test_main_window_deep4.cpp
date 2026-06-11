// test_main_window_deep4.cpp — Deep audit iteration 19: MainWindow initial refresh,
// toolbar event count, nav-triggered page refresh, status bar stability.

#include <QTest>
#include <QApplication>
#include <QListWidget>
#include <QStackedWidget>
#include <QLabel>
#include <QToolBar>
#include <QAction>
#include <memory>

#include "core/AppConfig.h"
#include "core/Database.h"
#include "core/CrimeEvent.h"
#include "ui/MainWindow.h"

class TestMainWindowDeep4 : public QObject {
    Q_OBJECT

private:
    static AppConfig headlessCfg()
    {
        AppConfig cfg;
        cfg.databasePath       = QStringLiteral(":memory:");
        cfg.autoRefreshEnabled = false;
        return cfg;
    }

    static CrimeEvent makeEvent(const QString& id)
    {
        CrimeEvent ev;
        ev.eventId    = id;
        ev.id         = id;
        ev.crimeType  = QStringLiteral("theft");
        ev.suburb     = QStringLiteral("MainWinSuburb");
        ev.ingestedAt = QDateTime::currentDateTimeUtc();
        ev.occurredAt = QDateTime::currentDateTimeUtc();
        ev.timestamp  = ev.occurredAt.value();
        ev.source     = QStringLiteral("deep4_test");
        return ev;
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

    static QLabel* eventCountLabel(MainWindow& window)
    {
        for (auto* lbl : window.findChildren<QLabel*>()) {
            if (lbl->text().startsWith(QStringLiteral("Events:")))
                return lbl;
        }
        return nullptr;
    }

private slots:
    void initTestCase()
    {
        qputenv("QT_QPA_PLATFORM", "offscreen");
    }

    void testInitialRefreshShowsZeroEvents()
    {
        WindowFixture fx;
        fx.window->show();
        QApplication::processEvents();

        auto* countLbl = eventCountLabel(*fx.window);
        QVERIFY(countLbl != nullptr);
        QVERIFY(countLbl->text().contains(QStringLiteral("0")));
    }

    void testEventCountUpdatesAfterInsert()
    {
        WindowFixture fx;
        for (int i = 0; i < 5; ++i)
            QVERIFY(fx.db->insertEvent(makeEvent(QStringLiteral("MW4-%1").arg(i))));

        fx.window->show();
        QApplication::processEvents();

        // Trigger refresh via toolbar action
        for (auto* tb : fx.window->findChildren<QToolBar*>()) {
            for (auto* act : tb->actions()) {
                if (act->text().contains(QStringLiteral("Refresh"), Qt::CaseInsensitive)) {
                    act->trigger();
                    break;
                }
            }
        }
        QApplication::processEvents();

        auto* countLbl = eventCountLabel(*fx.window);
        QVERIFY(countLbl != nullptr);
        QVERIFY(countLbl->text().contains(QStringLiteral("5")));
    }

    void testNavToLeadsAndEventsPagesRefresh()
    {
        WindowFixture fx;
        QVERIFY(fx.db->insertEvent(makeEvent(QStringLiteral("MW4-NAV"))));

        fx.window->show();
        QApplication::processEvents();

        auto* nav = fx.window->findChild<QListWidget*>();
        QVERIFY(nav != nullptr);

        nav->setCurrentRow(1);  // Crime Events
        QApplication::processEvents();
        nav->setCurrentRow(3);  // Investigative Leads
        QApplication::processEvents();
        nav->setCurrentRow(0);  // Dashboard
        QApplication::processEvents();

        QCOMPARE(fx.window->findChild<QStackedWidget*>()->currentIndex(), 0);
    }

    void testStatusBarReadyAfterConstruct()
    {
        WindowFixture fx;
        fx.window->show();
        QApplication::processEvents();

        bool foundReady = false;
        for (auto* lbl : fx.window->findChildren<QLabel*>()) {
            if (lbl->text() == QStringLiteral("Ready")) {
                foundReady = true;
                break;
            }
        }
        QVERIFY2(foundReady, "Status bar should show Ready after initial refresh");
    }

    void testNavLabelsContainExpectedSections()
    {
        WindowFixture fx;
        fx.window->show();
        QApplication::processEvents();

        auto* nav = fx.window->findChild<QListWidget*>();
        QVERIFY(nav != nullptr);
        QCOMPARE(nav->count(), 7);

        const QStringList expected = {
            QStringLiteral("Dashboard"),
            QStringLiteral("Crime Events"),
            QStringLiteral("Analytics"),
            QStringLiteral("Investigative Leads"),
            QStringLiteral("Audit Log"),
            QStringLiteral("Settings"),
            QStringLiteral("Debug Console"),
        };
        for (int i = 0; i < expected.size(); ++i)
            QVERIFY2(nav->item(i)->text().contains(expected.at(i)),
                     qPrintable(QStringLiteral("Nav item %1 missing '%2'")
                                    .arg(i).arg(expected.at(i))));
    }

    void testRapidNavSwitchStableStackIndex()
    {
        WindowFixture fx;
        fx.window->show();
        QApplication::processEvents();

        auto* nav   = fx.window->findChild<QListWidget*>();
        auto* stack = fx.window->findChild<QStackedWidget*>();
        QVERIFY(nav != nullptr);
        QVERIFY(stack != nullptr);

        for (int pass = 0; pass < 2; ++pass) {
            for (int i = 0; i < nav->count(); ++i) {
                nav->setCurrentRow(i);
                QApplication::processEvents();
                QCOMPARE(stack->currentIndex(), i);
            }
        }
    }

    void testWindowMinimumSizeRespected()
    {
        WindowFixture fx;
        fx.window->show();
        QApplication::processEvents();

        QCOMPARE(fx.window->minimumWidth(), 1280);
        QCOMPARE(fx.window->minimumHeight(), 720);
    }
};

QTEST_MAIN(TestMainWindowDeep4)

#include "test_main_window_deep4.moc"
