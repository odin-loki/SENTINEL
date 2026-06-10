// test_csv_real_data.cpp — CsvImporter tests using real and realistic data
#include <QTest>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QSet>
#include <QTemporaryFile>
#include <QTextStream>
#include "ingest/CsvImporter.h"
#include "ingest/DataQualityScorer.h"

class TestCsvRealData : public QObject
{
    Q_OBJECT

private:
    // Path to the real data file copied by CMake configure_file
    static QString realDataPath()
    {
        return QDir(QCoreApplication::applicationDirPath())
               .filePath(QStringLiteral("data/london_crimes_2024.csv"));
    }

    // Write a temporary CSV and return its path
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

    // Build a large synthetic CSV that mirrors the real london_crimes_2024.csv layout
    static QString buildSyntheticCsv(int rowCount = 120)
    {
        const QStringList crimeTypes = {
            "burglary", "theft", "assault", "robbery",
            "vehicle crime", "drug offence", "criminal damage", "fraud"
        };
        const QStringList outcomes = {"resolved", "unresolved", "under investigation"};

        QString csv = "id,date,crime_type,latitude,longitude,location,outcome\n";
        for (int i = 1; i <= rowCount; ++i) {
            double lat = 51.45 + (i % 20) * 0.005;
            double lon = -0.20 + (i % 15) * 0.008;
            QString crimeType = crimeTypes[i % crimeTypes.size()];
            QString outcome   = outcomes[i % outcomes.size()];
            // Spread dates across January–March 2024 to guarantee > 1 day span
            int day   = 1 + (i % 28);
            int month = 1 + (i % 3);
            csv += QString("syn_%1,2024-%2-%3,%4,%5,%6,On or near Test Street,%7\n")
                   .arg(i)
                   .arg(month, 2, 10, QChar('0'))
                   .arg(day,   2, 10, QChar('0'))
                   .arg(lat,  0, 'f', 4)
                   .arg(lon,  0, 'f', 4)
                   .arg(crimeType)
                   .arg(outcome);
        }
        return csv;
    }

private slots:

    // ── Real-file tests ───────────────────────────────────────────────────────

    void testRealDataFileFormat()
    {
        QString path = realDataPath();
        if (!QFile::exists(path))
            QSKIP("Real data file not found – skipping (run cmake to copy it)");

        QFile f(path);
        QVERIFY(f.open(QIODevice::ReadOnly | QIODevice::Text));
        QString headerLine = f.readLine().trimmed();
        f.close();

        // Verify expected columns are present in header
        QVERIFY2(headerLine.contains("id", Qt::CaseInsensitive),      "Missing id column");
        QVERIFY2(headerLine.contains("date", Qt::CaseInsensitive),    "Missing date column");
        QVERIFY2(headerLine.contains("crime_type", Qt::CaseInsensitive), "Missing crime_type column");
        QVERIFY2(headerLine.contains("latitude", Qt::CaseInsensitive), "Missing latitude column");
        QVERIFY2(headerLine.contains("longitude", Qt::CaseInsensitive),"Missing longitude column");
    }

    void testRealDataEventCount()
    {
        QString path = realDataPath();
        if (!QFile::exists(path))
            QSKIP("Real data file not found");

        auto events = CsvImporter::importFile(path, "london_real");
        QVERIFY2(events.size() >= 50,
                 qPrintable(QString("Expected >= 50 events, got %1").arg(events.size())));
    }

    void testRealDataCoordinates()
    {
        QString path = realDataPath();
        if (!QFile::exists(path))
            QSKIP("Real data file not found");

        auto events = CsvImporter::importFile(path, "london_real");
        QVERIFY(!events.isEmpty());

        int withCoords = 0;
        for (const auto& ev : events) {
            if (ev.lat.has_value() && ev.lon.has_value()) {
                // Coordinates must not be 0/0 and must be plausible for London
                double la = *ev.lat;
                double lo = *ev.lon;
                QVERIFY2(la != 0.0 || lo != 0.0, "Event has (0,0) coordinates");
                QVERIFY2(la > 50.0 && la < 53.0,
                         qPrintable(QString("Latitude %1 out of London range").arg(la)));
                QVERIFY2(lo > -1.5 && lo < 1.0,
                         qPrintable(QString("Longitude %1 out of London range").arg(lo)));
                ++withCoords;
            }
        }
        // At least 80 % of events should carry coordinates
        double ratio = static_cast<double>(withCoords) / events.size();
        QVERIFY2(ratio >= 0.8,
                 qPrintable(QString("Only %1% of events have coordinates").arg(ratio * 100, 0, 'f', 1)));
    }

