#include <QTest>
#include "models/SeriesDetector.h"
#include <cmath>

class TestSeriesDetectorDeep2 : public QObject
{
    Q_OBJECT

private:
    static SeriesEvent sev(const QString& id, double lat, double lon, double tDays,
                            const QString& mo = QStringLiteral(""),
                            const QString& type = QStringLiteral("burglary"))
    {
        SeriesEvent e;
        e.eventId   = id;
        e.lat       = lat;
        e.lon       = lon;
        e.tDays     = tDays;
        e.crimeType = type;
        e.moText    = mo;
        return e;
    }

private slots:

    void testHaversineKmLondonToParis()
    {
        const double london_lat = 51.5074, london_lon = -0.1278;
        const double paris_lat  = 48.8566, paris_lon  =  2.3522;
        const double dist = SeriesDetector::haversineKm(london_lat, london_lon,
                                                         paris_lat,  paris_lon);
        QVERIFY2(dist > 330.0 && dist < 355.0,
                 qPrintable(QStringLiteral("London–Paris should be ~343 km, got %1").arg(dist)));
    }

    void testHaversineKmSamePoint()
    {
        const double d = SeriesDetector::haversineKm(51.5, -0.1, 51.5, -0.1);
        QCOMPARE(d, 0.0);
    }

    void testHaversineKmSymmetric()
    {
        const double d1 = SeriesDetector::haversineKm(51.5, -0.1, 48.8, 2.3);
        const double d2 = SeriesDetector::haversineKm(48.8, 2.3, 51.5, -0.1);
        QVERIFY(qAbs(d1 - d2) < 1e-9);
    }

    void testMoJaccardIdentical()
    {
        const double j = SeriesDetector::moJaccard(
            QStringLiteral("forced entry residential night"),
            QStringLiteral("forced entry residential night"));
        QVERIFY2(qAbs(j - 1.0) < 1e-9,
                 qPrintable(QStringLiteral("Identical strings should give Jaccard 1.0, got %1").arg(j)));
    }

    void testMoJaccardCompletelyDifferent()
    {
        const double j = SeriesDetector::moJaccard(
            QStringLiteral("apple banana cherry"),
            QStringLiteral("delta echo foxtrot"));
        QVERIFY2(qAbs(j - 0.0) < 1e-9,
                 qPrintable(QStringLiteral("No-overlap strings should give Jaccard 0.0, got %1").arg(j)));
    }

    void testMoJaccardBothEmpty()
    {
        const double j = SeriesDetector::moJaccard(QString(), QString());
        QVERIFY2(qAbs(j - 0.0) < 1e-9,
                 qPrintable(QStringLiteral("Both-empty should give 0.0, got %1").arg(j)));
    }

    void testMoJaccardOneEmptyOneNonEmpty()
    {
        const double j = SeriesDetector::moJaccard(QString(), QStringLiteral("foo bar"));
        QVERIFY2(qAbs(j - 0.0) < 1e-9,
                 qPrintable(QStringLiteral("One-empty should give 0.0, got %1").arg(j)));

        const double j2 = SeriesDetector::moJaccard(QStringLiteral("foo bar"), QString());
        QVERIFY2(qAbs(j2 - 0.0) < 1e-9,
                 qPrintable(QStringLiteral("Other-empty should give 0.0, got %1").arg(j2)));
    }

    void testMoJaccardPartialOverlap()
    {
        const double j = SeriesDetector::moJaccard(
            QStringLiteral("forced entry night"),
            QStringLiteral("forced entry daytime"));
        QVERIFY(j > 0.0 && j < 1.0);
    }

    void testDetectSeriesColocated()
    {
        // 5 co-located events within default epsKm=0.3 and epsDays=14 → 1 series
        SeriesDetector sd(0.3, 14.0, 3);
        QVector<SeriesEvent> events;
        for (int i = 0; i < 5; ++i)
            events.append(sev(QStringLiteral("E%1").arg(i), 51.5, -0.1,
                              static_cast<double>(i)));

        const auto result = sd.detectSeries(events);
        QVERIFY2(result.size() == 1,
                 qPrintable(QStringLiteral("5 co-located events should form 1 series, got %1")
                            .arg(result.size())));
        QVERIFY(result[0].members.size() == 5);
    }

