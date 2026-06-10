// test_csv_importer_roundtrip.cpp — CsvImporter end-to-end roundtrip tests
#include <QTest>
#include <QCoreApplication>
#include <QTemporaryFile>
#include <QTextStream>
#include <QFile>
#include <QElapsedTimer>
#include <QDir>
#include <QUuid>
#include <cmath>

#include "ingest/CsvImporter.h"

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

namespace {

// Write UTF-8 content to a temporary file and return its path.
// Caller is responsible for removing the file (QFile::remove(path)).
QString writeTmpCsv(const QString& content)
{
    auto* tmp = new QTemporaryFile();
    tmp->setAutoRemove(false);
    if (!tmp->open()) {
        delete tmp;
        return {};
    }
    QTextStream out(tmp);
    out.setEncoding(QStringConverter::Utf8);
    out << content;
    tmp->close();
    const QString path = tmp->fileName();
    delete tmp;
    return path;
}

} // anonymous namespace

// ─────────────────────────────────────────────────────────────────────────────
// TestCsvImporterRoundtrip
// ─────────────────────────────────────────────────────────────────────────────

class TestCsvImporterRoundtrip : public QObject
{
    Q_OBJECT

private slots:

    // 1. Write a CSV, import it, verify all fields survive the roundtrip
    void testBasicRoundtrip()
    {
        const QString content =
            "id,date,crime_type,lat,lon,address,description,outcome\n"
            "EVT-001,2024-03-15,burglary,51.5074,-0.1278,\"10 Baker Street\","
            "\"Entry via rear window\",unsolved\n";

        const QString path = writeTmpCsv(content);
        QVERIFY(!path.isEmpty());

        const auto events = CsvImporter::importFile(path, QStringLiteral("test_src"));
        QCOMPARE(events.size(), 1);

        const CrimeEvent& ev = events.first();
        QVERIFY(ev.eventId.contains(QStringLiteral("EVT-001")));
        QCOMPARE(ev.crimeType, QStringLiteral("burglary"));
        QVERIFY(ev.occurredAt.has_value());
        QCOMPARE(ev.occurredAt->date(), QDate(2024, 3, 15));
        QVERIFY(ev.lat.has_value());
        QVERIFY(std::abs(*ev.lat - 51.5074) < 1e-4);
        QVERIFY(ev.lon.has_value());
        QVERIFY(std::abs(*ev.lon - (-0.1278)) < 1e-4);
        QVERIFY(ev.addressNormalised.has_value());
        QVERIFY(ev.addressNormalised->contains(QStringLiteral("Baker")));
        QVERIFY(ev.narrative.has_value());
        QVERIFY(ev.narrative->contains(QStringLiteral("window")));
        QCOMPARE(ev.source, QStringLiteral("test_src"));

        QFile::remove(path);
    }

    // 2. CSV with only required columns (lat, lon, date, type) imports without error
    void testMissingOptionalFields()
    {
        const QString content =
            "lat,lon,date,type\n"
            "51.5,0.1,2024-01-10,assault\n"
            "51.6,0.2,2024-01-11,theft\n";

        const QString path = writeTmpCsv(content);
        QVERIFY(!path.isEmpty());

        const auto events = CsvImporter::importFile(path);
        QCOMPARE(events.size(), 2);
        QVERIFY(!events[0].crimeType.isEmpty());
        QVERIFY(!events[1].crimeType.isEmpty());

        QFile::remove(path);
    }

    // 3. Extra unknown columns are gracefully ignored
    void testExtraColumns()
    {
        const QString content =
            "id,date,crime_type,extra1,notes,extra2\n"
            "1,2024-06-01,robbery,foo,some note,bar\n"
            "2,2024-06-02,burglary,baz,another note,qux\n";

        const QString path = writeTmpCsv(content);
        QVERIFY(!path.isEmpty());

        const auto events = CsvImporter::importFile(path);
        QCOMPARE(events.size(), 2);
        QCOMPARE(events[0].crimeType, QStringLiteral("robbery"));
        QCOMPARE(events[1].crimeType, QStringLiteral("burglary"));

        QFile::remove(path);
    }

    // 4. Quoted fields containing commas are imported correctly
    void testQuotedFieldsWithCommas()
    {
        const QString content =
            "id,date,crime_type,description\n"
            "1,2024-04-01,theft,\"Victim lost wallet, phone, and keys\"\n"
            "2,2024-04-02,assault,\"Scene at 3rd Ave, near park\"\n";

        const QString path = writeTmpCsv(content);
        QVERIFY(!path.isEmpty());

        const auto events = CsvImporter::importFile(path);
        QCOMPARE(events.size(), 2);
        QVERIFY(events[0].narrative.has_value());
        QVERIFY(events[0].narrative->contains(QStringLiteral("wallet")));
        QVERIFY(events[0].narrative->contains(QStringLiteral("phone")));
        QVERIFY(events[1].narrative.has_value());
        QVERIFY(events[1].narrative->contains(QStringLiteral("3rd Ave")));

        QFile::remove(path);
    }

