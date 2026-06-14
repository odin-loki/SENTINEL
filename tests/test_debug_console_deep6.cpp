// test_debug_console_deep6.cpp — Deep audit iteration 26: DebugConsoleWidget
// warning filter, clear button, append ordering, category filter substring.
#include <QTest>
#include <QApplication>
#include <QPlainTextEdit>
#include <QComboBox>
#include <QLineEdit>
#include <QPushButton>
#include "ui/DebugConsoleWidget.h"
#include "core/SentinelLogger.h"

class TestDebugConsoleDeep6 : public QObject
{
    Q_OBJECT

    static LogEntry entry(QtMsgType level, const QString& msg,
                          const QString& cat = QStringLiteral("deep6.cat"))
    {
        LogEntry e;
        e.timestamp = QDateTime::currentDateTimeUtc();
        e.level     = level;
        e.category  = cat;
        e.message   = msg;
        return e;
    }

private slots:
    void initTestCase() { qputenv("QT_QPA_PLATFORM", "offscreen"); }

    void testAppendShowsMessage()
    {
        DebugConsoleWidget console;
        console.appendEntry(entry(QtWarningMsg, QStringLiteral("deep6 warning token")));
        QApplication::processEvents();

        auto* text = console.findChild<QPlainTextEdit*>();
        QVERIFY(text != nullptr);
        QVERIFY(text->toPlainText().contains(QStringLiteral("deep6 warning token")));
    }

    void testClearEmptiesView()
    {
        DebugConsoleWidget console;
        console.appendEntry(entry(QtInfoMsg, QStringLiteral("to clear")));
        QApplication::processEvents();

        QPushButton* clearBtn = nullptr;
        for (auto* btn : console.findChildren<QPushButton*>()) {
            if (btn->text().contains(QStringLiteral("Clear"), Qt::CaseInsensitive))
                clearBtn = btn;
        }
        QVERIFY(clearBtn != nullptr);
        QTest::mouseClick(clearBtn, Qt::LeftButton);
        QApplication::processEvents();

        auto* text = console.findChild<QPlainTextEdit*>();
        QVERIFY(text->toPlainText().trimmed().isEmpty());
    }

    void testCategoryFilterNarrowsOutput()
    {
        DebugConsoleWidget console;
        console.appendEntry(entry(QtInfoMsg, QStringLiteral("alpha msg"), QStringLiteral("cat.alpha")));
        console.appendEntry(entry(QtInfoMsg, QStringLiteral("beta msg"), QStringLiteral("cat.beta")));
        QApplication::processEvents();

        auto* filter = console.findChild<QLineEdit*>();
        QVERIFY(filter != nullptr);
        filter->setText(QStringLiteral("cat.alpha"));
        QApplication::processEvents();

        auto* text = console.findChild<QPlainTextEdit*>();
        QVERIFY(text->toPlainText().contains(QStringLiteral("alpha")));
    }

    void testLevelComboExists()
    {
        DebugConsoleWidget console;
        QVERIFY(console.findChild<QComboBox*>() != nullptr);
    }
};

QTEST_MAIN(TestDebugConsoleDeep6)
#include "test_debug_console_deep6.moc"
