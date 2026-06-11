// test_nearrepeat_csv_mo.cpp
// Iteration-5 audit: NearRepeatVictimisation, CsvImporter, MOAnalyser
#include <QTest>
#include <QTemporaryFile>
#include <QTextStream>
#include <cmath>
#include <algorithm>

#include "models/NearRepeatVictimisation.h"
#include "models/SeriesDetector.h"
#include "ingest/CsvImporter.h"
#include "inference/MOAnalyser.h"
#include "core/CrimeEvent.h"

class NearRepeatCsvMOTest : public QObject
{
    Q_OBJECT

private:
    static SeriesEvent sev(const QString& id, double lat, double lon, double tDays,
                            const QString& type = QStringLiteral("burglary"))
    {
        SeriesEvent e;
        e.eventId   = id;
        e.lat       = lat;
        e.lon       = lon;
        e.tDays     = tDays;
        e.crimeType = type;
        return e;
    }

    static MOCaseRecord makeCase(const QString& id, const QString& mo,
                                  bool resolved = false)
    {
        MOCaseRecord r;
        r.caseId   = id;
        r.moText   = mo;
        r.resolved = resolved;
        return r;
    }

    static QString writeTmpCsv(const QString& content)
    {
        auto* tmp = new QTemporaryFile();
        tmp->setAutoRemove(false);
        if (!tmp->open()) return {};
        QTextStream out(tmp);
        out.setEncoding(QStringConverter::Utf8);
        out << content;
        tmp->close();
        const QString path = tmp->fileName();
        delete tmp;
        return path;
    }

    static QString writeBinaryCsv(const QByteArray& data)
    {
        auto* tmp = new QTemporaryFile();
        tmp->setAutoRemove(false);
        if (!tmp->open()) return {};
        tmp->write(data);
        tmp->close();
        const QString path = tmp->fileName();
        delete tmp;
        return path;
    }

private slots:

    // ── NearRepeatVictimisation ─────────────────────────────────────────────

    void testNearRepeatSingleEvent()
    {
        NearRepeatVictimisation nrv(200.0, 14.0);
        const auto alerts = nrv.analyse({ sev(QStringLiteral("E1"), 51.5, -0.1, 0.0) });
        QVERIFY(alerts.isEmpty());
        QCOMPARE(nrv.knoxStatistic({ sev(QStringLiteral("E1"), 51.5, -0.1, 0.0) }), 1.0);
    }

    void testNearRepeatTwoCloseEvents()
    {
        NearRepeatVictimisation nrv(200.0, 14.0);
        const auto evs = QVector<SeriesEvent>{
            sev(QStringLiteral("E1"), 51.5074, -0.1278, 0.0),
            sev(QStringLiteral("E2"), 51.5075, -0.1279, 3.0),
        };

        const auto alerts = nrv.analyse(evs);
        const bool hasAlert = !alerts.isEmpty();
        const double score  = nrv.alertScore(100.0, 3.0, QStringLiteral("burglary"));
        QVERIFY2(hasAlert || score > 0.0,
                 "Two close events within 100m/3 days should produce alerts or positive score");
    }

    void testNearRepeatSpatialDecay()
    {
        NearRepeatVictimisation nrv(200.0, 14.0);
        const double nearScore = nrv.alertScore(50.0, 2.0, QStringLiteral("burglary"));
        const double farScore  = nrv.alertScore(500.0, 2.0, QStringLiteral("burglary"));
        QVERIFY2(nearScore > farScore,
                 qPrintable(QStringLiteral("Nearer event should score higher: near=%1 far=%2")
                    .arg(nearScore).arg(farScore)));
    }

    void testNearRepeatTemporalDecay()
    {
        NearRepeatVictimisation nrv(200.0, 14.0);
        const double recentScore = nrv.alertScore(50.0, 1.0, QStringLiteral("burglary"));
        const double olderScore  = nrv.alertScore(50.0, 20.0, QStringLiteral("burglary"));
        QVERIFY2(recentScore > olderScore,
                 qPrintable(QStringLiteral("Recent event should score higher: recent=%1 older=%2")
                    .arg(recentScore).arg(olderScore)));
    }

    void testNearRepeatKnoxAboveOne()
    {
        NearRepeatVictimisation nrv(200.0, 7.0);
        QVector<SeriesEvent> evs;
        for (int i = 0; i < 6; ++i) {
            evs.append(sev(QStringLiteral("E%1").arg(i),
                           51.5074 + i * 0.0001,
                           -0.1278,
                           static_cast<double>(i % 3)));
        }

        const double knox = nrv.knoxStatistic(evs);
        QVERIFY2(knox > 1.0,
                 qPrintable(QStringLiteral("Clustered events should yield Knox > 1, got %1")
                    .arg(knox)));
    }