    void testRealDataCrimeTypes()
    {
        QString path = realDataPath();
        if (!QFile::exists(path))
            QSKIP("Real data file not found");

        auto events = CsvImporter::importFile(path, "london_real");
        QVERIFY(!events.isEmpty());

        QSet<QString> types;
        for (const auto& ev : events)
            if (!ev.crimeType.isEmpty())
                types.insert(ev.crimeType);

        QVERIFY2(types.size() >= 3,
                 qPrintable(QString("Expected >= 3 distinct crime types, got %1").arg(types.size())));
    }

    void testRealDataDateRange()
    {
        QString path = realDataPath();
        if (!QFile::exists(path))
            QSKIP("Real data file not found");

        auto events = CsvImporter::importFile(path, "london_real");
        QVERIFY(!events.isEmpty());

        QDate earliest, latest;
        for (const auto& ev : events) {
            if (!ev.occurredAt.has_value()) continue;
            QDate d = ev.occurredAt->date();
            if (!earliest.isValid() || d < earliest) earliest = d;
            if (!latest.isValid()   || d > latest)   latest   = d;
        }

        QVERIFY2(earliest.isValid() && latest.isValid(), "No valid dates parsed");
        QVERIFY2(earliest.daysTo(latest) >= 1,
                 qPrintable(QString("Date range only %1 days — expected >= 1")
                            .arg(earliest.daysTo(latest))));
    }

    void testRealDataQualityScores()
    {
        QString path = realDataPath();
        if (!QFile::exists(path))
            QSKIP("Real data file not found");

        auto events = CsvImporter::importFile(path, "london_real");
        QVERIFY(!events.isEmpty());

        DataQualityScorer scorer;
        auto reports = scorer.scoreBatch(events);
        QVERIFY(!reports.isEmpty());

        double sum = 0.0;
        for (const auto& r : reports)
            sum += r.compositeScore;
        double avg = sum / reports.size();

        QVERIFY2(avg > 0.3,
                 qPrintable(QString("Average quality score %1 <= 0.3").arg(avg, 0, 'f', 3)));
    }

    // ── Synthetic-data tests (always run, no file dependency) ─────────────────

    void testSyntheticDataEventCount()
    {
        QString path = writeTmpCsv(buildSyntheticCsv(120));
        QVERIFY(!path.isEmpty());
        auto events = CsvImporter::importFile(path, "synthetic");
        QFile::remove(path);

        QVERIFY2(events.size() >= 50,
                 qPrintable(QString("Expected >= 50 events from synthetic CSV, got %1")
                            .arg(events.size())));
    }

    void testSyntheticDataCoordinates()
    {
        QString path = writeTmpCsv(buildSyntheticCsv(100));
        QVERIFY(!path.isEmpty());
        auto events = CsvImporter::importFile(path, "synthetic");
        QFile::remove(path);

        QVERIFY(!events.isEmpty());
        for (const auto& ev : events) {
            if (ev.lat.has_value() && ev.lon.has_value()) {
                QVERIFY2(*ev.lat  != 0.0 || *ev.lon != 0.0, "Synthetic event has (0,0)");
                QVERIFY2(*ev.lat   > 50.0 && *ev.lat  < 53.0, "Synthetic lat out of range");
            }
        }
    }

    void testSyntheticDataCrimeTypes()
    {
        QString path = writeTmpCsv(buildSyntheticCsv(100));
        QVERIFY(!path.isEmpty());
        auto events = CsvImporter::importFile(path, "synthetic");
        QFile::remove(path);

        QSet<QString> types;
        for (const auto& ev : events)
            if (!ev.crimeType.isEmpty())
                types.insert(ev.crimeType);

        QVERIFY2(types.size() >= 3,
                 qPrintable(QString("Expected >= 3 crime types in synthetic data, got %1")
                            .arg(types.size())));
    }

    void testSyntheticDataDateRange()
    {
        QString path = writeTmpCsv(buildSyntheticCsv(100));
        QVERIFY(!path.isEmpty());
        auto events = CsvImporter::importFile(path, "synthetic");
        QFile::remove(path);

        QDate earliest, latest;
        for (const auto& ev : events) {
            if (!ev.occurredAt.has_value()) continue;
            QDate d = ev.occurredAt->date();
            if (!earliest.isValid() || d < earliest) earliest = d;
            if (!latest.isValid()   || d > latest)   latest   = d;
        }

        QVERIFY2(earliest.isValid() && latest.isValid(), "No valid dates parsed from synthetic CSV");
        QVERIFY2(earliest.daysTo(latest) >= 1, "Synthetic date range < 1 day");
    }

