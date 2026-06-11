// test_sentinel_logger_deep4.cpp — Iteration 16 deep audit: SentinelLogger
// categories, log levels, and stress paths that must not crash.
#include <QtTest>
#include <algorithm>
#include "core/SentinelLogger.h"

class TestSentinelLoggerDeep4 : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void testAllDeclaredCategoriesCaptured();
    void testFilterByLevelInfoExcludesDebug();
    void testFilterByLevelCriticalOnly();
    void testUninstallWithoutInstallNoCrash();
    void testRapidLoggingNoCrash();
    void testClearWhileInstalledNoCrash();
    void testSetMaxEntriesShrinksBuffer();
    void testDefaultCategoryOnPlainQWarning();
};

void TestSentinelLoggerDeep4::init()
{
    SentinelLogger& logger = SentinelLogger::instance();
    logger.uninstall();
    logger.clear();
    logger.setMaxEntries(2000);
}

void TestSentinelLoggerDeep4::cleanup()
{
    SentinelLogger& logger = SentinelLogger::instance();
    logger.uninstall();
    logger.setMaxEntries(2000);
}

void TestSentinelLoggerDeep4::testAllDeclaredCategoriesCaptured()
{
    SentinelLogger& logger = SentinelLogger::instance();
    logger.clear();
    logger.install();

    qCWarning(lcIngest,    "DEEP4_cat_ingest_marker");
    qCWarning(lcNlp,       "DEEP4_cat_nlp_marker");
    qCWarning(lcModels,    "DEEP4_cat_models_marker");
    qCWarning(lcInference, "DEEP4_cat_inference_marker");
    qCWarning(lcDatabase,  "DEEP4_cat_database_marker");
    qCWarning(lcUI,        "DEEP4_cat_ui_marker");

    const struct { const char* needle; const char* category; } cases[] = {
        { "DEEP4_cat_ingest_marker",    "ingest" },
        { "DEEP4_cat_nlp_marker",       "nlp" },
        { "DEEP4_cat_models_marker",    "models" },
        { "DEEP4_cat_inference_marker", "inference" },
        { "DEEP4_cat_database_marker",  "database" },
        { "DEEP4_cat_ui_marker",        "ui" },
    };

    for (const auto& c : cases) {
        const auto filtered = logger.filterByCategory(QString::fromLatin1(c.category));
        const bool found = std::any_of(filtered.begin(), filtered.end(),
            [c](const LogEntry& e) {
                return e.message.contains(QString::fromLatin1(c.needle));
            });
        QVERIFY2(found, c.needle);
    }

    logger.uninstall();
}

void TestSentinelLoggerDeep4::testFilterByLevelInfoExcludesDebug()
{
    // Note: QtMsgType enum order is Debug=0, Warning=1, Critical=2, Fatal=3, Info=4.
    // filterByLevel(Info) therefore also excludes Warning/Critical (see bug report).
    SentinelLogger& logger = SentinelLogger::instance();
    logger.clear();
    logger.install();

    qDebug("DEEP4_level_debug_marker");
    qInfo("DEEP4_level_info_marker");
    qWarning("DEEP4_level_warning_marker");

    const auto infoAndAbove = logger.filterByLevel(QtInfoMsg);
    logger.uninstall();

    const bool hasDebug = std::any_of(infoAndAbove.begin(), infoAndAbove.end(),
        [](const LogEntry& e) {
            return e.message.contains(QStringLiteral("DEEP4_level_debug_marker"));
        });
    QVERIFY2(!hasDebug, "filterByLevel(Info) must exclude debug entries");

    const bool hasInfo = std::any_of(infoAndAbove.begin(), infoAndAbove.end(),
        [](const LogEntry& e) {
            return e.message.contains(QStringLiteral("DEEP4_level_info_marker"));
        });
    QVERIFY(hasInfo);
}

