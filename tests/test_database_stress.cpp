// test_database_stress.cpp
// Iteration 6 audit: schema, CRUD, filters, leads, audit, stats, and AppConfig.
#include <QTest>
#include <QTimeZone>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QTemporaryFile>
#include <QFile>
#include <QSettings>
#include <cmath>

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

    static CrimeEvent makeEv(const QString& id,
                             const QString& type = QStringLiteral("burglary"),
                             const QDateTime& occurred = {},
                             double lat = 51.5,
                             double lon = -0.1,
                             double quality = 0.8)
    {
        CrimeEvent ev;
        ev.eventId      = id;
        ev.id           = id;
        ev.source       = QStringLiteral("stress_test");
        ev.crimeType    = type;
        ev.suburb       = QStringLiteral("TestSuburb");
        ev.lat          = lat;
        ev.lon          = lon;
        ev.latitude     = lat;
        ev.longitude    = lon;
        ev.qualityScore = quality;
        ev.ingestedAt   = QDateTime::currentDateTimeUtc();
        const QDateTime dt = occurred.isValid()
                                 ? occurred
                                 : QDateTime(QDate(2024, 6, 15), QTime(12, 0, 0),
                                             QTimeZone::utc());
        ev.occurredAt = dt;
        ev.timestamp  = dt;
        return ev;
    }

    static InvestigativeLead makeLead(const QString& headline)
    {
        InvestigativeLead lead;
        lead.rank             = 1;
        lead.category         = QStringLiteral("pattern");
        lead.headline         = headline;
        lead.detail           = QStringLiteral("Detail");
        lead.confidence       = 0.75;
        lead.confidenceMethod = QStringLiteral("test");
        lead.generatedAt      = QDateTime::currentDateTimeUtc();
        return lead;
    }

    static QString tempIniPath()
    {
        QTemporaryFile f(QStringLiteral("sentinel_stress_XXXXXX.ini"));
        f.setAutoRemove(false);
        if (!f.open())
            return {};
        const QString path = f.fileName();
        f.close();
        return path;
    }

    static QString tempDbPath()
    {
        QTemporaryFile f(QStringLiteral("sentinel_stress_XXXXXX.db"));
        f.setAutoRemove(false);
        if (!f.open())
            return {};
        const QString path = f.fileName();
        f.close();
        return path;
    }

    static bool createLegacyV1Database(const QString& path)
    {
        const QString conn = QStringLiteral("legacy_v1_setup");
        if (QSqlDatabase::contains(conn))
            QSqlDatabase::removeDatabase(conn);

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
        QSqlDatabase::removeDatabase(conn);
        return ok;
    }

