// test_csv_importer.cpp — CsvImporter unit tests
#include <QTest>
#include <QCoreApplication>
#include <QTemporaryFile>
#include <QTextStream>
#include "ingest/CsvImporter.h"

class TestCsvImporter : public QObject
{
    Q_OBJECT

private:
    static QString writeTmpCsv(const QString& content)
    {
        auto* tmp = new QTemporaryFile();
        tmp->setAutoRemove(false);
        if (!tmp->open()) return {};
        QTextStream out(tmp);
        out.setEncoding(QStringConverter::Utf8);
        out << content;
        tmp->close();
        QString path = tmp->fileName();
        delete tmp;
        return path;
    }

private slots:

    // ── Column detection ─────────────────────────────────────────────────────

    void testDetectChicagoColumns()
    {
        QStringList headers = {"ID", "Case Number", "Date", "Primary Type",
                               "Description", "Latitude", "Longitude", "Block",
                               "Arrest", "Location Description"};
        CsvColumnMap m = CsvImporter::detectColumns(headers);
        QVERIFY(m.idCol >= 0);
        QVERIFY(m.dateCol >= 0);
        QVERIFY(m.crimeTypeCol >= 0);
        QVERIFY(m.descCol >= 0);
        QVERIFY(m.latCol >= 0);
        QVERIFY(m.lonCol >= 0);
        QVERIFY(m.addressCol >= 0);
        QVERIFY(m.outcomeCol >= 0);
        QVERIFY(m.locationCol >= 0);
    }

    void testDetectNYCColumns()
    {
        QStringList headers = {"INCIDENT_KEY", "OCCUR_DATE", "OFFENSE",
                               "NARRATIVE", "Latitude", "Longitude"};
        CsvColumnMap m = CsvImporter::detectColumns(headers);
        QVERIFY(m.idCol >= 0);
        QVERIFY(m.dateCol >= 0);
        QVERIFY(m.crimeTypeCol >= 0);
        QVERIFY(m.latCol >= 0);
        QVERIFY(m.lonCol >= 0);
    }

    void testDetectUKPoliceColumns()
    {
        QStringList headers = {"Crime ID", "Month", "Crime type", "Latitude",
                               "Longitude", "Last outcome category"};
        CsvColumnMap m = CsvImporter::detectColumns(headers);
        QVERIFY(m.idCol >= 0);
        QVERIFY(m.crimeTypeCol >= 0);
        QVERIFY(m.latCol >= 0);
        QVERIFY(m.lonCol >= 0);
        QVERIFY(m.outcomeCol >= 0);
    }

    void testDetectEmptyHeadersGivesNegativeColumns()
    {
        CsvColumnMap m = CsvImporter::detectColumns({});
        QVERIFY(m.idCol < 0);
        QVERIFY(m.dateCol < 0);
        QVERIFY(m.latCol < 0);
    }

    void testDetectCaseInsensitive()
    {
        QStringList headers = {"DATE", "CRIME_TYPE", "LATITUDE", "LONGITUDE"};
        CsvColumnMap m = CsvImporter::detectColumns(headers);
        QVERIFY(m.dateCol >= 0);
        QVERIFY(m.crimeTypeCol >= 0);
        QVERIFY(m.latCol >= 0);
        QVERIFY(m.lonCol >= 0);
    }

    void testDetectTimestampAsDate()
    {
        QStringList headers = {"event_id", "timestamp", "type", "lat", "lon"};
        CsvColumnMap m = CsvImporter::detectColumns(headers);
        QVERIFY(m.dateCol >= 0);
    }

    // ── Row parsing ───────────────────────────────────────────────────────────

    void testParseRowBasic()
    {
        CsvColumnMap m;
        m.idCol = 0; m.dateCol = 1; m.crimeTypeCol = 2;
        m.latCol = 3; m.lonCol = 4; m.sourceTag = "test";
        QStringList fields = {"ABC-123", "2024-01-15 10:30:00", "BURGLARY",
                              "51.5074", "-0.1278"};
        auto ev = CsvImporter::parseRow(fields, m, "test");
        QVERIFY(ev.eventId.contains("ABC-123"));
        QCOMPARE(ev.crimeType, QStringLiteral("burglary"));
        QVERIFY(ev.occurredAt.has_value());
        QCOMPARE(ev.occurredAt->date(), QDate(2024, 1, 15));
        QVERIFY(ev.lat.has_value());
        QVERIFY(std::abs(*ev.lat - 51.5074) < 1e-4);
        QVERIFY(ev.lon.has_value());
    }

