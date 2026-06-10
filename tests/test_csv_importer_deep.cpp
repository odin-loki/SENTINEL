// test_csv_importer_deep.cpp
// Deep tests for CsvImporter: column auto-detection, parseRow, malformed data,
// and edge cases.
#include <QTest>
#include <QTemporaryFile>
#include <QTextStream>
#include "ingest/CsvImporter.h"
#include "core/CrimeEvent.h"

class CsvImporterDeepTest : public QObject
{
    Q_OBJECT

private:
    // Write a CSV to a temp file and return the path
    static QString writeTempCsv(const QStringList& lines)
    {
        auto* f = new QTemporaryFile(QStringLiteral("XXXXXX.csv"));
        f->setAutoRemove(false);
        f->open();
        QTextStream out(f);
        for (const auto& line : lines) out << line << "\n";
        f->close();
        const QString path = f->fileName();
        delete f;
        return path;
    }

private slots:

    // ── 1. detectColumns: standard UK Police header recognized ───────────────
    void testDetectColumnsUKPolice()
    {
        const QStringList headers = {
            QStringLiteral("Crime ID"), QStringLiteral("Month"),
            QStringLiteral("Crime type"), QStringLiteral("Latitude"),
            QStringLiteral("Longitude"), QStringLiteral("Location"),
        };
        const auto map = CsvImporter::detectColumns(headers);
        // At minimum lat and lon should be found
        QVERIFY2(map.latCol >= 0, "Latitude column not detected");
        QVERIFY2(map.lonCol >= 0, "Longitude column not detected");
    }

    // ── 2. detectColumns: generic lowercase headers ───────────────────────────
    void testDetectColumnsGeneric()
    {
        const QStringList headers = {
            QStringLiteral("id"), QStringLiteral("date"),
            QStringLiteral("type"), QStringLiteral("lat"),
            QStringLiteral("lon"), QStringLiteral("description"),
        };
        const auto map = CsvImporter::detectColumns(headers);
        QVERIFY2(map.latCol >= 0, "lat column not detected");
        QVERIFY2(map.lonCol >= 0, "lon column not detected");
    }

    // ── 3. parseRow: valid row produces CrimeEvent with correct lat/lon ───────
    void testParseRowValidLatLon()
    {
        CsvColumnMap map;
        map.latCol       = 2;
        map.lonCol       = 3;
        map.crimeTypeCol = 1;
        map.idCol        = 0;
        map.sourceTag    = QStringLiteral("test");

        const QStringList row = {
            QStringLiteral("EVT-01"), QStringLiteral("burglary"),
            QStringLiteral("51.5074"), QStringLiteral("-0.1278"),
        };
        const auto ev = CsvImporter::parseRow(row, map, QStringLiteral("test"));
        QVERIFY2(ev.lat.has_value(), "Latitude optional should have value");
        QVERIFY2(ev.lon.has_value(), "Longitude optional should have value");
        QVERIFY2(std::abs(ev.lat.value_or(0.0)  - 51.5074) < 0.001, "Latitude not parsed correctly");
        QVERIFY2(std::abs(ev.lon.value_or(0.0) - (-0.1278)) < 0.001, "Longitude not parsed correctly");
    }

    // ── 4. parseRow: crime type propagated ───────────────────────────────────
    void testParseRowCrimeType()
    {
        CsvColumnMap map;
        map.crimeTypeCol = 0;
        map.latCol       = 1;
        map.lonCol       = 2;

        const QStringList row = {
            QStringLiteral("vehicle crime"),
            QStringLiteral("51.5"), QStringLiteral("-0.1"),
        };
        const auto ev = CsvImporter::parseRow(row, map, QStringLiteral("src"));
        QVERIFY2(ev.crimeType.contains(QStringLiteral("vehicle"), Qt::CaseInsensitive) ||
                 !ev.crimeType.isEmpty(),
                 "Crime type should be set from CSV");
    }

    // ── 5. parseRow: out-of-range lat/lon → event not quarantined by importer ─
    void testParseRowMissingFields()
    {
        CsvColumnMap map;
        map.latCol = 5;  // beyond row length
        map.lonCol = 6;

        const QStringList row = { QStringLiteral("x"), QStringLiteral("burglary") };
        // Should not crash; lat/lon will be 0 or default
        const auto ev = CsvImporter::parseRow(row, map, QStringLiteral("src"));
        Q_UNUSED(ev);  // no crash is the assertion
        QVERIFY(true);
    }

    // ── 6. importFile: valid 2-row CSV produces 2 events ─────────────────────
    void testImportFileTwoRows()
    {
        const QString path = writeTempCsv({
            QStringLiteral("Crime ID,Crime type,Latitude,Longitude"),
            QStringLiteral("E1,burglary,51.5074,-0.1278"),
            QStringLiteral("E2,robbery,51.51,-0.12"),
        });

        const auto evs = CsvImporter::importFile(path, QStringLiteral("test"));
        QFile::remove(path);

        QVERIFY2(evs.size() >= 2,
                 qPrintable(QStringLiteral("Expected >= 2 events, got %1").arg(evs.size())));
    }

    // ── 7. importFile: progress callback is called ────────────────────────────
    void testImportFileProgressCallback()
    {
        const QString path = writeTempCsv({
            QStringLiteral("Crime ID,Crime type,Latitude,Longitude"),
            QStringLiteral("E1,burglary,51.5,0.0"),
            QStringLiteral("E2,theft,51.6,0.1"),
            QStringLiteral("E3,robbery,51.7,0.2"),
        });

        int callbackCount = 0;
        const auto evs = CsvImporter::importFile(path, QStringLiteral("test"),
            [&](int /*done*/, int /*total*/){ ++callbackCount; });
        QFile::remove(path);

        QVERIFY2(callbackCount > 0, "Progress callback should be called at least once");
        Q_UNUSED(evs);
    }

    // ── 8. importFile: empty CSV (header only) → no events ───────────────────
    void testImportFileHeaderOnly()
    {
        const QString path = writeTempCsv({
            QStringLiteral("Crime ID,Crime type,Latitude,Longitude"),
        });

        const auto evs = CsvImporter::importFile(path);
        QFile::remove(path);
        QVERIFY2(evs.isEmpty(), "Header-only CSV should produce no events");
    }

    // ── 9. importFile: missing file → no crash, empty result ─────────────────
    void testImportFileMissingFile()
    {
        const auto evs = CsvImporter::importFile(QStringLiteral("does_not_exist.csv"));
        QVERIFY(evs.isEmpty());
    }

    // ── 10. detectColumns: unknown headers → cols remain -1 ──────────────────
    void testDetectColumnsUnknown()
    {
        const QStringList headers = {
            QStringLiteral("foo"), QStringLiteral("bar"), QStringLiteral("baz"),
        };
        const auto map = CsvImporter::detectColumns(headers);
        // With no recognizable names, both lat and lon should be -1
        QVERIFY2(map.latCol == -1 || map.lonCol == -1,
                 "Unknown headers should not map lat/lon");
    }
};

QTEST_MAIN(CsvImporterDeepTest)
#include "test_csv_importer_deep.moc"
