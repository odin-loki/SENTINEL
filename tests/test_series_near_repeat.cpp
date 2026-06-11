// test_series_near_repeat.cpp
// Iteration-4 audit tests for SeriesDetector and NearRepeatVictimisation.
#include <QTest>
#include <cmath>
#include <algorithm>
#include "models/SeriesDetector.h"
#include "models/NearRepeatVictimisation.h"

class SeriesNearRepeatTest : public QObject
{
    Q_OBJECT

private:
    static SeriesEvent sev(const QString& id, double lat, double lon, double tDays,
                            const QString& mo = QStringLiteral("forced entry residential"),
                            const QString& type = QStringLiteral("burglary"))
    {
        SeriesEvent e;
        e.eventId    = id;
        e.lat        = lat;
        e.lon        = lon;
        e.tDays      = tDays;
        e.crimeType  = type;
        e.moText     = mo;
        return e;
    }

    static CrimeSeries makeSeriesFromEvents(const QVector<SeriesEvent>& evs)
    {
        CrimeSeries s;
        s.seriesId          = QStringLiteral("SERIES-TEST");
        s.dominantCrimeType = evs.first().crimeType;
        s.members           = evs;
        s.centroidLat       = evs.first().lat;
        s.centroidLon       = evs.first().lon;
        s.firstDays         = evs.first().tDays;
        s.lastDays          = evs.last().tDays;
        return s;
    }

private slots:

    // ── SeriesDetector ───────────────────────────────────────────────────────

    void testSeriesHaversineDistance()
    {
        const double d = SeriesDetector::haversineKm(0.0, 0.0, 1.0, 0.0);
        QVERIFY2(std::abs(d - 111.0) < 2.0,
                 qPrintable(QStringLiteral("1 deg lat should be ~111 km, got %1").arg(d)));
    }

    void testSeriesMOJaccard()
    {
        const QString mo = QStringLiteral("forced entry residential night");
        const double identical = SeriesDetector::moJaccard(mo, mo);
        QVERIFY2(std::abs(identical - 1.0) < 1e-9,
                 "Identical MO strings should have Jaccard similarity 1.0");

        const double disjoint = SeriesDetector::moJaccard(
            QStringLiteral("forced entry residential"),
            QStringLiteral("vehicle daytime weapon"));
        QVERIFY2(std::abs(disjoint) < 1e-9,
                 "Disjoint MO strings should have Jaccard similarity 0.0");
    }

    void testSeriesDBSCANMinClusterSize()
    {
        SeriesDetector sd(0.3, 14.0, 3);
        const auto result = sd.detectSeries({
            sev(QStringLiteral("E1"), 51.5, -0.1, 0.0),
            sev(QStringLiteral("E2"), 51.5, -0.1, 1.0),
        });
        QVERIFY2(result.isEmpty(),
                 "Fewer than minPts events should not form a cluster");
    }

    void testSeriesGroupsCloseEvents()
    {
        SeriesDetector sd(0.3, 14.0, 3);
        QVector<SeriesEvent> evs;
        for (int i = 0; i < 10; ++i) {
            evs.append(sev(QStringLiteral("E%1").arg(i),
                           51.5 + i * 0.0001,
                           -0.1 + i * 0.0001,
                           0.0));
        }

        const auto result = sd.detectSeries(evs);
        QCOMPARE(result.size(), 1);

        int totalMembers = 0;
        for (const auto& s : result)
            totalMembers += s.members.size();
        QCOMPARE(totalMembers, 10);
    }

    void testSeriesSeparatesDistantEvents()
    {
        SeriesDetector sd(0.3, 14.0, 3);
        QVector<SeriesEvent> evs;
        for (int i = 0; i < 3; ++i) {
            evs.append(sev(QStringLiteral("A%1").arg(i),
                           51.5 + i * 0.0001,
                           -0.1,
                           static_cast<double>(i)));
        }
        for (int i = 0; i < 3; ++i) {
            evs.append(sev(QStringLiteral("B%1").arg(i),
                           52.0 + i * 0.0001,
                           -0.1,
                           static_cast<double>(i)));
        }

        const auto result = sd.detectSeries(evs);
        QVERIFY2(result.size() >= 2,
                 "Two groups ~55 km apart should form separate clusters");
    }

    void testSeriesIdConsistent()
    {
        SeriesDetector sd(0.3, 14.0, 3);
        QVector<SeriesEvent> evs;
        for (int i = 0; i < 5; ++i)
            evs.append(sev(QStringLiteral("E%1").arg(i), 51.5, -0.1, static_cast<double>(i)));

        const auto result = sd.detectSeries(evs);
        QVERIFY(!result.isEmpty());
        QCOMPARE(result.size(), 1);

        const QString seriesId = result.first().seriesId;
        QVERIFY(!seriesId.isEmpty());
        QCOMPARE(result.first().members.size(), 5);
        for (const auto& member : result.first().members)
            QVERIFY2(!member.eventId.isEmpty(),
                     "Every clustered event should retain its eventId");
    }

