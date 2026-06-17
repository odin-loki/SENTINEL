// test_database_deep.cpp
// Deep tests for Database: schema versioning, CRUD, query filters,
// lead insertion, audit log, and stats helpers.
#include <QTest>
#include <QTimeZone>
#include "core/Database.h"
#include "core/AppConfig.h"
#include "core/CrimeEvent.h"
#include <QTemporaryFile>
#include <QUuid>

class DatabaseDeepTest : public QObject
{
    Q_OBJECT

private:
    static AppConfig inMemoryConfig()
    {
        AppConfig cfg;
        cfg.databasePath = QStringLiteral(":memory:");
        return cfg;
    }

    static CrimeEvent makeEvent(const QString& id,
                                 const QString& type = QStringLiteral("burglary"))
    {
        CrimeEvent ev;
        ev.eventId   = id;
        ev.id        = id;
        ev.crimeType = type;
        ev.suburb    = QStringLiteral("Soho");
        ev.lat       = 51.5074;
        ev.lon       = -0.1278;
        ev.latitude  = 51.5074;
        ev.longitude = -0.1278;
        ev.source    = QStringLiteral("test");
        const QDateTime dt = QDateTime(QDate(2024, 3, 15), QTime(22, 0, 0), QTimeZone::utc());
        ev.occurredAt = dt;
        ev.timestamp  = dt;
        ev.qualityScore = 0.85;
        return ev;
    }

    static InvestigativeLead makeLead(const QString& headline, double conf)
    {
        InvestigativeLead l;
        l.rank       = 1;
        l.headline   = headline;
        l.category   = QStringLiteral("series");
        l.detail     = QStringLiteral("Lead detail");
        l.confidence = conf;
        l.generatedAt = QDateTime::currentDateTimeUtc();
        return l;
    }

private slots:

    // ── 1. open() returns true for :memory: ───────────────────────────────────
    void testOpenInMemory()
    {
        Database db(inMemoryConfig());
        QVERIFY2(db.open(), qPrintable(db.lastError()));
        QVERIFY(db.isOpen());
    }

    // ── 2. eventCount() is 0 on fresh DB ────────────────────────────────────
    void testEventCountInitially()
    {
        Database db(inMemoryConfig());
        db.open();
        QCOMPARE(db.eventCount(), 0);
    }

    // ── 3. insertEvent + eventCount() reflects it ────────────────────────────
    void testInsertEvent()
    {
        Database db(inMemoryConfig());
        db.open();
        const bool ok = db.insertEvent(makeEvent(QStringLiteral("EVT-001")));
        QVERIFY2(ok, qPrintable(db.lastError()));
        QCOMPARE(db.eventCount(), 1);
    }

    // ── 4. queryEvents returns inserted events ───────────────────────────────
    void testQueryEventsReturnsAll()
    {
        Database db(inMemoryConfig());
        db.open();
        db.insertEvent(makeEvent(QStringLiteral("E1"), QStringLiteral("burglary")));
        db.insertEvent(makeEvent(QStringLiteral("E2"), QStringLiteral("robbery")));

        const auto evs = db.queryEvents();
        QVERIFY2(evs.size() >= 2,
                 qPrintable(QStringLiteral("Expected >= 2 events, got %1").arg(evs.size())));
    }

    // ── 5. queryEvents filters by crimeType ─────────────────────────────────
    void testQueryEventsByCrimeType()
    {
        Database db(inMemoryConfig());
        db.open();
        db.insertEvent(makeEvent(QStringLiteral("E1"), QStringLiteral("burglary")));
        db.insertEvent(makeEvent(QStringLiteral("E2"), QStringLiteral("robbery")));

        const auto evs = db.queryEvents(QStringLiteral("burglary"));
        QVERIFY2(evs.size() == 1,
                 qPrintable(QStringLiteral("Expected 1 burglary, got %1").arg(evs.size())));
    }

    // ── 6. eventById: retrieves correct event ────────────────────────────────
    void testEventById()
    {
        Database db(inMemoryConfig());
        db.open();
        db.insertEvent(makeEvent(QStringLiteral("FIND-ME"), QStringLiteral("theft")));

        const auto ev = db.eventById(QStringLiteral("FIND-ME"));
        QVERIFY2(!ev.eventId.isEmpty(), "eventById should return an event");
        QVERIFY2(ev.crimeType == QStringLiteral("theft"),
                 qPrintable(QStringLiteral("crimeType %1 expected theft").arg(ev.crimeType)));
    }

    // ── 7. insertLead + queryLeads ───────────────────────────────────────────
    void testInsertAndQueryLead()
    {
        Database db(inMemoryConfig());
        db.open();
        const QString evtId = QStringLiteral("E-LEAD");
        db.insertEvent(makeEvent(evtId));
        const bool ok = db.insertLead(makeLead(QStringLiteral("Test lead"), 0.8), evtId);
        QVERIFY2(ok, "insertLead should return true");

        const auto leads = db.queryLeads(evtId);
        QVERIFY2(leads.size() >= 1,
                 qPrintable(QStringLiteral("Expected >= 1 lead, got %1").arg(leads.size())));
    }

    // ── 8. insertAuditEntry + queryAudit ────────────────────────────────────
    void testAuditLog()
    {
        Database db(inMemoryConfig());
        db.open();
        db.insertEvent(makeEvent(QStringLiteral("E-AUDIT")));
        const bool ok = db.insertAuditEntry(
            QStringLiteral("E-AUDIT"),
            QStringLiteral("viewed"),
            QStringLiteral("Inspector opened record"));
        QVERIFY2(ok, "insertAuditEntry should return true");

        const auto log = db.queryAudit(100);
        QVERIFY2(!log.empty(), "queryAudit should return at least one entry");
    }

    // ── 9. crimeTypeCounts reflects insertions ───────────────────────────────
    void testCrimeTypeCounts()
    {
        Database db(inMemoryConfig());
        db.open();
        db.insertEvent(makeEvent(QStringLiteral("C1"), QStringLiteral("burglary")));
        db.insertEvent(makeEvent(QStringLiteral("C2"), QStringLiteral("burglary")));
        db.insertEvent(makeEvent(QStringLiteral("C3"), QStringLiteral("robbery")));

        const auto counts = db.crimeTypeCounts();
        QVERIFY2(counts.contains(QStringLiteral("burglary")), "burglary should be in counts");
        QVERIFY2(counts[QStringLiteral("burglary")] == 2,
                 qPrintable(QStringLiteral("burglary count %1 expected 2")
                    .arg(counts[QStringLiteral("burglary")])));
    }

    // ── 10. currentSchemaVersion returns a positive integer ─────────────────
    void testSchemaVersion()
    {
        Database db(inMemoryConfig());
        db.open();
        const int v = db.currentSchemaVersion();
        QVERIFY2(v > 0,
                 qPrintable(QStringLiteral("Schema version %1 should be > 0").arg(v)));
    }
};

QTEST_MAIN(DatabaseDeepTest)
#include "test_database_deep.moc"