    void testParseRowNoId()
    {
        CsvColumnMap m;
        m.idCol = -1; m.dateCol = 0; m.crimeTypeCol = 1; m.sourceTag = "csv";
        QStringList fields = {"2024-01-15", "theft"};
        auto ev = CsvImporter::parseRow(fields, m, "csv");
        QVERIFY(!ev.eventId.isEmpty());
        QVERIFY(ev.eventId.startsWith("csv_"));
    }

    void testParseRowChicagoDate()
    {
        CsvColumnMap m;
        m.idCol = 0; m.dateCol = 1; m.crimeTypeCol = 2; m.sourceTag = "chicago";
        QStringList fields = {"123", "01/15/2024 10:30:00 AM", "ROBBERY"};
        auto ev = CsvImporter::parseRow(fields, m, "chicago");
        QVERIFY(ev.occurredAt.has_value());
        QCOMPARE(ev.occurredAt->date(), QDate(2024, 1, 15));
    }

    void testParseRowIsoDate()
    {
        CsvColumnMap m;
        m.idCol = 0; m.dateCol = 1; m.crimeTypeCol = 2; m.sourceTag = "test";
        QStringList fields = {"1", "2024-06-15", "assault"};
        auto ev = CsvImporter::parseRow(fields, m, "test");
        QVERIFY(ev.occurredAt.has_value());
        QCOMPARE(ev.occurredAt->date().month(), 6);
    }

    void testParseRowInvalidCoordinatesIgnored()
    {
        CsvColumnMap m;
        m.latCol = 0; m.lonCol = 1; m.crimeTypeCol = 2; m.dateCol = 3;
        m.sourceTag = "test";
        QStringList fields = {"999.0", "-999.0", "burglary", "2024-01-15"};
        auto ev = CsvImporter::parseRow(fields, m, "test");
        QVERIFY(!ev.lat.has_value());
        QVERIFY(!ev.lon.has_value());
    }

    void testParseRowSourceTagPrefixOnId()
    {
        CsvColumnMap m;
        m.idCol = 0; m.crimeTypeCol = 1; m.dateCol = 2; m.sourceTag = "chicago";
        QStringList fields = {"MYID", "theft", "2024-01-01"};
        auto ev = CsvImporter::parseRow(fields, m, "chicago");
        QVERIFY(ev.eventId.startsWith("chicago_MYID"));
    }

    void testParseRowQualityScoreSet()
    {
        CsvColumnMap m;
        m.crimeTypeCol = 0; m.dateCol = 1; m.sourceTag = "test";
        QStringList fields = {"assault", "2024-06-01"};
        auto ev = CsvImporter::parseRow(fields, m, "test");
        QVERIFY(ev.qualityScore > 0.0 && ev.qualityScore <= 1.0);
    }

    void testParseRowNarrative()
    {
        CsvColumnMap m;
        m.descCol = 0; m.crimeTypeCol = 1; m.dateCol = 2; m.sourceTag = "test";
        QStringList fields = {"Victim approached from behind", "robbery", "2024-01-01"};
        auto ev = CsvImporter::parseRow(fields, m, "test");
        QVERIFY(ev.narrative.has_value() && ev.narrative->contains("Victim"));
    }

    void testParseRowAddress()
    {
        CsvColumnMap m;
        m.addressCol = 0; m.crimeTypeCol = 1; m.dateCol = 2; m.sourceTag = "test";
        QStringList fields = {"100 MAIN ST", "burglary", "2024-01-01"};
        auto ev = CsvImporter::parseRow(fields, m, "test");
        QCOMPARE(ev.addressNormalised, QStringLiteral("100 MAIN ST"));
    }

    // ── File import ───────────────────────────────────────────────────────────

