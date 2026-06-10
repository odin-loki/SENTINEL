// test_csv_edge_cases.cpp — Edge-case tests for CsvImporter
// Covers: line endings, quoting, UTF-8 BOM, coordinates, format detection,
// large files, missing columns, duplicate headers, empty rows, and progress.

#include <QTest>
#include <QCoreApplication>
#include <QTemporaryFile>
#include <QTextStream>
#include <QElapsedTimer>
#include <cmath>

#include "ingest/CsvImporter.h"

class TestCsvEdgeCases : public QObject
{
    Q_OBJECT

private:
    // Write CSV content as UTF-8 text via QTextStream (platform newline translation)
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

    // Write raw bytes to a temp file — no newline translation
    static QString writeBinaryCsv(const QByteArray& data)
    {
        auto* tmp = new QTemporaryFile();
        tmp->setAutoRemove(false);
        if (!tmp->open()) return {};
        tmp->write(data);
        tmp->close();
        QString path = tmp->fileName();
        delete tmp;
        return path;
    }

private slots:

    // ── 1. Windows \r\n line endings ─────────────────────────────────────────

    void testWindowsLineEndings()
    {
        // File written in binary mode with explicit \r\n
        QByteArray data =
            "id,date,crime_type,lat,lon\r\n"
            "1,2024-01-15,burglary,51.5074,-0.1278\r\n"
            "2,2024-01-16,theft,51.6,-0.2\r\n";
        QString path = writeBinaryCsv(data);
        auto events = CsvImporter::importFile(path);
        QCOMPARE(events.size(), 2);
        QCOMPARE(events[0].crimeType, QStringLiteral("burglary"));
        QCOMPARE(events[1].crimeType, QStringLiteral("theft"));
        QFile::remove(path);
    }

    // ── 2. Mixed quoting: some fields quoted, some not ───────────────────────

    void testMixedQuoting()
    {
        QString content =
            "id,date,crime_type,description\n"
            "1,2024-01-15,burglary,\"forced entry via rear\"\n"
            "2,2024-01-16,theft,no weapon used\n";
        QString path = writeTmpCsv(content);
        auto events = CsvImporter::importFile(path);
        QCOMPARE(events.size(), 2);
        QVERIFY(events[0].narrative.has_value());
        QVERIFY(events[0].narrative->contains("rear"));
        QVERIFY(events[1].narrative.has_value());
        QVERIFY(events[1].narrative->contains("weapon"));
        QFile::remove(path);
    }

    // ── 3. Embedded comma inside a quoted field ───────────────────────────────

    void testEmbeddedCommaInQuotes()
    {
        // "Smith, Jr." contains a comma — must stay as one field
        QString content =
            "id,date,crime_type,description\n"
            "1,2024-01-15,burglary,\"Smith, Jr. victim\"\n";
        QString path = writeTmpCsv(content);
        auto events = CsvImporter::importFile(path);
        QCOMPARE(events.size(), 1);
        QVERIFY(events[0].narrative.has_value());
        // Comma inside quotes must not split the field
        QVERIFY(events[0].narrative->contains("Smith"));
        QVERIFY(events[0].narrative->contains("Jr"));
        QFile::remove(path);
    }

    // ── 4. Embedded newline inside a quoted field ─────────────────────────────

    void testEmbeddedNewlineInQuotes()
    {
        // Binary write so we control the exact bytes; \n inside quotes is literal
        QByteArray data =
            "id,date,crime_type,description\r\n"
            "1,2024-01-15,burglary,\"line one\nline two\"\r\n";
        QString path = writeBinaryCsv(data);
        auto events = CsvImporter::importFile(path);
        // The importer should stitch lines until quote count is even
        QCOMPARE(events.size(), 1);
        QVERIFY(events[0].narrative.has_value());
        QFile::remove(path);
    }

    // ── 5. UTF-8 BOM at file start ────────────────────────────────────────────

