// test_database_wal.cpp — WAL mode, transaction and schema integrity tests
#include <QTest>
#include <QCoreApplication>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QDir>
#include <QFile>
#include <QUuid>
#include <QDateTime>
#include <cmath>

#include "core/Database.h"
#include "core/AppConfig.h"
#include "core/CrimeEvent.h"

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

namespace {

AppConfig inMemoryCfg()
{
    AppConfig cfg;
    cfg.databasePath = QStringLiteral(":memory:");
    return cfg;
}

// Returns a unique temp-file path (caller must remove it when done)
QString tempDbPath()
{
    return QDir::tempPath() + QStringLiteral("/sentinel_wal_test_")
           + QUuid::createUuid().toString(QUuid::Id128)
           + QStringLiteral(".db");
}

void removeTempDb(const QString& path)
{
    QFile::remove(path);
    QFile::remove(path + QStringLiteral("-wal"));
    QFile::remove(path + QStringLiteral("-shm"));
}

CrimeEvent makeEvent(const QString& id, const QString& crimeType = QStringLiteral("theft"))
{
    CrimeEvent ev;
    ev.eventId      = id;
    ev.crimeType    = crimeType;
    ev.ingestedAt   = QDateTime::currentDateTimeUtc();
    ev.occurredAt   = QDateTime::currentDateTimeUtc();
    ev.suburb       = QStringLiteral("TestSuburb");
    ev.qualityScore = 0.8;
    return ev;
}

} // anonymous namespace

// ─────────────────────────────────────────────────────────────────────────────
// TestDatabaseWAL
// ─────────────────────────────────────────────────────────────────────────────

class TestDatabaseWAL : public QObject
{
    Q_OBJECT

private slots:

