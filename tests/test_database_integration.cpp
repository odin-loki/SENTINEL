// test_database_integration.cpp — Full Database integration tests for SENTINEL
//
// Tests the Database class with in-memory SQLite, covering CRUD, queries,
// leads, audit, stats helpers, schema versioning, and edge cases.

#include <QTest>
#include <QCoreApplication>
#include <QDate>
#include <QDateTime>
#include <QMap>
#include <QString>
#include <QVector>
#include <cmath>
#include <numeric>

#include <QTimeZone>
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
// TestDatabaseIntegration
// ─────────────────────────────────────────────────────────────────────────────

class TestDatabaseIntegration : public QObject
{
    Q_OBJECT

private slots:

    // 1. Insert event, retrieve by ID, verify all canonical fields match
    void testInsertAndRetrieveById()
    {
        auto cfg = inMemoryCfg();
        Database db(cfg);
        QVERIFY(db.open());

        const QDateTime now = QDateTime::currentDateTimeUtc();
        CrimeEvent ev = makeEvent(QStringLiteral("EVT-001"), QStringLiteral("burglary"), now);
        ev.source         = QStringLiteral("test_source");
        ev.sourceVersion  = QStringLiteral("1.0");
        ev.outcome        = QStringLiteral("unresolved");

        QVERIFY(db.insertEvent(ev));

        const CrimeEvent retrieved = db.eventById(QStringLiteral("EVT-001"));
        QCOMPARE(retrieved.eventId,   ev.eventId);
        QCOMPARE(retrieved.crimeType, ev.crimeType);
        QCOMPARE(retrieved.source,    ev.source);
        QCOMPARE(retrieved.outcome,   ev.outcome);
        QVERIFY(retrieved.occurredAt.has_value());
        QCOMPARE(retrieved.occurredAt->toString(Qt::ISODate),
                 ev.occurredAt->toString(Qt::ISODate));
    }

    // 2. CrimeEvent with all optional fields set; retrieve and compare
    void testInsertAllFieldsRoundtrip()
    {
        auto cfg = inMemoryCfg();
        Database db(cfg);
        QVERIFY(db.open());

        const QDateTime now = QDateTime::currentDateTimeUtc();
        CrimeEvent ev;
        ev.eventId            = QStringLiteral("FULL-001");
        ev.source             = QStringLiteral("police_db");
        ev.sourceVersion      = QStringLiteral("2.3");
        ev.ingestedAt         = now;
        ev.occurredAt         = now.addSecs(-3600);
        ev.reportedAt         = now.addSecs(-1800);
        ev.crimeType          = QStringLiteral("robbery");
        ev.crimeSubtype       = QStringLiteral("street_robbery");
        ev.locationRaw        = QStringLiteral("14 Baker Street, London");
        ev.lat                = 51.5227;
        ev.lon                = -0.1571;
        ev.addressNormalised  = QStringLiteral("14 Baker St, London, NW1");
        ev.lga                = QStringLiteral("Westminster");
        ev.suburb             = QStringLiteral("Marylebone");
        ev.narrative          = QStringLiteral("Suspect approached victim on foot.");
        ev.outcome            = QStringLiteral("resolved");
        ev.conviction         = true;
        ev.suspectCount       = 2;
        ev.victimCount        = 1;
        ev.weapon             = QStringLiteral("knife");
        ev.qualityScore       = 0.92;

        QVERIFY(db.insertEvent(ev));

        const CrimeEvent r = db.eventById(QStringLiteral("FULL-001"));
        QCOMPARE(r.eventId,                            ev.eventId);
        QCOMPARE(r.source,                             ev.source);
        QCOMPARE(r.crimeType,                          ev.crimeType);
        QVERIFY(r.crimeSubtype.has_value());
        QCOMPARE(r.crimeSubtype.value(),               ev.crimeSubtype.value());
        QVERIFY(r.locationRaw.has_value());
        QCOMPARE(r.locationRaw.value(),                ev.locationRaw.value());
        QVERIFY(r.lat.has_value());
        QVERIFY(qAbs(r.lat.value()  - ev.lat.value())  < 1e-9);
        QVERIFY(r.lon.has_value());
        QVERIFY(qAbs(r.lon.value()  - ev.lon.value())  < 1e-9);
        QVERIFY(r.addressNormalised.has_value());
        QCOMPARE(r.addressNormalised.value(),          ev.addressNormalised.value());
        QVERIFY(r.lga.has_value());
        QCOMPARE(r.lga.value(),                        ev.lga.value());
        QCOMPARE(r.suburb,                             ev.suburb);
        QVERIFY(r.narrative.has_value());
        QCOMPARE(r.narrative.value(),                  ev.narrative.value());
        QVERIFY(r.conviction.has_value());
        QCOMPARE(r.conviction.value(),                 true);
        QVERIFY(r.suspectCount.has_value());
        QCOMPARE(r.suspectCount.value(),               2);
        QVERIFY(r.victimCount.has_value());
        QCOMPARE(r.victimCount.value(),                1);
        QVERIFY(r.weapon.has_value());
        QCOMPARE(r.weapon.value(),                     ev.weapon.value());
        QVERIFY(qAbs(r.qualityScore - ev.qualityScore) < 1e-9);
    }