    void testNearRepeatRandomData()
    {
        NearRepeatVictimisation nrv(200.0, 7.0);
        QVector<SeriesEvent> evs;
        for (int i = 0; i < 5; ++i) {
            evs.append(sev(QStringLiteral("E%1").arg(i),
                           51.5 + i * 0.09,
                           -0.1 + i * 0.09,
                           static_cast<double>(i * 30)));
        }

        const auto alerts = nrv.analyse(evs);
        double meanScore = 0.0;
        for (const auto& a : alerts)
            meanScore += a.alertScore;
        if (!alerts.isEmpty())
            meanScore /= alerts.size();

        QVERIFY2(alerts.isEmpty() || meanScore < 0.3,
                 qPrintable(QStringLiteral("Spread events should have low mean alert score, got %1")
                    .arg(meanScore)));
    }

    void testNearRepeatAlertScoreBounded()
    {
        NearRepeatVictimisation nrv(200.0, 14.0);
        QVector<SeriesEvent> evs;
        for (int i = 0; i < 8; ++i) {
            evs.append(sev(QStringLiteral("E%1").arg(i),
                           51.5074 + i * 0.0002,
                           -0.1278,
                           static_cast<double>(i)));
        }

        const auto alerts = nrv.analyse(evs);
        for (const auto& a : alerts) {
            QVERIFY2(a.alertScore >= 0.0 && a.alertScore <= 1.0,
                     qPrintable(QStringLiteral("alertScore %1 must be in [0,1]")
                        .arg(a.alertScore)));
        }
    }

    // ── CsvImporter ─────────────────────────────────────────────────────────

    void testCsvHandlesCRLF()
    {
        const QByteArray data =
            "id,date,crime_type,lat,lon\r\n"
            "1,2024-01-15,burglary,51.5074,-0.1278\r\n"
            "2,2024-01-16,theft,51.6,-0.2\r\n";
        const QString path = writeBinaryCsv(data);
        const auto events = CsvImporter::importFile(path);
        QCOMPARE(events.size(), 2);
        QCOMPARE(events[0].crimeType, QStringLiteral("burglary"));
        QCOMPARE(events[1].crimeType, QStringLiteral("theft"));
        QFile::remove(path);
    }

    void testCsvHeaderOnlyFile()
    {
        const QString path = writeTmpCsv(QStringLiteral("id,date,crime_type\n"));
        const auto events = CsvImporter::importFile(path);
        QCOMPARE(events.size(), 0);
        QFile::remove(path);
    }

    void testCsvLatLonParsing()
    {
        CsvColumnMap m;
        m.latCol = 0;
        m.lonCol = 1;
        m.crimeTypeCol = 2;
        m.dateCol = 3;
        const QStringList fields = {
            QStringLiteral("51.5"),
            QStringLiteral("-0.1"),
            QStringLiteral("burglary"),
            QStringLiteral("2024-01-15"),
        };
        const auto ev = CsvImporter::parseRow(fields, m, QStringLiteral("test"));
        QVERIFY(ev.lat.has_value());
        QVERIFY(ev.lon.has_value());
        QVERIFY(std::abs(*ev.lat - 51.5) < 1e-6);
        QVERIFY(std::abs(*ev.lon - (-0.1)) < 1e-6);
    }

    void testCsvChicagoFormat()
    {
        const QStringList headers = {
            QStringLiteral("ID"), QStringLiteral("Date"), QStringLiteral("Primary Type"),
            QStringLiteral("Description"), QStringLiteral("Latitude"), QStringLiteral("Longitude"),
            QStringLiteral("Block"), QStringLiteral("Arrest"),
            QStringLiteral("Location Description"),
        };
        const CsvColumnMap m = CsvImporter::detectColumns(headers);
        QVERIFY(m.crimeTypeCol >= 0);
        QVERIFY(m.latCol >= 0);
        QVERIFY(m.lonCol >= 0);
        QVERIFY(m.dateCol >= 0);
    }

