// test_csv_importer_deep6.cpp — Iteration 19 deep audit: column-detection
// misclassification, boundary parsing, and import edge cases.
#include <QtTest/QtTest>
#include <QTemporaryFile>
#include <QTextStream>
#include <QFile>
#include <QDir>
#include <QUuid>

#include "ingest/CsvImporter.h"
#include "core/CrimeEvent.h"

class CsvImporterDeep6Test : public QObject
{
    Q_OBJECT

private:
    static QString writeTempCsv(const QString& content, const char* encoding = "UTF-8")
    {
        QTemporaryFile* f = new QTemporaryFile(QStringLiteral("sentinel_deep6_XXXXXX.csv"));
        f->setAutoRemove(false);
        if (!f->open())
            return {};
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
    void testIncidentDateHeaderMisclassifiedAsId();
    void testLocationTypeHeaderMisclassifiedAsCrimeType();
    void testDateOnlyRowImportedWithoutCrimeType();
    void testLatLonOutOfRangeRejected();
    void testQuotedFieldWithEmbeddedCommaAndNewline();
    void testEmptyFileReturnsNoEvents();
    void testCrimeTypeNormalizedToLowercase();
    void testMissingFileReturnsEmptyVector();
};

void CsvImporterDeep6Test::testIncidentDateHeaderMisclassifiedAsId()
{
    const QStringList headers = {
        QStringLiteral("incident_date"),
        QStringLiteral("primary_type"),
        QStringLiteral("description"),
    };
    const CsvColumnMap map = CsvImporter::detectColumns(headers);

    QCOMPARE(map.dateCol, 0);
    QVERIFY(map.idCol < 0);
}

void CsvImporterDeep6Test::testLocationTypeHeaderMisclassifiedAsCrimeType()
{
    const QStringList headers = {
        QStringLiteral("case_number"),
        QStringLiteral("occurred"),
        QStringLiteral("location_type"),
        QStringLiteral("description"),
    };
    const CsvColumnMap map = CsvImporter::detectColumns(headers);

    QCOMPARE(map.locationCol, 2);
    QVERIFY(map.crimeTypeCol < 0);
    QCOMPARE(map.dateCol, 1);
}

void CsvImporterDeep6Test::testDateOnlyRowImportedWithoutCrimeType()
{
    const QString content =
        QStringLiteral("id,occurred,latitude,longitude\n"
                       "77,2024-05-20,51.5,-0.12\n");
    const QString path = writeTempCsv(content);
    QVERIFY(!path.isEmpty());

    const auto events = CsvImporter::importFile(path, QStringLiteral("deep6"));
    QCOMPARE(events.size(), 1);
    QVERIFY(events[0].occurredAt.has_value());
    QVERIFY(events[0].crimeType.isEmpty());
    QCOMPARE(events[0].eventId, QStringLiteral("deep6_77"));
    QFile::remove(path);
}

void CsvImporterDeep6Test::testLatLonOutOfRangeRejected()
{
    CsvColumnMap m;
    m.idCol        = 0;
    m.dateCol      = 1;
    m.crimeTypeCol = 2;
    m.latCol       = 3;
    m.lonCol       = 4;

    QStringList fields = {
        QStringLiteral("1"),
        QStringLiteral("2024-01-01"),
        QStringLiteral("theft"),
        QStringLiteral("91.5"),
        QStringLiteral("-200.0"),
    };
    const CrimeEvent ev = CsvImporter::parseRow(fields, m, QStringLiteral("deep6"));
    QVERIFY(!ev.lat.has_value());
    QVERIFY(!ev.lon.has_value());

    fields[3] = QStringLiteral("90.0");
    fields[4] = QStringLiteral("180.0");
    const CrimeEvent boundary = CsvImporter::parseRow(fields, m, QStringLiteral("deep6"));
    QVERIFY(boundary.lat.has_value());
    QVERIFY(boundary.lon.has_value());
    QCOMPARE(boundary.lat.value(), 90.0);
    QCOMPARE(boundary.lon.value(), 180.0);
}

void CsvImporterDeep6Test::testQuotedFieldWithEmbeddedCommaAndNewline()
{
    const QString content =
        QStringLiteral("id,date,crime_type,description\n"
                       "1,2024-02-01,theft,\"line one,\nline two\"\n");
    const QString path = writeTempCsv(content);
    QVERIFY(!path.isEmpty());

    const auto events = CsvImporter::importFile(path, QStringLiteral("deep6"));
    QCOMPARE(events.size(), 1);
    QVERIFY(events[0].narrative.has_value());
    QVERIFY(events[0].narrative->contains(QStringLiteral("line one")));
    QVERIFY(events[0].narrative->contains(QStringLiteral("line two")));
    QFile::remove(path);
}

void CsvImporterDeep6Test::testEmptyFileReturnsNoEvents()
{
    const QString path = writeTempCsv(QString{});
    QVERIFY(!path.isEmpty());

    const auto events = CsvImporter::importFile(path, QStringLiteral("deep6"));
    QVERIFY(events.isEmpty());
    QFile::remove(path);
}

void CsvImporterDeep6Test::testCrimeTypeNormalizedToLowercase()
{
    CsvColumnMap m;
    m.idCol        = 0;
    m.dateCol      = 1;
    m.crimeTypeCol = 2;

    const QStringList fields = {
        QStringLiteral("5"),
        QStringLiteral("2024-03-01"),
        QStringLiteral("VEHICLE THEFT"),
    };
    const CrimeEvent ev = CsvImporter::parseRow(fields, m, QStringLiteral("deep6"));
    QCOMPARE(ev.crimeType, QStringLiteral("vehicle theft"));
}

void CsvImporterDeep6Test::testMissingFileReturnsEmptyVector()
{
    const QString bogus = QDir::tempPath()
        + QStringLiteral("/sentinel_nonexistent_deep6_")
        + QUuid::createUuid().toString(QUuid::Id128)
        + QStringLiteral(".csv");

    const auto events = CsvImporter::importFile(bogus, QStringLiteral("deep6"));
    QVERIFY(events.isEmpty());
}

QTEST_GUILESS_MAIN(CsvImporterDeep6Test)
#include "test_csv_importer_deep6.moc"
