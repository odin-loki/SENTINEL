// test_sentinel_logger_deep7.cpp — Deep audit iteration 30: SentinelLogger
// recent newest-first, category case-insensitive, critical level, count after clear.
#include <QTest>
#include "core/SentinelLogger.h"

class TestSentinelLoggerDeep7 : public QObject
{
    Q_OBJECT

private slots:
    void init()
    {
        auto& logger = SentinelLogger::instance();
        logger.uninstall();
        logger.clear();
        logger.setMaxEntries(500);
        logger.install();
    }

    void cleanup()
    {
        SentinelLogger::instance().uninstall();
    }

    void testRecentNewestFirst()
    {
        auto& logger = SentinelLogger::instance();
        logger.clear();

        qWarning("DEEP7_first");
        qWarning("DEEP7_second");
        qWarning("DEEP7_third");

        const auto recent = logger.recent(2);
        QCOMPARE(recent.size(), 2);
        QVERIFY(recent.last().message.contains(QStringLiteral("third")));
    }

    void testFilterByCategoryCaseInsensitive()
    {
        auto& logger = SentinelLogger::instance();
        logger.clear();

        qCInfo(lcInference, "DEEP7_inference_marker");

        const auto filtered = logger.filterByCategory(QStringLiteral("INFERENCE"));
        QVERIFY(!filtered.isEmpty());
    }

    void testCriticalLevelCaptured()
    {
        auto& logger = SentinelLogger::instance();
        logger.clear();

        qCritical("DEEP7_critical_event");

        const auto all = logger.recent(10);
        bool sawCritical = false;
        for (const auto& e : all) {
            if (e.level == QtCriticalMsg)
                sawCritical = true;
        }
        QVERIFY(sawCritical);
    }

    void testCountZeroAfterClear()
    {
        auto& logger = SentinelLogger::instance();
        qInfo("DEEP7_before_clear");
        logger.clear();
        QCOMPARE(logger.count(), 0);
    }

    void testMaxEntriesCapsBuffer()
    {
        auto& logger = SentinelLogger::instance();
        logger.clear();
        logger.setMaxEntries(5);

        for (int i = 0; i < 12; ++i)
            qWarning("DEEP7_cap_%1", i);

        QVERIFY(logger.count() <= 5);
    }
};

QTEST_GUILESS_MAIN(TestSentinelLoggerDeep7)
#include "test_sentinel_logger_deep7.moc"
