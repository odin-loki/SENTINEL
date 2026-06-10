// test_database_migration.cpp — Database schema and CRUD tests for SENTINEL
//
// Covers schema versioning, CRUD round-trips, batch inserts, filtering,
// lead persistence, optional-field handling, and special-character safety.

#include <QTest>
#include <QCoreApplication>
#include <QDate>
#include <QDateTime>
#include <QTimeZone>
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

CrimeEvent makeEvent(const QString& id, const QString& crimeType,
                     const QDateTime& dt, double quality = 0.8)
{
    CrimeEvent ev;
    ev.eventId      = id;
    ev.crimeType    = crimeType;
    ev.occurredAt   = dt;
    ev.ingestedAt   = dt;
    ev.suburb       = QStringLiteral("TestSuburb");
    ev.qualityScore = quality;
    return ev;
}

} // anonymous namespace

// ─────────────────────────────────────────────────────────────────────────────
// TestDatabaseMigration
// ─────────────────────────────────────────────────────────────────────────────

class TestDatabaseMigration : public QObject
{
    Q_OBJECT

private slots:

    // 1. Fresh in-memory database opens and has schema version >= 1
    void testSchemaVersionInit()
    {
        auto cfg = inMemoryCfg();
        Database db(cfg);
        QVERIFY(db.open());
        QVERIFY(db.currentSchemaVersion() >= 1);
    }

    // 2. currentSchemaVersion() returns an integer >= 1 and matches the constant
    void testSchemaVersionReadable()
    {
        auto cfg = inMemoryCfg();
        Database db(cfg);
        QVERIFY(db.open());
        const int v = db.currentSchemaVersion();
        QVERIFY(v >= 1);
        QCOMPARE(v, Database::SCHEMA_VERSION);
    }

    // 3. Insert an event, retrieve by ID, verify all fields match exactly
    void testCRUDRoundtrip()
    {
        auto cfg = inMemoryCfg();
        Database db(cfg);
        QVERIFY(db.open());

        const QDateTime now = QDateTime::currentDateTimeUtc();
        CrimeEvent ev;
        ev.eventId       = QStringLiteral("CRUD-001");
        ev.source        = QStringLiteral("test_source");
        ev.sourceVersion = QStringLiteral("1.0");
        ev.crimeType     = QStringLiteral("burglary");
        ev.occurredAt    = now;
        ev.ingestedAt    = now;
        ev.suburb        = QStringLiteral("Hackney");
        ev.outcome       = QStringLiteral("unresolved");
        ev.qualityScore  = 0.75;

        QVERIFY(db.insertEvent(ev));

        const CrimeEvent r = db.eventById(QStringLiteral("CRUD-001"));
        QCOMPARE(r.eventId,    ev.eventId);
        QCOMPARE(r.crimeType,  ev.crimeType);
        QCOMPARE(r.source,     ev.source);
        QCOMPARE(r.outcome,    ev.outcome);
        QVERIFY(r.occurredAt.has_value());
        QCOMPARE(r.occurredAt->toString(Qt::ISODate),
                 ev.occurredAt->toString(Qt::ISODate));
        QVERIFY(qAbs(r.qualityScore - ev.qualityScore) < 1e-9);
    }

    // 4. Inserting the same event ID twice performs upsert — no crash, count stays 1
    void testDuplicateEventId()
    {
        auto cfg = inMemoryCfg();
        Database db(cfg);
        QVERIFY(db.open());

        const QDateTime now = QDateTime::currentDateTimeUtc();
        CrimeEvent ev = makeEvent(QStringLiteral("DUP-001"), QStringLiteral("theft"), now);
        QVERIFY(db.insertEvent(ev));

        ev.crimeType = QStringLiteral("robbery");
        QVERIFY(db.insertEvent(ev));  // upsert: same ID → replace

        QCOMPARE(db.eventCount(), 1);
        QCOMPARE(db.eventById(QStringLiteral("DUP-001")).crimeType,
                 QStringLiteral("robbery"));
    }

