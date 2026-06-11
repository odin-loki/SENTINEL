// Deep iteration 11 — CsvImporter + DataQualityScorer comprehensive tests
#include <QtTest/QtTest>
#include <QTemporaryFile>
#include <QTextStream>

#include "ingest/CsvImporter.h"
#include "ingest/DataQualityScorer.h"
#include "core/CrimeEvent.h"

class CsvImporterDeep4Test : public QObject
{
    Q_OBJECT

private:
    // Write CSV content to a temp file, return its path
    static QString writeTempCsv(const QString& content) {
        QTemporaryFile* f = new QTemporaryFile(QStringLiteral("sentinel_test_XXXXXX.csv"));
        f->setAutoRemove(false);
        if (!f->open()) return {};
        QTextStream out(f);
        out.setEncoding(QStringConverter::Utf8);
        out << content;
        f->close();
        const QString path = f->fileName();
        delete f;
        return path;
    }

private slots:

    // ─── detectColumns ─────────────────────────────────────────────────────

    void testDetectColumnsBasic()
    {
        QStringList h = {
            QStringLiteral("id"),
            QStringLiteral("date"),
            QStringLiteral("crime_type"),
            QStringLiteral("latitude"),
            QStringLiteral("longitude"),
            QStringLiteral("description")
        };
        const CsvColumnMap m = CsvImporter::detectColumns(h);
        QCOMPARE(m.idCol,        0);
        QCOMPARE(m.dateCol,      1);
        QCOMPARE(m.crimeTypeCol, 2);
        QCOMPARE(m.latCol,       3);
        QCOMPARE(m.lonCol,       4);
        QCOMPARE(m.descCol,      5);
    }

    void testDetectColumnsAliases()
    {
        QStringList h = {
            QStringLiteral("case_number"),
            QStringLiteral("occurred"),
            QStringLiteral("primary_type"),
            QStringLiteral("lat"),
            QStringLiteral("lng")
        };
        const CsvColumnMap m = CsvImporter::detectColumns(h);
        QCOMPARE(m.idCol,        0);
        QCOMPARE(m.dateCol,      1);
        QCOMPARE(m.crimeTypeCol, 2);
        QCOMPARE(m.latCol,       3);
        QCOMPARE(m.lonCol,       4);
    }

    void testDetectColumnsEmptyHeaders()
    {
        const CsvColumnMap m = CsvImporter::detectColumns({});
        QCOMPARE(m.idCol,        -1);
        QCOMPARE(m.dateCol,      -1);
        QCOMPARE(m.crimeTypeCol, -1);
        QCOMPARE(m.latCol,       -1);
        QCOMPARE(m.lonCol,       -1);
    }

    void testDetectColumnsUnknownHeaders()
    {
        QStringList h = {
            QStringLiteral("foo"),
            QStringLiteral("bar"),
            QStringLiteral("baz")
        };
        const CsvColumnMap m = CsvImporter::detectColumns(h);
        QCOMPARE(m.idCol,        -1);
        QCOMPARE(m.dateCol,      -1);
        QCOMPARE(m.crimeTypeCol, -1);
    }

    // ─── parseRow ──────────────────────────────────────────────────────────

    void testParseRowBasic()
    {
        CsvColumnMap m;
        m.idCol        = 0;
        m.dateCol      = 1;
        m.crimeTypeCol = 2;
        m.latCol       = 3;
        m.lonCol       = 4;

        QStringList fields = {
            QStringLiteral("12345"),
            QStringLiteral("2024-01-15 10:30:00"),
            QStringLiteral("Theft"),
            QStringLiteral("51.5074"),
            QStringLiteral("-0.1278")
        };

        const CrimeEvent ev = CsvImporter::parseRow(fields, m, QStringLiteral("test"));
        QVERIFY(ev.eventId.contains(QStringLiteral("12345")));
        QCOMPARE(ev.crimeType, QStringLiteral("theft"));
        QVERIFY(ev.occurredAt.has_value());
        QVERIFY(ev.lat.has_value());
        QVERIFY(ev.lon.has_value());
        QCOMPARE(*ev.lat, 51.5074);
        QCOMPARE(*ev.lon, -0.1278);
    }

