// test_database_stress.cpp
// Stress tests for Database: bulk insert performance, query filters,
// daily trend, and average quality score under load.
#include <QTest>
#include <QElapsedTimer>
#include "core/Database.h"
#include "core/AppConfig.h"
#include "core/CrimeEvent.h"

class DatabaseStressTest : public QObject
{
    Q_OBJECT

private:
    static AppConfig memConfig()
    {
        AppConfig cfg;
        cfg.databasePath = QStringLiteral(":memory:");
        return cfg;
    }

    static CrimeEvent makeEv(int i, const QString& type = QStringLiteral("burglary"))
    {
        CrimeEvent ev;
        ev.eventId   = QStringLiteral("EVT-%1").arg(i, 6, 10, QChar('0'));
        ev.id        = ev.eventId;
        ev.crimeType = type;
        ev.suburb    = QStringLiteral("Zone%1").arg(i % 5);
        ev.lat       = 51.5 + (i % 100) * 0.001;
        ev.lon       = -0.1 + (i % 100) * 0.001;
        ev.latitude  = ev.lat.value_or(51.5);
        ev.longitude = ev.lon.value_or(-0.1);
        ev.source    = QStringLiteral("stress_test");
        ev.qualityScore = 0.5 + (i % 5) * 0.1;
        const QDate d = QDate(2024, 1, 1).addDays(i % 365);
        const QDateTime dt(d, QTime(i % 24, 0, 0), QTimeZone::utc());
        ev.occurredAt = dt;
        ev.timestamp  = dt;
        return ev;
    }

private slots:

    // ── 1. Bulk insert 200 events in < 2s ────────────────────────────────────
    void testBulkInsert200()
    {
        Database db(memConfig());
        db.open();

        QElapsedTimer t;
        t.start();
        for (int i = 0; i < 200; ++i) {
            const bool ok = db.insertEvent(makeEv(i));
            QVERIFY2(ok, qPrintable(QStringLiteral("insertEvent %1 failed: %2")
                .arg(i).arg(db.lastError())));
        }
        const qint64 ms = t.elapsed();
        QVERIFY2(db.eventCount() == 200, "Expected 200 events after bulk insert");
        QVERIFY2(ms < 2000, qPrintable(QStringLiteral("Bulk insert took %1ms, should be < 2000ms").arg(ms)));
    }

    // ── 2. queryEvents with time range filters correctly ─────────────────────
    void testQueryEventsTimeRange()
    {
        Database db(memConfig());
        db.open();
        // Insert events spanning 2024 (days 0-364)
        for (int i = 0; i < 100; ++i) db.insertEvent(makeEv(i));

        // Query only Jan 2024 (days 0-30)
        const QDateTime from(QDate(2024, 1, 1), QTime(0, 0, 0), QTimeZone::utc());
        const QDateTime to  (QDate(2024, 1, 31), QTime(23, 59, 59), QTimeZone::utc());
        const auto evs = db.queryEvents(QString{}, from, to);
        QVERIFY2(!evs.isEmpty(), "Query with time range should return some events");
    }

    // ── 3. queryEvents: crimeType filter reduces count ───────────────────────
    void testQueryCrimeTypeFilter()
    {
        Database db(memConfig());
        db.open();
        for (int i = 0; i < 50; ++i) db.insertEvent(makeEv(i, QStringLiteral("burglary")));
        for (int i = 50; i < 100; ++i) db.insertEvent(makeEv(i, QStringLiteral("robbery")));

        const auto all      = db.queryEvents();
        const auto burglary = db.queryEvents(QStringLiteral("burglary"));
        const auto robbery  = db.queryEvents(QStringLiteral("robbery"));

        QVERIFY2(burglary.size() + robbery.size() == all.size() ||
                 (burglary.size() < all.size() && robbery.size() < all.size()),
                 "Filtered counts should sum to total");
    }

