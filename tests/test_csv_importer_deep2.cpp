#include <QtTest>
#include <QTemporaryFile>
#include <QTextStream>
#include "ingest/CsvImporter.h"
#include "core/CrimeEvent.h"

static QString writeTempCsv(const QStringList& lines)
{
    auto* f = new QTemporaryFile(QStringLiteral("XXXXXX.csv"));
    f->setAutoRemove(false);
    f->open();
    QTextStream out(f);
    out.setEncoding(QStringConverter::Utf8);
    for (const QString& line : lines) out << line << "\n";
    f->close();
    const QString path = f->fileName();
    delete f;
    return path;
}

class TestCsvImporterDeep2 : public QObject
{
    Q_OBJECT

private slots:

    // ── 1. Quoted field containing comma is parsed as single field ────────────
    void testQuotedFieldWithComma()
    {
        CsvColumnMap map;
        map.crimeTypeCol = 0;
        map.descCol      = 1;
        map.latCol       = 2;
        map.lonCol       = 3;

        const QStringList row = {
            QStringLiteral("theft"),
            QStringLiteral("stolen wallet, phone, and keys"),
            QStringLiteral("51.5074"),
            QStringLiteral("-0.1278"),
        };
        const auto ev = CsvImporter::parseRow(row, map, QStringLiteral("test"));
        QVERIFY2(ev.narrative.has_value(), "Description field should be set");
        QVERIFY2(ev.narrative->contains(QStringLiteral("wallet, phone")),
                 "Narrative should preserve embedded comma");
    }

    // ── 2. importFile: quoted field with comma produces one event, not split ──
    void testImportFileQuotedCommaField()
    {
        const QString path = writeTempCsv({
            QStringLiteral("Crime ID,Crime type,Description,Latitude,Longitude"),
            QStringLiteral("E1,theft,\"stolen wallet, keys\",51.5074,-0.1278"),
        });

        const auto evs = CsvImporter::importFile(path, QStringLiteral("test"));
        QFile::remove(path);

        QVERIFY2(evs.size() == 1,
                 qPrintable(QStringLiteral("Expected 1 event, got %1").arg(evs.size())));
        if (!evs.isEmpty()) {
            QVERIFY2(evs[0].narrative.has_value(), "Narrative should be set");
            QVERIFY2(evs[0].narrative->contains(QStringLiteral("wallet, keys")),
                     "Quoted field with comma should not be split");
        }
    }

    // ── 3. importFile: special characters in description survive round-trip ───
    void testImportFileSpecialCharsInDescription()
    {
        const QString path = writeTempCsv({
            QStringLiteral("Crime ID,Crime type,Description,Latitude,Longitude"),
            QStringLiteral("E2,robbery,\"Suspect wore balaclava & gloves (size 10/11)\",51.5,-0.1"),
        });

        const auto evs = CsvImporter::importFile(path, QStringLiteral("test"));
        QFile::remove(path);

        QVERIFY2(!evs.isEmpty(), "Should produce at least one event");
        if (!evs.isEmpty()) {
            QVERIFY2(evs[0].narrative.has_value(), "Narrative must be set");
            QVERIFY2(evs[0].narrative->contains(QStringLiteral("balaclava")),
                     "Special chars description should be preserved");
            QVERIFY2(evs[0].narrative->contains(QStringLiteral("&")),
                     "Ampersand should be preserved as-is in CSV narrative");
        }
    }

    // ── 4. parseRow: lat=91.0 is rejected (out of valid range) ───────────────
    void testCoordinateRejectsLatAbove90()
    {
        CsvColumnMap map;
        map.crimeTypeCol = 0;
        map.latCol       = 1;
        map.lonCol       = 2;

        const QStringList row = {
            QStringLiteral("theft"),
            QStringLiteral("91.0"),
            QStringLiteral("-0.1278"),
        };
        const auto ev = CsvImporter::parseRow(row, map, QStringLiteral("test"));
        QVERIFY2(!ev.lat.has_value(),
                 "Latitude 91.0 exceeds valid range [-90,90] and must be rejected");
    }

