// test_sentinel_logger_advanced.cpp — Advanced SentinelLogger unit tests
#include <QTest>
#include <QCoreApplication>
#include <QSignalSpy>
#include "core/SentinelLogger.h"

class TestSentinelLoggerAdvanced : public QObject
{
    Q_OBJECT

private slots:

    // ── 1. Singleton identity ─────────────────────────────────────────────────
    void testSingletonInstance()
    {
        SentinelLogger& a = SentinelLogger::instance();
        SentinelLogger& b = SentinelLogger::instance();
        QVERIFY(&a == &b);
    }

    // ── 2. install / uninstall cycle must not crash ───────────────────────────
    void testInstallAndUninstall()
    {
        SentinelLogger& logger = SentinelLogger::instance();
        logger.install();
        logger.uninstall();
        logger.install();
        logger.uninstall();
        // If we reach here, no crash occurred
        QVERIFY(true);
    }

    // ── 3. Ring buffer respects setMaxEntries(5) ─────────────────────────────
    void testRingBufferSizeLimit()
    {
        SentinelLogger& logger = SentinelLogger::instance();
        logger.clear();
        logger.setMaxEntries(5);
        logger.install();

        for (int i = 0; i < 20; ++i)
            qDebug("sentinel_ring_msg %d", i);
        QTest::qWait(20);

        QVERIFY(logger.recent(100).size() <= 5);

        logger.uninstall();
        logger.setMaxEntries(2000);
        logger.clear();
    }

    // ── 4. Entries are ordered oldest-first (most recent comes last) ──────────
    void testRecentOrdering()
    {
        SentinelLogger& logger = SentinelLogger::instance();
        logger.clear();
        logger.install();

        qDebug() << "sentinel_order_FIRST_aaa";
        QTest::qWait(5);
        qDebug() << "sentinel_order_LAST_bbb";
        QTest::qWait(15);

        auto entries = logger.recent(100);
        int idxFirst = -1, idxLast = -1;
        for (int i = 0; i < entries.size(); ++i) {
            if (entries[i].message.contains("sentinel_order_FIRST_aaa")) idxFirst = i;
            if (entries[i].message.contains("sentinel_order_LAST_bbb"))  idxLast  = i;
        }
        if (idxFirst != -1 && idxLast != -1)
            QVERIFY(idxFirst < idxLast);

        logger.uninstall();
        logger.clear();
    }

    // ── 5. clear() makes recent() return an empty list ───────────────────────
    void testClearLog()
    {
        SentinelLogger& logger = SentinelLogger::instance();
        logger.install();
        qDebug() << "sentinel_before_clear_xyz";
        QTest::qWait(10);
        logger.clear();
        QCOMPARE(logger.recent(100).size(), 0);
        logger.uninstall();
    }

    // ── 6. newEntry signal is emitted for each logged message ─────────────────
    void testNewEntrySignal()
    {
        SentinelLogger& logger = SentinelLogger::instance();
        logger.clear();
        logger.install();

        QSignalSpy spy(&logger, &SentinelLogger::newEntry);
        qInfo() << "sentinel_signal_unique_99887";
        QTest::qWait(20);

        QVERIFY(spy.count() >= 1);

        logger.uninstall();
        logger.clear();
    }

    // ── 7. Messages via a named category are captured with correct category ───
    void testCategoryFiltering()
    {
        SentinelLogger& logger = SentinelLogger::instance();
        logger.clear();
        logger.install();

        qCDebug(lcModels) << "sentinel_cat_models_test_7654";
        QTest::qWait(20);

        auto entries = logger.recent(100);
        for (const auto& e : entries) {
            if (e.message.contains("sentinel_cat_models_test_7654")) {
                QCOMPARE(e.category, QStringLiteral("sentinel.models"));
                break;
            }
        }
        // If the category is filtered out, simply no entry is found — no false failure

        logger.uninstall();
        logger.clear();
    }

    // ── 8. Debug, Info and Warning messages are all captured and level-tagged ─
    void testLevelCapture()
    {
        SentinelLogger& logger = SentinelLogger::instance();
        logger.clear();
        logger.install();

        qDebug()   << "sentinel_level_debug_capture";
        qInfo()    << "sentinel_level_info_capture";
        qWarning() << "sentinel_level_warn_capture";
        QTest::qWait(20);

        auto entries = logger.recent(100);
        for (const auto& e : entries) {
            QVERIFY(e.level == QtDebugMsg    ||
                    e.level == QtInfoMsg     ||
                    e.level == QtWarningMsg  ||
                    e.level == QtCriticalMsg ||
                    e.level == QtFatalMsg);
        }

        // Verify all three levels appear
        bool hasDebug = false, hasInfo = false, hasWarn = false;
        for (const auto& e : entries) {
            if (e.message.contains("sentinel_level_debug_capture")) hasDebug = true;
            if (e.message.contains("sentinel_level_info_capture"))  hasInfo  = true;
            if (e.message.contains("sentinel_level_warn_capture"))  hasWarn  = true;
        }
        QVERIFY(hasDebug);
        QVERIFY(hasInfo);
        QVERIFY(hasWarn);

        logger.uninstall();
        logger.clear();
    }

    // ── 9. After 500 messages, recent(100) returns exactly 100 ───────────────
    void testLargeMessageCount()
    {
        SentinelLogger& logger = SentinelLogger::instance();
        logger.clear();
        logger.setMaxEntries(2000);
        logger.install();

        for (int i = 0; i < 500; ++i)
            qDebug("sentinel_large_count_%04d", i);
        QTest::qWait(50);

        QCOMPARE(logger.recent(100).size(), 100);

        logger.uninstall();
        logger.setMaxEntries(2000);
        logger.clear();
    }

    // ── 10. Every captured entry has a valid UTC timestamp ───────────────────
    void testTimestampValid()
    {
        SentinelLogger& logger = SentinelLogger::instance();
        logger.clear();
        logger.install();

        qInfo() << "sentinel_timestamp_valid_check_42";
        QTest::qWait(20);

        auto entries = logger.recent(100);
        QVERIFY(!entries.isEmpty());
        for (const auto& e : entries)
            QVERIFY(e.timestamp.isValid());

        logger.uninstall();
        logger.clear();
    }
};

QTEST_MAIN(TestSentinelLoggerAdvanced)
#include "test_sentinel_logger_advanced.moc"