    void testParseRowMissingId_GeneratesUuid()
    {
        CsvColumnMap m;
        m.idCol = -1;  // no ID column
        m.crimeTypeCol = 0;

        QStringList fields = { QStringLiteral("Burglary") };
        const CrimeEvent ev1 = CsvImporter::parseRow(fields, m, QStringLiteral("src"));
        const CrimeEvent ev2 = CsvImporter::parseRow(fields, m, QStringLiteral("src"));
        QVERIFY(!ev1.eventId.isEmpty());
        QVERIFY(!ev2.eventId.isEmpty());
        QVERIFY(ev1.eventId != ev2.eventId);  // UUIDs should differ
    }

    void testParseRowInvalidLatLon_NotSet()
    {
        CsvColumnMap m;
        m.latCol = 0;
        m.lonCol = 1;

        // lat=999 is out of range [-90,90]
        QStringList fields = { QStringLiteral("999"), QStringLiteral("200") };
        const CrimeEvent ev = CsvImporter::parseRow(fields, m, QStringLiteral("test"));
        QVERIFY(!ev.lat.has_value());
        QVERIFY(!ev.lon.has_value());
    }

    void testParseRowCrimeTypeLowercased()
    {
        CsvColumnMap m;
        m.crimeTypeCol = 0;
        QStringList fields = { QStringLiteral("ROBBERY") };
        const CrimeEvent ev = CsvImporter::parseRow(fields, m, QStringLiteral("test"));
        QCOMPARE(ev.crimeType, QStringLiteral("robbery"));
    }

    // ─── importFile ────────────────────────────────────────────────────────

    void testImportFileBasic()
    {
        const QString content =
            QStringLiteral("id,date,crime_type,latitude,longitude\n"
                           "1,2024-01-01,theft,51.5074,-0.1278\n"
                           "2,2024-01-02,burglary,51.5100,-0.1300\n");

        const QString path = writeTempCsv(content);
        QVERIFY(!path.isEmpty());

        const auto events = CsvImporter::importFile(path, QStringLiteral("test"));
        QCOMPARE(events.size(), 2);
        QCOMPARE(events[0].crimeType, QStringLiteral("theft"));
        QCOMPARE(events[1].crimeType, QStringLiteral("burglary"));
        QFile::remove(path);
    }

    void testImportFileEmptyCsv()
    {
        const QString path = writeTempCsv(QStringLiteral("id,date,crime_type\n"));
        const auto events = CsvImporter::importFile(path, QStringLiteral("test"));
        QCOMPARE(events.size(), 0);
        QFile::remove(path);
    }

    void testImportFileQuotedFields()
    {
        const QString content =
            QStringLiteral("id,date,crime_type,description\n"
                           "1,2024-01-01,theft,\"Stolen wallet, phone\"\n");

        const QString path = writeTempCsv(content);
        const auto events = CsvImporter::importFile(path, QStringLiteral("test"));
        QCOMPARE(events.size(), 1);
        QVERIFY(events[0].narrative.has_value());
        QVERIFY(events[0].narrative->contains(QStringLiteral("wallet")));
        QFile::remove(path);
    }

    void testImportFileBlankLinesSkipped()
    {
        const QString content =
            QStringLiteral("id,date,crime_type\n"
                           "1,2024-01-01,theft\n"
                           "\n"
                           "2,2024-01-02,robbery\n");

        const QString path = writeTempCsv(content);
        const auto events = CsvImporter::importFile(path, QStringLiteral("test"));
        QCOMPARE(events.size(), 2);
        QFile::remove(path);
    }

