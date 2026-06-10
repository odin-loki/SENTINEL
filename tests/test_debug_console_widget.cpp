// test_debug_console_widget.cpp
// Headless unit tests for DebugConsoleWidget.
// Run with: test_debug_console_widget.exe -platform offscreen
#include <QTest>
#include <QApplication>
#include <QPlainTextEdit>
#include <QComboBox>
#include <QLineEdit>
#include <QPushButton>
#include <QTextDocument>
#include <QTextBlock>
#include <QTextFragment>
#include <QCoreApplication>

#include "ui/DebugConsoleWidget.h"
#include "core/SentinelLogger.h"

// ─── helpers ─────────────────────────────────────────────────────────────────

static LogEntry makeEntry(QtMsgType     level,
                           const QString& message,
                           const QString& category = "test.category")
{
    LogEntry e;
    e.timestamp = QDateTime::currentDateTime();
    e.level     = level;
    e.category  = category;
    e.message   = message;
    e.file      = "test_debug_console_widget.cpp";
    e.line      = 0;
    return e;
}

// Counts non-empty lines visible in the QPlainTextEdit
static int visibleLineCount(QPlainTextEdit* te)
{
    const QStringList lines = te->toPlainText().split('\n');
    int count = 0;
    for (const QString& l : lines)
        if (!l.trimmed().isEmpty()) ++count;
    return count;
}

// ─── test class ──────────────────────────────────────────────────────────────

class TestDebugConsoleWidget : public QObject {
    Q_OBJECT

private slots:

    // 1. Widget constructs without crash
    void testWidgetCreation()
    {
        SentinelLogger::instance().clear();
        DebugConsoleWidget w;
        QVERIFY(true);
    }

    // 2. appendEntry() with warning entry is stored and displayed
    void testAppendWarningEntry()
    {
        SentinelLogger::instance().clear();
        DebugConsoleWidget w;
        w.clear();

        w.appendEntry(makeEntry(QtWarningMsg, "test warning message"));

        auto* textEdit = w.findChild<QPlainTextEdit*>();
        QVERIFY(textEdit != nullptr);
        QVERIFY(!textEdit->toPlainText().trimmed().isEmpty());
    }

    // 3. clear() resets entries — textEdit becomes empty
    void testClearResetsEntries()
    {
        SentinelLogger::instance().clear();
        DebugConsoleWidget w;
        w.clear();

        w.appendEntry(makeEntry(QtInfoMsg, "entry 1"));
        w.appendEntry(makeEntry(QtInfoMsg, "entry 2"));

        auto* textEdit = w.findChild<QPlainTextEdit*>();
        QVERIFY(textEdit != nullptr);
        QVERIFY(!textEdit->toPlainText().trimmed().isEmpty());

        w.clear();
        QVERIFY(textEdit->toPlainText().trimmed().isEmpty());
    }

    // 4. Multiple appendEntry() calls increase the displayed line count
    void testMultipleAppendIncreasesCount()
    {
        SentinelLogger::instance().clear();
        DebugConsoleWidget w;
        w.clear();

        auto* textEdit = w.findChild<QPlainTextEdit*>();
        QVERIFY(textEdit != nullptr);
        const int before = visibleLineCount(textEdit);

        w.appendEntry(makeEntry(QtInfoMsg, "message 1"));
        w.appendEntry(makeEntry(QtInfoMsg, "message 2"));
        w.appendEntry(makeEntry(QtInfoMsg, "message 3"));

        QVERIFY(visibleLineCount(textEdit) > before);
        QCOMPARE(visibleLineCount(textEdit), before + 3);
    }

    // 5. appendEntry() with debug entry is stored (default filter shows Debug)
    void testAppendDebugEntry()
    {
        SentinelLogger::instance().clear();
        DebugConsoleWidget w;
        w.clear();

        w.appendEntry(makeEntry(QtDebugMsg, "debug message"));

        auto* textEdit = w.findChild<QPlainTextEdit*>();
        QVERIFY(textEdit != nullptr);
        QVERIFY(!textEdit->toPlainText().trimmed().isEmpty());
    }

