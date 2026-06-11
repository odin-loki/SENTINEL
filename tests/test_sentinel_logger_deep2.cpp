#include <QtTest>
#include <QCoreApplication>
#include "core/SentinelLogger.h"

// SentinelLogger is a singleton. Each test installs/uninstalls carefully to avoid
// leaving the Qt message handler in a broken state.

class TestSentinelLoggerDeep2 : public QObject {
    Q_OBJECT

private slots:

    void testCountIncreasesAfterLogging()
    {
        SentinelLogger& logger = SentinelLogger::instance();
        logger.clear();
        logger.install();

        const int before = logger.count();
        qWarning("DEEP2_count_test_marker");
        const int after = logger.count();

        logger.uninstall();

        QVERIFY2(after > before,
                 qPrintable(QStringLiteral("count() should increase: before=%1 after=%2")
                                .arg(before).arg(after)));
    }

    void testRecentReturnsAtMostN()
    {
        SentinelLogger& logger = SentinelLogger::instance();
        logger.clear();
        logger.install();

        for (int i = 0; i < 20; ++i)
            qWarning("DEEP2_recent_limit_%d", i);

        const auto limited = logger.recent(5);
        logger.uninstall();

        QVERIFY2(limited.size() <= 5,
                 qPrintable(QStringLiteral("recent(5) must return at most 5, got %1")
                                .arg(limited.size())));
    }

    void testFilterByLevel_excludesLowerSeverity()
    {
        SentinelLogger& logger = SentinelLogger::instance();
        logger.clear();
        logger.install();

        qDebug("DEEP2_filter_debug_msg");
        qWarning("DEEP2_filter_warning_msg");
        qCritical("DEEP2_filter_critical_msg");

        const auto filtered = logger.filterByLevel(QtWarningMsg);
        logger.uninstall();

        // Debug entries (level=0) must not appear; Warning(1) and Critical(2) must
        for (const auto& e : filtered) {
            QVERIFY2(e.level >= QtWarningMsg,
                     qPrintable(QStringLiteral("filterByLevel(Warning) returned entry with level %1")
                                    .arg(static_cast<int>(e.level))));
        }

        // At least the warning and critical messages we logged must be present
        const bool hasWarning = std::any_of(filtered.begin(), filtered.end(),
            [](const LogEntry& e){ return e.message.contains("DEEP2_filter_warning_msg"); });
        const bool hasCritical = std::any_of(filtered.begin(), filtered.end(),
            [](const LogEntry& e){ return e.message.contains("DEEP2_filter_critical_msg"); });

        QVERIFY2(hasWarning,  "filterByLevel(Warning) should include warning entries");
        QVERIFY2(hasCritical, "filterByLevel(Warning) should include critical entries");

        const bool hasDebug = std::any_of(filtered.begin(), filtered.end(),
            [](const LogEntry& e){ return e.message.contains("DEEP2_filter_debug_msg"); });
        QVERIFY2(!hasDebug, "filterByLevel(Warning) must not include debug entries");
    }

    void testFilterByCategory_returnsOnlyMatchingEntries()
    {
        SentinelLogger& logger = SentinelLogger::instance();
        logger.clear();
        logger.install();

        // Use a sentinel logging category (defined in SentinelLogger.cpp)
        qCWarning(lcInference, "DEEP2_inference_category_marker");
        qWarning("DEEP2_default_category_marker");

        const auto inferenceEntries = logger.filterByCategory(QStringLiteral("inference"));
        logger.uninstall();

        for (const auto& e : inferenceEntries) {
            QVERIFY2(e.category.contains(QStringLiteral("inference"), Qt::CaseInsensitive),
                     qPrintable(QStringLiteral("filterByCategory('inference') returned entry "
                                               "with category '%1'").arg(e.category)));
        }
    }

    void testRingBuffer_oldestEntryRemoved()
    {
        SentinelLogger& logger = SentinelLogger::instance();
        logger.clear();
        logger.setMaxEntries(5);
        logger.install();

        // Log the "oldest" message first, then 5 more to push it out
        qWarning("DEEP2_ring_OLDEST_UNIQUE_MARKER");
        for (int i = 0; i < 5; ++i)
            qWarning("DEEP2_ring_newer_%d", i);

        const auto entries = logger.recent(1000);
        logger.uninstall();
        logger.setMaxEntries(2000);

        QVERIFY2(entries.size() <= 5,
                 qPrintable(QStringLiteral("Ring buffer must hold at most 5, got %1")
                                .arg(entries.size())));

        const bool oldestPresent = std::any_of(entries.begin(), entries.end(),
            [](const LogEntry& e){
                return e.message.contains(QStringLiteral("DEEP2_ring_OLDEST_UNIQUE_MARKER"));
            });
        QVERIFY2(!oldestPresent, "Oldest entry should have been evicted from ring buffer");
    }

    void testClear_makesCountZero()
    {
        SentinelLogger& logger = SentinelLogger::instance();
        logger.install();

        qWarning("DEEP2_before_clear");

        logger.uninstall();
        logger.clear();

        QCOMPARE(logger.count(), 0);
    }
};

QTEST_GUILESS_MAIN(TestSentinelLoggerDeep2)
#include "test_sentinel_logger_deep2.moc"
