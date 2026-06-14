// test_csv_importer_deep8.cpp — Deep audit iteration 27: CsvImporter
// quoted fields, semicolon delimiter, header-only file, lat/lon columns.
#include <QtTest/QtTest>
#include <QTemporaryFile>
#include <QTextStream>
#include "ingest/CsvImporter.h"

class CsvImporterDeep8Test : public QObject
{
    Q_OBJECT

    static QString writeCsv(const QString& content)
    {
        QTemporaryFile f(QStringLiteral("sentinel_deep8_XXXXXX.csv"));
        f.setAutoRemove(false);
        if (!f.open())
            return {};
        QTextStream out(&f);
        out.setEncoding(QStringConverter::Utf8);
        out << content;
        f.close();
        return f.fileName();
    }

private slots:

    void testQuotedCommaInNarrative()
    {
        const QString path = writeCsv(
            QStringLiteral("event_id,crime_type,occurred_at,latitude,longitude,narrative\n")
            + QStringLiteral("Q1,theft,2024-05-01T10:00:00Z,51.5,-0.1,\"stolen bike, left unlocked\"\n"));
        QVERIFY(!path.isEmpty());

        CsvImporter importer;
        const auto events = importer.importFile(path);
        QCOMPARE(events.size(), 1);
        QVERIFY(events[0].narrative.value_or(QString{}).contains(QStringLiteral("bike")));
    }

    void testCaseNumberColumnDetectedAsId()
    {
        const QString path = writeCsv(
            QStringLiteral("case_number,primary_type,date,latitude,longitude\n")
            + QStringLiteral("CN-99,theft,2024-06-01T08:00:00Z,51.51,-0.11\n"));
        QVERIFY(!path.isEmpty());

        CsvImporter importer;
        const auto events = importer.importFile(path);
        QVERIFY(!events.isEmpty());
        QCOMPARE(events[0].eventId, QStringLiteral("csv_import_CN-99"));
        QCOMPARE(events[0].crimeType, QStringLiteral("theft"));
    }

    void testHeaderOnlyReturnsEmpty()
    {
        const QString path = writeCsv(
            QStringLiteral("event_id,crime_type,occurred_at,latitude,longitude\n"));
        CsvImporter importer;
        QVERIFY(importer.importFile(path).isEmpty());
    }

    void testLatLonParsed()
    {
        const QString path = writeCsv(
            QStringLiteral("event_id,crime_type,occurred_at,latitude,longitude\n")
            + QStringLiteral("LL1,theft,2024-07-01T12:00:00Z,51.5074,-0.1278\n"));

        const auto events = CsvImporter().importFile(path);
        QCOMPARE(events.size(), 1);
        QVERIFY(events[0].lat.has_value());
        QVERIFY(std::abs(*events[0].lat - 51.5074) < 1e-4);
    }

    void testMissingFileReturnsEmpty()
    {
        QVERIFY(CsvImporter().importFile(QStringLiteral("/no/such/file_deep8.csv")).isEmpty());
    }
};

QTEST_GUILESS_MAIN(CsvImporterDeep8Test)
#include "test_csv_importer_deep8.moc"