    // ── 5. parseRow: lat=51.5 is accepted (within valid range) ───────────────
    void testCoordinateAcceptsValidLat()
    {
        CsvColumnMap map;
        map.crimeTypeCol = 0;
        map.latCol       = 1;
        map.lonCol       = 2;

        const QStringList row = {
            QStringLiteral("theft"),
            QStringLiteral("51.5"),
            QStringLiteral("-0.12"),
        };
        const auto ev = CsvImporter::parseRow(row, map, QStringLiteral("test"));
        QVERIFY2(ev.lat.has_value(),
                 "Latitude 51.5 is in valid range and must be accepted");
        QVERIFY2(std::abs(ev.lat.value_or(0.0) - 51.5) < 0.0001,
                 "Parsed latitude should be 51.5");
    }

    // ── 6. parseRow: lon=181.0 is rejected ───────────────────────────────────
    void testCoordinateRejectsLonAbove180()
    {
        CsvColumnMap map;
        map.crimeTypeCol = 0;
        map.latCol       = 1;
        map.lonCol       = 2;

        const QStringList row = {
            QStringLiteral("theft"),
            QStringLiteral("51.5"),
            QStringLiteral("181.0"),
        };
        const auto ev = CsvImporter::parseRow(row, map, QStringLiteral("test"));
        QVERIFY2(!ev.lon.has_value(),
                 "Longitude 181.0 exceeds valid range [-180,180] and must be rejected");
    }

    // ── 7. parseRow: ISO 8601 date string is parsed correctly ────────────────
    void testDateParsingIso8601()
    {
        CsvColumnMap map;
        map.crimeTypeCol = 0;
        map.dateCol      = 1;
        map.latCol       = 2;
        map.lonCol       = 3;

        const QStringList row = {
            QStringLiteral("burglary"),
            QStringLiteral("2024-03-15T22:10:00"),
            QStringLiteral("51.5074"),
            QStringLiteral("-0.1278"),
        };
        const auto ev = CsvImporter::parseRow(row, map, QStringLiteral("test"));
        QVERIFY2(ev.occurredAt.has_value(), "occurredAt must be set from ISO 8601 string");
        QVERIFY2(ev.occurredAt->isValid(), "Parsed datetime must be valid");
        QCOMPARE(ev.occurredAt->date().year(),  2024);
        QCOMPARE(ev.occurredAt->date().month(), 3);
        QCOMPARE(ev.occurredAt->date().day(),   15);
        QCOMPARE(ev.occurredAt->time().hour(),  22);
    }

    // ── 8. parseRow: date in yyyy-MM-dd format is parsed ─────────────────────
    void testDateParsingYearMonthDay()
    {
        CsvColumnMap map;
        map.crimeTypeCol = 0;
        map.dateCol      = 1;

        const QStringList row = {
            QStringLiteral("theft"),
            QStringLiteral("2023-07-04"),
        };
        const auto ev = CsvImporter::parseRow(row, map, QStringLiteral("test"));
        QVERIFY2(ev.occurredAt.has_value(), "occurredAt must be parsed from yyyy-MM-dd");
        QCOMPARE(ev.occurredAt->date().year(),  2023);
        QCOMPARE(ev.occurredAt->date().month(), 7);
        QCOMPARE(ev.occurredAt->date().day(),   4);
    }

    // ── 9. parseRow: too few columns → no crash, missing fields default ───────
    void testMalformedRowTooFewColumns()
    {
        CsvColumnMap map;
        map.idCol        = 0;
        map.crimeTypeCol = 1;
        map.dateCol      = 2;
        map.latCol       = 5;
        map.lonCol       = 6;

        const QStringList shortRow = { QStringLiteral("ID1") };
        const auto ev = CsvImporter::parseRow(shortRow, map, QStringLiteral("test"));
        QVERIFY2(!ev.lat.has_value(), "Missing lat column must produce nullopt");
        QVERIFY2(!ev.lon.has_value(), "Missing lon column must produce nullopt");
    }

    // ── 10. parseRow: empty row produces safe defaults ────────────────────────
    void testEmptyRowSafeDefaults()
    {
        CsvColumnMap map;
        map.latCol       = 0;
        map.lonCol       = 1;
        map.crimeTypeCol = 2;

        const QStringList emptyRow = {};
        const auto ev = CsvImporter::parseRow(emptyRow, map, QStringLiteral("src"));
        QVERIFY2(!ev.lat.has_value(), "Empty row: lat must be nullopt");
        QVERIFY2(!ev.lon.has_value(), "Empty row: lon must be nullopt");
        QVERIFY2(ev.crimeType.isEmpty(), "Empty row: crimeType must be empty");
    }