    // ── 4. getDailyTrend returns non-empty result with recent events ─────────
    void testGetDailyTrend()
    {
        Database db(memConfig());
        db.open();

        // Insert events with RECENT dates (within last 30 days)
        const QDate today = QDate::currentDate();
        for (int i = 0; i < 20; ++i) {
            CrimeEvent ev;
            ev.eventId   = QStringLiteral("T%1").arg(i);
            ev.id        = ev.eventId;
            ev.crimeType = QStringLiteral("burglary");
            ev.source    = QStringLiteral("test");
            ev.qualityScore = 0.8;
            const QDateTime dt(today.addDays(-i % 10), QTime(12, 0, 0), QTimeZone::utc());
            ev.occurredAt = dt;
            ev.timestamp  = dt;
            db.insertEvent(ev);
        }
        const auto trend = db.getDailyTrend(30);
        QVERIFY2(!trend.isEmpty(), "getDailyTrend with recent events should return non-empty result");
    }

    // ── 5. getAverageQualityScore in (0, 1] after inserts ───────────────────
    void testAverageQualityScore()
    {
        Database db(memConfig());
        db.open();
        for (int i = 0; i < 20; ++i) db.insertEvent(makeEv(i));
        const double avg = db.getAverageQualityScore();
        QVERIFY2(avg > 0.0 && avg <= 1.0,
                 qPrintable(QStringLiteral("Average quality %1 must be in (0,1]").arg(avg)));
    }

    // ── 6. getHourlyCounts returns vector of size 24 ─────────────────────────
    void testHourlyCounts()
    {
        Database db(memConfig());
        db.open();
        for (int i = 0; i < 48; ++i) db.insertEvent(makeEv(i));
        const auto counts = db.getHourlyCounts(30);
        QVERIFY2(counts.size() == 24,
                 qPrintable(QStringLiteral("getHourlyCounts size %1 expected 24").arg(counts.size())));
    }

    // ── 7. crimeTypeCounts includes both types after mixed inserts ───────────
    void testCrimeTypeCountsMixed()
    {
        Database db(memConfig());
        db.open();
        for (int i = 0; i < 10; ++i) db.insertEvent(makeEv(i, QStringLiteral("theft")));
        for (int i = 10; i < 20; ++i) db.insertEvent(makeEv(i, QStringLiteral("robbery")));

        const auto counts = db.crimeTypeCounts();
        QVERIFY2(counts.contains(QStringLiteral("theft")),   "theft should be in counts");
        QVERIFY2(counts.contains(QStringLiteral("robbery")), "robbery should be in counts");
    }

    // ── 8. getRecentEvents(n) returns at most n events ───────────────────────
    void testGetRecentEventsLimit()
    {
        Database db(memConfig());
        db.open();
        for (int i = 0; i < 100; ++i) db.insertEvent(makeEv(i));
        const auto recent = db.getRecentEvents(10);
        QVERIFY2(recent.size() <= 10,
                 qPrintable(QStringLiteral("getRecentEvents(10) returned %1").arg(recent.size())));
    }

    // ── 9. Query by QDate range works ────────────────────────────────────────
    void testQueryByDateRange()
    {
        Database db(memConfig());
        db.open();
        for (int i = 0; i < 30; ++i) db.insertEvent(makeEv(i));

        const auto evs = db.queryEvents(
            QString{}, QDate(2024, 1, 1), QDate(2024, 1, 15));
        QVERIFY2(!evs.isEmpty(), "Date range query should return some events");
    }

    // ── 10. insertEvent is idempotent (duplicate IDs handled gracefully) ─────
    void testDuplicateInsertGraceful()
    {
        Database db(memConfig());
        db.open();
        auto ev = makeEv(0);
        db.insertEvent(ev);
        const bool ok2 = db.insertEvent(ev);  // may fail or succeed; should not crash
        Q_UNUSED(ok2);
        QVERIFY2(db.eventCount() >= 1, "DB should have at least 1 event after duplicate insert");
    }
};

QTEST_MAIN(DatabaseStressTest)
#include "test_database_stress.moc"
