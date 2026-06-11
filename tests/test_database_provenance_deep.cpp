// test_database_provenance_deep.cpp
// Iteration 8 deep audit tests for Database and ProvenanceLog.
#include <QTest>
#include <QSqlQuery>
#include <QSqlDatabase>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QTemporaryFile>
#include <QUuid>
#include <QThread>

#include "core/Database.h"
#include "core/AppConfig.h"
#include "core/CrimeEvent.h"
#include "audit/ProvenanceLog.h"

class DatabaseProvenanceDeepTest : public QObject
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
                                 const QString& type = QStringLiteral("burglary"),
                                 const QDateTime& occurred = {})
    {
        CrimeEvent ev;
        ev.eventId      = id;
        ev.id           = id;
        ev.crimeType    = type;
        ev.suburb       = QStringLiteral("Soho");
        ev.lat          = 51.5074;
        ev.lon          = -0.1278;
        ev.latitude     = 51.5074;
        ev.longitude    = -0.1278;
        ev.source       = QStringLiteral("test");
        ev.ingestedAt   = QDateTime::currentDateTimeUtc();
        const QDateTime dt = occurred.isValid()
            ? occurred
            : QDateTime(QDate(2024, 1, 1), QTime(12, 0, 0), QTimeZone::utc());
        ev.occurredAt   = dt;
        ev.timestamp    = dt;
        ev.qualityScore = 0.85;
        return ev;
    }