    // ── 11. detectColumns: standard header row detects lat/lon/id/date ────────
    void testDetectColumnsStandardHeaders()
    {
        const QStringList headers = {
            QStringLiteral("Crime ID"),
            QStringLiteral("Month"),
            QStringLiteral("Crime type"),
            QStringLiteral("Description"),
            QStringLiteral("Latitude"),
            QStringLiteral("Longitude"),
            QStringLiteral("Outcome"),
        };
        const auto map = CsvImporter::detectColumns(headers);
        QVERIFY2(map.idCol >= 0,        "Crime ID column not detected");
        QVERIFY2(map.dateCol >= 0,      "Month/date column not detected");
        QVERIFY2(map.crimeTypeCol >= 0, "Crime type column not detected");
        QVERIFY2(map.descCol >= 0,      "Description column not detected");
        QVERIFY2(map.latCol >= 0,       "Latitude column not detected");
        QVERIFY2(map.lonCol >= 0,       "Longitude column not detected");
        QVERIFY2(map.outcomeCol >= 0,   "Outcome column not detected");
    }

    // ── 12. detectColumns: all-unknown headers leave cols at -1 ──────────────
    void testDetectColumnsUnknownHeaders()
    {
        const QStringList headers = {
            QStringLiteral("alpha"), QStringLiteral("beta"), QStringLiteral("gamma"),
        };
        const auto map = CsvImporter::detectColumns(headers);
        QVERIFY2(map.latCol == -1, "Unknown header must not map to latCol");
        QVERIFY2(map.lonCol == -1, "Unknown header must not map to lonCol");
        QVERIFY2(map.dateCol == -1, "Unknown header must not map to dateCol");
    }

    // ── 13. importFile: header-only CSV → empty event list ───────────────────
    void testImportFileHeaderOnlyIsEmpty()
    {
        const QString path = writeTempCsv({
            QStringLiteral("Crime ID,Crime type,Latitude,Longitude"),
        });
        const auto evs = CsvImporter::importFile(path, QStringLiteral("test"));
        QFile::remove(path);
        QVERIFY2(evs.isEmpty(), "Header-only CSV must yield no events");
    }

    // ── 14. importFile: rows with duplicate IDs both appear in results ────────
    void testImportFileDuplicateIdsKeptBoth()
    {
        const QString path = writeTempCsv({
            QStringLiteral("Crime ID,Crime type,Latitude,Longitude"),
            QStringLiteral("DUPE-1,theft,51.5,-0.1"),
            QStringLiteral("DUPE-1,burglary,51.6,-0.2"),
        });
        const auto evs = CsvImporter::importFile(path, QStringLiteral("test"));
        QFile::remove(path);
        QVERIFY2(evs.size() == 2,
                 qPrintable(QStringLiteral("Duplicate IDs should both be kept; got %1 events")
                     .arg(evs.size())));
    }

    // ── 15. Quoted field with embedded double-quote (RFC 4180 ""-escape) ──────
    void testImportFileEmbeddedQuote()
    {
        const QString path = writeTempCsv({
            QStringLiteral("Crime ID,Crime type,Description,Latitude,Longitude"),
            QStringLiteral("E3,assault,\"He said \"\"stop\"\" loudly\",51.5,-0.1"),
        });
        const auto evs = CsvImporter::importFile(path, QStringLiteral("test"));
        QFile::remove(path);

        QVERIFY2(!evs.isEmpty(), "Should produce at least one event");
        if (!evs.isEmpty()) {
            QVERIFY2(evs[0].narrative.has_value(), "Narrative must be set");
            QVERIFY2(evs[0].narrative->contains(QStringLiteral("\"stop\"")),
                     "Escaped quote inside quoted field should be unescaped");
        }
    }
};

QTEST_GUILESS_MAIN(TestCsvImporterDeep2)
#include "test_csv_importer_deep2.moc"