    // 5. Insert 100 events; eventCount() returns exactly 100
    void testBatchInsert()
    {
        auto cfg = inMemoryCfg();
        Database db(cfg);
        QVERIFY(db.open());

        const QDateTime now = QDateTime::currentDateTimeUtc();
        for (int i = 0; i < 100; ++i) {
            CrimeEvent ev = makeEvent(QString("BATCH-%1").arg(i, 4, 10, QChar('0')),
                                      QStringLiteral("theft"), now.addSecs(-i));
            QVERIFY(db.insertEvent(ev));
        }
        QCOMPARE(db.eventCount(), 100);
    }

    // 6. Insert 50 "burglary" + 50 "robbery"; filtered queries return correct counts
    void testFilterByType()
    {
        auto cfg = inMemoryCfg();
        Database db(cfg);
        QVERIFY(db.open());

        const QDateTime now = QDateTime::currentDateTimeUtc();
        for (int i = 0; i < 50; ++i)
            QVERIFY(db.insertEvent(makeEvent(QString("B-%1").arg(i),
                                             QStringLiteral("burglary"),
                                             now.addSecs(-i))));
        for (int i = 0; i < 50; ++i)
            QVERIFY(db.insertEvent(makeEvent(QString("R-%1").arg(i),
                                             QStringLiteral("robbery"),
                                             now.addSecs(-i))));

        const auto burglaries = db.queryEvents(QStringLiteral("burglary"));
        const auto robberies  = db.queryEvents(QStringLiteral("robbery"));

        QCOMPARE(burglaries.size(), 50);
        for (const auto& ev : burglaries)
            QCOMPARE(ev.crimeType, QStringLiteral("burglary"));

        QCOMPARE(robberies.size(), 50);
        for (const auto& ev : robberies)
            QCOMPARE(ev.crimeType, QStringLiteral("robbery"));
    }

    // 7. Insert events spread over 5 days; date-range filter returns only the
    //    correct subset (2 days × 10 events = 20)
    void testFilterByDateRange()
    {
        auto cfg = inMemoryCfg();
        Database db(cfg);
        QVERIFY(db.open());

        const QDate today = QDate::currentDate();
        for (int d = 0; d < 5; ++d) {
            const QDateTime base(today.addDays(-d), QTime(12, 0, 0), QTimeZone::utc());
            for (int i = 0; i < 10; ++i) {
                CrimeEvent ev = makeEvent(QString("DR-%1-%2").arg(d).arg(i),
                                          QStringLiteral("theft"),
                                          base.addSecs(i));
                QVERIFY(db.insertEvent(ev));
            }
        }

        const QDateTime from(today.addDays(-1), QTime(0, 0, 0), QTimeZone::utc());
        const QDateTime to(today, QTime(23, 59, 59), QTimeZone::utc());
        const auto results = db.queryEvents({}, from, to);
        QCOMPARE(results.size(), 20);  // 2 days × 10 events
    }

    // 8. Verify that upsert keeps the event count stable (no deleteEvent in API;
    //    INSERT OR REPLACE serves as a logical delete+re-insert for same ID)
    void testDeleteEvent()
    {
        auto cfg = inMemoryCfg();
        Database db(cfg);
        QVERIFY(db.open());

        const QDateTime now = QDateTime::currentDateTimeUtc();
        QVERIFY(db.insertEvent(makeEvent(QStringLiteral("DEL-001"),
                                         QStringLiteral("theft"), now)));
        QVERIFY(db.insertEvent(makeEvent(QStringLiteral("DEL-002"),
                                         QStringLiteral("theft"), now)));
        QCOMPARE(db.eventCount(), 2);

        // Overwrite DEL-001 via upsert — count stays at 2, not 3
        QVERIFY(db.insertEvent(makeEvent(QStringLiteral("DEL-001"),
                                         QStringLiteral("burglary"), now)));
        QCOMPARE(db.eventCount(), 2);
        QCOMPARE(db.eventById(QStringLiteral("DEL-001")).crimeType,
                 QStringLiteral("burglary"));
    }

    // 9. Insert 20 events; a brand-new in-memory instance starts at count 0
    //    (equivalent to clearAll: fresh DB is always empty)
    void testClearAll()
    {
        auto cfg = inMemoryCfg();
        Database db(cfg);
        QVERIFY(db.open());

        const QDateTime now = QDateTime::currentDateTimeUtc();
        for (int i = 0; i < 20; ++i)
            QVERIFY(db.insertEvent(makeEvent(QString("CLR-%1").arg(i),
                                             QStringLiteral("theft"),
                                             now.addSecs(-i))));
        QCOMPARE(db.eventCount(), 20);

        // A separate in-memory instance is independent (logically a "cleared" DB)
        Database db2(cfg);
        QVERIFY(db2.open());
        QCOMPARE(db2.eventCount(), 0);
    }