    void testImportSimpleCsv()
    {
        QString content = "id,date,crime_type,lat,lon\n"
                          "1,2024-01-15,burglary,51.5,0.1\n"
                          "2,2024-01-16,theft,51.6,0.2\n"
                          "3,2024-01-17,robbery,51.7,0.3\n";
        QString path = writeTmpCsv(content);
        auto events = CsvImporter::importFile(path);
        QCOMPARE(events.size(), 3);
        QFile::remove(path);
    }

    void testImportWithQuotedFields()
    {
        QString content = "id,date,crime_type,description\n"
                          "1,2024-01-15,burglary,\"entry via rear window, forced\"\n"
                          "2,2024-01-16,theft,\"handbag stolen, no weapon\"\n";
        QString path = writeTmpCsv(content);
        auto events = CsvImporter::importFile(path);
        QCOMPARE(events.size(), 2);
        QVERIFY(events[0].narrative.has_value() && events[0].narrative->contains("window"));
        QFile::remove(path);
    }

    void testImportHeaderOnlyReturnsEmpty()
    {
        QString path = writeTmpCsv("id,date,crime_type\n");
        auto events = CsvImporter::importFile(path);
        QCOMPARE(events.size(), 0);
        QFile::remove(path);
    }

    void testImportNonExistentFileReturnsEmpty()
    {
        auto events = CsvImporter::importFile("/no/such/file_sentinel_test.csv");
        QCOMPARE(events.size(), 0);
    }

    void testImportProgressCallback()
    {
        QString rows;
        for (int i = 0; i < 600; ++i)
            rows += QString("E%1,2024-01-15,burglary,51.5,0.1\n").arg(i);
        QString path = writeTmpCsv("id,date,crime_type,lat,lon\n" + rows);
        int callbackCount = 0;
        CsvImporter::importFile(path, "test", [&](int, int) { ++callbackCount; });
        QVERIFY(callbackCount >= 1);
        QFile::remove(path);
    }

    void testImportCrimeTypeLowercased()
    {
        QString content = "id,date,crime_type\n1,2024-01-15,BURGLARY\n";
        QString path = writeTmpCsv(content);
        auto events = CsvImporter::importFile(path);
        QVERIFY(!events.isEmpty());
        QCOMPARE(events[0].crimeType, QStringLiteral("burglary"));
        QFile::remove(path);
    }

    void testImportSkipsRowsMissingBothDateAndType()
    {
        QString content = "id,date,crime_type\n"
                          "1,,\n"
                          "2,2024-01-15,theft\n";
        QString path = writeTmpCsv(content);
        auto events = CsvImporter::importFile(path);
        QCOMPARE(events.size(), 1);
        QFile::remove(path);
    }

    void testImportSourceTagPropagated()
    {
        QString content = "id,date,crime_type\n1,2024-01-15,theft\n";
        QString path = writeTmpCsv(content);
        auto events = CsvImporter::importFile(path, "nyc_open_data");
        QVERIFY(!events.isEmpty());
        QCOMPARE(events[0].source, QStringLiteral("nyc_open_data"));
        QFile::remove(path);
    }

    void testImportMixedFormats()
    {
        // Chicago-style date
        QString content = "case_number,date,primary_type,latitude,longitude\n"
                          "HZ100001,01/15/2024 10:30:00 AM,THEFT,41.8781,-87.6298\n"
                          "HZ100002,02/20/2024 02:00:00 PM,BURGLARY,41.8500,-87.6200\n";
        QString path = writeTmpCsv(content);
        auto events = CsvImporter::importFile(path, "chicago");
        QCOMPARE(events.size(), 2);
        QVERIFY(events[0].occurredAt.has_value());
        QVERIFY(events[0].lat.has_value());
        QFile::remove(path);
    }

    void testImportEventIdContainsSourceTag()
    {
        QString content = "id,date,crime_type\n42,2024-01-15,theft\n";
        QString path = writeTmpCsv(content);
        auto events = CsvImporter::importFile(path, "mysource");
        QVERIFY(!events.isEmpty());
        QVERIFY(events[0].eventId.contains("mysource"));
        QFile::remove(path);
    }
};

QTEST_MAIN(TestCsvImporter)
#include "test_csv_importer.moc"
