// test_csv_importer_deep7.cpp — Deep audit iteration 23: CsvImporter
// Chicago PD column detection, malformed rows, empty files, UTF-8 BOM,
// multi-format date parsing, and duplicate event IDs.

#include <QtTest/QtTest>
#include <QTemporaryFile>
#include <QTextStream>
#include <QFile>
#include <QDir>
#include <QUuid>

#include "ingest/CsvImporter.h"
#include "core/CrimeEvent.h"

class CsvImporterDeep7Test : public QObject
{
    Q_OBJECT

private:
    static QString writeTempCsv(const QString& content, const char* mode = "text")
    {
        QTemporaryFile* f = new QTemporaryFile(QStringLiteral("sentinel_deep7_XXXXXX.csv"));
        f->setAutoRemove(false);
        if (!f->open())
            return {};
        if (qstrcmp(mode, "raw") == 0) {
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
    void testChicagoPdColumnAutoDetection();
    void testMalformedRowSkippedNotCrash();
    void testEmptyFileReturnsZeroEvents();
    void testUtf8BomStrippedAndHeadersDetected();
    void testDateParsingMultipleFormats();
    void testDuplicateEventIdsBothImported();
    void testChicagoStyleDateInImportFile();
};

void CsvImporterDeep7Test::testChicagoPdColumnAutoDetection()
{
    const QStringList headers = {
        QStringLiteral("ID"),
        QStringLiteral("Case Number"),
        QStringLiteral("Date"),
        QStringLiteral("Primary Type"),
        QStringLiteral("Description"),
        QStringLiteral("Latitude"),
        QStringLiteral("Longitude"),
        QStringLiteral("Block"),
        QStringLiteral("Arrest"),
        QStringLiteral("Location Description"),
    };
    const CsvColumnMap map = CsvImporter::detectColumns(headers);

    QVERIFY(map.idCol >= 0);
    QCOMPARE(map.dateCol, 2);
    QVERIFY(map.crimeTypeCol >= 0);
    QVERIFY(map.descCol >= 0);
    QVERIFY(map.latCol >= 0);
    QVERIFY(map.lonCol >= 0);
    QVERIFY(map.addressCol >= 0);
    QVERIFY(map.outcomeCol >= 0);
    QVERIFY(map.locationCol >= 0);
}

void CsvImporterDeep7Test::testMalformedRowSkippedNotCrash()
{
    const QString content =
        QStringLiteral("id,date,crime_type,latitude,longitude\n"
                       "1,2024-01-01,theft,41.88,-87.63\n"
                       ",,\n"
                       "badrow\n"
                       "2,2024-01-02,robbery,41.89,-87.64\n");

    const QString path = writeTempCsv(content);
    QVERIFY(!path.isEmpty());

    const auto events = CsvImporter::importFile(path, QStringLiteral("chicago_pd"));
    QCOMPARE(events.size(), 2);
    QCOMPARE(events[0].crimeType, QStringLiteral("theft"));
    QCOMPARE(events[1].crimeType, QStringLiteral("robbery"));
    QFile::remove(path);
}

void CsvImporterDeep7Test::testEmptyFileReturnsZeroEvents()
{
    const QString path = writeTempCsv(QString{});
    QVERIFY(!path.isEmpty());

    const auto events = CsvImporter::importFile(path, QStringLiteral("deep7"));
    QVERIFY(events.isEmpty());
    QCOMPARE(events.size(), 0);
    QFile::remove(path);
}

void CsvImporterDeep7Test::testUtf8BomStrippedAndHeadersDetected()
{
    const QByteArray raw =
        QByteArray("\xEF\xBB\xBF") +
        "id,date,crime_type,description\n"
        "42,2024-03-15,theft,\"BOM-safe import\"\n";

    const QString path = writeTempCsv(QString::fromUtf8(raw), "raw");
    QVERIFY(!path.isEmpty());

    const auto events = CsvImporter::importFile(path, QStringLiteral("deep7"));
    QCOMPARE(events.size(), 1);
    QCOMPARE(events[0].eventId, QStringLiteral("deep7_42"));
    QCOMPARE(events[0].crimeType, QStringLiteral("theft"));
    QVERIFY(events[0].narrative.has_value());
    QVERIFY(events[0].narrative->contains(QStringLiteral("BOM-safe")));
    QFile::remove(path);
}

void CsvImporterDeep7Test::testDateParsingMultipleFormats()
{
    CsvColumnMap m;
    m.idCol        = 0;
    m.dateCol      = 1;
    m.crimeTypeCol = 2;

    const struct { const char* raw; int year; int month; int day; } cases[] = {
        { "2024-06-15 14:30:00", 2024, 6, 15 },
        { "2024-06-15",            2024, 6, 15 },
        { "06/15/2024 10:30:00",   2024, 6, 15 },
        { "06/15/2024",            2024, 6, 15 },
        { "15/06/2024 08:00",      2024, 6, 15 },
        { "6/5/2024 9:15",         2024, 6, 5  },
    };

    for (const auto& c : cases) {
        const QStringList fields = {
            QStringLiteral("fmt"),
            QString::fromUtf8(c.raw),
            QStringLiteral("theft"),
        };
        const CrimeEvent ev = CsvImporter::parseRow(fields, m, QStringLiteral("deep7"));
        QVERIFY2(ev.occurredAt.has_value(),
                 qPrintable(QStringLiteral("Failed to parse date: %1").arg(c.raw)));
        const QDate d = ev.occurredAt->date();
        QCOMPARE(d.year(),  c.year);
        QCOMPARE(d.month(), c.month);
        QCOMPARE(d.day(),   c.day);
    }
}

void CsvImporterDeep7Test::testDuplicateEventIdsBothImported()
{
    const QString content =
        QStringLiteral("id,date,crime_type\n"
                       "DUP-1,2024-04-01,theft\n"
                       "DUP-1,2024-04-02,burglary\n");

    const QString path = writeTempCsv(content);
    const auto events = CsvImporter::importFile(path, QStringLiteral("deep7"));

    QCOMPARE(events.size(), 2);
    QCOMPARE(events[0].eventId, QStringLiteral("deep7_DUP-1"));
    QCOMPARE(events[1].eventId, QStringLiteral("deep7_DUP-1"));
    QCOMPARE(events[0].crimeType, QStringLiteral("theft"));
    QCOMPARE(events[1].crimeType, QStringLiteral("burglary"));
    QFile::remove(path);
}

void CsvImporterDeep7Test::testChicagoStyleDateInImportFile()
{
    const QString content =
        QStringLiteral("ID,Date,Primary Type,Latitude,Longitude\n"
                       "1001,01/15/2024 10:30:00,THEFT,41.8781,-87.6298\n");

    const QString path = writeTempCsv(content);
    const auto events = CsvImporter::importFile(path, QStringLiteral("chicago_pd"));

    QCOMPARE(events.size(), 1);
    QCOMPARE(events[0].eventId, QStringLiteral("chicago_pd_1001"));
    QCOMPARE(events[0].crimeType, QStringLiteral("theft"));
    QVERIFY(events[0].occurredAt.has_value());
    QCOMPARE(events[0].occurredAt->date(), QDate(2024, 1, 15));
    QVERIFY(events[0].lat.has_value());
    QVERIFY(events[0].lon.has_value());
    QFile::remove(path);
}

QTEST_GUILESS_MAIN(CsvImporterDeep7Test)
#include "test_csv_importer_deep7.moc"