    void testSyntheticDataQualityScores()
    {
        QString path = writeTmpCsv(buildSyntheticCsv(100));
        QVERIFY(!path.isEmpty());
        auto events = CsvImporter::importFile(path, "synthetic");
        QFile::remove(path);

        DataQualityScorer scorer;
        auto reports = scorer.scoreBatch(events);
        QVERIFY(!reports.isEmpty());

        double sum = 0.0;
        for (const auto& r : reports)
            sum += r.compositeScore;
        double avg = sum / reports.size();

        QVERIFY2(avg > 0.3,
                 qPrintable(QString("Synthetic avg quality score %1 <= 0.3").arg(avg, 0, 'f', 3)));
    }

    // ── UK Police Open Data format ────────────────────────────────────────────

    void testUKPoliceDataFormat()
    {
        // Mimics the actual UK Police Open Data CSV format exactly
        QString ukCsv =
            "Crime ID,Month,Reported by,Falls within,Longitude,Latitude,"
            "Location,LSOA code,LSOA name,Crime type,Last outcome category,Context\n"
            "abc123def456,2024-01,Metropolitan Police Service,Metropolitan Police Service,"
            "-0.1278,51.5074,On or near High Street,E01000001,Westminster 001A,"
            "Burglary,Investigation complete; no suspect identified,\n"
            "xyz789uvw012,2024-01,Metropolitan Police Service,Metropolitan Police Service,"
            "-0.1300,51.5100,On or near Church Road,E01000002,Westminster 001B,"
            "Theft from the person,Under investigation,\n"
            "pqr345stu678,2024-02,Metropolitan Police Service,Metropolitan Police Service,"
            "-0.1432,51.5123,On or near Station Road,E01000003,Westminster 001C,"
            "Violence and sexual offences,Awaiting court outcome,\n"
            "lmn901opq234,2024-02,Metropolitan Police Service,Metropolitan Police Service,"
            "-0.1654,51.4834,On or near Oxford Street,E01000004,Westminster 001D,"
            "Robbery,Court result unavailable,\n"
            "efg567hij890,2024-03,Metropolitan Police Service,Metropolitan Police Service,"
            "-0.0987,51.5201,On or near Victoria Road,E01000005,Westminster 001E,"
            "Vehicle crime,No further action,\n";

        QString path = writeTmpCsv(ukCsv);
        QVERIFY(!path.isEmpty());

        // Verify column detection works on UK Police headers
        QStringList headers = {
            "Crime ID", "Month", "Reported by", "Falls within",
            "Longitude", "Latitude", "Location", "LSOA code", "LSOA name",
            "Crime type", "Last outcome category", "Context"
        };
        CsvColumnMap colMap = CsvImporter::detectColumns(headers);
        QVERIFY2(colMap.idCol >= 0,       "UK Police: id column not detected");
        // "Month" is not auto-detected as a date column by detectColumns (by design);
        // the UK Police source uses UKPoliceSource::fetchSync for full parsing.
        QVERIFY2(colMap.crimeTypeCol >= 0,"UK Police: crime type column not detected");
        QVERIFY2(colMap.latCol >= 0,      "UK Police: latitude column not detected");
        QVERIFY2(colMap.lonCol >= 0,      "UK Police: longitude column not detected");
        QVERIFY2(colMap.outcomeCol >= 0,  "UK Police: outcome column not detected");

        // Full import
        auto events = CsvImporter::importFile(path, "uk_police");
        QFile::remove(path);

        QVERIFY2(events.size() >= 3,
                 qPrintable(QString("Expected >= 3 events from UK Police CSV, got %1")
                            .arg(events.size())));

        // All events should carry valid London-area coordinates
        for (const auto& ev : events) {
            if (!ev.lat.has_value()) continue;
            double la = *ev.lat;
            double lo = *ev.lon;
            QVERIFY2(la > 50.0 && la < 53.0,
                     qPrintable(QString("UK Police lat %1 out of range").arg(la)));
            QVERIFY2(lo > -1.5 && lo < 1.0,
                     qPrintable(QString("UK Police lon %1 out of range").arg(lo)));
        }

        // Source tag should propagate
        for (const auto& ev : events)
            QVERIFY2(ev.source.contains("uk_police"),
                     "Source tag not propagated in UK Police import");
    }
};

QTEST_MAIN(TestCsvRealData)
#include "test_csv_real_data.moc"