    // 3. Insert then update event; verify new values
    void testUpdateEvent()
    {
        auto cfg = inMemoryCfg();
        Database db(cfg);
        QVERIFY(db.open());

        const QDateTime now = QDateTime::currentDateTimeUtc();
        CrimeEvent ev = makeEvent(QStringLiteral("UPD-001"), QStringLiteral("theft"), now);
        ev.outcome = QStringLiteral("unresolved");
        QVERIFY(db.insertEvent(ev));

        // Update: change outcome and crimeType
        ev.crimeType = QStringLiteral("robbery");
        ev.outcome   = QStringLiteral("resolved");
        QVERIFY(db.updateEvent(ev));

        const CrimeEvent updated = db.eventById(QStringLiteral("UPD-001"));
        QCOMPARE(updated.crimeType, QStringLiteral("robbery"));
        QCOMPARE(updated.outcome,   QStringLiteral("resolved"));
        QCOMPARE(db.eventCount(), 1);  // only one row: replaced, not duplicated
    }

    // 4. Insert 30 events over 30 days, query last 10, verify count
    void testQueryByDateRange()
    {
        auto cfg = inMemoryCfg();
        Database db(cfg);
        QVERIFY(db.open());

        const QDate today = QDate::currentDate();
        for (int i = 0; i < 30; ++i) {
            QDateTime dt(today.addDays(-i), QTime(12, 0, 0), QTimeZone::utc());
            CrimeEvent ev = makeEvent(QString("DR-%1").arg(i), "burglary", dt);
            QVERIFY(db.insertEvent(ev));
        }

        // Query events from 9 days ago through today (10 days inclusive)
        const QDateTime from(today.addDays(-9), QTime(0, 0, 0), QTimeZone::utc());
        const QDateTime to(today,              QTime(23, 59, 59), QTimeZone::utc());
        const auto results = db.queryEvents({}, from, to);
        QCOMPARE(results.size(), 10);
    }

    // 5. Mixed crime types, query for "burglary" only, verify all match
    void testQueryByCrimeType()
    {
        auto cfg = inMemoryCfg();
        Database db(cfg);
        QVERIFY(db.open());

        const QDateTime now = QDateTime::currentDateTimeUtc();
        for (int i = 0; i < 7; ++i) {
            CrimeEvent ev = makeEvent(QString("B-%1").arg(i), "burglary",
                                      now.addSecs(-i * 60));
            QVERIFY(db.insertEvent(ev));
        }
        for (int i = 0; i < 5; ++i) {
            CrimeEvent ev = makeEvent(QString("T-%1").arg(i), "theft",
                                      now.addSecs(-i * 60));
            QVERIFY(db.insertEvent(ev));
        }
        for (int i = 0; i < 3; ++i) {
            CrimeEvent ev = makeEvent(QString("A-%1").arg(i), "assault",
                                      now.addSecs(-i * 60));
            QVERIFY(db.insertEvent(ev));
        }

        const auto burglaries = db.queryEvents(QStringLiteral("burglary"));
        QCOMPARE(burglaries.size(), 7);
        for (const auto& ev : burglaries)
            QCOMPARE(ev.crimeType, QStringLiteral("burglary"));
    }

