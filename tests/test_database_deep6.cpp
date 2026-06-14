// test_database_deep6.cpp — Deep audit iteration 25: Database
// batch insert, event count, lat/lon round-trip, audit entries.
#include <QTest>
#include "core/Database.h"
#include "core/AppConfig.h"
#include "core/CrimeEvent.h"

class TestDatabaseDeep6 : public QObject
{
    Q_OBJECT

    static AppConfig memCfg()
    {
        AppConfig cfg;
        cfg.databasePath = QStringLiteral(":memory:");
        return cfg;
    }

    static CrimeEvent makeEvent(const QString& id, double lat = 51.5, double lon = -0.1)
    {
        CrimeEvent ev;
        ev.eventId      = id;
        ev.id           = id;
        ev.crimeType    = QStringLiteral("theft");
        ev.suburb       = QStringLiteral("Deep6");
        ev.lat          = lat;
        ev.lon          = lon;
        ev.latitude     = lat;
        ev.longitude    = lon;
        ev.source       = QStringLiteral("deep6_test");
        ev.ingestedAt   = QDateTime::currentDateTimeUtc();
        ev.occurredAt   = QDateTime(QDate(2024, 4, 1), QTime(10, 0), Qt::UTC);
        ev.timestamp    = ev.occurredAt.value();
        ev.qualityScore = 0.8;
        return ev;
    }

private slots:

    void testBatchInsertFiftyEvents()
    {
        Database db(memCfg());
        QVERIFY(db.open());

        QVector<CrimeEvent> events;
        for (int i = 0; i < 50; ++i)
            events.append(makeEvent(QStringLiteral("B6-%1").arg(i)));

        const int inserted = db.batchInsert(events);
        QCOMPARE(inserted, 50);
        QCOMPARE(db.eventCount(), 50);
    }

    void testLatLonRoundTrip()
    {
        Database db(memCfg());
        QVERIFY(db.open());

        auto ev = makeEvent(QStringLiteral("COORD-1"), 51.5074, -0.1278);
        QVERIFY(db.insertEvent(ev));

        const auto loaded = db.eventById(QStringLiteral("COORD-1"));
        QVERIFY(loaded.lat.has_value());
        QVERIFY(loaded.lon.has_value());
        QVERIFY2(std::abs(*loaded.lat - 51.5074) < 1e-6,
                 qPrintable(QStringLiteral("lat=%1").arg(*loaded.lat)));
        QVERIFY2(std::abs(*loaded.lon - (-0.1278)) < 1e-6,
                 qPrintable(QStringLiteral("lon=%1").arg(*loaded.lon)));
    }

    void testInsertAuditEntryQueryable()
    {
        Database db(memCfg());
        QVERIFY(db.open());

        QVERIFY(db.insertAuditEntry(QStringLiteral("EVT-AUDIT"),
                                    QStringLiteral("import"),
                                    QStringLiteral("Loaded 100 rows")));

        const auto entries = db.queryAudit(10);
        QVERIFY(!entries.isEmpty());
    }

    void testDeleteEventReducesCount()
    {
        Database db(memCfg());
        QVERIFY(db.open());
        QVERIFY(db.insertEvent(makeEvent(QStringLiteral("DEL-1"))));
        QCOMPARE(db.eventCount(), 1);
        QVERIFY(db.deleteEvent(QStringLiteral("DEL-1")));
        QCOMPARE(db.eventCount(), 0);
    }

    void testQueryBySuburbViaSearch()
    {
        Database db(memCfg());
        QVERIFY(db.open());

        auto ev = makeEvent(QStringLiteral("SUB-1"));
        ev.suburb = QStringLiteral("Camden");
        ev.locationRaw = QStringLiteral("Camden High Street");
        QVERIFY(db.insertEvent(ev));

        const auto results = db.queryEvents(
            QString{}, QDate(2024, 1, 1), QDate(2025, 12, 31),
            QStringLiteral("Camden"));
        QCOMPARE(results.size(), 1);
        QCOMPARE(results[0].suburb, QStringLiteral("Camden"));
    }

    void testSchemaVersionAtLeastOne()
    {
        Database db(memCfg());
        QVERIFY(db.open());
        QVERIFY(db.getSchemaVersion() >= 1);
    }

    void testAverageQualityScore()
    {
        Database db(memCfg());
        QVERIFY(db.open());

        auto hi = makeEvent(QStringLiteral("Q-HI"));
        hi.qualityScore = 0.9;
        auto lo = makeEvent(QStringLiteral("Q-LO"));
        lo.qualityScore = 0.5;
        QVERIFY(db.insertEvent(hi));
        QVERIFY(db.insertEvent(lo));

        const double avg = db.getAverageQualityScore();
        QVERIFY2(avg > 0.5 && avg < 0.9,
                 qPrintable(QStringLiteral("avg=%1").arg(avg)));
    }
};

QTEST_GUILESS_MAIN(TestDatabaseDeep6)
#include "test_database_deep6.moc"
