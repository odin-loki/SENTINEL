// test_database_deep4.cpp — Iteration 15 deep audit: insert/query roundtrip,
// migration idempotency, and event count accuracy.
#include <QTest>
#include <QTimeZone>
#include <QSqlQuery>
#include <QSqlDatabase>
#include <QDir>
#include <QFile>
#include <QUuid>

#include "core/Database.h"
#include "core/AppConfig.h"
#include "core/CrimeEvent.h"

class TestDatabaseDeep4 : public QObject
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
        return QDir::tempPath() + QStringLiteral("/sentinel_deep4_")
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
                                const QString& narrative = {},
                                double quality = 0.85)
    {
        CrimeEvent ev;
        ev.eventId       = id;
        ev.id            = id;
        ev.crimeType     = type;
        ev.suburb        = QStringLiteral("Deep4Zone");
        ev.lat           = 51.5074;
        ev.lon           = -0.1278;
        ev.latitude      = 51.5074;
        ev.longitude     = -0.1278;
        ev.source        = QStringLiteral("deep4_test");
        ev.sourceVersion = QStringLiteral("1.0");
        ev.ingestedAt    = QDateTime::currentDateTimeUtc();
        ev.qualityScore  = quality;
        ev.narrative     = narrative.isEmpty()
                               ? std::optional<QString>{}
                               : std::make_optional(narrative);
        const QDateTime occ = QDateTime(QDate(2024, 9, 10), QTime(14, 45, 0), QTimeZone::utc());
        ev.occurredAt    = occ;
        ev.timestamp     = occ;
        return ev;
    }

    static bool createLegacyV1Database(const QString& path)
    {
        const QString conn = QStringLiteral("legacy_v1_deep4_%1")
                                 .arg(QUuid::createUuid().toString(QUuid::Id128));
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
    void testInsertQueryRoundTripPreservesFields();
    void testEventByIdRoundTrip();
    void testQueryEventsFiltersByCrimeType();
    void testEmptyDatabaseEventCountZero();
    void testEventCountAfterInsertAndDelete();
    void testEventCountTracksBatchInsert();
    void testMigrateSchemaIdempotentOnMemoryDb();
    void testLegacyV1MigrationPreservesInsertCapability();
};

void TestDatabaseDeep4::testInsertQueryRoundTripPreservesFields()
{
    Database db(memCfg());
    QVERIFY(db.open());

    const CrimeEvent orig = makeEvent(QStringLiteral("RT-001"),
                                      QStringLiteral("burglary"),
                                      QStringLiteral("forced entry via rear door"),
                                      0.72);
    QVERIFY2(db.insertEvent(orig), qPrintable(db.lastError()));

    const QVector<CrimeEvent> results = db.getAllEvents();
    QCOMPARE(results.size(), 1);

    const CrimeEvent fetched = results[0];
    QCOMPARE(fetched.eventId,   orig.eventId);
    QCOMPARE(fetched.crimeType, orig.crimeType);
    QCOMPARE(fetched.suburb,    orig.suburb);
    QCOMPARE(fetched.source,    orig.source);
    QVERIFY(fetched.narrative.has_value());
    QCOMPARE(fetched.narrative.value(), orig.narrative.value());
    QVERIFY(qAbs(fetched.qualityScore - orig.qualityScore) < 1e-9);
    QVERIFY(fetched.lat.has_value());
    QVERIFY(qAbs(fetched.lat.value() - orig.lat.value()) < 1e-9);
}

void TestDatabaseDeep4::testEventByIdRoundTrip()
{
    Database db(memCfg());
    QVERIFY(db.open());

    const CrimeEvent orig = makeEvent(QStringLiteral("BYID-42"), QStringLiteral("robbery"));
    QVERIFY(db.insertEvent(orig));

    const CrimeEvent fetched = db.eventById(QStringLiteral("BYID-42"));
    QCOMPARE(fetched.eventId,   QStringLiteral("BYID-42"));
    QCOMPARE(fetched.crimeType, QStringLiteral("robbery"));
    QVERIFY(!fetched.eventId.isEmpty());
}