private slots:

    // ── Database (12 tests) ───────────────────────────────────────────────────

    void testWALModeEnabled()
    {
        QTemporaryFile tmp;
        QVERIFY(tmp.open());
        const QString path = tmp.fileName();
        tmp.close();

        AppConfig cfg;
        cfg.databasePath = path;
        Database db(cfg);
        QVERIFY2(db.open(), qPrintable(db.lastError()));

        const QString connName = QStringLiteral("wal_deep_")
                                 + QUuid::createUuid().toString(QUuid::Id128);
        QSqlDatabase rawDb = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connName);
        rawDb.setDatabaseName(path);
        QVERIFY(rawDb.open());
        QSqlQuery q(rawDb);
        QVERIFY(q.exec(QStringLiteral("PRAGMA journal_mode")));
        QVERIFY(q.next());
        QCOMPARE(q.value(0).toString(), QStringLiteral("wal"));
        rawDb.close();
        QSqlDatabase::removeDatabase(connName);
    }

    void testInsertAndQueryEvent()
    {
        Database db(inMemoryConfig());
        QVERIFY(db.open());

        const CrimeEvent inserted = makeEvent(QStringLiteral("EVT-DEEP-001"), QStringLiteral("theft"));
        QVERIFY2(db.insertEvent(inserted), qPrintable(db.lastError()));

        const CrimeEvent found = db.eventById(QStringLiteral("EVT-DEEP-001"));
        QCOMPARE(found.eventId,   inserted.eventId);
        QCOMPARE(found.crimeType, inserted.crimeType);
        QCOMPARE(found.suburb,    inserted.suburb);
        QCOMPARE(found.lat.value_or(0.0), inserted.lat.value_or(0.0));
        QCOMPARE(found.lon.value_or(0.0), inserted.lon.value_or(0.0));
        QCOMPARE(found.qualityScore, inserted.qualityScore);
    }

    void testDuplicateEventIdUpsert()
    {
        Database db(inMemoryConfig());
        QVERIFY(db.open());

        CrimeEvent first = makeEvent(QStringLiteral("DUP-001"), QStringLiteral("burglary"));
        CrimeEvent second = makeEvent(QStringLiteral("DUP-001"), QStringLiteral("robbery"));
        QVERIFY(db.insertEvent(first));
        QVERIFY(db.insertEvent(second));

        QCOMPARE(db.eventCount(), 1);
        const CrimeEvent stored = db.eventById(QStringLiteral("DUP-001"));
        QCOMPARE(stored.crimeType, QStringLiteral("robbery"));
    }

    void testDeleteEventExists()
    {
        Database db(inMemoryConfig());
        QVERIFY(db.open());
        QVERIFY(db.insertEvent(makeEvent(QStringLiteral("DEL-001"))));
        QCOMPARE(db.eventCount(), 1);

        QVERIFY(db.deleteEvent(QStringLiteral("DEL-001")));
        QCOMPARE(db.eventCount(), 0);
    }

    void testDeleteEventMissing()
    {
        Database db(inMemoryConfig());
        QVERIFY(db.open());
        QVERIFY(!db.deleteEvent(QStringLiteral("NO-SUCH-ID")));
        QVERIFY(!db.lastError().isEmpty());
    }

    void testQueryByDateRange()
    {
        Database db(inMemoryConfig());
        QVERIFY(db.open());

        const QDateTime t1 = QDateTime(QDate(2024, 1, 1), QTime(10, 0, 0), QTimeZone::utc());
        const QDateTime t2 = QDateTime(QDate(2024, 1, 15), QTime(10, 0, 0), QTimeZone::utc());
        const QDateTime t3 = QDateTime(QDate(2024, 2, 1), QTime(10, 0, 0), QTimeZone::utc());

        QVERIFY(db.insertEvent(makeEvent(QStringLiteral("R1"), QStringLiteral("theft"), t1)));
        QVERIFY(db.insertEvent(makeEvent(QStringLiteral("R2"), QStringLiteral("theft"), t2)));
        QVERIFY(db.insertEvent(makeEvent(QStringLiteral("R3"), QStringLiteral("theft"), t3)));

        const QDateTime from = QDateTime(QDate(2024, 1, 10), QTime(0, 0, 0), QTimeZone::utc());
        const QDateTime to   = QDateTime(QDate(2024, 1, 20), QTime(23, 59, 59), QTimeZone::utc());
        const auto results = db.queryEvents(QString{}, from, to);

        QCOMPARE(results.size(), 1);
        QCOMPARE(results.first().eventId, QStringLiteral("R2"));
    }

    void testQueryByCrimeType()
    {
        Database db(inMemoryConfig());
        QVERIFY(db.open());

        QVERIFY(db.insertEvent(makeEvent(QStringLiteral("T1"), QStringLiteral("burglary"))));
        QVERIFY(db.insertEvent(makeEvent(QStringLiteral("T2"), QStringLiteral("robbery"))));
        QVERIFY(db.insertEvent(makeEvent(QStringLiteral("T3"), QStringLiteral("assault"))));

        const auto results = db.queryEvents(QStringLiteral("robbery"));
        QCOMPARE(results.size(), 1);
        QCOMPARE(results.first().crimeType, QStringLiteral("robbery"));
    }

    void testAuditLogInsert()
    {
        Database db(inMemoryConfig());
        QVERIFY(db.open());
        QVERIFY(db.insertEvent(makeEvent(QStringLiteral("AUD-001"))));

        QVERIFY2(db.insertAuditEntry(
                     QStringLiteral("AUD-001"),
                     QStringLiteral("viewed"),
                     QStringLiteral("Inspector opened record")),
                 qPrintable(db.lastError()));

        const auto log = db.queryAudit(10);
        QCOMPARE(log.size(), 1);
        const auto& [ts, eventId, action, detail] = log.first();
        Q_UNUSED(ts);
        QCOMPARE(eventId, QStringLiteral("AUD-001"));
        QCOMPARE(action,  QStringLiteral("viewed"));
        QCOMPARE(detail,  QStringLiteral("Inspector opened record"));
    }

    void testAuditLogOrdering()
    {
        Database db(inMemoryConfig());
        QVERIFY(db.open());
        QVERIFY(db.insertEvent(makeEvent(QStringLiteral("ORD-001"))));

        QVERIFY(db.insertAuditEntry(QStringLiteral("ORD-001"), QStringLiteral("first"),  QStringLiteral("d1")));
        QThread::msleep(5);
        QVERIFY(db.insertAuditEntry(QStringLiteral("ORD-001"), QStringLiteral("second"), QStringLiteral("d2")));
        QThread::msleep(5);
        QVERIFY(db.insertAuditEntry(QStringLiteral("ORD-001"), QStringLiteral("third"),  QStringLiteral("d3")));

        const auto log = db.queryAudit(10);
        QCOMPARE(log.size(), 3);
        QCOMPARE(std::get<2>(log[0]), QStringLiteral("third"));
        QCOMPARE(std::get<2>(log[1]), QStringLiteral("second"));
        QCOMPARE(std::get<2>(log[2]), QStringLiteral("first"));
    }

    void testLastErrorPopulated()
    {
        Database db(inMemoryConfig());
        QVERIFY(!db.insertEvent(makeEvent(QStringLiteral("ERR-001"))));
        QVERIFY(!db.lastError().isEmpty());
    }

    void testEventCountAfterDelete()
    {
        Database db(inMemoryConfig());
        QVERIFY(db.open());

        for (int i = 0; i < 5; ++i)
            QVERIFY(db.insertEvent(makeEvent(QStringLiteral("C%1").arg(i))));

        QCOMPARE(db.eventCount(), 5);
        QVERIFY(db.deleteEvent(QStringLiteral("C1")));
        QVERIFY(db.deleteEvent(QStringLiteral("C3")));
        QCOMPARE(db.eventCount(), 3);
    }

    void testInMemoryDatabaseIsolation()
    {
        Database db1(inMemoryConfig());
        Database db2(inMemoryConfig());
        QVERIFY(db1.open());
        QVERIFY(db2.open());

        QVERIFY(db1.insertEvent(makeEvent(QStringLiteral("ISO-001"))));
        QCOMPARE(db1.eventCount(), 1);
        QCOMPARE(db2.eventCount(), 0);
    }

    // ── ProvenanceLog (10 tests) ──────────────────────────────────────────────

    void testAddEntrySuccess()
    {
        ProvenanceLog log;
        log.addEntry(QStringLiteral("uk_police"), QStringLiteral("PoissonBaseline"),
                     QStringLiteral("fit"), QStringLiteral("lambda=0.42"));
        QCOMPARE(log.size(), 1);
    }

    void testAddEntryEmptyFields()
    {
        ProvenanceLog log;
        log.addEntry(QString{}, QString{}, QString{}, QString{});
        QCOMPARE(log.size(), 1);
        const auto entries = log.getEntries();
        QCOMPARE(entries.size(), 1);
        QCOMPARE(entries.first().source, QString{});
        QCOMPARE(entries.first().model,  QString{});
    }

    void testGetEntriesOrder()
    {
        ProvenanceLog log;
        const QDateTime t1 = QDateTime(QDate(2024, 1, 1), QTime(8, 0, 0), QTimeZone::utc());
        const QDateTime t2 = QDateTime(QDate(2024, 1, 2), QTime(8, 0, 0), QTimeZone::utc());
        const QDateTime t3 = QDateTime(QDate(2024, 1, 3), QTime(8, 0, 0), QTimeZone::utc());

        log.addEntry(QStringLiteral("s1"), QStringLiteral("m1"), QStringLiteral("a1"), QStringLiteral("d1"), t1);
        log.addEntry(QStringLiteral("s2"), QStringLiteral("m2"), QStringLiteral("a2"), QStringLiteral("d2"), t2);
        log.addEntry(QStringLiteral("s3"), QStringLiteral("m3"), QStringLiteral("a3"), QStringLiteral("d3"), t3);

        const auto entries = log.getEntries();
        QCOMPARE(entries.size(), 3);
        QCOMPARE(entries[0].action, QStringLiteral("a3"));
        QCOMPARE(entries[1].action, QStringLiteral("a2"));
        QCOMPARE(entries[2].action, QStringLiteral("a1"));
    }

    void testExportToJsonValid()
    {
        ProvenanceLog log;
        log.addEntry(QStringLiteral("csv"), QStringLiteral("NLP"), QStringLiteral("parse"), QStringLiteral("ok"));
        const QJsonDocument doc = QJsonDocument::fromJson(log.exportToJson().toUtf8());
        QVERIFY(doc.isArray());
        QCOMPARE(doc.array().size(), 1);
    }

    void testExportToJsonFields()
    {
        ProvenanceLog log;
        log.addEntry(QStringLiteral("uk_police"), QStringLiteral("Hawkes"),
                     QStringLiteral("infer"), QStringLiteral("alpha=0.1"));
        const QJsonArray arr = QJsonDocument::fromJson(log.exportToJson().toUtf8()).array();
        QVERIFY(!arr.isEmpty());
        const QJsonObject obj = arr.first().toObject();
        QCOMPARE(obj.value(QStringLiteral("source")).toString(), QStringLiteral("uk_police"));
        QCOMPARE(obj.value(QStringLiteral("model")).toString(),  QStringLiteral("Hawkes"));
        QCOMPARE(obj.value(QStringLiteral("action")).toString(), QStringLiteral("infer"));
        QCOMPARE(obj.value(QStringLiteral("detail")).toString(), QStringLiteral("alpha=0.1"));
        QVERIFY(obj.contains(QStringLiteral("timestamp")));
    }

    void testExportToCsvHasHeader()
    {
        ProvenanceLog log;
        log.addEntry(QStringLiteral("s"), QStringLiteral("m"), QStringLiteral("a"), QStringLiteral("d"));
        const QString csv = log.exportToCsv();
        const QString firstLine = csv.section(QLatin1Char('\n'), 0, 0);
        QCOMPARE(firstLine, QStringLiteral("timestamp,source,model,action,detail"));
    }

    void testExportToCsvEscapesComma()
    {
        ProvenanceLog log;
        log.addEntry(QStringLiteral("s"), QStringLiteral("m"), QStringLiteral("a"),
                     QStringLiteral("value, with comma"));
        const QString csv = log.exportToCsv();
        QVERIFY(csv.contains(QStringLiteral("\"value, with comma\"")));
    }

    void testClearRemovesAll()
    {
        ProvenanceLog log;
        log.addEntry(QStringLiteral("s1"), QStringLiteral("m1"), QStringLiteral("a1"), QStringLiteral("d1"));
        log.addEntry(QStringLiteral("s2"), QStringLiteral("m2"), QStringLiteral("a2"), QStringLiteral("d2"));
        log.addEntry(QStringLiteral("s3"), QStringLiteral("m3"), QStringLiteral("a3"), QStringLiteral("d3"));
        QCOMPARE(log.size(), 3);
        log.clear();
        QCOMPARE(log.size(), 0);
        QVERIFY(log.getEntries().isEmpty());
    }

    void testFilterByModel()
    {
        ProvenanceLog log;
        log.addEntry(QStringLiteral("s1"), QStringLiteral("Poisson"), QStringLiteral("fit"), QStringLiteral("d1"));
        log.addEntry(QStringLiteral("s2"), QStringLiteral("Hawkes"),   QStringLiteral("fit"), QStringLiteral("d2"));
        log.addEntry(QStringLiteral("s3"), QStringLiteral("Poisson"), QStringLiteral("score"), QStringLiteral("d3"));

        const auto filtered = log.filterByModel(QStringLiteral("Poisson"));
        QCOMPARE(filtered.size(), 2);
        for (const auto& e : filtered)
            QCOMPARE(e.model, QStringLiteral("Poisson"));
    }

    void testSizeConsistent()
    {
        ProvenanceLog log;
        QCOMPARE(log.size(), log.getEntries().size());

        log.addEntry(QStringLiteral("s"), QStringLiteral("m"), QStringLiteral("a"), QStringLiteral("d"));
        QCOMPARE(log.size(), log.getEntries().size());

        log.addEntry(QStringLiteral("s2"), QStringLiteral("m2"), QStringLiteral("a2"), QStringLiteral("d2"));
        QCOMPARE(log.size(), log.getEntries().size());

        log.clear();
        QCOMPARE(log.size(), log.getEntries().size());
    }
};

QTEST_MAIN(DatabaseProvenanceDeepTest)
#include "test_database_provenance_deep.moc"
