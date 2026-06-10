// test_database_migration_deep.cpp
// Database schema migration tests: current version, migrateSchema,
// read/write integrity after migration, and CRUD after schema creation.
#include <QTest>
#include <QUuid>
#include "core/Database.h"
#include "core/AppConfig.h"

class DatabaseMigrationDeepTest : public QObject
{
    Q_OBJECT

private:
    static AppConfig inMemoryConfig()
    {
        AppConfig cfg;
        cfg.databasePath = QStringLiteral(":memory:");
        return cfg;
    }

    static CrimeEvent makeEvent(const QString& id)
    {
        CrimeEvent ev;
        ev.id        = id;
        ev.eventId   = id;
        ev.crimeType = QStringLiteral("burglary");
        ev.suburb    = QStringLiteral("Brixton");
        ev.latitude  = 51.5;
        ev.longitude = -0.1;
        ev.timestamp = QDateTime(QDate::currentDate().addDays(-5), QTime(10, 0), QTimeZone::utc());
        ev.occurredAt = ev.timestamp;
        return ev;
    }

private slots:

    // 1. Database opens with in-memory config
    void testOpenSucceeds()
    {
        Database db(inMemoryConfig());
        QVERIFY2(db.open(), qPrintable(QStringLiteral("DB open failed: %1").arg(db.lastError())));
    }

    // 2. currentSchemaVersion() returns SCHEMA_VERSION after open
    void testCurrentSchemaVersionAfterOpen()
    {
        Database db(inMemoryConfig());
        QVERIFY(db.open());
        const int v = db.currentSchemaVersion();
        QCOMPARE(v, Database::SCHEMA_VERSION);
    }

    // 3. SCHEMA_VERSION is >= 1
    void testSchemaVersionAtLeastOne()
    {
        QVERIFY2(Database::SCHEMA_VERSION >= 1, "SCHEMA_VERSION should be >= 1");
    }

    // 4. insertEvent succeeds on fresh in-memory DB
    void testInsertEventSucceeds()
    {
        Database db(inMemoryConfig());
        QVERIFY(db.open());
        QVERIFY2(db.insertEvent(makeEvent(QStringLiteral("E1"))),
                 qPrintable(QStringLiteral("insertEvent failed: %1").arg(db.lastError())));
    }

    // 5. eventCount() matches inserted count
    void testEventCountMatchesInserted()
    {
        Database db(inMemoryConfig());
        QVERIFY(db.open());
        for (int i = 0; i < 5; ++i)
            QVERIFY(db.insertEvent(makeEvent(QStringLiteral("E%1").arg(i))));
        QCOMPARE(db.eventCount(), 5);
    }

    // 6. queryEvents() retrieves inserted events
    void testQueryEventsRetrievesInserted()
    {
        Database db(inMemoryConfig());
        QVERIFY(db.open());
        QVERIFY(db.insertEvent(makeEvent(QStringLiteral("QE1"))));
        const auto evs = db.queryEvents();
        QVERIFY2(!evs.isEmpty(), "queryEvents should return at least 1 event");
    }

    // 7. migrateSchema(currentVersion) returns true (no-op migration)
    void testMigrateSchemaCurrentVersionNoOp()
    {
        Database db(inMemoryConfig());
        QVERIFY(db.open());
        const int cur = db.currentSchemaVersion();
        const bool ok = db.migrateSchema(cur);
        QVERIFY2(ok, "migrateSchema with current version should not fail");
    }

    // 8. insertAuditEntry + queryAudit works
    void testAuditCRUD()
    {
        Database db(inMemoryConfig());
        QVERIFY(db.open());
        QVERIFY(db.insertAuditEntry(QStringLiteral("EVT1"),
                                     QStringLiteral("test_action"),
                                     QStringLiteral("test detail")));
        const auto audit = db.queryAudit(100);
        QVERIFY2(!audit.isEmpty(), "queryAudit should return at least one entry");
    }

    // 9. insertLead + queryLeads roundtrip
    void testLeadCRUD()
    {
        Database db(inMemoryConfig());
        QVERIFY(db.open());

        InvestigativeLead lead;
        lead.rank       = 1;
        lead.category   = QStringLiteral("series");
        lead.headline   = QStringLiteral("Test lead");
        lead.detail     = QStringLiteral("Detail");
        lead.confidence = 0.85;

        QVERIFY2(db.insertLead(lead, QStringLiteral("EVT1")),
                 qPrintable(QStringLiteral("insertLead failed: %1").arg(db.lastError())));
        const auto leads = db.queryLeads(QStringLiteral("EVT1"));
        QVERIFY2(!leads.isEmpty(), "queryLeads should return inserted lead");
    }

    // 10. crimeTypeCounts() returns correct count for inserted events
    void testCrimeTypeCountsCorrect()
    {
        Database db(inMemoryConfig());
        QVERIFY(db.open());
        for (int i = 0; i < 3; ++i) {
            auto ev = makeEvent(QStringLiteral("CT%1").arg(i));
            ev.crimeType = QStringLiteral("theft");
            QVERIFY(db.insertEvent(ev));
        }
        const auto counts = db.crimeTypeCounts();
        QVERIFY2(counts.contains(QStringLiteral("theft")),
                 "crimeTypeCounts should contain 'theft'");
        QCOMPARE(counts[QStringLiteral("theft")], 3);
    }
};

QTEST_MAIN(DatabaseMigrationDeepTest)
#include "test_database_migration_deep.moc"
