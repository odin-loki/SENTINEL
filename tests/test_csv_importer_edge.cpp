// test_csv_importer_edge.cpp
// Edge case tests for CsvImporter: malformed CSV, empty files,
// auto column detection, and single-row parsing.
#include <QTest>
#include <QTemporaryFile>
#include <QTextStream>
#include "ingest/CsvImporter.h"

class CsvImporterEdgeTest : public QObject
{
    Q_OBJECT

private:
    static QString writeTempCsv(const QString& content)
    {
        QTemporaryFile* tmp = new QTemporaryFile(QStringLiteral("csv_XXXXXX.csv"));
        tmp->setAutoRemove(false);
        if (!tmp->open()) return {};
        QTextStream ts(tmp);
        ts << content;
        tmp->close();
        const QString path = tmp->fileName();
        delete tmp;
        return path;
    }

private slots:

    // 1. importFile: empty file returns empty vector, no crash
    void testEmptyFileNoCrash()
    {
        const QString path = writeTempCsv(QStringLiteral(""));
        const auto evs = CsvImporter::importFile(path, QStringLiteral("test"));
        QFile::remove(path);
        QVERIFY(evs.isEmpty());
    }

    // 2. importFile: header-only file returns empty vector
    void testHeaderOnlyNoCrash()
    {
        const QString path = writeTempCsv(
            QStringLiteral("Crime ID,Month,Reported by,Falls within,Longitude,Latitude,Crime type\n"));
        const auto evs = CsvImporter::importFile(path, QStringLiteral("test"));
        QFile::remove(path);
        QVERIFY(evs.isEmpty());
    }

    // 3. importFile: valid minimal CSV parses one event
    void testValidMinimalCsvParsesEvent()
    {
        const QString csv =
            QStringLiteral("Crime ID,Month,Longitude,Latitude,Crime type\n"
                           "ABC123,2024-01,-0.1,51.5,burglary\n");
        const QString path = writeTempCsv(csv);
        const auto evs = CsvImporter::importFile(path, QStringLiteral("test"));
        QFile::remove(path);
        QVERIFY2(evs.size() == 1, qPrintable(QStringLiteral("Expected 1 event, got %1").arg(evs.size())));
    }

    // 4. detectColumns: standard UK police headers detected
    void testDetectColumnsUKPolice()
    {
        const QStringList headers = {
            QStringLiteral("Crime ID"),
            QStringLiteral("Month"),
            QStringLiteral("Latitude"),
            QStringLiteral("Longitude"),
            QStringLiteral("Crime type")
        };
        const auto map = CsvImporter::detectColumns(headers);
        QVERIFY2(map.latCol >= 0, "latCol should be detected");
        QVERIFY2(map.lonCol >= 0, "lonCol should be detected");
        QVERIFY2(map.crimeTypeCol >= 0, "crimeTypeCol should be detected");
    }

    // 5. detectColumns: missing columns return -1
    void testDetectColumnsMissingReturnsMinusOne()
    {
        const QStringList headers = { QStringLiteral("Foo"), QStringLiteral("Bar") };
        const auto map = CsvImporter::detectColumns(headers);
        QVERIFY2(map.latCol == -1, "Unknown header: latCol should be -1");
        QVERIFY2(map.lonCol == -1, "Unknown header: lonCol should be -1");
    }

    // 6. parseRow: lat/lon correctly parsed as double
    void testParseRowLatLon()
    {
        CsvColumnMap map;
        map.latCol        = 2;
        map.lonCol        = 3;
        map.crimeTypeCol  = 4;
        map.dateCol       = 1;
        map.idCol         = 0;

        const QStringList fields = { QStringLiteral("ID1"), QStringLiteral("2024-03"),
                                      QStringLiteral("51.5074"), QStringLiteral("-0.1278"),
                                      QStringLiteral("theft") };
        const auto ev = CsvImporter::parseRow(fields, map, QStringLiteral("test"));
        // CsvImporter sets the optional ev.lat/ev.lon fields (not convenience fields)
        QVERIFY2(ev.lat.has_value() && std::abs(ev.lat.value() - 51.5074) < 1e-4,
                 qPrintable(QStringLiteral("lat %1 expected ~51.5074")
                    .arg(ev.lat.value_or(-999.0))));
        QVERIFY2(ev.lon.has_value() && std::abs(ev.lon.value() - (-0.1278)) < 1e-4,
                 qPrintable(QStringLiteral("lon %1 expected ~-0.1278")
                    .arg(ev.lon.value_or(-999.0))));
    }

    // 7. parseRow: crimeType set correctly
    void testParseRowCrimeType()
    {
        CsvColumnMap map;
        map.crimeTypeCol = 0;
        const QStringList fields = { QStringLiteral("burglary") };
        const auto ev = CsvImporter::parseRow(fields, map, QStringLiteral("test"));
        QVERIFY2(ev.crimeType.contains(QStringLiteral("burglary"), Qt::CaseInsensitive),
                 qPrintable(QStringLiteral("crimeType '%1' should contain 'burglary'").arg(ev.crimeType)));
    }

    // 8. importFile: multiple rows produce correct event count
    void testMultipleRowsCount()
    {
        const QString csv =
            QStringLiteral("Crime ID,Month,Longitude,Latitude,Crime type\n"
                           "A1,2024-01,-0.1,51.5,burglary\n"
                           "A2,2024-01,-0.2,51.6,theft\n"
                           "A3,2024-02,-0.3,51.7,robbery\n");
        const QString path = writeTempCsv(csv);
        const auto evs = CsvImporter::importFile(path, QStringLiteral("test"));
        QFile::remove(path);
        QCOMPARE(evs.size(), 3);
    }

    // 9. importFile: non-existent file returns empty vector, no crash
    void testNonExistentFileNoCrash()
    {
        const auto evs = CsvImporter::importFile(
            QStringLiteral("c:/nonexistent/file_that_does_not_exist.csv"), QStringLiteral("test"));
        QVERIFY(evs.isEmpty());
    }

    // 10. importFile: progress callback is called
    void testProgressCallbackCalled()
    {
        const QString csv =
            QStringLiteral("Crime ID,Month,Longitude,Latitude,Crime type\n"
                           "A1,2024-01,-0.1,51.5,burglary\n"
                           "A2,2024-01,-0.2,51.6,theft\n");
        const QString path = writeTempCsv(csv);

        int callCount = 0;
        const auto evs = CsvImporter::importFile(
            path, QStringLiteral("test"),
            [&callCount](int /*done*/, int /*total*/) { ++callCount; });

        QFile::remove(path);
        QVERIFY2(callCount > 0, "Progress callback should be called at least once");
    }
};

QTEST_MAIN(CsvImporterEdgeTest)
#include "test_csv_importer_edge.moc"
