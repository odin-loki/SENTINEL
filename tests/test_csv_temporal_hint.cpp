// tests/test_csv_temporal_hint.cpp
// Qt Test unit tests: CsvImporter, TemporalFeatures, HintEngine

#include <QTest>
#include <QCoreApplication>
#include <QTemporaryFile>
#include <QTextStream>
#include <QFile>
#include <QDateTime>
#include <QDate>
#include <QTime>
#include <QVector>
#include <QtMath>
#include <cmath>
#include <algorithm>

#include "ingest/CsvImporter.h"
#include "models/TemporalFeatures.h"
#include "inference/HintEngine.h"
#include "core/CrimeEvent.h"

// ─────────────────────────────────────────────────────────────────────────────
// Shared helpers
// ─────────────────────────────────────────────────────────────────────────────

// Write content to a temp file and return the path.
// autoRemove=false keeps the file on disk until explicitly deleted by the test.
static QString writeTempCsv(const QString& content)
{
    QTemporaryFile tmp;
    tmp.setAutoRemove(false);
    if (!tmp.open()) return {};
    QTextStream out(&tmp);
    out << content;
    out.flush();
    return tmp.fileName();
}

// Standard header whose column names all match CsvImporter::detectColumns keywords
static const QString kCsvHeader =
    QStringLiteral("id,date,type,description,latitude,longitude,address,outcome,location\n");

// Build n identical valid data rows
static QString buildValidRows(int n)
{
    QString rows;
    rows.reserve(n * 80);
    for (int i = 0; i < n; ++i) {
        rows += QString("ROW%1,2024-03-15 14:30:00,burglary,Description %1,"
                        "-33.8688,151.2093,123 Main St,unresolved,Residential\n")
                    .arg(i + 1);
    }
    return rows;
}

// ─────────────────────────────────────────────────────────────────────────────
// TestCsvImporter
// ─────────────────────────────────────────────────────────────────────────────

class TestCsvImporter : public QObject
{
    Q_OBJECT

private slots:
    void testBasicParsing()
    {
        const QString csv  = kCsvHeader + buildValidRows(5);
        const QString path = writeTempCsv(csv);
        QVERIFY(!path.isEmpty());

        const QVector<CrimeEvent> events = CsvImporter::importFile(path, "test");
        QCOMPARE(events.size(), 5);

        QFile::remove(path);
    }

    void testMissingColumns()
    {
        // Only id/date/type columns — no lat, lon, address, etc.
        // Importer should handle gracefully: returns events without coordinates.
        const QString csv = QStringLiteral("id,date,type,description\n")
            + QStringLiteral("1,2024-01-15 10:30:00,theft,Some description\n")
            + QStringLiteral("2,2024-01-16 11:00:00,robbery,Another description\n");

        const QString path = writeTempCsv(csv);
        QVERIFY(!path.isEmpty());

        const QVector<CrimeEvent> events = CsvImporter::importFile(path, "test");
        QCOMPARE(events.size(), 2);  // both rows have date & type → neither skipped

        // No lat/lon columns detected → coordinates absent
        for (const auto& ev : events) {
            QVERIFY(!ev.lat.has_value());
            QVERIFY(!ev.lon.has_value());
        }

        QFile::remove(path);
    }

    void testEmptyCsv()
    {
        const QString path = writeTempCsv(QString());  // truly empty file
        QVERIFY(!path.isEmpty());

        const QVector<CrimeEvent> events = CsvImporter::importFile(path, "test");
        QCOMPARE(events.size(), 0);

        QFile::remove(path);
    }

    void testMalformedRows()
    {
        // Header: id=0, date=1, type=2
        // Rows with only 1 field → both date(col 1) and type(col 2) missing → skipped
        const QString csv = kCsvHeader
            + QStringLiteral("1,2024-01-15 10:30:00,theft,desc,-33.8,151.2,Main St,unresolved,House\n")
            + QStringLiteral("badrow\n")   // 1 field → col 1 & 2 out of range → skipped
            + QStringLiteral("2,2024-01-16 11:00:00,robbery,desc,-33.9,151.3,Oak St,unresolved,Shop\n")
            + QStringLiteral("alsobad\n")  // 1 field → skipped
            + QStringLiteral("3,2024-01-17 12:00:00,assault,desc,-33.7,151.1,Pine St,unresolved,Street\n");

        const QString path = writeTempCsv(csv);
        QVERIFY(!path.isEmpty());

        const QVector<CrimeEvent> events = CsvImporter::importFile(path, "test");
        QCOMPARE(events.size(), 3);  // 2 malformed rows skipped

        QFile::remove(path);
    }

