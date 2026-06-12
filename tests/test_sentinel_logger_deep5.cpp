// test_sentinel_logger_deep5.cpp — Iteration 21 deep audit: SentinelLogger
// signal emission, recent()/filter edge cases, install idempotency, UTC timestamps.
#include <QtTest>
#include <QSignalSpy>
#include <algorithm>
#include "core/SentinelLogger.h"

class TestSentinelLoggerDeep5 : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void testNewEntrySignalEmittedOnLog();
    void testDoubleInstallIsIdempotent();
    void testRecentZeroAndNegativeReturnEmpty();
    void testFilterByLevelWarningIncludedAtInfoThreshold();
    void testFilterByCategoryCaseInsensitivePartialMatch();
    void testLogEntryTimestampIsUtcValid();
    void testMessagesNotCapturedWhenUninstalled();
    void testRecentReturnsLastNInChronologicalOrder();
};

void TestSentinelLoggerDeep5::init()
{
    SentinelLogger& logger = SentinelLogger::instance();
    logger.uninstall();
    logger.clear();
    logger.setMaxEntries(2000);
}

void TestSentinelLoggerDeep5::cleanup()
{
    SentinelLogger& logger = SentinelLogger::instance();
    logger.uninstall();
    logger.setMaxEntries(2000);
}

void TestSentinelLoggerDeep5::testNewEntrySignalEmittedOnLog()
{
    SentinelLogger& logger = SentinelLogger::instance();
    logger.clear();
    logger.install();

    QSignalSpy spy(&logger, &SentinelLogger::newEntry);
    qCWarning(lcIngest, "DEEP5_signal_marker");

    QCOMPARE(spy.count(), 1);

    const auto entries = logger.recent(1);
    QCOMPARE(entries.size(), 1);
    QVERIFY(entries[0].message.contains(QStringLiteral("DEEP5_signal_marker")));
    QVERIFY(entries[0].category.contains(QStringLiteral("ingest"), Qt::CaseInsensitive));

    logger.uninstall();
}

void TestSentinelLoggerDeep5::testDoubleInstallIsIdempotent()
{
    SentinelLogger& logger = SentinelLogger::instance();
    logger.clear();
    logger.install();
    logger.install();

    qWarning("DEEP5_double_install_marker");
    QCOMPARE(logger.count(), 1);

    logger.uninstall();
    qWarning("DEEP5_after_uninstall_marker");
    QCOMPARE(logger.count(), 1);
}

void TestSentinelLoggerDeep5::testRecentZeroAndNegativeReturnEmpty()
{
    SentinelLogger& logger = SentinelLogger::instance();
    logger.clear();
    logger.install();

    qWarning("DEEP5_recent_fill_marker");
    QVERIFY(logger.count() > 0);

    QVERIFY(logger.recent(0).isEmpty());
    QVERIFY(logger.recent(-3).isEmpty());

    logger.uninstall();
}

void TestSentinelLoggerDeep5::testFilterByLevelWarningIncludedAtInfoThreshold()
{
    // msgSeverity ranks Info=1, Warning=2 — filterByLevel(Info) must include warnings.
    SentinelLogger& logger = SentinelLogger::instance();
    logger.clear();
    logger.install();

    qDebug("DEEP5_sev_debug_marker");
    qInfo("DEEP5_sev_info_marker");
    qWarning("DEEP5_sev_warning_marker");
    qCritical("DEEP5_sev_critical_marker");

    const auto infoPlus = logger.filterByLevel(QtInfoMsg);
    logger.uninstall();

    const auto contains = [&infoPlus](const char* needle) {
        return std::any_of(infoPlus.begin(), infoPlus.end(),
            [needle](const LogEntry& e) {
                return e.message.contains(QString::fromLatin1(needle));
            });
    };

    QVERIFY2(!contains("DEEP5_sev_debug_marker"), "Debug must be below Info threshold");
    QVERIFY(contains("DEEP5_sev_info_marker"));
    QVERIFY2(contains("DEEP5_sev_warning_marker"),
             "Warning must pass filterByLevel(Info) — uses explicit severity ranks");
    QVERIFY(contains("DEEP5_sev_critical_marker"));
}

void TestSentinelLoggerDeep5::testFilterByCategoryCaseInsensitivePartialMatch()
{
    SentinelLogger& logger = SentinelLogger::instance();
    logger.clear();
    logger.install();

    qCWarning(lcDatabase, "DEEP5_cat_db_marker");
    qCWarning(lcNlp, "DEEP5_cat_nlp_marker");

    const auto dbUpper = logger.filterByCategory(QStringLiteral("DATABASE"));
    const auto nlpPartial = logger.filterByCategory(QStringLiteral("nlp"));
    logger.uninstall();

    QCOMPARE(dbUpper.size(), 1);
    QVERIFY(dbUpper[0].message.contains(QStringLiteral("DEEP5_cat_db_marker")));

    QCOMPARE(nlpPartial.size(), 1);
    QVERIFY(nlpPartial[0].message.contains(QStringLiteral("DEEP5_cat_nlp_marker")));
}

void TestSentinelLoggerDeep5::testLogEntryTimestampIsUtcValid()
{
    SentinelLogger& logger = SentinelLogger::instance();
    logger.clear();
    logger.install();

    const QDateTime before = QDateTime::currentDateTimeUtc();
    qWarning("DEEP5_ts_marker");
    const QDateTime after = QDateTime::currentDateTimeUtc();

    const auto entries = logger.recent(1);
    logger.uninstall();

    QCOMPARE(entries.size(), 1);
    QVERIFY2(entries[0].timestamp.isValid(), "Captured timestamp must be valid");
    QVERIFY2(entries[0].timestamp.timeSpec() == Qt::UTC,
             "SentinelLogger stores UTC timestamps");
    QVERIFY(entries[0].timestamp >= before);
    QVERIFY(entries[0].timestamp <= after);
}

void TestSentinelLoggerDeep5::testMessagesNotCapturedWhenUninstalled()
{
    SentinelLogger& logger = SentinelLogger::instance();
    logger.uninstall();
    logger.clear();

    qWarning("DEEP5_uninstalled_marker");
    QCOMPARE(logger.count(), 0);
    QVERIFY(logger.recent(10).isEmpty());
}

void TestSentinelLoggerDeep5::testRecentReturnsLastNInChronologicalOrder()
{
    SentinelLogger& logger = SentinelLogger::instance();
    logger.clear();
    logger.install();

    for (int i = 0; i < 5; ++i)
        qCWarning(lcModels, "DEEP5_order_%d", i);

    const auto slice = logger.recent(3);
    logger.uninstall();

    QCOMPARE(slice.size(), 3);
    QCOMPARE(slice[0].message, QStringLiteral("DEEP5_order_2"));
    QCOMPARE(slice[1].message, QStringLiteral("DEEP5_order_3"));
    QCOMPARE(slice[2].message, QStringLiteral("DEEP5_order_4"));
}

QTEST_GUILESS_MAIN(TestSentinelLoggerDeep5)
#include "test_sentinel_logger_deep5.moc"
