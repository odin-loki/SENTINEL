// test_database_deep3.cpp — Iteration 12 deep audit: WAL mode, prepared-statement
// safety, batch-insert transaction, and schema-migration idempotency.
#include <QTest>
#include <QApplication>
#include <QTemporaryFile>
#include <QSqlQuery>
#include <QSqlDatabase>
#include <QDir>
#include <QFile>
#include <QUuid>

#include "core/Database.h"
#include "core/AppConfig.h"
#include "core/CrimeEvent.h"

class TestDatabaseDeep3 : public QObject
{
    Q_OBJECT

private:
    static AppConfig memCfg()
    {
        AppConfig cfg;
        cfg.databasePath = QStringLiteral(":memory:");
        return cfg;
    }

    static QString tempDbPath()
    {
        return QDir::tempPath() + QStringLiteral("/sentinel_deep3_")
               + QUuid::createUuid().toString(QUuid::Id128)
               + QStringLiteral(".db");
    }

    static void removeDbFiles(const QString& path)
    {
        QFile::remove(path);
        QFile::remove(path + QStringLiteral("-wal"));
        QFile::remove(path + QStringLiteral("-shm"));
    }

    static CrimeEvent makeEvent(const QString& id,
                                const QString& type = QStringLiteral("theft"),
                                const QString& narrative = {})
    {
        CrimeEvent ev;
        ev.eventId       = id;
        ev.id            = id;
        ev.crimeType     = type;
        ev.suburb        = QStringLiteral("TestZone");
        ev.lat           = 51.5074;
        ev.lon           = -0.1278;
        ev.latitude      = 51.5074;
        ev.longitude     = -0.1278;
        ev.source        = QStringLiteral("deep3_test");
        ev.sourceVersion = QStringLiteral("1.0");
        ev.ingestedAt    = QDateTime::currentDateTimeUtc();
        ev.qualityScore  = 0.85;
        ev.narrative     = narrative.isEmpty()
                               ? std::optional<QString>{}
                               : std::make_optional(narrative);
        const QDateTime occ = QDateTime(QDate(2024, 8, 15), QTime(10, 30, 0), QTimeZone::utc());
        ev.occurredAt    = occ;
        ev.timestamp     = occ;
        return ev;
    }

    static bool createLegacyV1Database(const QString& path)
    {
        const QString conn = QStringLiteral("legacy_v1_%1").arg(QUuid::createUuid().toString(QUuid::Id128));
        {
            QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), conn);
            db.setDatabaseName(path);
            if (!db.open())
                return false;

            QSqlQuery q(db);
            const bool ok = q.exec(QStringLiteral(R"(
                CREATE TABLE events (
                    event_id          TEXT PRIMARY KEY,
                    source            TEXT,
                    source_version    TEXT,
                    ingested_at       TEXT,
                    occurred_at       TEXT,
                    reported_at       TEXT,
                    crime_type        TEXT,
                    crime_subtype     TEXT,
                    location_raw      TEXT,
                    lat               REAL,
                    lon               REAL,
                    address_normalised TEXT,
                    lga               TEXT,
                    suburb            TEXT,
                    narrative         TEXT,
                    outcome           TEXT,
                    conviction        INTEGER,
                    suspect_count     INTEGER,
                    victim_count      INTEGER,
                    weapon            TEXT,
                    meta              TEXT
                )
            )")) && q.exec(QStringLiteral(
                "CREATE TABLE schema_version (version INTEGER NOT NULL)"))
              && q.exec(QStringLiteral("INSERT INTO schema_version (version) VALUES (1)"));

            db.close();
            if (!ok)
                return false;
        }
        QSqlDatabase::removeDatabase(conn);
        return true;
    }

private slots:
    void testWalModeEnabledOnFileDatabase();
    void testPreparedStatementsPreventSqlInjection();
    void testSearchWithSqlInjectionPayloadIsSafe();
    void testBatchInsertTransactionCommitsAll();
    void testBatchInsertEmptyReturnsZero();
    void testMigrateSchemaIdempotent();
    void testOpenLegacyV1DatabaseTwiceIsIdempotent();
    void testSchemaVersionUnchangedAfterRepeatedMigration();
};

void TestDatabaseDeep3::testWalModeEnabledOnFileDatabase()
{
    QTemporaryFile tmp;
    tmp.setAutoRemove(true);
    QVERIFY(tmp.open());
    const QString path = tmp.fileName();
    tmp.close();

    AppConfig cfg;
    cfg.databasePath = path;
    Database db(cfg);
    QVERIFY2(db.open(), qPrintable(db.lastError()));

    const QString connName = QStringLiteral("wal_check_deep3");
    {
        QSqlDatabase verifyDb = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connName);
        verifyDb.setDatabaseName(path);
        QVERIFY(verifyDb.open());

        QSqlQuery q(verifyDb);
        QVERIFY(q.exec(QStringLiteral("PRAGMA journal_mode")));
        QVERIFY(q.next());
        QCOMPARE(q.value(0).toString().toLower(), QStringLiteral("wal"));

        verifyDb.close();
    }
    QSqlDatabase::removeDatabase(connName);
}

