#include <QTest>
#include <QTemporaryFile>
#include <QTextStream>

#include "ingest/CsvImporter.h"
#include "core/CrimeEvent.h"

class TestCsvImporterDeep3 : public QObject
{
    Q_OBJECT

private:
    static QString writeTempCsv(const QStringList& lines)
    {
        QTemporaryFile* f = new QTemporaryFile(QStringLiteral("XXXXXX_deep3.csv"));
        f->setAutoRemove(false);
        f->open();
        QTextStream out(f);
        out.setEncoding(QStringConverter::Utf8);
        for (const QString& line : lines)
            out << line << "\n";
        f->close();
        const QString path = f->fileName();
        delete f;
        return path;
    }

private slots:
    void testBasicImport();
    void testQuotedCommasInFields();
    void testDoubleQuotedEscapedQuotes();
    void testInvalidLatitudeRejected();
    void testMissingColumnsNocrash();
    void testEmptyCsvHeaderOnly();
    void testDuplicateEventIds();
};

void TestCsvImporterDeep3::testBasicImport()
{
    const QStringList lines = {
        QStringLiteral("id,date,type,lat,lon,description"),
        QStringLiteral("001,2024-06-01,theft,51.5074,-0.1278,A basic theft"),
        QStringLiteral("002,2024-06-02,robbery,51.5080,-0.1290,A robbery"),
    };
    const QString path = writeTempCsv(lines);
    const QVector<CrimeEvent> events = CsvImporter::importFile(path, QStringLiteral("test"));

    QCOMPARE(events.size(), 2);
    QVERIFY(events[0].eventId.contains(QStringLiteral("001")));
    QCOMPARE(events[0].crimeType, QStringLiteral("theft"));
    QVERIFY(events[0].lat.has_value());
    QVERIFY(qAbs(events[0].lat.value() - 51.5074) < 1e-4);
    QVERIFY(events[0].lon.has_value());
}

void TestCsvImporterDeep3::testQuotedCommasInFields()
{
    const QStringList lines = {
        QStringLiteral("id,date,type,lat,lon,description"),
        QStringLiteral("003,2024-06-03,burglary,51.50,-0.12,\"Main Street, London\""),
    };
    const QString path = writeTempCsv(lines);
    const QVector<CrimeEvent> events = CsvImporter::importFile(path, QStringLiteral("test"));

    QCOMPARE(events.size(), 1);
    QVERIFY(events[0].narrative.has_value());
    QVERIFY(events[0].narrative.value().contains(QStringLiteral("Main Street")));
    QVERIFY(events[0].narrative.value().contains(QStringLiteral("London")));
}

void TestCsvImporterDeep3::testDoubleQuotedEscapedQuotes()
{
    const QStringList lines = {
        QStringLiteral("id,date,type,lat,lon,description"),
        QStringLiteral(R"(004,2024-06-04,assault,51.50,-0.12,"He said ""help"" loudly")"),
    };
    const QString path = writeTempCsv(lines);
    const QVector<CrimeEvent> events = CsvImporter::importFile(path, QStringLiteral("test"));

    QCOMPARE(events.size(), 1);
    QVERIFY(events[0].narrative.has_value());
    QVERIFY(events[0].narrative.value().contains(QStringLiteral("help")));
    QVERIFY(events[0].narrative.value().contains(QLatin1Char('"')));
}

void TestCsvImporterDeep3::testInvalidLatitudeRejected()
{
    const QStringList lines = {
        QStringLiteral("id,date,type,lat,lon,description"),
        QStringLiteral("005,2024-06-05,theft,91.0,-0.12,Invalid latitude"),
        QStringLiteral("006,2024-06-06,theft,51.5,-181.0,Invalid longitude"),
        QStringLiteral("007,2024-06-07,theft,-91.0,0.0,Negative invalid lat"),
    };
    const QString path = writeTempCsv(lines);
    const QVector<CrimeEvent> events = CsvImporter::importFile(path, QStringLiteral("test"));

    QCOMPARE(events.size(), 3);

    // lat=91 → rejected; ev.lat should be nullopt
    QVERIFY(!events[0].lat.has_value());

    // lon=-181 → rejected; ev.lon should be nullopt
    QVERIFY(!events[1].lon.has_value());

    // lat=-91 → rejected
    QVERIFY(!events[2].lat.has_value());
}

void TestCsvImporterDeep3::testMissingColumnsNocrash()
{
    // Header has no date or crime type — both missing means rows are skipped,
    // but the importer must not crash.
    const QStringList lines = {
        QStringLiteral("id,lat,lon"),
        QStringLiteral("008,51.5,-0.1"),
        QStringLiteral("009,51.6,-0.2"),
    };
    const QString path = writeTempCsv(lines);

    bool threw = false;
    QVector<CrimeEvent> events;
    try {
        events = CsvImporter::importFile(path, QStringLiteral("test"));
    } catch (...) {
        threw = true;
    }
    QVERIFY(!threw);
    // Rows missing both date and crime type are skipped
    QCOMPARE(events.size(), 0);
}

void TestCsvImporterDeep3::testEmptyCsvHeaderOnly()
{
    const QStringList lines = {
        QStringLiteral("id,date,type,lat,lon,description"),
    };
    const QString path = writeTempCsv(lines);
    const QVector<CrimeEvent> events = CsvImporter::importFile(path, QStringLiteral("test"));

    QCOMPARE(events.size(), 0);
}

void TestCsvImporterDeep3::testDuplicateEventIds()
{
    const QStringList lines = {
        QStringLiteral("id,date,type,lat,lon,description"),
        QStringLiteral("DUP,2024-06-10,theft,51.5,-0.1,First occurrence"),
        QStringLiteral("DUP,2024-06-11,robbery,51.6,-0.2,Second occurrence"),
    };
    const QString path = writeTempCsv(lines);
    const QVector<CrimeEvent> events = CsvImporter::importFile(path, QStringLiteral("test"));

    // CsvImporter doesn't deduplicate internally; both are returned
    QCOMPARE(events.size(), 2);

    // Both should share the same eventId (sourceTag + '_' + rawId)
    QCOMPARE(events[0].eventId, events[1].eventId);

    // The second row's crime type is different — last-wins applies at DB insert level
    QCOMPARE(events[0].crimeType, QStringLiteral("theft"));
    QCOMPARE(events[1].crimeType, QStringLiteral("robbery"));
}

QTEST_GUILESS_MAIN(TestCsvImporterDeep3)
#include "test_csv_importer_deep3.moc"