    void testSeriesTemporalDecay()
    {
        SeriesDetector sd;
        QVector<SeriesEvent> members;
        for (int i = 0; i < 4; ++i)
            members.append(sev(QStringLiteral("M%1").arg(i), 51.5, -0.1, static_cast<double>(i)));

        const CrimeSeries series = makeSeriesFromEvents(members);

        const SeriesEvent recent  = sev(QStringLiteral("NEW-R"), 51.5, -0.1, 3.5);
        const SeriesEvent farPast = sev(QStringLiteral("NEW-F"), 51.5, -0.1, 100.0);

        const SeriesMatch recentMatch = sd.linkProbability(recent, series, 1.0);
        const SeriesMatch farMatch    = sd.linkProbability(farPast, series, 1.0);

        QVERIFY2(recentMatch.compositeScore > farMatch.compositeScore,
                 qPrintable(QStringLiteral("Older event should have lower composite score: recent=%1 far=%2")
                    .arg(recentMatch.compositeScore).arg(farMatch.compositeScore)));
    }

    void testSeriesEmptyInput()
    {
        SeriesDetector sd;
        const auto result = sd.detectSeries({});
        QVERIFY(result.isEmpty());
    }

    void testSeriesSingleEvent()
    {
        SeriesDetector sd;
        const auto result = sd.detectSeries({
            sev(QStringLiteral("E1"), 51.5, -0.1, 0.0),
        });
        QCOMPARE(result.size(), 0);
    }

    // ── NearRepeatVictimisation ──────────────────────────────────────────────

    void testNearRepeatMinimumEvents()
    {
        NearRepeatVictimisation nrv(200.0, 14.0);
        const auto alerts = nrv.analyse({ sev(QStringLiteral("E1"), 51.5, -0.1, 0.0) });
        QVERIFY(alerts.isEmpty());
        QCOMPARE(nrv.knoxStatistic({ sev(QStringLiteral("E1"), 51.5, -0.1, 0.0) }), 1.0);
    }

    void testNearRepeatAlertsForCloseEvents()
    {
        NearRepeatVictimisation nrv(200.0, 7.0);
        QVector<SeriesEvent> evs;
        for (int i = 0; i < 5; ++i) {
            evs.append(sev(QStringLiteral("E%1").arg(i),
                           51.5074 + i * 0.0002,
                           -0.1278,
                           static_cast<double>(i)));
        }

        const auto alerts = nrv.analyse(evs);
        QVERIFY2(!alerts.isEmpty(), "Close events within 7 days should produce alerts");

        const bool anyPositive = std::any_of(alerts.begin(), alerts.end(),
            [](const NearRepeatAlert& a) { return a.alertScore > 0.0; });
        QVERIFY2(anyPositive, "At least one alertScore should be > 0");
    }

    void testNearRepeatDecayWithDistance()
    {
        NearRepeatVictimisation nrv(200.0, 14.0);
        const double nearScore = nrv.alertScore(50.0, 2.0, QStringLiteral("burglary"));
        const double farScore  = nrv.alertScore(500.0, 2.0, QStringLiteral("burglary"));
        QVERIFY2(nearScore > farScore,
                 qPrintable(QStringLiteral("Closer events should score higher: near=%1 far=%2")
                    .arg(nearScore).arg(farScore)));
    }

    void testNearRepeatDecayWithTime()
    {
        NearRepeatVictimisation nrv(200.0, 14.0);
        const double recentScore = nrv.alertScore(50.0, 1.0, QStringLiteral("burglary"));
        const double olderScore  = nrv.alertScore(50.0, 20.0, QStringLiteral("burglary"));
        QVERIFY2(recentScore > olderScore,
                 qPrintable(QStringLiteral("Recent events should score higher: recent=%1 older=%2")
                    .arg(recentScore).arg(olderScore)));
    }

    void testNearRepeatKnoxStat()
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
                 qPrintable(QStringLiteral("Clustered events should yield Knox statistic > 1, got %1")
                    .arg(knox)));
    }

    void testNearRepeatNoFalsePositives()
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
        QVERIFY2(alerts.isEmpty(),
                 "Randomly spread events (~10 km) should not produce near-repeat alerts");
    }
};

QTEST_MAIN(SeriesNearRepeatTest)
#include "test_series_near_repeat.moc"