    void testDetectSeriesSpread()
    {
        // Events 1 degree apart (>>epsKm) → all noise → 0 series
        SeriesDetector sd(0.3, 14.0, 3);
        QVector<SeriesEvent> events;
        for (int i = 0; i < 5; ++i)
            events.append(sev(QStringLiteral("S%1").arg(i),
                              51.5 + i * 1.0, -0.1,
                              static_cast<double>(i)));

        const auto result = sd.detectSeries(events);
        QVERIFY2(result.isEmpty(),
                 qPrintable(QStringLiteral("Spread events should produce 0 series, got %1")
                            .arg(result.size())));
    }

    void testLinkProbabilityRangeCheck()
    {
        SeriesDetector sd;
        const SeriesEvent newEv = sev(QStringLiteral("NEW"), 51.5, -0.1, 100.0,
                                       QStringLiteral("smashed window"), QStringLiteral("burglary"));

        CrimeSeries series;
        series.seriesId          = QStringLiteral("SERIES-0000");
        series.dominantCrimeType = QStringLiteral("burglary");
        SeriesEvent member       = sev(QStringLiteral("M1"), 51.5, -0.1, 99.0);
        member.crimeType         = QStringLiteral("burglary");
        series.members           = { member };

        const auto match = sd.linkProbability(newEv, series, 0.8);
        QVERIFY2(match.linkProbability >= 0.0 && match.linkProbability <= 0.95,
                 qPrintable(QStringLiteral("linkProbability must be in [0, 0.95], got %1")
                            .arg(match.linkProbability)));
        QVERIFY2(match.compositeScore >= 0.0 && match.compositeScore <= 1.0,
                 qPrintable(QStringLiteral("compositeScore must be in [0, 1], got %1")
                            .arg(match.compositeScore)));
    }

    void testLinkProbabilityZeroDistance()
    {
        // Perfect spatial/temporal match with high MO similarity → high composite
        SeriesDetector sd;
        const SeriesEvent newEv = sev(QStringLiteral("NEW"), 51.5, -0.1, 100.0);

        CrimeSeries series;
        series.seriesId          = QStringLiteral("SERIES-0001");
        series.dominantCrimeType = QStringLiteral("burglary");
        series.members           = { sev(QStringLiteral("M1"), 51.5, -0.1, 100.0) };

        const auto match = sd.linkProbability(newEv, series, 1.0);
        QVERIFY(match.compositeScore > 0.9);
        QVERIFY(match.linkProbability > 0.0);
    }

    void testNearRepeatForBurglary()
    {
        const auto params = SeriesDetector::nearRepeatFor(QStringLiteral("burglary"));
        const auto defaultParams = SeriesDetector::NearRepeatParams{};

        QVERIFY2(params.distM != defaultParams.distM || params.days != defaultParams.days,
                 "nearRepeatFor('burglary') should return non-default parameters");
        QVERIFY(params.distM > 0.0);
        QVERIFY(params.days > 0.0);
        QVERIFY(params.multiplier > 1.0);
    }

    void testNearRepeatForUnknownType()
    {
        const auto params = SeriesDetector::nearRepeatFor(QStringLiteral("unknown_crime_xyz"));
        const auto def    = SeriesDetector::NearRepeatParams{};
        QCOMPARE(params.distM,      def.distM);
        QCOMPARE(params.days,       def.days);
        QCOMPARE(params.multiplier, def.multiplier);
    }

    void testNearRepeatForCaseInsensitive()
    {
        const auto lower = SeriesDetector::nearRepeatFor(QStringLiteral("burglary"));
        const auto upper = SeriesDetector::nearRepeatFor(QStringLiteral("BURGLARY"));
        QCOMPARE(lower.distM,      upper.distM);
        QCOMPARE(lower.days,       upper.days);
        QCOMPARE(lower.multiplier, upper.multiplier);
    }
};

QTEST_GUILESS_MAIN(TestSeriesDetectorDeep2)
#include "test_series_detector_deep2.moc"
