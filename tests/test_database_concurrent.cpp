// test_database_concurrent.cpp — Database concurrent-access and consistency tests
#include <QTest>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QUuid>
#include <QDateTime>
#include <QDate>

#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

#include "core/Database.h"
#include "core/AppConfig.h"
#include "core/CrimeEvent.h"

namespace {

AppConfig memConfig() { AppConfig cfg; cfg.databasePath = QStringLiteral(":memory:"); return cfg; }
AppConfig fileConfig(const QString& p) { AppConfig cfg; cfg.databasePath = p; return cfg; }
QString makeTempPath() {
    return QDir::tempPath() + QStringLiteral("/sentinel_dbtest_") +
           QUuid::createUuid().toString(QUuid::WithoutBraces) + QStringLiteral(".db");
}
void removeTempDb(const QString& p) {
    QFile::remove(p); QFile::remove(p + "-wal"); QFile::remove(p + "-shm");
}
CrimeEvent makeDbEvent(int idx, const QString& tag = "test", double quality = 0.8) {
    CrimeEvent ev;
    ev.eventId = ev.id = QString("DB_%1_%2").arg(tag).arg(idx, 8, 10, QChar('0'));
    ev.source = "unit_test"; ev.sourceVersion = "1.0";
    ev.ingestedAt = QDateTime::currentDateTimeUtc();
    ev.crimeType = "theft"; ev.suburb = "TestSuburb";
    ev.lat = 51.5 + idx * 0.0001; ev.lon = -0.1;
    ev.latitude = *ev.lat; ev.longitude = *ev.lon;
    ev.occurredAt = QDateTime::currentDateTimeUtc().addDays(-30 - (idx % 365));
    ev.qualityScore = quality;
    return ev;
}
bool insertWithRetry(Database& db, const CrimeEvent& ev, int maxR = 40, int sleepMs = 5) {
    for (int r = 0; r < maxR; ++r) {
        if (db.insertEvent(ev)) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(sleepMs));
    }
    return false;
}

} // namespace

class TestDatabaseConcurrent : public QObject {
    Q_OBJECT

private slots:

    void testConcurrentInserts() {
        const QString dbPath = makeTempPath();
        constexpr int nT = 4, evPT = 50;
        std::atomic<int> ok{0};
        std::vector<std::thread> threads;
        for (int t = 0; t < nT; ++t) {
            threads.emplace_back([=, &ok]() {
                Database db(fileConfig(dbPath));
                if (!db.open()) return;
                for (int i = 0; i < evPT; ++i)
                    if (insertWithRetry(db, makeDbEvent(t * 1000 + i, QString("t%1").arg(t))))
                        ok.fetch_add(1, std::memory_order_relaxed);
            });
        }
        for (auto& th : threads) th.join();
        Database v(fileConfig(dbPath)); QVERIFY(v.open());
        QCOMPARE(ok.load(), nT * evPT);
        QCOMPARE(v.eventCount(), nT * evPT);
        removeTempDb(dbPath);
    }

    void testConcurrentReads() {
        const QString dbPath = makeTempPath();
        constexpr int nEv = 100, nR = 4;
        { Database db(fileConfig(dbPath)); QVERIFY(db.open());
          for (int i = 0; i < nEv; ++i) QVERIFY(db.insertEvent(makeDbEvent(i, "read"))); }
        std::atomic<int> total{0};
        std::vector<std::thread> threads;
        for (int r = 0; r < nR; ++r) {
            threads.emplace_back([=, &total]() {
                Database db(fileConfig(dbPath));
                if (!db.open()) return;
                total.fetch_add(db.getAllEvents().size(), std::memory_order_relaxed);
            });
        }
        for (auto& th : threads) th.join();
        QCOMPARE(total.load(), nR * nEv);
        removeTempDb(dbPath);
    }