    // 6. Level filter: only shows entries at or above selected level
    void testLevelFilter()
    {
        SentinelLogger::instance().clear();
        DebugConsoleWidget w;
        w.clear();

        w.appendEntry(makeEntry(QtDebugMsg,    "debug msg"));
        w.appendEntry(makeEntry(QtInfoMsg,     "info msg"));
        w.appendEntry(makeEntry(QtWarningMsg,  "warning msg"));
        w.appendEntry(makeEntry(QtCriticalMsg, "critical msg"));

        auto* textEdit    = w.findChild<QPlainTextEdit*>();
        auto* levelFilter = w.findChild<QComboBox*>();
        QVERIFY(textEdit != nullptr && levelFilter != nullptr);

        const int allCount = visibleLineCount(textEdit);
        QCOMPARE(allCount, 4);  // all 4 visible at Debug level (index 0)

        // Switch to Warning (index 2) — only Warning + Critical should be visible
        levelFilter->setCurrentIndex(2);
        QCoreApplication::processEvents();

        const int filteredCount = visibleLineCount(textEdit);
        QVERIFY(filteredCount < allCount);
        QCOMPARE(filteredCount, 2);
    }

    // 7. Category filter text doesn't crash
    void testCategoryFilterNoCrash()
    {
        SentinelLogger::instance().clear();
        DebugConsoleWidget w;
        w.clear();

        w.appendEntry(makeEntry(QtInfoMsg, "msg a", "alpha.module"));
        w.appendEntry(makeEntry(QtInfoMsg, "msg b", "beta.module"));

        auto* catFilter = w.findChild<QLineEdit*>();
        if (!catFilter) QSKIP("No QLineEdit (category filter) found in widget");

        catFilter->setText("alpha");
        QCoreApplication::processEvents();
        catFilter->clear();
        QCoreApplication::processEvents();
        QVERIFY(true);
    }

    // 8. formatEntry returns non-empty string — verified via textEdit content
    void testFormatEntryNonEmpty()
    {
        SentinelLogger::instance().clear();
        DebugConsoleWidget w;
        w.clear();

        // appendEntry calls formatEntry internally; check the result is non-empty
        w.appendEntry(makeEntry(QtInfoMsg, "formatted info entry"));

        auto* textEdit = w.findChild<QPlainTextEdit*>();
        QVERIFY(textEdit != nullptr);
        QVERIFY(!textEdit->toPlainText().trimmed().isEmpty());
    }

    // 9. levelTag returns non-empty string — verified via tag in formatted output
    void testLevelTagNonEmpty()
    {
        SentinelLogger::instance().clear();
        DebugConsoleWidget w;
        w.clear();

        // levelTag(QtWarningMsg) → "WRN"; formatEntry embeds it as [WRN]
        w.appendEntry(makeEntry(QtWarningMsg, "warning message"));

        auto* textEdit = w.findChild<QPlainTextEdit*>();
        QVERIFY(textEdit != nullptr);
        QVERIFY(textEdit->toPlainText().contains("WRN"));
    }

    // 10. colorForLevel returns non-empty string — verified via colored text in document
    void testColorForLevelNonEmpty()
    {
        SentinelLogger::instance().clear();
        DebugConsoleWidget w;
        w.clear();

        // Append one entry per level; colorForLevel is called for each
        w.appendEntry(makeEntry(QtDebugMsg,    "debug entry"));
        w.appendEntry(makeEntry(QtInfoMsg,     "info entry"));
        w.appendEntry(makeEntry(QtWarningMsg,  "warning entry"));
        w.appendEntry(makeEntry(QtCriticalMsg, "critical entry"));

        auto* textEdit = w.findChild<QPlainTextEdit*>();
        QVERIFY(textEdit != nullptr);

        // Verify colored text exists in the document (colorForLevel sets foreground)
        bool hasColor = false;
        QTextDocument* doc = textEdit->document();
        QTextBlock block = doc->begin();
        while (block.isValid() && !hasColor) {
            for (auto it = block.begin(); !it.atEnd(); ++it) {
                const QColor fg = it.fragment().charFormat().foreground().color();
                if (fg.isValid() && fg.alpha() > 0) {
                    hasColor = true;
                    break;
                }
            }
            block = block.next();
        }
        QVERIFY(hasColor);
    }
};

// ─── main ────────────────────────────────────────────────────────────────────

int main(int argc, char** argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    TestDebugConsoleWidget t;
    return QTest::qExec(&t, argc, argv);
}

#include "test_debug_console_widget.moc"