    // 1. WAL mode is set on a real file-based database; in-memory DB opens fine
    void testWALModeEnabled()
    {
        // In-memory DB: WAL pragma runs without crash; DB opens successfully
        {
            auto cfg = inMemoryCfg();
            Database db(cfg);
            QVERIFY(db.open());
            QVERIFY(db.isOpen());
        }

        // File-based DB: PRAGMA journal_mode must return "wal"
        const QString path = tempDbPath();
        {
            AppConfig cfg;
            cfg.databasePath = path;
            Database db(cfg);
            QVERIFY(db.open());
        }

        const QString connName = QStringLiteral("wal_verify_")
                                 + QUuid::createUuid().toString(QUuid::Id128);
        {
            QSqlDatabase rawDb =
                QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connName);
            rawDb.setDatabaseName(path);
            QVERIFY(rawDb.open());
            QSqlQuery q(rawDb);
            QVERIFY(q.exec(QStringLiteral("PRAGMA journal_mode")));
            QVERIFY(q.next());
            QCOMPARE(q.value(0).toString(), QStringLiteral("wal"));
            rawDb.close();
        }
        QSqlDatabase::removeDatabase(connName);
        removeTempDb(path);
    }

    // 2. Two connections to the same WAL file: writer inserts, reader sees data
    void testConcurrentReadWrite()
    {
        const QString path = tempDbPath();

        AppConfig cfg;
        cfg.databasePath = path;

        Database writer(cfg);
        Database reader(cfg);
        QVERIFY(writer.open());
        QVERIFY(reader.open());

        QCOMPARE(reader.eventCount(), 0);

        CrimeEvent ev = makeEvent(QStringLiteral("CONC-001"), QStringLiteral("burglary"));
        QVERIFY(writer.insertEvent(ev));

        // WAL reader sees committed data from writer
        QCOMPARE(reader.eventCount(), 1);

        removeTempDb(path);
    }

    // 3. Transaction rollback — inserted row disappears
    void testTransactionRollback()
    {
        const QString connName = QStringLiteral("txn_rollback_")
                                 + QUuid::createUuid().toString(QUuid::Id128);
        {
            QSqlDatabase db =
                QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connName);
            db.setDatabaseName(QStringLiteral(":memory:"));
            QVERIFY(db.open());

            QSqlQuery q(db);
            QVERIFY(q.exec(QStringLiteral(
                "CREATE TABLE t (id TEXT PRIMARY KEY)")));

            QVERIFY(db.transaction());
            QVERIFY(q.exec(QStringLiteral("INSERT INTO t VALUES ('row1')")));
            QVERIFY(db.rollback());

            QVERIFY(q.exec(QStringLiteral("SELECT COUNT(*) FROM t")));
            QVERIFY(q.next());
            QCOMPARE(q.value(0).toInt(), 0);

            db.close();
        }
        QSqlDatabase::removeDatabase(connName);
    }

    // 4. Transaction commit — inserted row persists
    void testTransactionCommit()
    {
        const QString connName = QStringLiteral("txn_commit_")
                                 + QUuid::createUuid().toString(QUuid::Id128);
        {
            QSqlDatabase db =
                QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connName);
            db.setDatabaseName(QStringLiteral(":memory:"));
            QVERIFY(db.open());

            QSqlQuery q(db);
            QVERIFY(q.exec(QStringLiteral(
                "CREATE TABLE t (id TEXT PRIMARY KEY)")));

            QVERIFY(db.transaction());
            QVERIFY(q.exec(QStringLiteral("INSERT INTO t VALUES ('row1')")));
            QVERIFY(db.commit());

            QVERIFY(q.exec(QStringLiteral("SELECT COUNT(*) FROM t")));
            QVERIFY(q.next());
            QCOMPARE(q.value(0).toInt(), 1);

            db.close();
        }
        QSqlDatabase::removeDatabase(connName);
    }

    // 5. Page size is a power of 2
    void testPragmaPageSize()
    {
        const QString connName = QStringLiteral("page_size_")
                                 + QUuid::createUuid().toString(QUuid::Id128);
        {
            QSqlDatabase db =
                QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connName);
            db.setDatabaseName(QStringLiteral(":memory:"));
            QVERIFY(db.open());

            QSqlQuery q(db);
            QVERIFY(q.exec(QStringLiteral("PRAGMA page_size")));
            QVERIFY(q.next());
            const int ps = q.value(0).toInt();
            QVERIFY(ps >= 512);
            QCOMPARE(ps & (ps - 1), 0);  // power of 2
            db.close();
        }
        QSqlDatabase::removeDatabase(connName);
    }

    // 6. After open(), schema_version table exists with version >= 1
    void testSchemaVersionPresent()
    {
        auto cfg = inMemoryCfg();
        Database db(cfg);
        QVERIFY(db.open());
        QVERIFY(db.currentSchemaVersion() >= 1);
    }

    // 7. idx_events_source and idx_events_suburb exist in sqlite_master
    void testIndexesExist()
    {
        const QString path = tempDbPath();

        {
            AppConfig cfg;
            cfg.databasePath = path;
            Database db(cfg);
            QVERIFY(db.open());
        }

        const QString connName = QStringLiteral("idx_check_")
                                 + QUuid::createUuid().toString(QUuid::Id128);
        {
            QSqlDatabase rawDb =
                QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connName);
            rawDb.setDatabaseName(path);
            QVERIFY(rawDb.open());

            QSqlQuery q(rawDb);
            QVERIFY(q.exec(QStringLiteral(
                "SELECT name FROM sqlite_master "
                "WHERE type='index' AND name='idx_events_source'")));
            QVERIFY2(q.next(), "idx_events_source index not found");

            QVERIFY(q.exec(QStringLiteral(
                "SELECT name FROM sqlite_master "
                "WHERE type='index' AND name='idx_events_suburb'")));
            QVERIFY2(q.next(), "idx_events_suburb index not found");

            rawDb.close();
        }
        QSqlDatabase::removeDatabase(connName);
        removeTempDb(path);
    }

    // 8. PRAGMA foreign_keys can be enabled and returns 1
    void testForeignKeySupport()
    {
        const QString connName = QStringLiteral("fk_test_")
                                 + QUuid::createUuid().toString(QUuid::Id128);
        {
            QSqlDatabase db =
                QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connName);
            db.setDatabaseName(QStringLiteral(":memory:"));
            QVERIFY(db.open());

            QSqlQuery q(db);
            q.exec(QStringLiteral("PRAGMA foreign_keys=ON"));
            QVERIFY(q.exec(QStringLiteral("PRAGMA foreign_keys")));
            QVERIFY(q.next());
            QCOMPARE(q.value(0).toInt(), 1);
            db.close();
        }
        QSqlDatabase::removeDatabase(connName);
    }

    // 9. In-memory DB: basic insert + select roundtrip
    void testInMemoryDbFunctional()
    {
        auto cfg = inMemoryCfg();
        Database db(cfg);
        QVERIFY(db.open());
        QCOMPARE(db.eventCount(), 0);

        CrimeEvent ev = makeEvent(QStringLiteral("IMEM-001"), QStringLiteral("assault"));
        QVERIFY(db.insertEvent(ev));

        const CrimeEvent r = db.eventById(QStringLiteral("IMEM-001"));
        QCOMPARE(r.eventId,   QStringLiteral("IMEM-001"));
        QCOMPARE(r.crimeType, QStringLiteral("assault"));
        QCOMPARE(db.eventCount(), 1);
    }

    // 10. Insert 100 events and retrieve all of them
    void testMultipleEventsQuery()
    {
        auto cfg = inMemoryCfg();
        Database db(cfg);
        QVERIFY(db.open());

        const QDateTime base = QDateTime::currentDateTimeUtc();
        for (int i = 0; i < 100; ++i) {
            CrimeEvent ev = makeEvent(QString("ME-%1").arg(i, 3, 10, QChar('0')));
            ev.occurredAt = base.addSecs(-i);
            QVERIFY(db.insertEvent(ev));
        }

        QCOMPARE(db.eventCount(), 100);
        const auto all = db.getAllEvents();
        QCOMPARE(all.size(), 100);
    }
};

QTEST_MAIN(TestDatabaseWAL)
#include "test_database_wal.moc"
