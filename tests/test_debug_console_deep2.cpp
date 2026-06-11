// test_debug_console_deep2.cpp — Deep audit iteration 17: DebugConsoleWidget
// construct, appendEntry (log append), clear, logger integration.

#include <QTest>
#include <QApplication>
#include <QPlainTextEdit>
#include <QComboBox>
#include <QLineEdit>
#include <QPushButton>

#include "ui/DebugConsoleWidget.h"
#include "core/SentinelLogger.h"

class TestDebugConsoleDeep2 : public QObject {
    Q_OBJECT

    static LogEntry makeEntry(QtMsgType level,
                              const QString& message,
                              const QString& category = QStringLiteral("deep2.test"))
    {
        LogEntry e;
        e.timestamp = QDateTime::currentDateTimeUtc();
        e.level     = level;
        e.category  = category;
        e.message   = message;
        e.file      = QStringLiteral("test_debug_console_deep2.cpp");
        e.line      = 0;
        return e;
    }

    static int visibleLineCount(QPlainTextEdit* te)
    {
        const QStringList lines = te->toPlainText().split(QLatin1Char('\n'));
        int count = 0;
        for (const QString& l : lines)
            if (!l.trimmed().isEmpty())
                ++count;
        return count;
    }

private slots:
    void initTestCase()
    {
        qputenv("QT_QPA_PLATFORM", "offscreen");
    }

    void init()
    {
        SentinelLogger::instance().clear();
    }

    void testConstructExposesControls()
    {
        DebugConsoleWidget w;
        w.resize(640, 400);

        QVERIFY(w.findChild<QPlainTextEdit*>() != nullptr);
        QVERIFY(w.findChild<QComboBox*>() != nullptr);
        QVERIFY(w.findChild<QLineEdit*>() != nullptr);
        bool hasClearBtn = false;
        for (auto* btn : w.findChildren<QPushButton*>()) {
            if (btn->text() == QStringLiteral("Clear")) {
                hasClearBtn = true;
                break;
            }
        }
        QVERIFY(hasClearBtn);
    }

    void testAppendEntryDisplaysMessage()
    {
        DebugConsoleWidget w;
        w.clear();

        w.appendEntry(makeEntry(QtInfoMsg, QStringLiteral("DEEP2_append_marker")));

        auto* textEdit = w.findChild<QPlainTextEdit*>();
        QVERIFY(textEdit != nullptr);
        QVERIFY(textEdit->toPlainText().contains(QStringLiteral("DEEP2_append_marker")));
        QVERIFY(textEdit->toPlainText().contains(QStringLiteral("INF")));
    }

    void testClearEmptiesDisplay()
    {
        DebugConsoleWidget w;
        w.clear();

        w.appendEntry(makeEntry(QtWarningMsg, QStringLiteral("before clear")));
        auto* textEdit = w.findChild<QPlainTextEdit*>();
        QVERIFY(textEdit != nullptr);
        QVERIFY(!textEdit->toPlainText().trimmed().isEmpty());

        w.clear();
        QCOMPARE(textEdit->toPlainText().trimmed(), QString());
        QCOMPARE(SentinelLogger::instance().count(), 0);
    }

    void testMultipleAppendIncreasesLineCount()
    {
        DebugConsoleWidget w;
        w.clear();

        auto* textEdit = w.findChild<QPlainTextEdit*>();
        QVERIFY(textEdit != nullptr);
        const int before = visibleLineCount(textEdit);

        w.appendEntry(makeEntry(QtInfoMsg, QStringLiteral("line one")));
        w.appendEntry(makeEntry(QtInfoMsg, QStringLiteral("line two")));
        w.appendEntry(makeEntry(QtInfoMsg, QStringLiteral("line three")));

        QCOMPARE(visibleLineCount(textEdit), before + 3);
    }

    void testLoggerSignalAppendsToWidget()
    {
        SentinelLogger::instance().clear();
        DebugConsoleWidget w;
        w.clear();

        SentinelLogger::instance().install();
        qWarning("DEEP2_logger_signal_marker");
        QApplication::processEvents();
        SentinelLogger::instance().uninstall();

        auto* textEdit = w.findChild<QPlainTextEdit*>();
        QVERIFY(textEdit != nullptr);
        QVERIFY2(textEdit->toPlainText().contains(QStringLiteral("DEEP2_logger_signal_marker")),
                 "DebugConsoleWidget must display entries emitted via SentinelLogger::newEntry");
    }

    void testConstructPreloadsLoggerHistory()
    {
        SentinelLogger::instance().clear();
        SentinelLogger::instance().install();
        qInfo("DEEP2_history_preload_marker");
        SentinelLogger::instance().uninstall();

        DebugConsoleWidget w;

        auto* textEdit = w.findChild<QPlainTextEdit*>();
        QVERIFY(textEdit != nullptr);
        QVERIFY2(textEdit->toPlainText().contains(QStringLiteral("DEEP2_history_preload_marker")),
                 "constructor should replay SentinelLogger::recent() history");

        w.clear();
    }
};

QTEST_MAIN(TestDebugConsoleDeep2)

#include "test_debug_console_deep2.moc"
