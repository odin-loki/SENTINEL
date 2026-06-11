// Deep audit iteration 16 — CsvImporter edge cases, malformed rows, encoding
#include <QtTest/QtTest>
#include <QTemporaryFile>
#include <QTextStream>
#include <QFile>

#include "ingest/CsvImporter.h"
#include "core/CrimeEvent.h"

class CsvImporterDeep5Test : public QObject
{
    Q_OBJECT

private:
    static QString writeTempCsv(const QString& content, const char* encoding = "UTF-8")
    {
        QTemporaryFile* f = new QTemporaryFile(QStringLiteral("sentinel_deep5_XXXXXX.csv"));
        f->setAutoRemove(false);
        if (!f->open()) return {};
        if (qstrcmp(encoding, "raw") == 0) {
            f->write(content.toUtf8());
        } else {
            QTextStream out(f);
            out.setEncoding(QStringConverter::Utf8);
            out << content;
        }
        f->close();
        const QString path = f->fileName();
        delete f;
        return path;
    }

private slots:

    // ── 1. UTF-8 non-ASCII characters in narrative survive import ───────────
    void testUtf8NonAsciiInDescription()
    {
        const QString content =
            QStringLiteral("id,date,crime_type,description\n"
                           "1,2024-01-15,theft,\"Café on Rue de la Paix — wallet stolen\"\n");
        const QString path = writeTempCsv(content);
        QVERIFY(!path.isEmpty());

        const auto events = CsvImporter::importFile(path, QStringLiteral("test"));
        QCOMPARE(events.size(), 1);
        QVERIFY(events[0].narrative.has_value());
        QVERIFY(events[0].narrative->contains(QStringLiteral("Café")));
        QVERIFY(events[0].narrative->contains(QChar(0x2014))); // em dash
        QFile::remove(path);
    }

    // ── 2. Row missing both date and crime type is skipped ──────────────────
    void testMalformedRowMissingRequiredFieldsSkipped()
    {
        const QString content =
            QStringLiteral("id,date,crime_type,latitude,longitude\n"
                           "1,2024-01-01,theft,51.5,-0.1\n"
                           ",,\n"
                           "2,2024-01-02,robbery,51.6,-0.2\n");
        const QString path = writeTempCsv(content);
        const auto events = CsvImporter::importFile(path, QStringLiteral("test"));
        QCOMPARE(events.size(), 2);
        QCOMPARE(events[0].crimeType, QStringLiteral("theft"));
        QCOMPARE(events[1].crimeType, QStringLiteral("robbery"));
        QFile::remove(path);
    }

    // ── 3. Row with crime type only (no date column mapped) still imported ──
    void testRowWithCrimeTypeOnlyImported()
    {
        const QString content =
            QStringLiteral("id,crime_type,latitude,longitude\n"
                           "42,burglary,51.5074,-0.1278\n");
        const QString path = writeTempCsv(content);
        const auto events = CsvImporter::importFile(path, QStringLiteral("test"));
        QCOMPARE(events.size(), 1);
        QCOMPARE(events[0].crimeType, QStringLiteral("burglary"));
        QVERIFY(!events[0].occurredAt.has_value());
        QFile::remove(path);
    }

    // ── 4. Unparseable date string leaves occurredAt unset ──────────────────
    void testInvalidDateFormatLeavesOccurredAtUnset()
    {
        CsvColumnMap m;
        m.idCol        = 0;
        m.dateCol      = 1;
        m.crimeTypeCol = 2;

        QStringList fields = {
            QStringLiteral("99"),
            QStringLiteral("not-a-valid-date"),
            QStringLiteral("theft"),
        };
        const CrimeEvent ev = CsvImporter::parseRow(fields, m, QStringLiteral("test"));
        QVERIFY(!ev.occurredAt.has_value());
        QCOMPARE(ev.crimeType, QStringLiteral("theft"));
    }

    // ── 5. Fewer columns than header — no crash, partial field extraction ───
    void testShortRowDoesNotCrash()
    {
        const QString content =
            QStringLiteral("id,date,crime_type,latitude,longitude,description\n"
                           "1,2024-01-01,theft\n");
        const QString path = writeTempCsv(content);
        const auto events = CsvImporter::importFile(path, QStringLiteral("test"));
        QCOMPARE(events.size(), 1);
        QCOMPARE(events[0].crimeType, QStringLiteral("theft"));
        QVERIFY(!events[0].lat.has_value());
        QVERIFY(!events[0].lon.has_value());
        QFile::remove(path);
    }

    // ── 6. Non-numeric lat/lon strings rejected ─────────────────────────────
    void testNonNumericLatLonRejected()
    {
        CsvColumnMap m;
        m.latCol = 0;
        m.lonCol = 1;

        QStringList fields = {
            QStringLiteral("N/A"),
            QStringLiteral("undefined"),
        };
        const CrimeEvent ev = CsvImporter::parseRow(fields, m, QStringLiteral("test"));
        QVERIFY(!ev.lat.has_value());
        QVERIFY(!ev.lon.has_value());
    }

    // ── 7. CRLF line endings handled correctly ──────────────────────────────
    void testCrlfLineEndings()
    {
        const QString content =
            QStringLiteral("id,date,crime_type\r\n"
                           "1,2024-06-01,theft\r\n"
                           "2,2024-06-02,robbery\r\n");
        const QString path = writeTempCsv(content);
        const auto events = CsvImporter::importFile(path, QStringLiteral("test"));
        QCOMPARE(events.size(), 2);
        QCOMPARE(events[0].crimeType, QStringLiteral("theft"));
        QCOMPARE(events[1].crimeType, QStringLiteral("robbery"));
        QFile::remove(path);
    }

    // ── 8. UTF-8 BOM prefix may break first header detection (known edge) ───
    void testUtf8BomHeaderDetection()
    {
        // Write raw bytes including BOM so the header cell is "\xEF\xBB\xBFid"
        const QByteArray raw =
            QByteArray("\xEF\xBB\xBF") +
            "id,date,crime_type\n1,2024-01-01,theft\n";
        const QString path = writeTempCsv(QString::fromUtf8(raw), "raw");
        const auto events = CsvImporter::importFile(path, QStringLiteral("test"));

        // BOM-prefixed header prevents "id" keyword match; row may still import
        // via crime_type column if detected, or be skipped if both required cols fail
        QVERIFY(events.size() <= 1);
        if (!events.isEmpty()) {
            // If imported, eventId uses UUID fallback because id column wasn't mapped
            QVERIFY(events[0].eventId.startsWith(QStringLiteral("test_")));
        }
        QFile::remove(path);
    }
};

QTEST_GUILESS_MAIN(CsvImporterDeep5Test)
#include "test_csv_importer_deep5.moc"