    void testLatLonParsing()
    {
        const double testLat = -33.8688;
        const double testLon =  151.2093;

        const QString csv = kCsvHeader
            + QString("E1,2024-01-15 10:30:00,theft,desc,%1,%2,Addr,unresolved,Loc\n")
                  .arg(testLat, 0, 'f', 4)
                  .arg(testLon, 0, 'f', 4);

        const QString path = writeTempCsv(csv);
        QVERIFY(!path.isEmpty());

        const QVector<CrimeEvent> events = CsvImporter::importFile(path, "test");
        QCOMPARE(events.size(), 1);

        QVERIFY(events[0].lat.has_value());
        QVERIFY(events[0].lon.has_value());
        QVERIFY(qAbs(*events[0].lat - testLat) < 0.001);
        QVERIFY(qAbs(*events[0].lon - testLon) < 0.001);

        QFile::remove(path);
    }

    void testDateTimeParsing()
    {
        // Exercise several of the formats listed in CsvImporter.cpp
        const QString csv = QStringLiteral("id,date,type\n")
            + QStringLiteral("1,2024-01-15 10:30:00,theft\n")    // yyyy-MM-dd HH:mm:ss
            + QStringLiteral("2,2024-01-15T10:30:00,robbery\n")  // yyyy-MM-ddTHH:mm:ss
            + QStringLiteral("3,2024-01-15,assault\n")            // yyyy-MM-dd
            + QStringLiteral("4,01/15/2024,burglary\n");          // MM/dd/yyyy

        const QString path = writeTempCsv(csv);
        QVERIFY(!path.isEmpty());

        const QVector<CrimeEvent> events = CsvImporter::importFile(path, "test");
        QCOMPARE(events.size(), 4);

        for (const auto& ev : events) {
            QVERIFY(ev.occurredAt.has_value());
            QVERIFY(ev.occurredAt->isValid());
        }

        QFile::remove(path);
    }

    void testQualityScoreAssignment()
    {
        const QString csv  = kCsvHeader + buildValidRows(3);
        const QString path = writeTempCsv(csv);
        QVERIFY(!path.isEmpty());

        const QVector<CrimeEvent> events = CsvImporter::importFile(path, "test");
        QCOMPARE(events.size(), 3);

        for (const auto& ev : events) {
            QVERIFY(ev.qualityScore >= 0.0);
            QVERIFY(ev.qualityScore <= 1.0);
        }

        QFile::remove(path);
    }

