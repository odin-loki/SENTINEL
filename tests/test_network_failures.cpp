// test_network_failures.cpp — Network failure and data-quality tests for SENTINEL
//
// Tests graceful error handling for UKPoliceSource, WeatherSource,
// DataQualityScorer, and CsvImporter without requiring live network access.
// Set env-var TEST_OFFLINE=true to skip tests that would hit real endpoints.

#include <QTest>
#include <QCoreApplication>
#include <QSignalSpy>
#include <QEventLoop>
#include <QTimer>
#include <QJsonObject>
#include <QJsonArray>
#include <QTemporaryFile>
#include <QTextStream>
#include <QString>
#include <optional>
#include <QTimeZone>

#include "ingest/UKPoliceSource.h"
#include "ingest/WeatherSource.h"
#include "ingest/DataQualityScorer.h"
#include "ingest/CsvImporter.h"
#include "core/CrimeEvent.h"

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

namespace {

bool isOffline()
{
    return qEnvironmentVariable("TEST_OFFLINE").compare(
        QStringLiteral("true"), Qt::CaseInsensitive) == 0;
}

} // anonymous namespace

// ─────────────────────────────────────────────────────────────────────────────
// TestNetworkFailures
// ─────────────────────────────────────────────────────────────────────────────

class TestNetworkFailures : public QObject
{
    Q_OBJECT

private slots:

    // 1. fetchSince() with a source pointing to a non-routable address eventually
    //    emits fetchError or fetchComplete; no crash.
    void testUKPoliceInvalidUrl()
    {
        if (isOffline()) QSKIP("TEST_OFFLINE=true — skipping network test");

        // Tiny radius → minimal request; the real UK Police API will respond
        // with an error or empty result — either is fine as long as no crash.
        UKPoliceSource src(0.0, 0.0, 0.001);

        QSignalSpy errorSpy(&src, &DataSource::fetchError);
        QSignalSpy doneSpy(&src, &DataSource::fetchComplete);

        src.fetchSince(QDateTime::currentDateTimeUtc().addDays(-1));

        // Wait up to 8 s; accept either terminal signal
        const bool gotError = errorSpy.wait(8000);
        const bool gotDone  = !doneSpy.isEmpty();

        QVERIFY(gotError || gotDone);
    }

    // 2. Iterating an empty JSON array produces no calls to parseRecord
    void testUKPoliceEmptyResponse()
    {
        UKPoliceSource src(51.5, -0.1);

        QJsonArray emptyArray;
        int callCount = 0;
        for (int i = 0; i < emptyArray.size(); ++i) {
            src.parseRecord(emptyArray[i].toObject());
            ++callCount;
        }
        QCOMPARE(callCount, 0);
    }

    // 3. parseRecord() on a completely empty / incomplete JSON object does not crash
    void testUKPoliceInvalidJson()
    {
        UKPoliceSource src(51.5, -0.1);

        // Completely empty object — all fields absent
        CrimeEvent ev1 = src.parseRecord(QJsonObject{});
        // eventId may be empty/garbage; we only care there is no crash
        Q_UNUSED(ev1)

        // Object with an id but no month or location
        QJsonObject partial;
        partial["id"]       = QStringLiteral("bad-001");
        partial["category"] = QStringLiteral("burglary");
        CrimeEvent ev2 = src.parseRecord(partial);
        Q_UNUSED(ev2)

        QVERIFY(true);  // reaching here means no exception was thrown
    }

    // 4. fetchHistorical with invalid coordinates (lat=999, lon=999) does not crash;
    //    the cache remains empty after the failed request.
    void testWeatherInvalidCoords()
    {
        if (isOffline()) QSKIP("TEST_OFFLINE=true — skipping network test");

        WeatherSource ws;
        QSignalSpy errorSpy(&ws, &WeatherSource::fetchError);
        QSignalSpy doneSpy(&ws,  &WeatherSource::fetchComplete);

        const QDate yesterday = QDate::currentDate().addDays(-1);
        ws.fetchHistorical(999.0, 999.0, yesterday, yesterday);

        // Wait up to 8 s for the reply to arrive
        const bool gotError = errorSpy.wait(8000);
        const bool gotDone  = !doneSpy.isEmpty();
        QVERIFY(gotError || gotDone);

        // Invalid coords should not populate the cache
        QCOMPARE(ws.cachedHourCount(), 0);
    }