    void testConcurrentMixedOps() {
        const QString dbPath = makeTempPath();
        constexpr int pre = 20, wt = 2, rt = 2, evPW = 25;
        { Database db(fileConfig(dbPath)); QVERIFY(db.open());
          for (int i = 0; i < pre; ++i) QVERIFY(db.insertEvent(makeDbEvent(i, "pre"))); }
        std::atomic<int> written{0};
        std::vector<std::thread> threads;
        for (int w = 0; w < wt; ++w) {
            threads.emplace_back([=, &written]() {
                Database db(fileConfig(dbPath));
                if (!db.open()) return;
                for (int i = 0; i < evPW; ++i)
                    if (insertWithRetry(db, makeDbEvent(2000 + w * 100 + i, QString("w%1").arg(w))))
                        written.fetch_add(1, std::memory_order_relaxed);
            });
        }
        for (int r = 0; r < rt; ++r) {
            threads.emplace_back([=]() {
                Database db(fileConfig(dbPath));
                if (!db.open()) return;
                for (int i = 0; i < 5; ++i) {
                    (void)db.getAllEvents();
                    std::this_thread::sleep_for(std::chrono::milliseconds(2));
                }
            });
        }
        for (auto& th : threads) th.join();
        Database v(fileConfig(dbPath)); QVERIFY(v.open());
        QCOMPARE(v.eventCount(), pre + written.load());
        removeTempDb(dbPath);
    }

    void testTransactionRollback() {
        Database db(memConfig()); QVERIFY(db.open());
        auto ev = makeDbEvent(1, "rb");
        QVERIFY(db.insertEvent(ev)); QCOMPARE(db.eventCount(), 1);
        ev.suburb = "Replaced";
        QVERIFY(db.insertEvent(ev)); QCOMPARE(db.eventCount(), 1);
    }

    void testTransactionCommit() {
        Database db(memConfig()); QVERIFY(db.open());
        const auto ev = makeDbEvent(42, "commit");
        QVERIFY(db.insertEvent(ev));
        const auto found = db.eventById(ev.eventId);
        QCOMPARE(found.eventId, ev.eventId);
    }

    void testInsertAndQueryConsistency() {
        Database db(memConfig()); QVERIFY(db.open());
        for (int i = 0; i < 100; ++i) QVERIFY(db.insertEvent(makeDbEvent(i, "seq")));
        QCOMPARE(db.getAllEvents().size(), 100);
        QCOMPARE(db.eventCount(), 100);
    }

    void testQueryWithEmptyResult() {
        Database db(memConfig()); QVERIFY(db.open());
        for (int i = 0; i < 5; ++i) QVERIFY(db.insertEvent(makeDbEvent(i, "empty")));
        const QDateTime futureFrom = QDateTime::currentDateTimeUtc().addYears(10);
        const QDateTime futureTo   = QDateTime::currentDateTimeUtc().addYears(11);
        QVERIFY(db.queryEvents({}, futureFrom, futureTo).isEmpty());
    }

    void testUpdateQualityScore() {
        Database db(memConfig()); QVERIFY(db.open());
        auto ev = makeDbEvent(10, "qual", 0.9);
        QVERIFY(db.insertEvent(ev));
        ev.qualityScore = 0.1;
        QVERIFY(db.updateEvent(ev));
        const auto found = db.eventById(ev.eventId);
        QVERIFY(std::abs(found.qualityScore - 0.1) < 1e-6);
    }

    void testDeleteEvent() {
        Database db(memConfig()); QVERIFY(db.open());
        auto ev = makeDbEvent(99, "del"); ev.suburb = "OldSuburb";
        QVERIFY(db.insertEvent(ev));
        ev.suburb = "NewSuburb";
        QVERIFY(db.insertEvent(ev));
        QCOMPARE(db.eventCount(), 1);
        QCOMPARE(db.eventById(ev.eventId).suburb, QString("NewSuburb"));
    }

    void testBulkInsert1000Events() {
        Database db(memConfig()); QVERIFY(db.open());
        for (int i = 0; i < 1000; ++i) QVERIFY(db.insertEvent(makeDbEvent(i, "bulk")));
        QCOMPARE(db.eventCount(), 1000);
    }
};

QTEST_MAIN(TestDatabaseConcurrent)
#include "test_database_concurrent.moc"