    void testLargeFile()
    {
        const QString csv  = kCsvHeader + buildValidRows(1000);
        const QString path = writeTempCsv(csv);
        QVERIFY(!path.isEmpty());

        const QVector<CrimeEvent> events = CsvImporter::importFile(path, "test");
        QCOMPARE(events.size(), 1000);

        QFile::remove(path);
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// TestTemporalFeatures
// ─────────────────────────────────────────────────────────────────────────────

class TestTemporalFeatures : public QObject
{
    Q_OBJECT

private slots:
    void testExtractBasicFeatures()
    {
        // 2024-01-09 = Tuesday (Qt dayOfWeek()=2 → dow=1, not weekend)
        const QDateTime weekday(QDate(2024, 1, 9), QTime(10, 0, 0), Qt::UTC);
        const TemporalFeatureVector fvW = TemporalFeatures::compute(weekday);

        QVERIFY(!fvW.isWeekend);
        QCOMPARE(fvW.hourRaw, 10);
        QCOMPARE(fvW.dowRaw,  1);  // Tuesday

        // 2024-01-13 = Saturday (Qt dayOfWeek()=6 → dow=5, weekend)
        const QDateTime weekend(QDate(2024, 1, 13), QTime(10, 0, 0), Qt::UTC);
        const TemporalFeatureVector fvE = TemporalFeatures::compute(weekend);

        QVERIFY(fvE.isWeekend);
        QCOMPARE(fvE.dowRaw, 5);  // Saturday
    }

    void testHourBinning()
    {
        const QDate d(2024, 6, 15);

        // Midnight — night
        const TemporalFeatureVector fvMid = TemporalFeatures::compute(
            QDateTime(d, QTime(0, 0, 0), Qt::UTC));
        QCOMPARE(fvMid.hourRaw, 0);
        QVERIFY(fvMid.isNight);

        // Noon — not night
        const TemporalFeatureVector fvNoon = TemporalFeatures::compute(
            QDateTime(d, QTime(12, 0, 0), Qt::UTC));
        QCOMPARE(fvNoon.hourRaw, 12);
        QVERIFY(!fvNoon.isNight);

        // 6 PM — not night
        const TemporalFeatureVector fvEve = TemporalFeatures::compute(
            QDateTime(d, QTime(18, 0, 0), Qt::UTC));
        QCOMPARE(fvEve.hourRaw, 18);
        QVERIFY(!fvEve.isNight);

        // 22:30 — night
        const TemporalFeatureVector fvLate = TemporalFeatures::compute(
            QDateTime(d, QTime(22, 30, 0), Qt::UTC));
        QCOMPARE(fvLate.hourRaw, 22);
        QVERIFY(fvLate.isNight);
    }

    void testSeasonality()
    {
        // July mid-summer vs January mid-winter (northern hemisphere)
        const TemporalFeatureVector fvSummer = TemporalFeatures::compute(
            QDateTime(QDate(2024, 7, 15), QTime(12, 0, 0), Qt::UTC));
        const TemporalFeatureVector fvWinter = TemporalFeatures::compute(
            QDateTime(QDate(2024, 1, 15), QTime(12, 0, 0), Qt::UTC));

        // Month cyclical encodings must differ between month=7 and month=1
        QVERIFY(qAbs(fvSummer.monthSin - fvWinter.monthSin) > 0.01 ||
                qAbs(fvSummer.monthCos - fvWinter.monthCos) > 0.01);

        // Day-of-year encodings also differ (~180 days apart)
        QVERIFY(qAbs(fvSummer.doySin - fvWinter.doySin) > 0.01 ||
                qAbs(fvSummer.doyCos - fvWinter.doyCos) > 0.01);
    }

    void testRecencyDecay()
    {
        // TemporalFeatures encodes *when* an event occurred, not how long ago.
        // Two events 30 days apart should produce measurably different feature vectors.
        const TemporalFeatureVector fvRecent = TemporalFeatures::compute(
            QDateTime(QDate(2024, 6, 10), QTime(12, 0, 0), Qt::UTC));
        const TemporalFeatureVector fvOlder = TemporalFeatures::compute(
            QDateTime(QDate(2024, 5, 11), QTime(12, 0, 0), Qt::UTC));  // 30 days earlier

        // Day-of-year cyclical features should differ (30 days = ~30/365 * 2π apart)
        const double dSin = qAbs(fvRecent.doySin - fvOlder.doySin);
        const double dCos = qAbs(fvRecent.doyCos - fvOlder.doyCos);
        QVERIFY(dSin > 0.01 || dCos > 0.01);

        // The month encoding should also differ (June vs May)
        QVERIFY(qAbs(fvRecent.monthSin - fvOlder.monthSin) > 0.01 ||
                qAbs(fvRecent.monthCos - fvOlder.monthCos) > 0.01);
    }

    void testTemporalCluster()
    {
        // 10 events in a ~2-hour window (12-minute intervals: 10:00–11:48)
        // → their hourSin values should stay within a narrow band
        const QDate d(2024, 3, 20);
        QVector<TemporalFeatureVector> fvs;
        fvs.reserve(10);

        for (int i = 0; i < 10; ++i) {
            const int totalMinutes = i * 12;          // 0, 12, 24, … 108
            const int h = 10 + totalMinutes / 60;     // 10 or 11
            const int m = totalMinutes % 60;
            fvs.append(TemporalFeatures::compute(
                QDateTime(d, QTime(h, m, 0), Qt::UTC)));
        }

        QCOMPARE(fvs.size(), 10);

        // Max pairwise difference in hourSin should be small over a 2-hour window
        double maxDiff = 0.0;
        for (int i = 0; i < fvs.size(); ++i)
            for (int j = i + 1; j < fvs.size(); ++j)
                maxDiff = std::max(maxDiff, qAbs(fvs[i].hourSin - fvs[j].hourSin));

        QVERIFY(maxDiff < 0.5);  // sin(2π*h/24) changes slowly over 2 hours
    }

    void testEmptyInput()
    {
        // Default-constructed QDateTime is invalid — compute() must not crash.
        const QDateTime invalid;
        const TemporalFeatureVector fv = TemporalFeatures::compute(invalid);
        Q_UNUSED(fv);  // reaching here without crash is the assertion
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// TestHintEngine
// ─────────────────────────────────────────────────────────────────────────────

class TestHintEngine : public QObject
{
    Q_OBJECT

private:
    static CrimeEvent makeEvent(const QString& id = QStringLiteral("EVT001"))
    {
        CrimeEvent ev;
        ev.eventId    = id;
        ev.crimeType  = QStringLiteral("burglary");
        ev.occurredAt = QDateTime::currentDateTimeUtc();
        ev.lat        = -33.8688;
        ev.lon        =  151.2093;
        return ev;
    }

    static SeriesMatch makeSeriesMatch(double prob,
                                       const QString& seriesId = QStringLiteral("S1"))
    {
        SeriesMatch m;
        m.seriesId             = seriesId;
        m.memberCount          = 5;
        m.linkProbability      = prob;
        m.spatialDistanceM     = 120.0;
        m.temporalDistanceDays = 2.0;
        m.moSimilarity         = 0.75;
        m.compositeScore       = prob;
        m.method               = QStringLiteral("near_repeat");
        return m;
    }

    static MOMatch makeMOMatch(double similarity,
                               const QString& caseId = QStringLiteral("C1"))
    {
        MOMatch m;
        m.caseId          = caseId;
        m.similarityScore = similarity;
        m.sharedFeatures  = {QStringLiteral("forced_entry"), QStringLiteral("night")};
        m.resolved        = false;
        return m;
    }

private slots:
    void testBasicLeadGeneration()
    {
        HintEngineInput input;
        input.event       = makeEvent();
        input.dataQuality = 1.0;
        input.seriesMatches.append(makeSeriesMatch(0.7));
        input.moMatches.append(makeMOMatch(0.6));

        HintEngine engine;
        const QVector<InvestigativeLead> leads = engine.generate(input);

        QVERIFY(leads.size() >= 1);

        // Each lead must have a non-empty headline and category
        for (const auto& l : leads) {
            QVERIFY(!l.headline.isEmpty());
            QVERIFY(!l.category.isEmpty());
        }
    }

    void testLeadRanking()
    {
        // High-quality input: strong series match + high data quality
        HintEngineInput highInput;
        highInput.event       = makeEvent("EVT_HIGH");
        highInput.dataQuality = 1.0;
        highInput.seriesMatches.append(makeSeriesMatch(0.9));

        // Sparse input: weak series match + low data quality
        HintEngineInput lowInput;
        lowInput.event       = makeEvent("EVT_LOW");
        lowInput.dataQuality = 0.3;
        lowInput.seriesMatches.append(makeSeriesMatch(0.25));

        HintEngine engine;
        const QVector<InvestigativeLead> highLeads = engine.generate(highInput);
        const QVector<InvestigativeLead> lowLeads  = engine.generate(lowInput);

        QVERIFY(!highLeads.isEmpty());
        QVERIFY(!lowLeads.isEmpty());

        // High-quality series match should produce a higher-confidence lead
        QVERIFY(highLeads.first().confidence > lowLeads.first().confidence);
    }

    void testGeographicLeads()
    {
        HintEngineInput input;
        input.event       = makeEvent();
        input.dataQuality = 1.0;

        GeographicProfile gp;
        gp.peakLat         = -33.87;
        gp.peakLon         =  151.21;
        gp.peakProbability =  0.05;   // above the 0.001 threshold in geoLeads()
        gp.searchArea50pct =  2.5;
        gp.searchArea80pct =  5.0;
        gp.method          = QStringLiteral("rossmo_cgt");
        input.geoProfile   = gp;

        HintEngine engine;
        const QVector<InvestigativeLead> leads = engine.generate(input);

        const bool hasGeoLead = std::any_of(leads.begin(), leads.end(),
            [](const InvestigativeLead& l) {
                return l.category == QLatin1String("geographic_profile");
            });
        QVERIFY(hasGeoLead);
    }

    void testTemporalPatternLeads()
    {
        // HintEngine processes pre-computed inference inputs; simulate a recurring
        // daily series (7 members, tight temporal spacing) via a SeriesMatch.
        HintEngineInput input;
        input.event       = makeEvent();
        input.dataQuality = 1.0;

        SeriesMatch m      = makeSeriesMatch(0.8, QStringLiteral("DAILY_SERIES"));
        m.memberCount      = 7;    // 7 days
        m.temporalDistanceDays = 1.0;  // one day between events
        m.method           = QStringLiteral("near_repeat_temporal");
        input.seriesMatches.append(m);

        HintEngine engine;
        const QVector<InvestigativeLead> leads = engine.generate(input);

        const bool hasSeriesLead = std::any_of(leads.begin(), leads.end(),
            [](const InvestigativeLead& l) {
                return l.category == QLatin1String("series_linkage");
            });
        QVERIFY(hasSeriesLead);
    }

    void testMRRBenchmark()
    {
        // Five test cases; for each we know which lead category should appear first.
        // MRR = mean(1/rank_of_correct_lead).  Target: MRR > 0.3.
        HintEngine engine;

        struct TC {
            HintEngineInput input;
            QString expectedCategory;
        };

        QVector<TC> cases;

        // Case 1 — series match only → series_linkage expected at rank 1
        {
            TC tc;
            tc.input.event = makeEvent("E1");
            tc.input.dataQuality = 1.0;
            tc.input.seriesMatches.append(makeSeriesMatch(0.9));
            tc.expectedCategory = QStringLiteral("series_linkage");
            cases.append(tc);
        }

        // Case 2 — geo profile only → geographic_profile expected at rank 1
        {
            TC tc;
            tc.input.event = makeEvent("E2");
            tc.input.dataQuality = 1.0;
            GeographicProfile gp;
            gp.peakProbability = 0.02;
            gp.peakLat = -33.9; gp.peakLon = 151.1;
            gp.searchArea50pct = 1.0; gp.searchArea80pct = 2.0;
            gp.method = QStringLiteral("rossmo_cgt");
            tc.input.geoProfile = gp;
            tc.expectedCategory = QStringLiteral("geographic_profile");
            cases.append(tc);
        }

        // Case 3 — MO match only → mo_similarity expected at rank 1
        {
            TC tc;
            tc.input.event = makeEvent("E3");
            tc.input.dataQuality = 1.0;
            tc.input.moMatches.append(makeMOMatch(0.8));
            tc.expectedCategory = QStringLiteral("mo_similarity");
            cases.append(tc);
        }

        // Case 4 — network lead only → network_association expected at rank 1
        {
            TC tc;
            tc.input.event = makeEvent("E4");
            tc.input.dataQuality = 1.0;
            NetworkLead nl;
            nl.personId        = QStringLiteral("P001");
            nl.connectionType  = QStringLiteral("direct_cooffender");
            nl.sharedIncidents = 3;
            nl.centralityScore = 0.7;
            nl.communityId     = 1;
            nl.riskScore       = 0.8;
            nl.reasoning       = QStringLiteral("Known associate");
            tc.input.networkLeads.append(nl);
            tc.expectedCategory = QStringLiteral("network_association");
            cases.append(tc);
        }

        // Case 5 — anomaly signal only → statistical_anomaly expected at rank 1
        {
            TC tc;
            tc.input.event = makeEvent("E5");
            tc.input.dataQuality = 1.0;
            AnomalySignal sig;
            sig.eventId        = QStringLiteral("E5");
            sig.isolationScore = 0.90;
            sig.lofScore       = 0.85;
            sig.zScoreTemporal = 3.0;
            sig.zScoreSpatial  = 2.5;
            sig.combinedScore  = 0.87;
            sig.isAnomaly      = true;
            sig.signalReasons  = {"temporal_spike", "spatial_outlier"};
            tc.input.anomalySignal = sig;
            tc.expectedCategory = QStringLiteral("statistical_anomaly");
            cases.append(tc);
        }

        double totalRR  = 0.0;
        int    nCases   = 0;

        for (const auto& tc : cases) {
            const QVector<InvestigativeLead> leads = engine.generate(tc.input);
            if (leads.isEmpty()) continue;

            // Find 1-indexed rank of the first lead matching the expected category
            int rankFound = -1;
            for (int i = 0; i < leads.size(); ++i) {
                if (leads[i].category == tc.expectedCategory) {
                    rankFound = i + 1;
                    break;
                }
            }

            if (rankFound > 0)
                totalRR += 1.0 / rankFound;

            ++nCases;
        }

        QVERIFY(nCases > 0);
        const double mrr = totalRR / nCases;
        QVERIFY2(mrr > 0.3,
                 qPrintable(QString("MRR = %1 (expected > 0.3)").arg(mrr, 0, 'f', 3)));
    }

    void testEmptyInput()
    {
        HintEngineInput empty;
        empty.event = makeEvent("EMPTY");
        // No matches, no profile, no anomaly, no network leads

        HintEngine engine;
        const QVector<InvestigativeLead> leads = engine.generate(empty);
        QCOMPARE(leads.size(), 0);
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// main
// ─────────────────────────────────────────────────────────────────────────────

static int runTest(QObject* obj, const char* logFile)
{
    QStringList args = { QStringLiteral("test"),
                         QStringLiteral("-o"),
                         QStringLiteral("%1,txt").arg(logFile) };
    return QTest::qExec(obj, args);
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    int r = 0;
    TestCsvImporter      t1; r |= runTest(&t1, "csv_importer.txt");
    TestTemporalFeatures t2; r |= runTest(&t2, "temporal_features.txt");
    TestHintEngine       t3; r |= runTest(&t3, "hint_engine.txt");
    return r;
}
#include "test_csv_temporal_hint.moc"