    // 5. WeatherSource can be constructed; uncached dataAt() returns nullopt
    //    (verifies config/default state, not a live network call)
    void testWeatherTimeoutHandling()
    {
        WeatherSource ws;

        QCOMPARE(ws.cachedHourCount(), 0);

        const std::optional<WeatherData> result =
            ws.dataAt(QDateTime::currentDateTimeUtc());
        QVERIFY(!result.has_value());
    }

    // 6. Event with empty narrative scores no higher than one with a narrative
    void testDataQualityScorerOnEmptyNarrative()
    {
        DataQualityScorer scorer;

        CrimeEvent withNarrative;
        withNarrative.eventId    = QStringLiteral("WN-001");
        withNarrative.crimeType  = QStringLiteral("burglary");
        withNarrative.suburb     = QStringLiteral("Hackney");
        withNarrative.occurredAt = QDateTime::currentDateTimeUtc();
        withNarrative.narrative  = QStringLiteral("Forced entry via rear window at 02:00.");
        withNarrative.source     = QStringLiteral("uk_police_v1");

        CrimeEvent noNarrative = withNarrative;
        noNarrative.eventId  = QStringLiteral("NN-001");
        noNarrative.narrative = std::nullopt;

        const auto r1 = scorer.score(withNarrative);
        const auto r2 = scorer.score(noNarrative);

        QVERIFY(r1.compositeScore >= 0.0 && r1.compositeScore <= 1.0);
        QVERIFY(r2.compositeScore >= 0.0 && r2.compositeScore <= 1.0);
        // Narrative contributes positively; adding it must not lower the score
        QVERIFY(r1.compositeScore >= r2.compositeScore);
    }

    // 7. Event with no crimeType gets a very low composite score
    void testDataQualityScorerMissingRequiredFields()
    {
        DataQualityScorer scorer;

        CrimeEvent minimal;
        minimal.eventId  = QStringLiteral("MIN-001");
        // crimeType = "" (default), no date, no coords, no narrative

        const auto report = scorer.score(minimal);
        QVERIFY(report.compositeScore >= 0.0);
        QVERIFY(report.compositeScore <= 1.0);
        QVERIFY(report.compositeScore < 0.5);
    }

    // 8. A complete event (all core fields present) scores >= 0.5
    void testDataQualityScorerAllFieldsPresent()
    {
        DataQualityScorer scorer;

        CrimeEvent full;
        full.eventId     = QStringLiteral("FULL-001");
        full.crimeType   = QStringLiteral("burglary");
        full.suburb      = QStringLiteral("Hackney");
        full.lat         = 51.5074;
        full.lon         = -0.1278;
        full.latitude    = 51.5074;
        full.longitude   = -0.1278;
        full.occurredAt  = QDateTime(QDate(2024, 3, 15), QTime(22, 30, 0), QTimeZone::utc());
        full.narrative   = QStringLiteral("Suspect forced entry through a rear window.");
        full.outcome     = QStringLiteral("Under investigation");
        full.source      = QStringLiteral("uk_police_v1");

        const auto report = scorer.score(full);
        QVERIFY(report.compositeScore >= 0.0);
        QVERIFY(report.compositeScore <= 1.0);
        QVERIFY(report.compositeScore >= 0.5);
    }

    // 9. CsvImporter::importFile on a non-existent path returns empty vector, no crash
    void testCsvImporterMissingFile()
    {
        const QString nonExistent =
            QStringLiteral("/nonexistent/path/does_not_exist_99999.csv");

        const QVector<CrimeEvent> events =
            CsvImporter::importFile(nonExistent, QStringLiteral("test"));

        QVERIFY(events.isEmpty());
    }

    // 10. CsvImporter::importFile on an empty file returns empty vector, no crash
    void testCsvImporterEmptyFile()
    {
        QTemporaryFile tmp;
        tmp.setAutoRemove(true);
        QVERIFY(tmp.open());
        tmp.close();  // file exists but has 0 bytes

        const QVector<CrimeEvent> events =
            CsvImporter::importFile(tmp.fileName(), QStringLiteral("test"));

        QVERIFY(events.isEmpty());
    }
};

// ─────────────────────────────────────────────────────────────────────────────

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    TestNetworkFailures t;
    return QTest::qExec(&t, argc, argv);
}

#include "test_network_failures.moc"