    // 6. Events at various UK coordinates, query for London bbox, verify subset
    void testQueryByBoundingBox()
    {
        auto cfg = inMemoryCfg();
        Database db(cfg);
        QVERIFY(db.open());

        const QDateTime now = QDateTime::currentDateTimeUtc();

        // 3 events in London (lat ~51.4-51.6, lon ~-0.4 to 0.0)
        const std::vector<std::pair<double,double>> londonCoords = {
            {51.50, -0.10}, {51.42, -0.30}, {51.58, 0.00}
        };
        for (int i = 0; i < 3; ++i) {
            CrimeEvent ev = makeEvent(QString("LON-%1").arg(i), "theft", now);
            ev.lat = londonCoords[i].first;
            ev.lon = londonCoords[i].second;
            QVERIFY(db.insertEvent(ev));
        }

        // 2 events in Manchester (outside London bbox)
        const std::vector<std::pair<double,double>> manchCoords = {
            {53.48, -2.24}, {53.50, -2.18}
        };
        for (int i = 0; i < 2; ++i) {
            CrimeEvent ev = makeEvent(QString("MAN-%1").arg(i), "theft", now);
            ev.lat = manchCoords[i].first;
            ev.lon = manchCoords[i].second;
            QVERIFY(db.insertEvent(ev));
        }

        // Query London bounding box
        const auto londonEvents = db.queryEvents(
            {}, QDateTime{}, QDateTime{},
            51.3, 51.7,   // latMin, latMax
            -0.5, 0.1     // lonMin, lonMax
        );
        QCOMPARE(londonEvents.size(), 3);
        for (const auto& ev : londonEvents) {
            QVERIFY(ev.latitude >= 51.3 && ev.latitude <= 51.7);
        }
    }

    // 7. Events with different narratives, search for "window", verify matches
    void testQueryWithKeywordSearch()
    {
        auto cfg = inMemoryCfg();
        Database db(cfg);
        QVERIFY(db.open());

        const QDate today = QDate::currentDate();

        CrimeEvent ev1 = makeEvent("KW-001", "burglary",
            QDateTime(today, QTime(10, 0, 0), QTimeZone::utc()));
        ev1.narrative = QStringLiteral("Suspect forced entry through a rear window.");
        QVERIFY(db.insertEvent(ev1));

        CrimeEvent ev2 = makeEvent("KW-002", "burglary",
            QDateTime(today, QTime(11, 0, 0), QTimeZone::utc()));
        ev2.narrative = QStringLiteral("Broken window found on ground floor.");
        QVERIFY(db.insertEvent(ev2));

        CrimeEvent ev3 = makeEvent("KW-003", "theft",
            QDateTime(today, QTime(12, 0, 0), QTimeZone::utc()));
        ev3.narrative = QStringLiteral("Shoplifting incident at retail premises.");
        QVERIFY(db.insertEvent(ev3));

        // Search for "window" — should return ev1 and ev2 only
        const auto results = db.queryEvents({}, today.addDays(-1), today.addDays(1),
                                             QStringLiteral("window"));
        QCOMPARE(results.size(), 2);
        for (const auto& ev : results) {
            QVERIFY(ev.narrative.has_value());
            QVERIFY(ev.narrative.value().contains(QStringLiteral("window"),
                                                   Qt::CaseInsensitive));
        }
    }

    // 8. Insert 50 events, eventCount() == 50
    void testEventCount()
    {
        auto cfg = inMemoryCfg();
        Database db(cfg);
        QVERIFY(db.open());

        const QDateTime now = QDateTime::currentDateTimeUtc();
        for (int i = 0; i < 50; ++i) {
            CrimeEvent ev = makeEvent(QString("CNT-%1").arg(i), "theft",
                                      now.addSecs(-i));
            QVERIFY(db.insertEvent(ev));
        }
        QCOMPARE(db.eventCount(), 50);
    }

    // 9. Insert investigative lead with eventId, retrieve via queryLeads
    void testInsertLead()
    {
        auto cfg = inMemoryCfg();
        Database db(cfg);
        QVERIFY(db.open());

        const QString eventId = QStringLiteral("LD-EVT-001");
        const QDateTime now = QDateTime::currentDateTimeUtc();
        CrimeEvent ev = makeEvent(eventId, "burglary", now);
        QVERIFY(db.insertEvent(ev));

        InvestigativeLead lead;
        lead.rank             = 1;
        lead.category         = QStringLiteral("series_linkage");
        lead.headline         = QStringLiteral("Linked to known burglary series.");
        lead.detail           = QStringLiteral("Spatial distance: 150 m");
        lead.confidence       = 0.85;
        lead.confidenceMethod = QStringLiteral("composite_kernel");
        lead.generatedAt      = now;
        QVERIFY(db.insertLead(lead, eventId));

        const auto leads = db.queryLeads(eventId);
        QCOMPARE(leads.size(), 1);
        QCOMPARE(leads.first().category,    QStringLiteral("series_linkage"));
        QCOMPARE(leads.first().headline,    lead.headline);
        QVERIFY(qAbs(leads.first().confidence - lead.confidence) < 1e-9);
    }