    void testUtf8BOM()
    {
        QByteArray bom  = QByteArray::fromHex("EFBBBF");
        QByteArray csv  = "id,date,crime_type\n1,2024-01-15,burglary\n";
        QString path    = writeBinaryCsv(bom + csv);
        // Must not crash; BOM should not prevent parsing
        auto events = CsvImporter::importFile(path);
        QCOMPARE(events.size(), 1);
        QCOMPARE(events[0].crimeType, QStringLiteral("burglary"));
        QFile::remove(path);
    }

    // ── 6. Numeric lat/lon parsed to double within 1e-6 ──────────────────────

    void testNumericLatLonParsing()
    {
        CsvColumnMap m;
        m.latCol = 0; m.lonCol = 1; m.crimeTypeCol = 2; m.dateCol = 3;
        m.sourceTag = "test";
        QStringList fields = {"51.5074", "-0.1278", "burglary", "2024-01-15"};
        auto ev = CsvImporter::parseRow(fields, m, "test");
        QVERIFY(ev.lat.has_value());
        QVERIFY(std::abs(*ev.lat - 51.5074) < 1e-6);
    }

    // ── 7. Negative longitude sign preserved ─────────────────────────────────

    void testNegativeCoordinates()
    {
        CsvColumnMap m;
        m.latCol = 0; m.lonCol = 1; m.crimeTypeCol = 2; m.dateCol = 3;
        m.sourceTag = "test";
        QStringList fields = {"51.5074", "-0.1278", "burglary", "2024-01-15"};
        auto ev = CsvImporter::parseRow(fields, m, "test");
        QVERIFY(ev.lon.has_value());
        QVERIFY(*ev.lon < 0.0);
        QVERIFY(std::abs(*ev.lon - (-0.1278)) < 1e-6);
    }

    // ── 8. Chicago-style column detection ────────────────────────────────────

    void testChicagoFormat()
    {
        // Headers as they appear in the Chicago Data Portal export
        QStringList headers = {
            "ID", "Date", "Primary Type", "Description",
            "Latitude", "Longitude", "Block", "Arrest",
            "Location Description"
        };
        CsvColumnMap m = CsvImporter::detectColumns(headers);
        QVERIFY(m.crimeTypeCol >= 0);   // "Primary Type" contains "type"
        QVERIFY(m.latCol       >= 0);   // "Latitude"     contains "lat"
        QVERIFY(m.lonCol       >= 0);   // "Longitude"    contains "lon"
        QVERIFY(m.dateCol      >= 0);   // "Date"         contains "date"
        QVERIFY(m.outcomeCol   >= 0);   // "Arrest"       contains "arrest"
        QVERIFY(m.addressCol   >= 0);   // "Block"        contains "block"
        QVERIFY(m.locationCol  >= 0);   // "Location Description" contains "location"
    }

    // ── 9. NYC-style column detection ────────────────────────────────────────

    void testNYCFormat()
    {
        // Headers matching NYC open-data incident exports
        QStringList headers = {
            "INCIDENT_KEY", "OCCUR_DATE", "OFFENSE",
            "NARRATIVE", "Latitude", "Longitude"
        };
        CsvColumnMap m = CsvImporter::detectColumns(headers);
        QVERIFY(m.idCol        >= 0);   // "INCIDENT_KEY" contains "incident"
        QVERIFY(m.dateCol      >= 0);   // "OCCUR_DATE"   contains "date"
        QVERIFY(m.crimeTypeCol >= 0);   // "OFFENSE"      contains "offense"
        QVERIFY(m.descCol      >= 0);   // "NARRATIVE"    contains "narrative"
        QVERIFY(m.latCol       >= 0);   // "Latitude"     contains "lat"
        QVERIFY(m.lonCol       >= 0);   // "Longitude"    contains "lon"
    }

    // ── 10. 1000-row import completes in < 5 s ────────────────────────────────