private slots:

    // ── Schema ───────────────────────────────────────────────────────────────

    void testSchemaCreatedOnOpen()
    {
        Database db(memConfig());
        QVERIFY2(db.open(), qPrintable(db.lastError()));
        QVERIFY(db.isOpen());
        QVERIFY2(db.insertEvent(makeEv(QStringLiteral("schema-1"))),
                 qPrintable(db.lastError()));
        QCOMPARE(db.eventCount(), 1);
        QVERIFY2(db.insertAuditEntry(QStringLiteral("schema-1"),
                                     QStringLiteral("created"),
                                     QStringLiteral("test")),
                 qPrintable(db.lastError()));
        QVERIFY2(!db.queryAudit(1).isEmpty(), "audit_log table should exist");
    }

    void testSchemaMigrationFromV1()
    {
        const QString path = tempDbPath();
        if (path.isEmpty())
            QSKIP("Could not create temp database path");

        if (!createLegacyV1Database(path))
            QSKIP("Could not create legacy v1 schema");

        AppConfig cfg;
        cfg.databasePath = path;
        Database db(cfg);
        QVERIFY2(db.open(), qPrintable(db.lastError()));
        QCOMPARE(db.currentSchemaVersion(), Database::SCHEMA_VERSION);

        CrimeEvent ev = makeEv(QStringLiteral("migrated-1"));
        ev.qualityScore = 0.65;
        QVERIFY2(db.insertEvent(ev), qPrintable(db.lastError()));
        const CrimeEvent loaded = db.eventById(QStringLiteral("migrated-1"));
        QVERIFY(!loaded.eventId.isEmpty());
        QVERIFY(qAbs(loaded.qualityScore - 0.65) < 1e-9);

        QFile::remove(path);
    }

    void testSchemaMigrationIdempotent()
    {
        const QString path = tempDbPath();
        if (path.isEmpty())
            QSKIP("Could not create temp database path");

        AppConfig cfg;
        cfg.databasePath = path;

        {
            Database db(cfg);
            QVERIFY2(db.open(), qPrintable(db.lastError()));
            QVERIFY2(db.insertEvent(makeEv(QStringLiteral("idem-1"))),
                     qPrintable(db.lastError()));
        }
        {
            Database db(cfg);
            QVERIFY2(db.open(), qPrintable(db.lastError()));
            QCOMPARE(db.eventCount(), 1);
            QCOMPARE(db.currentSchemaVersion(), Database::SCHEMA_VERSION);
            QVERIFY2(db.open(), "Second open() on same instance should be safe");
        }

        QFile::remove(path);
    }

    void testSchemaVersionCorrect()
    {
        Database db(memConfig());
        QVERIFY(db.open());
        QCOMPARE(db.currentSchemaVersion(), Database::SCHEMA_VERSION);
    }

    // ── Insert + Query ───────────────────────────────────────────────────────

    void testInsertAndQueryEvent()
    {
        Database db(memConfig());
        db.open();
        const CrimeEvent ev = makeEv(QStringLiteral("ins-1"), QStringLiteral("theft"));
        QVERIFY2(db.insertEvent(ev), qPrintable(db.lastError()));
        const auto results = db.queryEvents();
        QCOMPARE(results.size(), 1);
        QCOMPARE(results.first().eventId, QStringLiteral("ins-1"));
    }

    void testInsertDuplicateId()
    {
        Database db(memConfig());
        db.open();
        auto ev = makeEv(QStringLiteral("dup-1"), QStringLiteral("theft"));
        QVERIFY(db.insertEvent(ev));
        ev.crimeType = QStringLiteral("robbery");
        QVERIFY2(db.insertEvent(ev), qPrintable(db.lastError()));
        QCOMPARE(db.eventCount(), 1);
        QCOMPARE(db.eventById(QStringLiteral("dup-1")).crimeType,
                 QStringLiteral("robbery"));
    }

    void testQueryFilterCrimeType()
    {
        Database db(memConfig());
        db.open();
        db.insertEvent(makeEv(QStringLiteral("b1"), QStringLiteral("burglary")));
        db.insertEvent(makeEv(QStringLiteral("b2"), QStringLiteral("burglary")));
        db.insertEvent(makeEv(QStringLiteral("r1"), QStringLiteral("robbery")));

        const auto burglaries = db.queryEvents(QStringLiteral("burglary"));
        QCOMPARE(burglaries.size(), 2);
        for (const auto& ev : burglaries)
            QCOMPARE(ev.crimeType, QStringLiteral("burglary"));
    }

    void testQueryFilterDateRange()
    {
        Database db(memConfig());
        db.open();

        const QDateTime early(QDate(2024, 1, 10), QTime(10, 0, 0), QTimeZone::utc());
        const QDateTime mid(QDate(2024, 3, 15), QTime(10, 0, 0), QTimeZone::utc());
        const QDateTime late(QDate(2024, 6, 20), QTime(10, 0, 0), QTimeZone::utc());

        db.insertEvent(makeEv(QStringLiteral("e-early"), QStringLiteral("theft"), early));
        db.insertEvent(makeEv(QStringLiteral("e-mid"), QStringLiteral("theft"), mid));
        db.insertEvent(makeEv(QStringLiteral("e-late"), QStringLiteral("theft"), late));

        const QDateTime from(QDate(2024, 3, 1), QTime(0, 0, 0), QTimeZone::utc());
        const QDateTime to(QDate(2024, 4, 1), QTime(23, 59, 59), QTimeZone::utc());
        const auto results = db.queryEvents(QString{}, from, to);
        QCOMPARE(results.size(), 1);
        QCOMPARE(results.first().eventId, QStringLiteral("e-mid"));
    }

    void testQueryFilterSpatialBounds()
    {
        Database db(memConfig());
        db.open();
        db.insertEvent(makeEv(QStringLiteral("in-bounds"), QStringLiteral("theft"),
                              {}, 51.50, -0.10));
        db.insertEvent(makeEv(QStringLiteral("out-bounds"), QStringLiteral("theft"),
                              {}, 48.85, 2.35));

        const auto results = db.queryEvents(
            QString{}, QDateTime{}, QDateTime{},
            51.0, 52.0, -1.0, 0.0);
        QCOMPARE(results.size(), 1);
        QCOMPARE(results.first().eventId, QStringLiteral("in-bounds"));
    }

    void testQueryLimitRespected()
    {
        Database db(memConfig());
        db.open();
        for (int i = 0; i < 10; ++i)
            db.insertEvent(makeEv(QStringLiteral("lim-%1").arg(i)));

        const auto results = db.queryEvents(
            QString{}, QDateTime{}, QDateTime{},
            -90, 90, -180, 180, 5);
        QCOMPARE(results.size(), 5);
    }

    void testQueryEventById()
    {
        Database db(memConfig());
        db.open();
        db.insertEvent(makeEv(QStringLiteral("find-me"), QStringLiteral("assault")));
        const CrimeEvent ev = db.eventById(QStringLiteral("find-me"));
        QVERIFY(!ev.eventId.isEmpty());
        QCOMPARE(ev.crimeType, QStringLiteral("assault"));
    }

    void testRowToEventOptionalFields()
    {
        Database db(memConfig());
        db.open();

        CrimeEvent ev;
        ev.eventId    = QStringLiteral("null-opt");
        ev.crimeType  = QStringLiteral("theft");
        ev.ingestedAt = QDateTime::currentDateTimeUtc();
        ev.suburb     = QStringLiteral("Test");
        // lat, lon, narrative, crimeSubtype deliberately unset

        QVERIFY(db.insertEvent(ev));
        const CrimeEvent loaded = db.eventById(QStringLiteral("null-opt"));
        QVERIFY(!loaded.lat.has_value());
        QVERIFY(!loaded.lon.has_value());
        QVERIFY(!loaded.narrative.has_value());
        QVERIFY(!loaded.crimeSubtype.has_value());
    }

    // ── Leads + Audit ────────────────────────────────────────────────────────

    void testInsertLead()
    {
        Database db(memConfig());
        db.open();
        const QString evtId = QStringLiteral("lead-evt");
        db.insertEvent(makeEv(evtId));
        QVERIFY2(db.insertLead(makeLead(QStringLiteral("Hot lead")), evtId),
                 qPrintable(db.lastError()));

        const auto leads = db.queryLeads(evtId);
        QCOMPARE(leads.size(), 1);
        QCOMPARE(leads.first().headline, QStringLiteral("Hot lead"));
    }

    void testAuditEntryInserted()
    {
        Database db(memConfig());
        db.open();
        const QString evtId = QStringLiteral("audit-evt");
        db.insertEvent(makeEv(evtId));
        QVERIFY2(db.insertAuditEntry(evtId, QStringLiteral("viewed"),
                                     QStringLiteral("Opened record")),
                 qPrintable(db.lastError()));

        const auto log = db.queryAudit(10);
        QVERIFY2(!log.isEmpty(), "queryAudit should return inserted entry");
        const auto& [ts, eid, action, detail] = log.first();
        Q_UNUSED(ts);
        QCOMPARE(eid, evtId);
        QCOMPARE(action, QStringLiteral("viewed"));
        QCOMPARE(detail, QStringLiteral("Opened record"));
    }

    void testAuditSortedDescending()
    {
        Database db(memConfig());
        db.open();
        db.insertEvent(makeEv(QStringLiteral("audit-sort")));

        db.insertAuditEntry(QStringLiteral("audit-sort"), QStringLiteral("first"),
                            QStringLiteral("oldest"));
        QTest::qWait(10);
        db.insertAuditEntry(QStringLiteral("audit-sort"), QStringLiteral("second"),
                            QStringLiteral("middle"));
        QTest::qWait(10);
        db.insertAuditEntry(QStringLiteral("audit-sort"), QStringLiteral("third"),
                            QStringLiteral("newest"));

        const auto log = db.queryAudit(10);
        QVERIFY(log.size() >= 3);
        QCOMPARE(std::get<2>(log.at(0)), QStringLiteral("third"));
        QCOMPARE(std::get<2>(log.at(1)), QStringLiteral("second"));
        QCOMPARE(std::get<2>(log.at(2)), QStringLiteral("first"));

        for (int i = 1; i < log.size(); ++i) {
            const QDateTime prev = std::get<0>(log.at(i - 1));
            const QDateTime cur  = std::get<0>(log.at(i));
            QVERIFY2(prev >= cur,
                     "queryAudit should be sorted by timestamp DESC");
        }
    }

    // ── Stats ────────────────────────────────────────────────────────────────

    void testCrimeTypeCountsAccurate()
    {
        Database db(memConfig());
        db.open();
        for (int i = 0; i < 3; ++i)
            db.insertEvent(makeEv(QStringLiteral("bur-%1").arg(i),
                                  QStringLiteral("burglary")));
        for (int i = 0; i < 2; ++i)
            db.insertEvent(makeEv(QStringLiteral("rob-%1").arg(i),
                                  QStringLiteral("robbery")));

        const auto counts = db.crimeTypeCounts();
        QCOMPARE(counts.value(QStringLiteral("burglary")), 3);
        QCOMPARE(counts.value(QStringLiteral("robbery")), 2);
    }

    void testDailyTrendNonEmpty()
    {
        Database db(memConfig());
        db.open();
        const QDate today = QDate::currentDate();
        for (int i = 0; i < 5; ++i) {
            const QDateTime dt(today.addDays(-i), QTime(14, 0, 0), QTimeZone::utc());
            db.insertEvent(makeEv(QStringLiteral("trend-%1").arg(i),
                                  QStringLiteral("theft"), dt));
        }
        const auto trend = db.getDailyTrend(30);
        QVERIFY2(!trend.isEmpty(), "getDailyTrend should return entries when events exist");
    }

    void testAverageQualityScore()
    {
        Database db(memConfig());
        db.open();
        db.insertEvent(makeEv(QStringLiteral("q1"), QStringLiteral("theft"), {}, 51.5, -0.1, 0.6));
        db.insertEvent(makeEv(QStringLiteral("q2"), QStringLiteral("theft"), {}, 51.5, -0.1, 0.8));
        db.insertEvent(makeEv(QStringLiteral("q3"), QStringLiteral("theft"), {}, 51.5, -0.1, 1.0));

        const double avg = db.getAverageQualityScore();
        QVERIFY(qAbs(avg - 0.8) < 1e-6);
    }

    void testAverageQualityScoreZeroRows()
    {
        Database db(memConfig());
        db.open();
        QCOMPARE(db.getAverageQualityScore(), 0.0);
    }

    // ── AppConfig ────────────────────────────────────────────────────────────

    void testAppConfigDefaults()
    {
        AppConfig cfg;
        cfg.databasePath = QStringLiteral(":memory:");
        QVERIFY2(cfg.validate(), "Default config with databasePath should validate");
        QCOMPARE(cfg.theme, QStringLiteral("dark"));
        QVERIFY(cfg.hawkesHistoryDays >= 7 && cfg.hawkesHistoryDays <= 365);
        QVERIFY(cfg.defaultRadius > 0.0);
        QVERIFY(cfg.alertElevated < cfg.alertHigh);
        QVERIFY(cfg.alertHigh < cfg.alertCritical);
    }

    void testAppConfigSaveLoad()
    {
        const QString path = tempIniPath();
        if (path.isEmpty())
            QSKIP("Could not create temp INI path");

        AppConfig orig;
        orig.databasePath           = QStringLiteral("/tmp/sentinel_test.db");
        orig.defaultLat             = 40.7128;
        orig.defaultLon             = -74.0060;
        orig.hawkesHistoryDays      = 180;
        orig.theme                  = QStringLiteral("light");
        orig.mapZoomLevel           = 11.0;
        orig.maxLeadCount           = 25;
        orig.ensemblePoissonWeight  = 0.6;
        orig.ensembleHawkesWeight   = 0.4;
        orig.saveTo(path);

        const AppConfig loaded = AppConfig::loadFrom(path);
        QFile::remove(path);

        auto near = [](double a, double b) { return std::abs(a - b) < 1e-4; };
        QVERIFY2(near(loaded.defaultLat, orig.defaultLat), "defaultLat roundtrip");
        QVERIFY2(near(loaded.defaultLon, orig.defaultLon), "defaultLon roundtrip");
        QCOMPARE(loaded.hawkesHistoryDays, orig.hawkesHistoryDays);
        QCOMPARE(loaded.theme, orig.theme);
        QVERIFY2(near(loaded.mapZoomLevel, orig.mapZoomLevel), "mapZoomLevel roundtrip");
        QCOMPARE(loaded.maxLeadCount, orig.maxLeadCount);
        QVERIFY2(near(loaded.ensemblePoissonWeight, orig.ensemblePoissonWeight),
                 "ensemblePoissonWeight roundtrip");
    }

    void testAppConfigValidation()
    {
        AppConfig bad;
        bad.databasePath = QStringLiteral(":memory:");
        bad.defaultLat = 999.0;
        QVERIFY2(!bad.validate(), "Out-of-range defaultLat should fail validate()");

        const QString path = tempIniPath();
        if (path.isEmpty())
            QSKIP("Could not create temp INI path");

        {
            QSettings s(path, QSettings::IniFormat);
            s.setValue(QStringLiteral("ui/map_zoom_level"), 999.0);
            s.setValue(QStringLiteral("alert/elevated"), -1.0);
            s.setValue(QStringLiteral("ui/max_lead_count"), 50000);
            s.sync();
        }

        const AppConfig clamped = AppConfig::loadFrom(path);
        QFile::remove(path);

        QVERIFY2(clamped.mapZoomLevel >= 1.0 && clamped.mapZoomLevel <= 20.0,
                 "loadFrom should clamp mapZoomLevel");
        QVERIFY2(clamped.alertElevated >= 0.0 && clamped.alertElevated <= 1.0,
                 "loadFrom should clamp alertElevated");
        QVERIFY2(clamped.maxLeadCount >= 1 && clamped.maxLeadCount <= 10000,
                 "loadFrom should clamp maxLeadCount");
        bad = clamped;
        bad.databasePath = QStringLiteral(":memory:");
        QVERIFY2(bad.validate(), "Clamped config should pass validate()");
    }
};

QTEST_MAIN(DatabaseStressTest)
#include "test_database_stress.moc"
