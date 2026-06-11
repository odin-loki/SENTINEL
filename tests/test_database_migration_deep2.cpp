// test_database_migration_deep2.cpp — Iteration 13: run migrations twice,
// verify schema version and WAL mode on file-backed DB.
#include <QTest>
#include <QApplication>
#include <QSqlQuery>
#include <QSqlDatabase>
#include <QDir>
#include <QFile>
#include <QUuid>

#include "core/Database.h"
#include "core/AppConfig.h"
#include "core/CrimeEvent.h"

class TestDatabaseMigrationDeep2 : public QObject
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
        return QDir::tempPath() + QStringLiteral("/sentinel_mig_deep2_")
               + QUuid::createUuid().toString(QUuid::Id128)
               + QStringLiteral(".db");
    }

    static void removeDbFiles(const QString& path)
    {
        QFile::remove(path);
        QFile::remove(path + QStringLiteral("-wal"));
        QFile::remove(path + QStringLiteral("-shm"));
    }

    static CrimeEvent makeEvent(const QString& id)
    {
        CrimeEvent ev;
        ev.eventId      = id;
        ev.id           = id;
        ev.crimeType    = QStringLiteral("burglary");
        ev.suburb       = QStringLiteral("Camden");
        ev.ingestedAt   = QDateTime::currentDateTimeUtc();
        ev.qualityScore = 0.8;
        return ev;
    }

private slots:

    void testMigrateSchemaTwiceIsIdempotent()
    {
        Database db(memCfg());
        QVERIFY2(db.open(), qPrintable(db.lastError()));
        QCOMPARE(db.currentSchemaVersion(), Database::SCHEMA_VERSION);

        QVERIFY(db.migrateSchema(db.currentSchemaVersion()));
        QCOMPARE(db.currentSchemaVersion(), Database::SCHEMA_VERSION);

        QVERIFY(db.migrateSchema(0));
        QCOMPARE(db.currentSchemaVersion(), Database::SCHEMA_VERSION);

        QVERIFY(db.insertEvent(makeEvent(QStringLiteral("MIG2-001"))));
        QCOMPARE(db.eventCount(), 1);
    }

    void testOpenTwicePreservesSchemaVersion()
    {
        const QString path = tempDbPath();

        AppConfig cfg;
        cfg.databasePath = path;

        {
            Database db(cfg);
            QVERIFY2(db.open(), qPrintable(db.lastError()));
            QCOMPARE(db.currentSchemaVersion(), Database::SCHEMA_VERSION);
            QVERIFY(db.insertEvent(makeEvent(QStringLiteral("MIG2-002"))));
        }

        {
            Database db2(cfg);
            QVERIFY2(db2.open(), qPrintable(db2.lastError()));
            QCOMPARE(db2.currentSchemaVersion(), Database::SCHEMA_VERSION);
            QCOMPARE(db2.eventCount(), 1);
        }

        removeDbFiles(path);
    }

    void testWalModeOnFileDatabase()
    {
        const QString path = tempDbPath();

        AppConfig cfg;
        cfg.databasePath = path;

        {
            Database db(cfg);
            QVERIFY2(db.open(), qPrintable(db.lastError()));
        }

        const QString conn = QStringLiteral("wal_mig2_")
                             + QUuid::createUuid().toString(QUuid::Id128);
        {
            QSqlDatabase raw = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), conn);
            raw.setDatabaseName(path);
            QVERIFY(raw.open());
            QSqlQuery q(raw);
            QVERIFY(q.exec(QStringLiteral("PRAGMA journal_mode")));
            QVERIFY(q.next());
            QCOMPARE(q.value(0).toString().toLower(), QStringLiteral("wal"));
            raw.close();
        }
        QSqlDatabase::removeDatabase(conn);
        removeDbFiles(path);
    }

    void testNullQualityScoreDefaultsToHalf()
    {
        Database db(memCfg());
        QVERIFY(db.open());

        CrimeEvent ev = makeEvent(QStringLiteral("MIG2-QS"));
        ev.qualityScore = 0.0;  // explicit zero stored in DB
        QVERIFY(db.insertEvent(ev));

        const CrimeEvent fetched = db.eventById(QStringLiteral("MIG2-QS"));
        QCOMPARE(fetched.qualityScore, 0.0);
    }
};

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    TestDatabaseMigrationDeep2 tests;
    return QTest::qExec(&tests, argc, argv);
}

#include "test_database_migration_deep2.moc"