    void testLargeFile()
    {
        QString rows;
        rows.reserve(60 * 1000);
        for (int i = 0; i < 1000; ++i)
            rows += QString("E%1,2024-01-15,burglary,51.5,0.1\n").arg(i);
        QString path = writeTmpCsv("id,date,crime_type,lat,lon\n" + rows);

        QElapsedTimer timer;
        timer.start();
        auto events = CsvImporter::importFile(path);
        const qint64 elapsed = timer.elapsed();

        QCOMPARE(events.size(), 1000);
        QVERIFY2(elapsed < 5000,
                 qPrintable(QString("Import took %1 ms, expected < 5000 ms").arg(elapsed)));
        QFile::remove(path);
    }

    // ── 11. File with only required columns doesn't crash ────────────────────

    void testMissingOptionalColumns()
    {
        // No lat, lon, description, or address columns
        QString content =
            "date,crime_type\n"
            "2024-01-15,burglary\n"
            "2024-01-16,theft\n";
        QString path = writeTmpCsv(content);
        auto events = CsvImporter::importFile(path);
        QCOMPARE(events.size(), 2);
        for (const auto& ev : events) {
            QVERIFY(!ev.lat.has_value());
            QVERIFY(!ev.lon.has_value());
            QVERIFY(!ev.narrative.has_value());
        }
        QFile::remove(path);
    }

    // ── 12. Duplicate header name — no crash, first column wins ──────────────

    void testDuplicateHeaders()
    {
        // Two columns with the same name; detectColumns uses the first match
        QString content =
            "id,date,crime_type,crime_type\n"
            "1,2024-01-15,burglary,theft\n";
        QString path = writeTmpCsv(content);
        auto events = CsvImporter::importFile(path);
        // Must not crash
        QCOMPARE(events.size(), 1);
        // First crime_type column (index 2 = "burglary") should be used
        QCOMPARE(events[0].crimeType, QStringLiteral("burglary"));
        QFile::remove(path);
    }

    // ── 13. Rows with only commas are skipped ────────────────────────────────

    void testEmptyRows()
    {
        QString content =
            "id,date,crime_type\n"
            ",,\n"
            "1,2024-01-15,burglary\n"
            ",,\n"
            "2,2024-01-16,theft\n";
        QString path = writeTmpCsv(content);
        auto events = CsvImporter::importFile(path);
        // Rows with empty date AND empty crime_type are skipped
        QCOMPARE(events.size(), 2);
        QFile::remove(path);
    }

    // ── 14. Progress callback called with non-decreasing values ending at 1.0 ─

    void testProgressCallback()
    {
        // 1000 rows → callback fires at row 500 and at end
        QString rows;
        rows.reserve(60 * 1000);
        for (int i = 0; i < 1000; ++i)
            rows += QString("E%1,2024-01-15,burglary,51.5,0.1\n").arg(i);
        QString path = writeTmpCsv("id,date,crime_type,lat,lon\n" + rows);

        QVector<double> progressValues;
        CsvImporter::importFile(path, "test", [&](int done, int total) {
            if (total > 0)
                progressValues.append(static_cast<double>(done) / static_cast<double>(total));
        });

        QVERIFY(!progressValues.isEmpty());
        // Values must be non-decreasing
        for (int i = 1; i < progressValues.size(); ++i)
            QVERIFY(progressValues[i] >= progressValues[i - 1]);
        // Final callback always passes done==total → ratio is exactly 1.0
        QCOMPARE(progressValues.last(), 1.0);
        QFile::remove(path);
    }

    // ── 15. Narrative with embedded quotes and commas parsed correctly ────────

    void testSpecialCharsInNarrative()
    {
        // RFC 4180: double-double-quote escapes a literal quote inside a field
        QString content =
            "id,date,crime_type,description\n"
            "1,2024-01-15,burglary,\"suspect said \"\"hello\"\", then fled\"\n";
        QString path = writeTmpCsv(content);
        auto events = CsvImporter::importFile(path);
        QCOMPARE(events.size(), 1);
        QVERIFY(events[0].narrative.has_value());
        QVERIFY(events[0].narrative->contains("hello"));
        // The comma after "hello" must remain inside the field (not split it)
        QVERIFY(events[0].narrative->contains("fled"));
        QFile::remove(path);
    }
};

QTEST_MAIN(TestCsvEdgeCases)
#include "test_csv_edge_cases.moc"
