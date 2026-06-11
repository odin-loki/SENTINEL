#include <QTest>
#include <QApplication>
#include <QTemporaryFile>
#include <QUuid>
#include <QSqlQuery>
#include <QSqlDatabase>

#include "core/Database.h"
#include "core/AppConfig.h"
#include "core/CrimeEvent.h"

class TestDatabaseDeep2 : public QObject
{
    Q_OBJECT

private:
    static AppConfig memCfg()
    {
        AppConfig cfg;
        cfg.databasePath = QStringLiteral(":memory:");
        return cfg;
    }

    static CrimeEvent makeEvent(const QString& id,
                                const QString& type = QStringLiteral("theft"),
                                double lat = 51.5074, double lon = -0.1278)
    {
        CrimeEvent ev;
        ev.eventId      = id;
        ev.id           = id;
        ev.crimeType    = type;
        ev.suburb       = QStringLiteral("TestZone");
        ev.lat          = lat;
        ev.lon          = lon;
        ev.latitude     = lat;
        ev.longitude    = lon;
        ev.source       = QStringLiteral("test");
        ev.sourceVersion = QStringLiteral("1.0");
        ev.ingestedAt   = QDateTime::currentDateTimeUtc();
        ev.qualityScore = 0.9;
        const QDateTime occ = QDateTime(QDate(2024, 6, 1), QTime(14, 0, 0), QTimeZone::utc());
        ev.occurredAt   = occ;
        ev.timestamp    = occ;
        return ev;
    }

private slots:
    void testInsertAndFetchRoundTrip();
    void testBatchInsertAllEvents();
    void testDeleteEventRemovesIt();
    void testUpdateEventModifies();
    void testWalModeEnabled();
    void testDuplicateIdUpsertNoDuplicate();
    void testFetchEventsZoneFilter();
};

void TestDatabaseDeep2::testInsertAndFetchRoundTrip()
{
    Database db(memCfg());
    QVERIFY(db.open());

    const CrimeEvent ev = makeEvent(QStringLiteral("EVT-001"), QStringLiteral("robbery"));
    QVERIFY(db.insertEvent(ev));

    const QVector<CrimeEvent> results = db.getAllEvents();
    QCOMPARE(results.size(), 1);
    QCOMPARE(results[0].eventId,   ev.eventId);
    QCOMPARE(results[0].crimeType, ev.crimeType);
    QVERIFY(results[0].lat.has_value());
    QVERIFY(qAbs(results[0].lat.value() - ev.lat.value()) < 1e-9);
}

void TestDatabaseDeep2::testBatchInsertAllEvents()
{
    Database db(memCfg());
    QVERIFY(db.open());

    QVector<CrimeEvent> events;
    for (int i = 0; i < 10; ++i)
        events.append(makeEvent(QStringLiteral("BATCH-%1").arg(i)));

    const int inserted = db.batchInsert(events);
    QCOMPARE(inserted, 10);
    QCOMPARE(db.eventCount(), 10);
}

void TestDatabaseDeep2::testDeleteEventRemovesIt()
{
    Database db(memCfg());
    QVERIFY(db.open());

    const CrimeEvent ev = makeEvent(QStringLiteral("DEL-001"));
    QVERIFY(db.insertEvent(ev));
    QCOMPARE(db.eventCount(), 1);

    QVERIFY(db.deleteEvent(QStringLiteral("DEL-001")));
    QCOMPARE(db.eventCount(), 0);

    // Deleting again returns false (not found)
    QVERIFY(!db.deleteEvent(QStringLiteral("DEL-001")));
}

void TestDatabaseDeep2::testUpdateEventModifies()
{
    Database db(memCfg());
    QVERIFY(db.open());

    CrimeEvent ev = makeEvent(QStringLiteral("UPD-001"), QStringLiteral("theft"));
    QVERIFY(db.insertEvent(ev));

    ev.crimeType    = QStringLiteral("arson");
    ev.qualityScore = 0.5;
    QVERIFY(db.updateEvent(ev));

    const CrimeEvent fetched = db.eventById(QStringLiteral("UPD-001"));
    QCOMPARE(fetched.crimeType,    QStringLiteral("arson"));
    QVERIFY(qAbs(fetched.qualityScore - 0.5) < 1e-9);
    QCOMPARE(db.eventCount(), 1);
}

void TestDatabaseDeep2::testWalModeEnabled()
{
    // WAL mode requires a real file; :memory: always uses "memory" journal mode
    QTemporaryFile tmp;
    tmp.setAutoRemove(true);
    QVERIFY(tmp.open());
    const QString path = tmp.fileName();
    tmp.close();

    AppConfig cfg;
    cfg.databasePath = path;
    Database db(cfg);
    QVERIFY(db.open());

    // Query via the same connection through Qt SQL directly
    // The Database constructor assigns a unique connection name; we query via
    // a fresh connection opened on the same file to verify WAL mode.
    const QString connName = QStringLiteral("wal_verify_conn");
    {
        QSqlDatabase verifyDb = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connName);
        verifyDb.setDatabaseName(path);
        QVERIFY(verifyDb.open());
        QSqlQuery q(verifyDb);
        QVERIFY(q.exec(QStringLiteral("PRAGMA journal_mode")));
        QVERIFY(q.next());
        const QString mode = q.value(0).toString().toLower();
        QCOMPARE(mode, QStringLiteral("wal"));
        verifyDb.close();
    }
    QSqlDatabase::removeDatabase(connName);
}

void TestDatabaseDeep2::testDuplicateIdUpsertNoDuplicate()
{
    Database db(memCfg());
    QVERIFY(db.open());

    CrimeEvent ev = makeEvent(QStringLiteral("DUP-001"), QStringLiteral("theft"));
    QVERIFY(db.insertEvent(ev));

    ev.crimeType = QStringLiteral("vandalism");
    QVERIFY(db.insertEvent(ev));

    QCOMPARE(db.eventCount(), 1);

    const CrimeEvent fetched = db.eventById(QStringLiteral("DUP-001"));
    QCOMPARE(fetched.crimeType, QStringLiteral("vandalism"));
}

void TestDatabaseDeep2::testFetchEventsZoneFilter()
{
    Database db(memCfg());
    QVERIFY(db.open());

    const CrimeEvent london  = makeEvent(QStringLiteral("LON-001"), QStringLiteral("theft"),
                                         51.5074, -0.1278);
    const CrimeEvent newYork = makeEvent(QStringLiteral("NYC-001"), QStringLiteral("theft"),
                                         40.7128, -74.0060);
    QVERIFY(db.insertEvent(london));
    QVERIFY(db.insertEvent(newYork));
    QCOMPARE(db.eventCount(), 2);

    // Filter to London bounding box only
    const QVector<CrimeEvent> results = db.queryEvents(
        QString{}, QDateTime{}, QDateTime{},
        50.0, 52.0,   // latMin, latMax
        -1.0,  1.0,   // lonMin, lonMax
        100
    );

    QCOMPARE(results.size(), 1);
    QCOMPARE(results[0].eventId, QStringLiteral("LON-001"));
}

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    TestDatabaseDeep2 tests;
    return QTest::qExec(&tests, argc, argv);
}
#include "test_database_deep2.moc"