    void testImportFileProgressCallback()
    {
        // Build a CSV with 600 rows to exercise the progress callback
        QString content = QStringLiteral("id,date,crime_type\n");
        for (int i = 0; i < 600; ++i) {
            content += QStringLiteral("%1,2024-01-01,theft\n").arg(i);
        }
        const QString path = writeTempCsv(content);

        int callbackCount = 0;
        const auto events = CsvImporter::importFile(
            path,
            QStringLiteral("test"),
            [&](int done, int total) {
                QVERIFY(done <= total);
                ++callbackCount;
            });

        QCOMPARE(events.size(), 600);
        QVERIFY(callbackCount > 0);
        QFile::remove(path);
    }

    void testImportFileNonExistentFile()
    {
        const auto events = CsvImporter::importFile(
            QStringLiteral("/nonexistent/path/file.csv"), QStringLiteral("test"));
        QCOMPARE(events.size(), 0);
    }

    void testImportFileSourceTagApplied()
    {
        const QString content =
            QStringLiteral("id,date,crime_type\n"
                           "1,2024-01-01,theft\n");
        const QString path = writeTempCsv(content);
        const auto events = CsvImporter::importFile(path, QStringLiteral("my_source"));
        QVERIFY(!events.isEmpty());
        QCOMPARE(events[0].source, QStringLiteral("my_source"));
        QFile::remove(path);
    }

    // ─── DataQualityScorer ─────────────────────────────────────────────────

    void testEmptyEventScoresLow()
    {
        const DataQualityScorer scorer = DataQualityScorer::withDefaults();
        CrimeEvent ev;
        ev.source = QStringLiteral("unknown");
        const QualityReport r = scorer.score(ev);
        QVERIFY2(r.compositeScore < 0.5,
                 qPrintable(QStringLiteral("Expected low score for empty event, got %1")
                                .arg(r.compositeScore)));
    }

    void testFullEventScoresHigh()
    {
        const DataQualityScorer scorer = DataQualityScorer::withDefaults();
        CrimeEvent ev;
        ev.eventId    = QStringLiteral("EVT-001");
        ev.crimeType  = QStringLiteral("theft");
        ev.occurredAt = QDateTime(QDate(2024, 6, 15), QTime(14, 30, 0), Qt::UTC);
        ev.lat        = 51.50740;   // 5 decimal places → "exact"
        ev.lon        = -0.12780;
        ev.suburb     = QStringLiteral("Westminster");
        ev.source     = QStringLiteral("uk_police_v1");  // reliability = 0.90

        const QualityReport r = scorer.score(ev);
        QVERIFY2(r.compositeScore > 0.7,
                 qPrintable(QStringLiteral("Expected high score for full event, got %1")
                                .arg(r.compositeScore)));
        QVERIFY(!r.quarantined);
    }

    void testInvalidLatPenalised()
    {
        const DataQualityScorer scorer = DataQualityScorer::withDefaults();
        CrimeEvent ev;
        ev.crimeType  = QStringLiteral("theft");
        ev.occurredAt = QDateTime(QDate(2024, 1, 1), QTime(0, 0, 0), Qt::UTC);
        ev.lat        = 200.0;   // invalid
        ev.lon        = -0.1;
        ev.source     = QStringLiteral("uk_police_v1");

        const CrimeEvent evGood = [&](){
            CrimeEvent e = ev;
            e.lat = 51.5;
            return e;
        }();

        const QualityReport rBad  = scorer.score(ev);
        const QualityReport rGood = scorer.score(evGood);
        QVERIFY2(rBad.compositeScore < rGood.compositeScore,
                 "Invalid lat should produce a lower score than valid lat");
    }

