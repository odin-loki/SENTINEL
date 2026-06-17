// test_data_quality_deep5.cpp — Deep audit iteration 23: DataQualityScorer
// completeness decay, source reliability weighting, quarantine boundary,
// aggregate score range, temporal precision, and spatial precision.

#include <QTest>
#include <QTimeZone>
#include <cmath>

#include "ingest/DataQualityScorer.h"
#include "core/CrimeEvent.h"

class TestDataQualityDeep5 : public QObject
{
    Q_OBJECT

    static CrimeEvent baseEvent()
    {
        CrimeEvent ev;
        ev.eventId   = QStringLiteral("DEEP5");
        ev.crimeType = QStringLiteral("theft");
        ev.source    = QStringLiteral("uk_police_v1");
        ev.occurredAt = QDateTime(QDate(2024, 6, 15), QTime(14, 30, 0), QTimeZone::utc());
        ev.lat       = 51.50740;
        ev.lon       = -0.12780;
        ev.suburb    = QStringLiteral("Westminster");
        return ev;
    }

private slots:
    void testCompletenessDecreasesWithMissingFields();
    void testSourceReliabilityWeightingAffectsComposite();
    void testQuarantineThresholdBoundary();
    void testAggregateScoreClampedToUnitInterval();
    void testTemporalPrecisionScoringLabelsAndValues();
    void testSpatialPrecisionScoringLabelsAndValues();
};

void TestDataQualityDeep5::testCompletenessDecreasesWithMissingFields()
{
    const DataQualityScorer scorer = DataQualityScorer::withDefaults();

    const CrimeEvent full = baseEvent();

    CrimeEvent noCoords = full;
    noCoords.lat.reset();
    noCoords.lon.reset();

    CrimeEvent noTime = full;
    noTime.occurredAt.reset();

    CrimeEvent noLocationText = full;
    noLocationText.suburb.clear();
    noLocationText.locationRaw.reset();
    noLocationText.addressNormalised.reset();

    const QualityReport rFull          = scorer.score(full);
    const QualityReport rNoCoords      = scorer.score(noCoords);
    const QualityReport rNoTime        = scorer.score(noTime);
    const QualityReport rNoLocationText  = scorer.score(noLocationText);

    QCOMPARE(rFull.completeness, 1.0);
    QCOMPARE(rNoCoords.completeness, 0.75);
    QCOMPARE(rNoTime.completeness, 0.75);
    QCOMPARE(rNoLocationText.completeness, 0.75);

    QVERIFY(rFull.compositeScore > rNoCoords.compositeScore);
    QVERIFY(rFull.compositeScore > rNoTime.compositeScore);
    QVERIFY(rFull.compositeScore > rNoLocationText.compositeScore);
}

void TestDataQualityDeep5::testSourceReliabilityWeightingAffectsComposite()
{
    const DataQualityScorer scorer = DataQualityScorer::withDefaults();

    CrimeEvent high = baseEvent();
    high.source = QStringLiteral("uk_police_v1"); // 0.90

    CrimeEvent low = baseEvent();
    low.source = QStringLiteral("manual"); // 0.40

    const QualityReport rHigh = scorer.score(high);
    const QualityReport rLow  = scorer.score(low);

    QCOMPARE(rHigh.sourceReliability, 0.90);
    QCOMPARE(rLow.sourceReliability, 0.40);
    QVERIFY(rHigh.compositeScore > rLow.compositeScore);

    // Reliability contributes 30% of composite — identical other fields
    const double delta = rHigh.compositeScore - rLow.compositeScore;
    QVERIFY(std::abs(delta - 0.30 * (0.90 - 0.40)) < 1e-9);
}

void TestDataQualityDeep5::testQuarantineThresholdBoundary()
{
    const DataQualityScorer scorer = DataQualityScorer::withDefaults();

    CrimeEvent empty;
    empty.source = QStringLiteral("unknown"); // default reliability 0.5
    const QualityReport rEmpty = scorer.score(empty);
    QVERIFY(rEmpty.compositeScore < 0.3);
    QVERIFY(rEmpty.quarantined);

    CrimeEvent rich = baseEvent();
    const QualityReport rRich = scorer.score(rich);
    QVERIFY(rRich.compositeScore >= 0.3);
    QVERIFY(!rRich.quarantined);

    // Strict less-than: composite == 0.3 must not quarantine
    QMap<QString, double> tuned {
        { QStringLiteral("edge"), 0.3 },
    };
    const DataQualityScorer edgeScorer(tuned);

    CrimeEvent edge;
    edge.source = QStringLiteral("edge");
    const QualityReport rEdge = edgeScorer.score(edge);
    if (std::abs(rEdge.compositeScore - 0.3) < 1e-9) {
        QVERIFY2(!rEdge.quarantined,
                 "compositeScore == 0.3 must NOT quarantine (strict < threshold)");
    }
}