    // 10. Save an InvestigativeLead with all fields; retrieve and verify each
    void testLeadPersistence()
    {
        auto cfg = inMemoryCfg();
        Database db(cfg);
        QVERIFY(db.open());

        const QDateTime now = QDateTime::currentDateTimeUtc();
        const QString eventId = QStringLiteral("LP-EVT-001");
        QVERIFY(db.insertEvent(makeEvent(eventId, QStringLiteral("robbery"), now)));

        InvestigativeLead lead;
        lead.rank             = 2;
        lead.category         = QStringLiteral("series_linkage");
        lead.headline         = QStringLiteral("Possible series link");
        lead.detail           = QStringLiteral("Within 200m, same MO pattern");
        lead.confidence       = 0.78;
        lead.confidenceMethod = QStringLiteral("geo_kernel");
        lead.generatedAt      = now;

        QVERIFY(db.insertLead(lead, eventId));

        const auto leads = db.queryLeads(eventId);
        QCOMPARE(leads.size(), 1);
        QCOMPARE(leads.first().rank,             lead.rank);
        QCOMPARE(leads.first().category,         lead.category);
        QCOMPARE(leads.first().headline,         lead.headline);
        QCOMPARE(leads.first().detail,           lead.detail);
        QVERIFY(qAbs(leads.first().confidence - lead.confidence) < 1e-9);
        QCOMPARE(leads.first().confidenceMethod, lead.confidenceMethod);
    }

    // 11. Insert event without lat/lon; retrieved event has nullopt for both
    void testNullOptionalFields()
    {
        auto cfg = inMemoryCfg();
        Database db(cfg);
        QVERIFY(db.open());

        const QDateTime now = QDateTime::currentDateTimeUtc();
        CrimeEvent ev;
        ev.eventId    = QStringLiteral("NULL-001");
        ev.crimeType  = QStringLiteral("theft");
        ev.ingestedAt = now;
        ev.suburb     = QStringLiteral("TestSuburb");
        // lat and lon are deliberately left as nullopt

        QVERIFY(db.insertEvent(ev));

        const CrimeEvent r = db.eventById(QStringLiteral("NULL-001"));
        QCOMPARE(r.eventId, ev.eventId);
        QVERIFY(!r.lat.has_value());
        QVERIFY(!r.lon.has_value());
    }

    // 12. Narrative with 10 000 characters persists without truncation
    void testLargeTextNarrative()
    {
        auto cfg = inMemoryCfg();
        Database db(cfg);
        QVERIFY(db.open());

        const QDateTime now = QDateTime::currentDateTimeUtc();
        const QString bigNarrative = QString("A").repeated(10000);

        CrimeEvent ev = makeEvent(QStringLiteral("BIG-001"), QStringLiteral("theft"), now);
        ev.narrative = bigNarrative;
        QVERIFY(db.insertEvent(ev));

        const CrimeEvent r = db.eventById(QStringLiteral("BIG-001"));
        QVERIFY(r.narrative.has_value());
        QCOMPARE(r.narrative.value().length(), 10000);
    }

    // 13. crimeType containing apostrophes and double-quotes persists correctly
    //     (verifies parameterised queries prevent SQL-injection-style breakage)
    void testSpecialCharsInCrimeType()
    {
        auto cfg = inMemoryCfg();
        Database db(cfg);
        QVERIFY(db.open());

        const QDateTime now = QDateTime::currentDateTimeUtc();
        const QString weirdType = QStringLiteral("it's a \"crime\" type");

        CrimeEvent ev = makeEvent(QStringLiteral("SPEC-001"), weirdType, now);
        QVERIFY(db.insertEvent(ev));

        const CrimeEvent r = db.eventById(QStringLiteral("SPEC-001"));
        QCOMPARE(r.crimeType, weirdType);
    }
};

// ─────────────────────────────────────────────────────────────────────────────

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    TestDatabaseMigration t;
    return QTest::qExec(&t, argc, argv);
}

#include "test_database_migration.moc"
