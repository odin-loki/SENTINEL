// test_debug_console_deep3.cpp — Deep audit iteration 22: DebugConsoleWidget
// level filter, category substring filter, clear, max-entry trim, timestamps.

#include <QTest>
#include <QApplication>
#include <QPlainTextEdit>
#include <QComboBox>
#include <QLineEdit>
#include <QPushButton>

#include "ui/DebugConsoleWidget.h"
#include "core/SentinelLogger.h"

class TestDebugConsoleDeep3 : public QObject {
    Q_OBJECT

    static LogEntry makeEntry(QtMsgType level,
                              const QString& message,
                              const QString& category = QStringLiteral("deep3.test"),
                              const QDateTime& timestamp = QDateTime())
    {
        LogEntry e;
        e.timestamp = timestamp.isValid() ? timestamp : QDateTime::currentDateTimeUtc();
        e.level     = level;
        e.category  = category;
        e.message   = message;
        e.file      = QStringLiteral("test_debug_console_deep3.cpp");
        e.line      = 0;
        return e;
    }

    static QComboBox* levelFilter(DebugConsoleWidget& w)
    {
        return w.findChild<QComboBox*>();
    }

    static QLineEdit* categoryFilter(DebugConsoleWidget& w)
    {
        return w.findChild<QLineEdit*>();
    }

    static QPlainTextEdit* textEdit(DebugConsoleWidget& w)
    {
        return w.findChild<QPlainTextEdit*>();
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

    void testLevelFilterHidesDebugEntries()
    {
        DebugConsoleWidget w;
        w.clear();

        w.appendEntry(makeEntry(QtDebugMsg, QStringLiteral("DEEP3_debug_hidden")));
        w.appendEntry(makeEntry(QtInfoMsg, QStringLiteral("DEEP3_info_visible")));

        auto* level = levelFilter(w);
        auto* text  = textEdit(w);
        QVERIFY(level != nullptr);
        QVERIFY(text != nullptr);

        level->setCurrentIndex(1); // Info minimum — hides Debug
        QApplication::processEvents();

        const QString plain = text->toPlainText();
        QVERIFY(!plain.contains(QStringLiteral("DEEP3_debug_hidden")));
        QVERIFY(plain.contains(QStringLiteral("DEEP3_info_visible")));
        QVERIFY(plain.contains(QStringLiteral("INF")));
    }

    void testCategoryFilterSubstringMatch()
    {
        DebugConsoleWidget w;
        w.clear();

        w.appendEntry(makeEntry(QtInfoMsg,
                                QStringLiteral("DEEP3_nlp_message"),
                                QStringLiteral("nlp.pipeline")));
        w.appendEntry(makeEntry(QtInfoMsg,
                                QStringLiteral("DEEP3_db_message"),
                                QStringLiteral("db.storage")));

        auto* catFilter = categoryFilter(w);
        auto* text      = textEdit(w);
        QVERIFY(catFilter != nullptr);
        QVERIFY(text != nullptr);

        catFilter->setText(QStringLiteral("nlp"));
        QApplication::processEvents();

        const QString plain = text->toPlainText();
        QVERIFY(plain.contains(QStringLiteral("DEEP3_nlp_message")));
        QVERIFY(!plain.contains(QStringLiteral("DEEP3_db_message")));
    }

    void testClearEmptiesDisplay()
    {
        DebugConsoleWidget w;
        w.clear();

        w.appendEntry(makeEntry(QtWarningMsg, QStringLiteral("DEEP3_before_clear")));
        auto* text = textEdit(w);
        QVERIFY(text != nullptr);
        QVERIFY(!text->toPlainText().trimmed().isEmpty());

        w.clear();
        QApplication::processEvents();

        QCOMPARE(text->toPlainText().trimmed(), QString());
        QCOMPARE(SentinelLogger::instance().count(), 0);
    }

    void testAppendPreservesTimestamp()
    {
        DebugConsoleWidget w;
        w.clear();

        const QDateTime fixed = QDateTime(QDate(2024, 3, 15), QTime(14, 30, 45, 123), Qt::UTC);
        w.appendEntry(makeEntry(QtInfoMsg,
                                QStringLiteral("DEEP3_timestamp_marker"),
                                QStringLiteral("deep3.test"),
                                fixed));

        auto* text = textEdit(w);
        QVERIFY(text != nullptr);
        QVERIFY(text->toPlainText().contains(
            fixed.toString(QStringLiteral("HH:mm:ss.zzz"))));
        QVERIFY(text->toPlainText().contains(QStringLiteral("DEEP3_timestamp_marker")));
    }

    void testMaxEntriesTrimDropsOldest()
    {
        DebugConsoleWidget w;
        w.clear();

        constexpr int kOverLimit = 5010;
        for (int i = 0; i < kOverLimit; ++i) {
            w.appendEntry(makeEntry(QtInfoMsg,
                                    QStringLiteral("DEEP3_TRIM_%1").arg(i),
                                    QStringLiteral("deep3.trim")));
        }
        QApplication::processEvents();

        auto* text = textEdit(w);
        QVERIFY(text != nullptr);
        const QString plain = text->toPlainText();

        QVERIFY2(!plain.contains(QStringLiteral("DEEP3_TRIM_0")),
                 "Oldest cached entry should be trimmed after max-entry limit");
        QVERIFY2(plain.contains(QStringLiteral("DEEP3_TRIM_%1").arg(kOverLimit - 1)),
                 "Most recent entry should remain after trim");
    }

    void testConstructExposesFilterControls()
    {
        DebugConsoleWidget w;
        w.resize(640, 400);

        QVERIFY(textEdit(w) != nullptr);
        QVERIFY(levelFilter(w) != nullptr);
        QVERIFY(categoryFilter(w) != nullptr);

        bool hasClearBtn = false;
        for (auto* btn : w.findChildren<QPushButton*>()) {
            if (btn->text() == QStringLiteral("Clear")) {
                hasClearBtn = true;
                break;
            }
        }
        QVERIFY(hasClearBtn);
    }
};

QTEST_MAIN(TestDebugConsoleDeep3)

#include "test_debug_console_deep3.moc"