    // 10. 3 events, 2 leads for event A, 1 for event B, verify separate retrieval
    void testQueryLeadsForEvent()
    {
        auto cfg = inMemoryCfg();
        Database db(cfg);
        QVERIFY(db.open());

        const QDateTime now = QDateTime::currentDateTimeUtc();
        const QString evA = QStringLiteral("QLF-A");
        const QString evB = QStringLiteral("QLF-B");
        const QString evC = QStringLiteral("QLF-C");

        QVERIFY(db.insertEvent(makeEvent(evA, "theft", now)));
        QVERIFY(db.insertEvent(makeEvent(evB, "theft", now)));
        QVERIFY(db.insertEvent(makeEvent(evC, "theft", now)));

        auto makeLead = [&](int rank, const QString& cat) {
            InvestigativeLead l;
            l.rank        = rank;
            l.category    = cat;
            l.headline    = QStringLiteral("Headline");
            l.confidence  = 0.7;
            l.generatedAt = now;
            return l;
        };

        QVERIFY(db.insertLead(makeLead(1, "series_linkage"),  evA));
        QVERIFY(db.insertLead(makeLead(2, "mo_similarity"),   evA));
        QVERIFY(db.insertLead(makeLead(1, "geographic_profile"), evB));

        QCOMPARE(db.queryLeads(evA).size(), 2);
        QCOMPARE(db.queryLeads(evB).size(), 1);
        QCOMPARE(db.queryLeads(evC).size(), 0);
    }

    // 11. Insert audit entry, verify in queryAudit
    void testInsertAuditEntry()
    {
        auto cfg = inMemoryCfg();
        Database db(cfg);
        QVERIFY(db.open());

        const QString eventId = QStringLiteral("AUD-001");
        QVERIFY(db.insertAuditEntry(eventId, QStringLiteral("viewed"),
                                    QStringLiteral("User viewed event details")));

        const auto entries = db.queryAudit();
        QVERIFY(!entries.isEmpty());

        // Find our entry
        bool found = false;
        for (const auto& entry : entries) {
            if (std::get<1>(entry) == eventId &&
                std::get<2>(entry) == QStringLiteral("viewed")) {
                found = true;
                QCOMPARE(std::get<3>(entry), QStringLiteral("User viewed event details"));
                break;
            }
        }
        QVERIFY(found);
    }

    // 12. 10 burglaries + 5 thefts, getCrimeTypeCounts() correct
    void testCrimeTypeCounts()
    {
        auto cfg = inMemoryCfg();
        Database db(cfg);
        QVERIFY(db.open());

        const QDateTime now = QDateTime::currentDateTimeUtc();
        for (int i = 0; i < 10; ++i)
            QVERIFY(db.insertEvent(makeEvent(QString("CTC-B%1").arg(i), "burglary", now)));
        for (int i = 0; i < 5; ++i)
            QVERIFY(db.insertEvent(makeEvent(QString("CTC-T%1").arg(i), "theft", now)));

        const auto counts = db.getCrimeTypeCounts();
        QCOMPARE(counts.value(QStringLiteral("burglary")), 10);
        QCOMPARE(counts.value(QStringLiteral("theft")), 5);
    }

    // 13. Insert events at specific hours, verify hourly distribution total
    void testGetHourlyCounts()
    {
        auto cfg = inMemoryCfg();
        Database db(cfg);
        QVERIFY(db.open());

        // Insert 5 events exactly 1 day ago (within 30-day window)
        const QDateTime baseTime = QDateTime::currentDateTimeUtc().addDays(-1);
        for (int i = 0; i < 5; ++i) {
            CrimeEvent ev = makeEvent(QString("HC-%1").arg(i), "theft", baseTime);
            QVERIFY(db.insertEvent(ev));
        }

        const auto counts = db.getHourlyCounts(30);
        QCOMPARE(counts.size(), 24);

        const int total = std::accumulate(counts.constBegin(), counts.constEnd(), 0);
        QCOMPARE(total, 5);
    }

    // 14. Insert events over 7 days, verify getDailyTrend() has 7 entries
    void testGetDailyTrend()
    {
        auto cfg = inMemoryCfg();
        Database db(cfg);
        QVERIFY(db.open());

        const QDate today = QDate::currentDate();
        for (int i = 0; i < 7; ++i) {
            QDateTime dt(today.addDays(-i), QTime(12, 0, 0), QTimeZone::utc());
            CrimeEvent ev = makeEvent(QString("DT-%1").arg(i), "burglary", dt);
            QVERIFY(db.insertEvent(ev));
        }

        const auto trend = db.getDailyTrend(7);
        QCOMPARE(trend.size(), 7);

        // Each entry should have count 1
        for (const auto& [date, count] : trend)
            QCOMPARE(count, 1);
    }