void TestDatabaseDeep3::testPreparedStatementsPreventSqlInjection()
{
    Database db(memCfg());
    QVERIFY(db.open());

    const QString maliciousId = QStringLiteral("'; DROP TABLE events; --");
    const CrimeEvent ev = makeEvent(maliciousId, QStringLiteral("injection_test"));
    QVERIFY2(db.insertEvent(ev), qPrintable(db.lastError()));

    // Table must survive — prepared INSERT OR REPLACE must not interpolate raw SQL
    QCOMPARE(db.eventCount(), 1);

    const CrimeEvent fetched = db.eventById(maliciousId);
    QCOMPARE(fetched.eventId, maliciousId);
    QCOMPARE(fetched.crimeType, QStringLiteral("injection_test"));

    // events table must still exist and be queryable
    QCOMPARE(db.eventCount(), 1);
    QVERIFY(!db.getAllEvents().isEmpty());
}

void TestDatabaseDeep3::testSearchWithSqlInjectionPayloadIsSafe()
{
    Database db(memCfg());
    QVERIFY(db.open());

    QVERIFY(db.insertEvent(makeEvent(QStringLiteral("SAFE-001"), QStringLiteral("burglary"),
                                     QStringLiteral("normal narrative"))));
    QVERIFY(db.insertEvent(makeEvent(QStringLiteral("SAFE-002"), QStringLiteral("robbery"),
                                     QStringLiteral("another record"))));

    const QString injectionSearch = QStringLiteral("'; DROP TABLE events; --");
    const auto results = db.queryEvents(QString{}, QDate{}, QDate{}, injectionSearch, 100);

    // No match expected, but table must remain intact
    QCOMPARE(results.size(), 0);
    QCOMPARE(db.eventCount(), 2);
}

void TestDatabaseDeep3::testBatchInsertTransactionCommitsAll()
{
    const QString path = tempDbPath();
    QVERIFY2(!path.isEmpty(), "Could not create temp db path");

    AppConfig cfg;
    cfg.databasePath = path;
    Database db(cfg);
    QVERIFY2(db.open(), qPrintable(db.lastError()));

    QVector<CrimeEvent> events;
    events.reserve(50);
    for (int i = 0; i < 50; ++i)
        events.append(makeEvent(QStringLiteral("BATCH3-%1").arg(i, 3, 10, QChar('0'))));

    const int inserted = db.batchInsert(events);
    QCOMPARE(inserted, 50);
    QCOMPARE(db.eventCount(), 50);

    db.close();

    // Re-open and verify persistence (transaction committed)
    Database db2(cfg);
    QVERIFY2(db2.open(), qPrintable(db2.lastError()));
    QCOMPARE(db2.eventCount(), 50);

    removeDbFiles(path);
}

void TestDatabaseDeep3::testBatchInsertEmptyReturnsZero()
{
    Database db(memCfg());
    QVERIFY(db.open());
    QCOMPARE(db.batchInsert({}), 0);
    QCOMPARE(db.eventCount(), 0);
}

void TestDatabaseDeep3::testMigrateSchemaIdempotent()
{
    Database db(memCfg());
    QVERIFY(db.open());
    QCOMPARE(db.currentSchemaVersion(), Database::SCHEMA_VERSION);

    // Running migration at current version must be a no-op success
    QVERIFY(db.migrateSchema(db.currentSchemaVersion()));
    QCOMPARE(db.currentSchemaVersion(), Database::SCHEMA_VERSION);

    // Re-running from version 0 must not corrupt schema
    QVERIFY(db.migrateSchema(0));
    QCOMPARE(db.currentSchemaVersion(), Database::SCHEMA_VERSION);
    QVERIFY(db.insertEvent(makeEvent(QStringLiteral("POST-MIG-001"))));
    QCOMPARE(db.eventCount(), 1);
}

void TestDatabaseDeep3::testOpenLegacyV1DatabaseTwiceIsIdempotent()
{
    const QString path = tempDbPath();
    QVERIFY2(!path.isEmpty(), "Could not create temp db path");
    QVERIFY2(createLegacyV1Database(path), "Could not seed legacy v1 schema");

    AppConfig cfg;
    cfg.databasePath = path;

    {
        Database db(cfg);
        QVERIFY2(db.open(), qPrintable(db.lastError()));
        QCOMPARE(db.currentSchemaVersion(), Database::SCHEMA_VERSION);
        QVERIFY(db.insertEvent(makeEvent(QStringLiteral("LEG-001"))));
        QCOMPARE(db.eventCount(), 1);
    }

    {
        Database db2(cfg);
        QVERIFY2(db2.open(), qPrintable(db2.lastError()));
        QCOMPARE(db2.currentSchemaVersion(), Database::SCHEMA_VERSION);
        QCOMPARE(db2.eventCount(), 1);
        const CrimeEvent ev = db2.eventById(QStringLiteral("LEG-001"));
        QVERIFY(!ev.eventId.isEmpty());
    }

    removeDbFiles(path);
}

void TestDatabaseDeep3::testSchemaVersionUnchangedAfterRepeatedMigration()
{
    Database db(memCfg());
    QVERIFY(db.open());

    for (int i = 0; i < 3; ++i) {
        QVERIFY(db.migrateSchema(0));
        QCOMPARE(db.currentSchemaVersion(), Database::SCHEMA_VERSION);
    }

    // Schema intact after repeated migrations — CRUD still works
    QVERIFY(db.insertEvent(makeEvent(QStringLiteral("IDEM-001"))));
    QCOMPARE(db.eventCount(), 1);
}

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    TestDatabaseDeep3 tests;
    return QTest::qExec(&tests, argc, argv);
}

#include "test_database_deep3.moc"