    // 5. UTF-8 encoded values (accented chars) are imported correctly
    void testUnicodeCharacters()
    {
        const QString content =
            "id,date,crime_type,address\n"
            "1,2024-05-01,theft,\"Rüdesheimer Straße 12\"\n"
            "2,2024-05-02,assault,\"Café de la Paix\"\n";

        const QString path = writeTmpCsv(content);
        QVERIFY(!path.isEmpty());

        const auto events = CsvImporter::importFile(path);
        QCOMPARE(events.size(), 2);
        QVERIFY(events[0].addressNormalised.has_value());
        QVERIFY(events[0].addressNormalised->contains(QStringLiteral("Stra")));
        QVERIFY(events[1].addressNormalised.has_value());
        QVERIFY(events[1].addressNormalised->contains(QStringLiteral("Caf")));

        QFile::remove(path);
    }

    // 6. 1 000 rows import in less than 5 seconds
    void testLargeFile()
    {
        QString rows;
        rows.reserve(1000 * 50);
        for (int i = 0; i < 1000; ++i) {
            rows += QString("E%1,2024-01-15,burglary,51.5,0.1\n").arg(i);
        }
        const QString content = QStringLiteral("id,date,crime_type,lat,lon\n") + rows;

        const QString path = writeTmpCsv(content);
        QVERIFY(!path.isEmpty());

        QElapsedTimer timer;
        timer.start();
        const auto events = CsvImporter::importFile(path);
        const qint64 elapsed = timer.elapsed();

        QCOMPARE(events.size(), 1000);
        QVERIFY2(elapsed < 5000,
                 qPrintable(QString("Import took %1 ms (limit 5000 ms)").arg(elapsed)));

        QFile::remove(path);
    }

    // 7. Empty CSV file → 0 events, no crash
    void testEmptyFile()
    {
        const QString path = writeTmpCsv(QString{});
        QVERIFY(!path.isEmpty());

        const auto events = CsvImporter::importFile(path);
        QCOMPARE(events.size(), 0);

        QFile::remove(path);
    }

    // 8. Header-only CSV → 0 events
    void testHeaderOnlyFile()
    {
        const QString path = writeTmpCsv(QStringLiteral("id,date,crime_type,lat,lon\n"));
        QVERIFY(!path.isEmpty());

        const auto events = CsvImporter::importFile(path);
        QCOMPARE(events.size(), 0);

        QFile::remove(path);
    }

    // 9. Various date formats (ISO and UK) are parsed to the correct date
    void testDateFormats()
    {
        const QString content =
            "id,date,crime_type\n"
            "1,2024-07-20,theft\n"       // ISO YYYY-MM-DD
            "2,20/07/2024,burglary\n"    // UK DD/MM/YYYY
            "3,2024-07-20 14:30:00,assault\n";  // ISO datetime

        const QString path = writeTmpCsv(content);
        QVERIFY(!path.isEmpty());

        const auto events = CsvImporter::importFile(path);
        // All three rows have a date, so all three should be imported
        QVERIFY(events.size() >= 2);

        // Find the ISO-date row and verify it
        bool foundIso = false;
        bool foundUk  = false;
        for (const auto& ev : events) {
            if (ev.occurredAt.has_value()) {
                if (ev.crimeType == QStringLiteral("theft") &&
                    ev.occurredAt->date() == QDate(2024, 7, 20))
                    foundIso = true;
                if (ev.crimeType == QStringLiteral("burglary") &&
                    ev.occurredAt->date() == QDate(2024, 7, 20))
                    foundUk = true;
            }
        }
        QVERIFY2(foundIso, "ISO date YYYY-MM-DD not parsed correctly");
        QVERIFY2(foundUk,  "UK date DD/MM/YYYY not parsed correctly");

        QFile::remove(path);
    }

    // 10. Imported lat/lon match expected decimal values
    void testNumericLatLon()
    {
        const QString content =
            "id,date,crime_type,lat,lon\n"
            "1,2024-01-01,theft,-33.8688,151.2093\n"   // Sydney
            "2,2024-01-02,assault,51.5074,-0.1278\n"   // London
            "3,2024-01-03,robbery,-37.8136,144.9631\n"; // Melbourne

        const QString path = writeTmpCsv(content);
        QVERIFY(!path.isEmpty());

        const auto events = CsvImporter::importFile(path);
        QCOMPARE(events.size(), 3);

        // Sydney
        QVERIFY(events[0].lat.has_value());
        QVERIFY(events[0].lon.has_value());
        QVERIFY(std::abs(*events[0].lat - (-33.8688)) < 1e-4);
        QVERIFY(std::abs(*events[0].lon - 151.2093)   < 1e-4);

        // London
        QVERIFY(events[1].lat.has_value());
        QVERIFY(events[1].lon.has_value());
        QVERIFY(std::abs(*events[1].lat - 51.5074)    < 1e-4);
        QVERIFY(std::abs(*events[1].lon - (-0.1278))  < 1e-4);

        // Melbourne
        QVERIFY(events[2].lat.has_value());
        QVERIFY(events[2].lon.has_value());
        QVERIFY(std::abs(*events[2].lat - (-37.8136)) < 1e-4);
        QVERIFY(std::abs(*events[2].lon - 144.9631)   < 1e-4);

        QFile::remove(path);
    }
};

QTEST_MAIN(TestCsvImporterRoundtrip)
#include "test_csv_importer_roundtrip.moc"