    void testSourceReliabilityApplied()
    {
        const DataQualityScorer scorer = DataQualityScorer::withDefaults();
        CrimeEvent evHigh, evLow;
        evHigh.crimeType  = evLow.crimeType  = QStringLiteral("theft");
        evHigh.occurredAt = evLow.occurredAt = QDateTime(QDate(2024, 1, 1), QTime(0, 0, 0), Qt::UTC);
        evHigh.lat = evLow.lat = 51.50740;
        evHigh.lon = evLow.lon = -0.12780;
        evHigh.suburb = evLow.suburb = QStringLiteral("Zone");

        evHigh.source = QStringLiteral("uk_police_v1");  // 0.90
        evLow.source  = QStringLiteral("manual");        // 0.40

        const QualityReport rH = scorer.score(evHigh);
        const QualityReport rL = scorer.score(evLow);
        QVERIFY(rH.compositeScore > rL.compositeScore);
    }

    void testDefaultReliabilityMapNonEmpty()
    {
        const auto map = DataQualityScorer::defaultReliabilityMap();
        QVERIFY(!map.isEmpty());
        QVERIFY(map.contains(QStringLiteral("uk_police_v1")));
        QVERIFY(map.contains(QStringLiteral("csv_import")));
        // All values in [0,1]
        for (const double v : map.values()) {
            QVERIFY2(v >= 0.0 && v <= 1.0,
                     qPrintable(QStringLiteral("Reliability value %1 out of range").arg(v)));
        }
    }

    void testWithDefaultsConstructor()
    {
        const DataQualityScorer scorer = DataQualityScorer::withDefaults();
        CrimeEvent ev;
        ev.source = QStringLiteral("uk_police_v1");
        // Should not crash
        const QualityReport r = scorer.score(ev);
        QVERIFY(r.compositeScore >= 0.0 && r.compositeScore <= 1.0);
    }

    void testPassRate()
    {
        const DataQualityScorer scorer = DataQualityScorer::withDefaults();
        QVector<QualityReport> reports;

        // Create 3 high-quality events (should pass)
        for (int i = 0; i < 3; ++i) {
            CrimeEvent ev;
            ev.crimeType  = QStringLiteral("theft");
            ev.occurredAt = QDateTime(QDate(2024, 6, 15), QTime(14, 30, 0), Qt::UTC);
            ev.lat        = 51.50740;
            ev.lon        = -0.12780;
            ev.suburb     = QStringLiteral("Zone");
            ev.source     = QStringLiteral("uk_police_v1");
            reports.append(scorer.score(ev));
        }

        // Create 2 empty events (should be quarantined)
        for (int i = 0; i < 2; ++i) {
            CrimeEvent ev;
            ev.source = QStringLiteral("unknown");
            reports.append(scorer.score(ev));
        }

        const double rate = DataQualityScorer::passRate(reports);
        QVERIFY2(rate > 0.4 && rate < 0.8,
                 qPrintable(QStringLiteral("Expected pass rate ~0.6, got %1").arg(rate)));
    }

    void testBatchScoreReturnsSameCount()
    {
        const DataQualityScorer scorer = DataQualityScorer::withDefaults();
        QVector<CrimeEvent> events;
        for (int i = 0; i < 10; ++i) {
            CrimeEvent ev;
            ev.crimeType = QStringLiteral("theft");
            ev.source    = QStringLiteral("csv_import");
            events.append(ev);
        }
        const auto reports = scorer.scoreBatch(events);
        QCOMPARE(reports.size(), events.size());
    }

    void testScoreCompositeInRange()
    {
        const DataQualityScorer scorer = DataQualityScorer::withDefaults();
        CrimeEvent ev;
        ev.crimeType  = QStringLiteral("robbery");
        ev.occurredAt = QDateTime(QDate(2024, 3, 20), QTime(22, 0, 0), Qt::UTC);
        ev.lat        = 51.4900;
        ev.lon        = -0.1500;
        ev.source     = QStringLiteral("test");

        const QualityReport r = scorer.score(ev);
        QVERIFY2(r.compositeScore >= 0.0 && r.compositeScore <= 1.0,
                 qPrintable(QStringLiteral("Composite score out of [0,1]: %1")
                                .arg(r.compositeScore)));
    }
};

QTEST_GUILESS_MAIN(CsvImporterDeep4Test)
#include "test_csv_importer_deep4.moc"