void TestDatabaseDeep4::testQueryEventsFiltersByCrimeType()
{
    Database db(memCfg());
    QVERIFY(db.open());

    QVERIFY(db.insertEvent(makeEvent(QStringLiteral("F-001"), QStringLiteral("burglary"))));
    QVERIFY(db.insertEvent(makeEvent(QStringLiteral("F-002"), QStringLiteral("robbery"))));
    QVERIFY(db.insertEvent(makeEvent(QStringLiteral("F-003"), QStringLiteral("burglary"))));

    const auto burglaries = db.queryEvents(QStringLiteral("burglary"),
                                           QDate{}, QDate{}, QString{}, 100);
    QCOMPARE(burglaries.size(), 2);
    for (const CrimeEvent& ev : burglaries)
        QCOMPARE(ev.crimeType, QStringLiteral("burglary"));
}

void TestDatabaseDeep4::testEmptyDatabaseEventCountZero()
{
    Database db(memCfg());
    QVERIFY(db.open());
    QCOMPARE(db.eventCount(), 0);
    QCOMPARE(db.getTotalEventCount(), 0);
    QVERIFY(db.getAllEvents().isEmpty());
}

void TestDatabaseDeep4::testEventCountAfterInsertAndDelete()
{
    Database db(memCfg());
    QVERIFY(db.open());

    QVERIFY(db.insertEvent(makeEvent(QStringLiteral("CNT-001"))));
    QVERIFY(db.insertEvent(makeEvent(QStringLiteral("CNT-002"))));
    QCOMPARE(db.eventCount(), 2);

    QVERIFY(db.deleteEvent(QStringLiteral("CNT-001")));
    QCOMPARE(db.eventCount(), 1);

    const CrimeEvent remaining = db.eventById(QStringLiteral("CNT-002"));
    QCOMPARE(remaining.eventId, QStringLiteral("CNT-002"));
}

void TestDatabaseDeep4::testEventCountTracksBatchInsert()
{
    Database db(memCfg());
    QVERIFY(db.open());

    QVector<CrimeEvent> events;
    events.reserve(12);
    for (int i = 0; i < 12; ++i)
        events.append(makeEvent(QStringLiteral("BATCH4-%1").arg(i, 2, 10, QChar('0'))));

    const int inserted = db.batchInsert(events);
    QCOMPARE(inserted, 12);
    QCOMPARE(db.eventCount(), 12);
}

void TestDatabaseDeep4::testMigrateSchemaIdempotentOnMemoryDb()
{
    Database db(memCfg());
    QVERIFY(db.open());
    QCOMPARE(db.currentSchemaVersion(), Database::SCHEMA_VERSION);

    for (int i = 0; i < 4; ++i) {
        QVERIFY(db.migrateSchema(0));
        QCOMPARE(db.currentSchemaVersion(), Database::SCHEMA_VERSION);
    }

    QVERIFY(db.insertEvent(makeEvent(QStringLiteral("MIG-OK"))));
    QCOMPARE(db.eventCount(), 1);
}

void TestDatabaseDeep4::testLegacyV1MigrationPreservesInsertCapability()
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
        QVERIFY(db.insertEvent(makeEvent(QStringLiteral("LEG4-001"))));
        QCOMPARE(db.eventCount(), 1);
    }

    {
        Database db2(cfg);
        QVERIFY2(db2.open(), qPrintable(db2.lastError()));
        QCOMPARE(db2.currentSchemaVersion(), Database::SCHEMA_VERSION);
        QCOMPARE(db2.eventCount(), 1);
        QCOMPARE(db2.eventById(QStringLiteral("LEG4-001")).crimeType,
                 QStringLiteral("theft"));
    }

    removeDbFiles(path);
}

QTEST_GUILESS_MAIN(TestDatabaseDeep4)
#include "test_database_deep4.moc"
