// test_data_exporter_deep4.cpp — Iteration 15 deep audit: CSV/JSON export,
// empty-database output, and field escaping for crime events.
#include <QTest>
#include <QJsonArray>
#include <QJsonObject>
#include <QSet>
#include <cmath>

#include "core/DataExporter.h"
#include "core/Database.h"
#include "core/AppConfig.h"
#include "core/CrimeEvent.h"

class TestDataExporterDeep4 : public QObject
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
                                const QString& type,
                                const QString& suburb,
                                double quality = 0.85)
    {
        CrimeEvent e;
        e.eventId      = id;
        e.crimeType    = type;
        e.suburb       = suburb;
        e.lat          = 51.5074;
        e.lon          = -0.1278;
        e.occurredAt   = QDateTime(QDate(2024, 5, 20), QTime(9, 30, 0), Qt::UTC);
        e.qualityScore = quality;
        return e;
    }

private slots:
    void testExportEmptyDatabaseCsvHeaderOnly();
    void testExportEmptyDatabaseJsonEmpty();
    void testEventsCsvJsonRoundTripFromDatabase();
    void testEventsCsvEscapesCommaInSuburb();
    void testEventsCsvEscapesQuoteInCrimeType();
    void testEventsCsvEscapesNewlineInEventId();
    void testEventsJsonPreservesQualityScore();
    void testEventsCsvRowCountMatchesDatabaseCount();
};

void TestDataExporterDeep4::testExportEmptyDatabaseCsvHeaderOnly()
{
    Database db(memCfg());
    QVERIFY(db.open());

    const QVector<CrimeEvent> events = db.getAllEvents();
    QVERIFY(events.isEmpty());

    const QString csv = DataExporter::eventsToCsv(events);
    QVERIFY(csv.startsWith(QStringLiteral("event_id,crime_type,suburb,lat,lon")));
    QCOMPARE(csv.count('\n'), 1);
}

void TestDataExporterDeep4::testExportEmptyDatabaseJsonEmpty()
{
    Database db(memCfg());
    QVERIFY(db.open());

    const QJsonArray arr = DataExporter::eventsToJson(db.getAllEvents());
    QVERIFY(arr.isEmpty());
}

void TestDataExporterDeep4::testEventsCsvJsonRoundTripFromDatabase()
{
    Database db(memCfg());
    QVERIFY(db.open());

    QVERIFY(db.insertEvent(makeEvent(QStringLiteral("EXP-001"),
                                   QStringLiteral("burglary"),
                                   QStringLiteral("Camden"))));
    QVERIFY(db.insertEvent(makeEvent(QStringLiteral("EXP-002"),
                                   QStringLiteral("robbery"),
                                   QStringLiteral("Westminster"))));

    const QVector<CrimeEvent> events = db.getAllEvents();
    QCOMPARE(events.size(), 2);

    const QJsonArray json = DataExporter::eventsToJson(events);
    QCOMPARE(json.size(), 2);

    const QSet<QString> ids = {
        json[0].toObject()[QStringLiteral("eventId")].toString(),
        json[1].toObject()[QStringLiteral("eventId")].toString()
    };
    QVERIFY(ids.contains(QStringLiteral("EXP-001")));
    QVERIFY(ids.contains(QStringLiteral("EXP-002")));

    const QString csv = DataExporter::eventsToCsv(events);
    QVERIFY(csv.contains(QStringLiteral("EXP-001")));
    QVERIFY(csv.contains(QStringLiteral("EXP-002")));
    QVERIFY(csv.contains(QStringLiteral("burglary")));
    QCOMPARE(csv.count('\n'), 3);
}

void TestDataExporterDeep4::testEventsCsvEscapesCommaInSuburb()
{
    const QVector<CrimeEvent> events = {
        makeEvent(QStringLiteral("ESC-COMMA"), QStringLiteral("theft"),
                  QStringLiteral("Zone A, North"))
    };

    const QString csv = DataExporter::eventsToCsv(events);
    QVERIFY2(csv.contains(QStringLiteral("\"Zone A, North\"")),
             "Suburb containing comma must be CSV-quoted");
}

void TestDataExporterDeep4::testEventsCsvEscapesQuoteInCrimeType()
{
    const QVector<CrimeEvent> events = {
        makeEvent(QStringLiteral("ESC-QUOTE"), QStringLiteral("theft \"armed\""),
                  QStringLiteral("SafeZone"))
    };

    const QString csv = DataExporter::eventsToCsv(events);
    QVERIFY2(csv.contains(QStringLiteral("\"theft \"\"armed\"\"\"")),
             "Embedded double-quotes must be doubled in CSV");
}

void TestDataExporterDeep4::testEventsCsvEscapesNewlineInEventId()
{
    const QVector<CrimeEvent> events = {
        makeEvent(QStringLiteral("ID\nLINE"), QStringLiteral("assault"),
                  QStringLiteral("EastEnd"))
    };

    const QString csv = DataExporter::eventsToCsv(events);
    QVERIFY2(csv.contains(QLatin1Char('"')), "Newline-containing event_id must be quoted");
    QVERIFY2(csv.contains(QStringLiteral("ID\nLINE")),
             "Newline content must survive inside quoted CSV field");
}

void TestDataExporterDeep4::testEventsJsonPreservesQualityScore()
{
    const QVector<CrimeEvent> events = {
        makeEvent(QStringLiteral("Q-001"), QStringLiteral("fraud"),
                  QStringLiteral("City"), 0.637)
    };

    const QJsonObject obj = DataExporter::eventsToJson(events).first().toObject();
    QVERIFY2(std::abs(obj[QStringLiteral("quality")].toDouble() - 0.637) < 1e-9,
             "quality score not preserved in JSON export");
}

void TestDataExporterDeep4::testEventsCsvRowCountMatchesDatabaseCount()
{
    Database db(memCfg());
    QVERIFY(db.open());

    for (int i = 0; i < 5; ++i)
        QVERIFY(db.insertEvent(makeEvent(QStringLiteral("ROW-%1").arg(i),
                                         QStringLiteral("burglary"),
                                         QStringLiteral("Suburb"))));

    QCOMPARE(db.eventCount(), 5);

    const QString csv = DataExporter::eventsToCsv(db.getAllEvents());
    QCOMPARE(csv.count('\n'), 6);
}

QTEST_GUILESS_MAIN(TestDataExporterDeep4)
#include "test_data_exporter_deep4.moc"