void TestSentinelLoggerDeep4::testFilterByLevelCriticalOnly()
{
    SentinelLogger& logger = SentinelLogger::instance();
    logger.clear();
    logger.install();

    qWarning("DEEP4_crit_warning_marker");
    qCritical("DEEP4_crit_critical_marker");

    const auto criticalOnly = logger.filterByLevel(QtCriticalMsg);
    logger.uninstall();

    QCOMPARE(criticalOnly.size(), 1);
    QCOMPARE(criticalOnly[0].level, QtCriticalMsg);
    QVERIFY(criticalOnly[0].message.contains(QStringLiteral("DEEP4_crit_critical_marker")));
}

void TestSentinelLoggerDeep4::testUninstallWithoutInstallNoCrash()
{
    SentinelLogger& logger = SentinelLogger::instance();
    logger.uninstall();
    logger.uninstall();
    qWarning("DEEP4_uninstall_safe_marker");
    QCOMPARE(logger.count(), 0);
}

void TestSentinelLoggerDeep4::testRapidLoggingNoCrash()
{
    SentinelLogger& logger = SentinelLogger::instance();
    logger.clear();
    logger.setMaxEntries(50);
    logger.install();

    for (int i = 0; i < 500; ++i)
        qCWarning(lcDatabase, "DEEP4_rapid_%d", i);

    const int captured = logger.count();
    logger.uninstall();
    logger.setMaxEntries(2000);

    QVERIFY2(captured <= 50,
             qPrintable(QStringLiteral("Ring buffer must cap at 50, got %1").arg(captured)));
    QVERIFY2(captured > 0, "Rapid logging must capture at least one entry");
}

void TestSentinelLoggerDeep4::testClearWhileInstalledNoCrash()
{
    SentinelLogger& logger = SentinelLogger::instance();
    logger.clear();
    logger.install();

    qWarning("DEEP4_before_clear_marker");
    QVERIFY(logger.count() > 0);

    logger.clear();
    QCOMPARE(logger.count(), 0);
    QVERIFY(logger.recent(10).isEmpty());

    qWarning("DEEP4_after_clear_marker");
    QVERIFY(logger.count() > 0);

    logger.uninstall();
}

void TestSentinelLoggerDeep4::testSetMaxEntriesShrinksBuffer()
{
    SentinelLogger& logger = SentinelLogger::instance();
    logger.clear();
    logger.setMaxEntries(100);
    logger.install();

    for (int i = 0; i < 80; ++i)
        qWarning("DEEP4_shrink_%d", i);
    QCOMPARE(logger.count(), 80);

    logger.setMaxEntries(10);
    QCOMPARE(logger.count(), 10);

    const auto entries = logger.recent(100);
    logger.uninstall();
    logger.setMaxEntries(2000);

    const bool hasEarly = std::any_of(entries.begin(), entries.end(),
        [](const LogEntry& e) {
            return e.message.contains(QStringLiteral("DEEP4_shrink_0"));
        });
    QVERIFY2(!hasEarly, "setMaxEntries must evict oldest entries");

    const bool hasLate = std::any_of(entries.begin(), entries.end(),
        [](const LogEntry& e) {
            return e.message.contains(QStringLiteral("DEEP4_shrink_79"));
        });
    QVERIFY2(hasLate, "setMaxEntries must retain newest entries");
}

void TestSentinelLoggerDeep4::testDefaultCategoryOnPlainQWarning()
{
    SentinelLogger& logger = SentinelLogger::instance();
    logger.clear();
    logger.install();

    qWarning("DEEP4_default_cat_marker");

    const auto entries = logger.recent(5);
    logger.uninstall();

    QVERIFY(!entries.isEmpty());
    const LogEntry& last = entries.last();
    QVERIFY2(last.category.contains(QStringLiteral("default"), Qt::CaseInsensitive)
             || last.category.isEmpty(),
             qPrintable(QStringLiteral("Plain qWarning category: %1").arg(last.category)));
    QVERIFY(last.message.contains(QStringLiteral("DEEP4_default_cat_marker")));
}

QTEST_GUILESS_MAIN(TestSentinelLoggerDeep4)
#include "test_sentinel_logger_deep4.moc"