    // 15. Insert 10 events with quality 0.1..1.0, verify average
    void testAverageQualityScore()
    {
        auto cfg = inMemoryCfg();
        Database db(cfg);
        QVERIFY(db.open());

        const QDateTime now = QDateTime::currentDateTimeUtc();
        for (int i = 1; i <= 10; ++i) {
            CrimeEvent ev = makeEvent(QString("QS-%1").arg(i), "theft",
                                      now, i * 0.1);
            QVERIFY(db.insertEvent(ev));
        }

        // Average of 0.1 + 0.2 + ... + 1.0 = 5.5 / 10 = 0.55
        const double avg = db.getAverageQualityScore();
        QVERIFY(qAbs(avg - 0.55) < 0.001);
    }

    // 16. currentSchemaVersion() == 3 after open()
    void testSchemaVersionAfterOpen()
    {
        auto cfg = inMemoryCfg();
        Database db(cfg);
        QVERIFY(db.open());
        QCOMPARE(db.currentSchemaVersion(), Database::SCHEMA_VERSION);
        QCOMPARE(db.currentSchemaVersion(), 3);
    }

    // 17. migrateSchema(1) runs without error on an already-open database
    void testSchemaMigration()
    {
        auto cfg = inMemoryCfg();
        Database db(cfg);
        QVERIFY(db.open());
        // Schema is already at v3; calling migrate(1) re-runs safe migration paths
        QVERIFY(db.migrateSchema(1));
    }

    // 18. Inserting event with same ID twice should replace gracefully
    void testDuplicateEventId()
    {
        auto cfg = inMemoryCfg();
        Database db(cfg);
        QVERIFY(db.open());

        const QDateTime now = QDateTime::currentDateTimeUtc();
        CrimeEvent ev = makeEvent(QStringLiteral("DUP-001"), QStringLiteral("theft"), now);

        QVERIFY(db.insertEvent(ev));
        ev.crimeType = QStringLiteral("burglary");  // mutate before second insert
        QVERIFY(db.insertEvent(ev));  // same ID → INSERT OR REPLACE

        // Exactly one row — the second insert replaced the first
        QCOMPARE(db.eventCount(), 1);
        QCOMPARE(db.eventById("DUP-001").crimeType, QStringLiteral("burglary"));
    }

    // 19. Insert 200 events, query with limit=10, verify exactly 10 returned
    void testQueryLimit()
    {
        auto cfg = inMemoryCfg();
        Database db(cfg);
        QVERIFY(db.open());

        const QDateTime now = QDateTime::currentDateTimeUtc();
        for (int i = 0; i < 200; ++i) {
            CrimeEvent ev = makeEvent(QString("LIM-%1").arg(i, 4, 10, QChar('0')),
                                      "theft", now.addSecs(-i));
            QVERIFY(db.insertEvent(ev));
        }

        const auto results = db.queryEvents({}, QDateTime{}, QDateTime{},
                                             -90, 90, -180, 180, 10);
        QCOMPARE(results.size(), 10);
    }

    // 20. Two separate in-memory Database instances are independent
    void testMultipleDatabaseInstances()
    {
        auto cfg = inMemoryCfg();
        Database db1(cfg);
        Database db2(cfg);
        QVERIFY(db1.open());
        QVERIFY(db2.open());

        const QDateTime now = QDateTime::currentDateTimeUtc();
        CrimeEvent ev = makeEvent(QStringLiteral("MULTI-001"), "theft", now);
        QVERIFY(db1.insertEvent(ev));

        // db2 is a separate in-memory database — should see 0 events
        QCOMPARE(db1.eventCount(), 1);
        QCOMPARE(db2.eventCount(), 0);
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Helper for running a named test object
// ─────────────────────────────────────────────────────────────────────────────

static int runTest(QObject* obj, const QString& logFile)
{
    QStringList args = { QStringLiteral("test"), QStringLiteral("-o"),
                         QStringLiteral("%1,txt").arg(logFile) };
    return QTest::qExec(obj, args);
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    int r = 0;
    { TestDatabaseIntegration t; r |= runTest(&t, "db_integration.txt"); }
    return r;
}

#include "test_database_integration.moc"
