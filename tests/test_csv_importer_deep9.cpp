// test_csv_importer_deep9.cpp — Deep audit iteration 29: CsvImporter
// primary_type alias, location column, invalid row skip, UTF-8 narrative.
#include <QtTest/QtTest>
#include <QTemporaryFile>
#include <QTextStream>
#include "ingest/CsvImporter.h"

class CsvImporterDeep9Test : public QObject
{
    Q_OBJECT

    static QString writeCsv(const QString& content)
    {
        QTemporaryFile f(QStringLiteral("sentinel_deep9_XXXXXX.csv"));
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

    void testPrimaryTypeAlias()
    {
        const QString path = writeCsv(
            QStringLiteral("event_id,primary_type,occurred_at,latitude,longitude\n")
            + QStringLiteral("PT1,assault,2024-08-01T10:00:00Z,51.5,-0.1\n"));
        QVERIFY(!path.isEmpty());

        const auto events = CsvImporter().importFile(path);
        QCOMPARE(events.size(), 1);
        QCOMPARE(events[0].crimeType, QStringLiteral("assault"));
    }

    void testLocationColumnMapped()
    {
        const QString path = writeCsv(
            QStringLiteral("event_id,crime_type,occurred_at,latitude,longitude,location\n")
            + QStringLiteral("LOC1,theft,2024-08-02T11:00:00Z,51.51,-0.11,High Street shop front\n"));
        const auto events = CsvImporter().importFile(path);
        QCOMPARE(events.size(), 1);
        QCOMPARE(events[0].locationRaw.value_or(QString{}),
                 QStringLiteral("High Street shop front"));
    }

    void testRowMissingDateAndTypeSkipped()
    {
        const QString path = writeCsv(
            QStringLiteral("event_id,crime_type,occurred_at,latitude,longitude\n")
            + QStringLiteral(",,,51.5,-0.1\n")
            + QStringLiteral("GOOD1,theft,2024-08-03 09:00:00,51.5,-0.1\n"));
        const auto events = CsvImporter().importFile(path);
        QCOMPARE(events.size(), 1);
        QCOMPARE(events[0].eventId, QStringLiteral("csv_import_GOOD1"));
    }

    void testUtf8NarrativePreserved()
    {
        const QString path = writeCsv(
            QStringLiteral("event_id,crime_type,occurred_at,latitude,longitude,narrative\n")
            + QStringLiteral("UTF1,theft,2024-08-04T12:00:00Z,51.5,-0.1,\"caf\u00e9 robbery\"\n"));
        const auto events = CsvImporter().importFile(path);
        QCOMPARE(events.size(), 1);
        QVERIFY(events[0].narrative.value_or(QString{}).contains(QStringLiteral("caf")));
    }

    void testEmptyFileReturnsEmpty()
    {
        const QString path = writeCsv(QString{});
        QVERIFY(CsvImporter().importFile(path).isEmpty());
    }
};

QTEST_GUILESS_MAIN(CsvImporterDeep9Test)
#include "test_csv_importer_deep9.moc"