    void testCsvNYPDFormat()
    {
        const QStringList headers = {
            QStringLiteral("INCIDENT_KEY"), QStringLiteral("OCCUR_DATE"), QStringLiteral("OFFENSE"),
            QStringLiteral("NARRATIVE"), QStringLiteral("Latitude"), QStringLiteral("Longitude"),
        };
        const CsvColumnMap m = CsvImporter::detectColumns(headers);
        QVERIFY(m.idCol >= 0);
        QVERIFY(m.dateCol >= 0);
        QVERIFY(m.crimeTypeCol >= 0);
        QVERIFY(m.descCol >= 0);
        QVERIFY(m.latCol >= 0);
        QVERIFY(m.lonCol >= 0);
    }

    void testCsvMissingColumns()
    {
        const QString content =
            QStringLiteral("date,crime_type\n")
            + QStringLiteral("2024-01-15,burglary\n");
        const QString path = writeTmpCsv(content);
        const auto events = CsvImporter::importFile(path);
        QCOMPARE(events.size(), 1);
        QVERIFY(!events[0].lat.has_value());
        QVERIFY(!events[0].lon.has_value());
        QFile::remove(path);
    }

    void testCsvDuplicateEventIds()
    {
        const QString content =
            QStringLiteral("id,date,crime_type,lat,lon\n")
            + QStringLiteral("DUP1,2024-01-15,burglary,51.5,-0.1\n")
            + QStringLiteral("DUP1,2024-01-16,theft,51.6,-0.2\n");
        const QString path = writeTmpCsv(content);
        const auto events = CsvImporter::importFile(path, QStringLiteral("src"));
        QCOMPARE(events.size(), 2);
        QCOMPARE(events[0].eventId, events[1].eventId);
        QFile::remove(path);
    }

    // ── MOAnalyser ────────────────────────────────────────────────────────────

    void testMOAnalyserSingleWordQuery()
    {
        MOAnalyser ana;
        ana.fit({ makeCase(QStringLiteral("C1"), QStringLiteral("burglary")) });

        const auto results = ana.findSimilar(QStringLiteral("burglary"), 1, 0.0);
        QVERIFY2(!results.isEmpty(), "Single-word query should return a match");
        QVERIFY2(results.first().similarityScore > 0.0,
                 qPrintable(QStringLiteral("Single-word cosine similarity should be > 0, got %1")
                    .arg(results.first().similarityScore)));
    }

    void testMOAnalyserCaseInsensitive()
    {
        MOAnalyser ana;
        const QString mo = QStringLiteral("forced entry residential night");
        ana.fit({ makeCase(QStringLiteral("C1"), mo) });

        const auto lower = ana.findSimilar(mo, 1, 0.0);
        const auto upper = ana.findSimilar(QStringLiteral("FORCED ENTRY RESIDENTIAL NIGHT"), 1, 0.0);
        QVERIFY2(!lower.isEmpty() && !upper.isEmpty(), "Both case variants should match");
        QVERIFY2(std::abs(lower.first().similarityScore - upper.first().similarityScore) < 1e-9,
                 "Case variants should produce identical similarity scores");
    }

    void testMOAnalyserAddCaseGrowsVocab()
    {
        MOAnalyser ana;
        ana.fit({ makeCase(QStringLiteral("C1"), QStringLiteral("alpha token one")) });
        QCOMPARE(ana.caseCount(), 1);

        ana.fit({
            makeCase(QStringLiteral("C1"), QStringLiteral("alpha token one")),
            makeCase(QStringLiteral("C2"), QStringLiteral("beta token two unique_word")),
        });
        QCOMPARE(ana.caseCount(), 2);

        const auto results = ana.findSimilar(QStringLiteral("unique_word"), 2, 0.0);
        QVERIFY2(!results.isEmpty(), "Expanded corpus should include new vocabulary");
        QCOMPARE(results.first().caseId, QStringLiteral("C2"));
    }

    void testMOAnalyserEmptyVocab()
    {
        MOAnalyser ana;
        const auto results = ana.findSimilar(QStringLiteral("burglary forced entry"), 5, 0.0);
        QCOMPARE(results.size(), 0);
    }

    void testMOAnalyserResolvedBoost()
    {
        MOAnalyser ana;
        ana.fit({
            makeCase(QStringLiteral("UNRES"), QStringLiteral("forced entry residential tok_a"), false),
            makeCase(QStringLiteral("RES"),   QStringLiteral("forced entry residential tok_b"), true),
        });

        const auto results = ana.findSimilar(QStringLiteral("forced entry residential"), 2, 0.0);
        QVERIFY2(results.size() >= 2, "Both cases should match the query");
        QCOMPARE(results.first().caseId, QStringLiteral("RES"));
        QVERIFY(results.first().resolved);
    }
};

QTEST_MAIN(NearRepeatCsvMOTest)
#include "test_nearrepeat_csv_mo.moc"