void TestDataQualityDeep5::testAggregateScoreClampedToUnitInterval()
{
    const DataQualityScorer scorer = DataQualityScorer::withDefaults();

    QMap<QString, double> maxRel {
        { QStringLiteral("perfect"), 1.0 },
    };
    const DataQualityScorer maxScorer(maxRel);

    CrimeEvent perfect = baseEvent();
    perfect.source = QStringLiteral("perfect");
    const QualityReport rPerfect = maxScorer.score(perfect);
    QVERIFY(rPerfect.compositeScore <= 1.0);
    QVERIFY(rPerfect.compositeScore >= 0.0);

    CrimeEvent worst;
    worst.source = QStringLiteral("manual");
    const QualityReport rWorst = scorer.score(worst);
    QVERIFY(rWorst.compositeScore >= 0.0);
    QVERIFY(rWorst.compositeScore <= 1.0);
}

void TestDataQualityDeep5::testTemporalPrecisionScoringLabelsAndValues()
{
    const DataQualityScorer scorer = DataQualityScorer::withDefaults();

    CrimeEvent hourLevel = baseEvent();
    hourLevel.occurredAt = QDateTime(QDate(2024, 7, 4), QTime(9, 45, 0), QTimeZone::utc());

    CrimeEvent dayLevel = baseEvent();
    dayLevel.occurredAt = QDateTime(QDate(2024, 7, 4), QTime(0, 0, 0), QTimeZone::utc());

    CrimeEvent monthLevel = baseEvent();
    monthLevel.occurredAt = QDateTime(QDate(2024, 7, 1), QTime(0, 0, 0), QTimeZone::utc());

    CrimeEvent unknown;
    unknown.source = QStringLiteral("test");

    QCOMPARE(scorer.score(hourLevel).temporalPrecision,  QStringLiteral("hour"));
    QCOMPARE(scorer.score(dayLevel).temporalPrecision,   QStringLiteral("day"));
    QCOMPARE(scorer.score(monthLevel).temporalPrecision, QStringLiteral("month"));
    QCOMPARE(scorer.score(unknown).temporalPrecision,    QStringLiteral("unknown"));

    QVERIFY(scorer.score(hourLevel).compositeScore >
            scorer.score(dayLevel).compositeScore);
    QVERIFY(scorer.score(dayLevel).compositeScore >
            scorer.score(monthLevel).compositeScore);
}

void TestDataQualityDeep5::testSpatialPrecisionScoringLabelsAndValues()
{
    const DataQualityScorer scorer = DataQualityScorer::withDefaults();

    CrimeEvent exact = baseEvent();
    exact.lat = 51.50740;
    exact.lon = -0.12780;

    CrimeEvent block = baseEvent();
    block.lat = 51.12;
    block.lon = -0.34;

    CrimeEvent suburb = baseEvent();
    suburb.lat = 51.5;
    suburb.lon = -0.1;

    CrimeEvent unknown = baseEvent();
    unknown.lat.reset();
    unknown.lon.reset();

    QCOMPARE(scorer.score(exact).spatialPrecision,   QStringLiteral("exact"));
    QCOMPARE(scorer.score(block).spatialPrecision,   QStringLiteral("block"));
    QCOMPARE(scorer.score(suburb).spatialPrecision, QStringLiteral("suburb"));
    QCOMPARE(scorer.score(unknown).spatialPrecision, QStringLiteral("unknown"));

    QVERIFY(scorer.score(exact).compositeScore > scorer.score(block).compositeScore);
    QVERIFY(scorer.score(block).compositeScore > scorer.score(suburb).compositeScore);
    QVERIFY(scorer.score(suburb).compositeScore > scorer.score(unknown).compositeScore);
}

QTEST_GUILESS_MAIN(TestDataQualityDeep5)
#include "test_data_quality_deep5.moc"
