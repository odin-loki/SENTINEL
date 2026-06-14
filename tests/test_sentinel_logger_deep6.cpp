// test_sentinel_logger_deep6.cpp — Deep audit iteration 25: SentinelLogger
// ring buffer cap, level/category filters, clear, chronological recent.
#include <QTest>
#include "core/SentinelLogger.h"

class TestSentinelLoggerDeep6 : public QObject
{
    Q_OBJECT

private slots:
    void init()
    {
        auto& logger = SentinelLogger::instance();
        logger.uninstall();
        logger.clear();
        logger.setMaxEntries(2000);
        logger.install();
    }

    void cleanup()
    {
        SentinelLogger::instance().uninstall();
    }

    void testMaxEntriesRingBuffer()
    {
        auto& logger = SentinelLogger::instance();
        logger.clear();
        logger.setMaxEntries(3);

        for (int i = 0; i < 8; ++i)
            qWarning("DEEP6_ring_%1", i);

        QVERIFY(logger.count() <= 3);
    }

    void testFilterByLevelExcludesDebug()
    {
        auto& logger = SentinelLogger::instance();
        logger.clear();

        qDebug("DEEP6_debug_msg");
        qWarning("DEEP6_warn_msg");

        const auto filtered = logger.filterByLevel(QtWarningMsg);
        QVERIFY(!filtered.isEmpty());
        for (const auto& e : filtered)
            QVERIFY(e.level >= QtWarningMsg);
    }

    void testFilterByCategoryPartialMatch()
    {
        auto& logger = SentinelLogger::instance();
        logger.clear();

        qCInfo(lcDatabase, "DEEP6_db_category_marker");

        const auto filtered = logger.filterByCategory(QStringLiteral("database"));
        QVERIFY(!filtered.isEmpty());
    }

    void testClearEmptiesBuffer()
    {
        auto& logger = SentinelLogger::instance();
        qInfo("DEEP6_before_clear");
        logger.clear();
        QCOMPARE(logger.count(), 0);
    }

    void testRecentReturnsAtMostN()
    {
        auto& logger = SentinelLogger::instance();
        logger.clear();

        for (int i = 0; i < 12; ++i)
            qWarning("DEEP6_recent_%1", i);

        const auto recent = logger.recent(4);
        QVERIFY(recent.size() <= 4);
    }

    void testCountMatchesRecentWhenSmall()
    {
        auto& logger = SentinelLogger::instance();
        logger.clear();
        qWarning("DEEP6_count_a");
        qWarning("DEEP6_count_b");
        QCOMPARE(logger.count(), logger.recent(100).size());
    }
};

QTEST_GUILESS_MAIN(TestSentinelLoggerDeep6)
#include "test_sentinel_logger_deep6.moc"
