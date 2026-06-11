// test_sentinel_logger_deep3.cpp — Iteration 13: ring buffer overflow,
// filter by category, and recent entries.
#include <QtTest>
#include <algorithm>
#include "core/SentinelLogger.h"

class TestSentinelLoggerDeep3 : public QObject
{
    Q_OBJECT

private slots:

    void init()
    {
        SentinelLogger& logger = SentinelLogger::instance();
        logger.uninstall();
        logger.clear();
        logger.setMaxEntries(2000);
    }

    void cleanup()
    {
        SentinelLogger::instance().uninstall();
        SentinelLogger::instance().setMaxEntries(2000);
    }

    void testRingBufferOverflow_evictsOldest()
    {
        SentinelLogger& logger = SentinelLogger::instance();
        logger.clear();
        logger.setMaxEntries(3);
        logger.install();

        qWarning("DEEP3_ring_FIRST_MARKER");
        qWarning("DEEP3_ring_SECOND_MARKER");
        qWarning("DEEP3_ring_THIRD_MARKER");
        qWarning("DEEP3_ring_FOURTH_MARKER");

        const auto entries = logger.recent(100);
        logger.uninstall();

        QCOMPARE(entries.size(), 3);

        const bool firstPresent = std::any_of(entries.begin(), entries.end(),
            [](const LogEntry& e) {
                return e.message.contains(QStringLiteral("DEEP3_ring_FIRST_MARKER"));
            });
        QVERIFY2(!firstPresent, "Oldest entry must be evicted when ring buffer overflows");

        const bool fourthPresent = std::any_of(entries.begin(), entries.end(),
            [](const LogEntry& e) {
                return e.message.contains(QStringLiteral("DEEP3_ring_FOURTH_MARKER"));
            });
        QVERIFY2(fourthPresent, "Newest entry must remain after ring buffer overflow");
    }

    void testFilterByCategory_returnsOnlyMatching()
    {
        SentinelLogger& logger = SentinelLogger::instance();
        logger.clear();
        logger.install();

        qCWarning(lcDatabase,  "DEEP3_db_category_marker");
        qCWarning(lcInference, "DEEP3_inference_category_marker");
        qWarning("DEEP3_default_category_marker");

        const auto dbEntries = logger.filterByCategory(QStringLiteral("database"));
        const auto infEntries = logger.filterByCategory(QStringLiteral("inference"));
        logger.uninstall();

        QVERIFY2(!dbEntries.isEmpty(), "filterByCategory('database') should match logged entries");
        for (const auto& e : dbEntries) {
            QVERIFY2(e.category.contains(QStringLiteral("database"), Qt::CaseInsensitive),
                     qPrintable(QStringLiteral("Unexpected category: %1").arg(e.category)));
        }

        QVERIFY2(!infEntries.isEmpty(), "filterByCategory('inference') should match logged entries");
        for (const auto& e : infEntries) {
            QVERIFY2(e.category.contains(QStringLiteral("inference"), Qt::CaseInsensitive),
                     qPrintable(QStringLiteral("Unexpected category: %1").arg(e.category)));
        }
    }

    void testRecentReturnsLastNEntries()
    {
        SentinelLogger& logger = SentinelLogger::instance();
        logger.clear();
        logger.install();

        for (int i = 0; i < 10; ++i)
            qWarning("DEEP3_recent_seq_%d", i);

        const auto last3 = logger.recent(3);
        const auto all   = logger.recent(100);
        logger.uninstall();

        QCOMPARE(last3.size(), 3);
        QCOMPARE(all.size(), 10);

        QVERIFY2(last3.last().message.contains(QStringLiteral("DEEP3_recent_seq_9")),
                 "recent(n) must return the newest entries");
        QVERIFY2(last3.first().message.contains(QStringLiteral("DEEP3_recent_seq_7")),
                 "recent(3) must return the last three entries in order");
    }

    void testRecentZeroReturnsEmpty()
    {
        SentinelLogger& logger = SentinelLogger::instance();
        logger.clear();
        logger.install();
        qWarning("DEEP3_recent_zero_marker");
        logger.uninstall();

        QCOMPARE(logger.recent(0).size(), 0);
    }

    void testDoubleInstallIsIdempotent()
    {
        SentinelLogger& logger = SentinelLogger::instance();
        logger.clear();
        logger.install();
        logger.install();  // must not break handler chain

        qWarning("DEEP3_double_install_marker");
        const int count = logger.count();
        logger.uninstall();
        logger.uninstall();  // second uninstall must be safe

        QVERIFY2(count > 0, "Double install should still capture messages");
    }
};

QTEST_GUILESS_MAIN(TestSentinelLoggerDeep3)
#include "test_sentinel_logger_deep3.moc"
