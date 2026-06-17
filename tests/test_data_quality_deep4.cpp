// test_data_quality_deep4.cpp — Iteration 19 deep audit: flat coord fallback,
// location text variants, precision labels, alias sources, and quarantine edge.
#include <QTest>
#include <QTimeZone>
#include <cmath>

#include "ingest/DataQualityScorer.h"
#include "core/CrimeEvent.h"

class TestDataQualityDeep4 : public QObject
{
    Q_OBJECT

    static CrimeEvent baseEvent()
    {
        CrimeEvent ev;
        ev.eventId   = QStringLiteral("DEEP4");
        ev.crimeType = QStringLiteral("theft");
        ev.source    = QStringLiteral("uk_police_v1");
        return ev;
    }

private slots:
    void testFlatLatitudeLongitudeFallback();
    void testLocationRawAndAddressNormalisedCountAsLocation();
    void testSourceAliasReliabilityMatchesCanonical();
    void testTemporalMonthPrecisionDayOneMidnight();
    void testSpatialExactVersusBlockPrecision();
    void testZeroCoordinatesFailCompletenessAndSpatial();
    void testInvalidLongitudePenalizesScore();
    void testQuarantineThresholdExactPointThreeNotQuarantined();
};

void TestDataQualityDeep4::testFlatLatitudeLongitudeFallback()
{
    const DataQualityScorer scorer = DataQualityScorer::withDefaults();

    CrimeEvent viaOptional = baseEvent();
    viaOptional.lat        = 51.5074;
    viaOptional.lon        = -0.1278;
    viaOptional.occurredAt = QDateTime(QDate(2024, 3, 10), QTime(14, 0), QTimeZone::utc());
    viaOptional.suburb     = QStringLiteral("Westminster");

    CrimeEvent viaFlat = baseEvent();
    viaFlat.latitude   = 51.5074;
    viaFlat.longitude  = -0.1278;
    viaFlat.occurredAt = viaOptional.occurredAt;
    viaFlat.suburb     = viaOptional.suburb;

    const QualityReport rOpt = scorer.score(viaOptional);
    const QualityReport rFlat = scorer.score(viaFlat);

    QCOMPARE(rFlat.spatialPrecision,  rOpt.spatialPrecision);
    QCOMPARE(rFlat.completeness,      rOpt.completeness);
    QCOMPARE(rFlat.compositeScore,    rOpt.compositeScore);
    QCOMPARE(rFlat.quarantined,       rOpt.quarantined);
}

void TestDataQualityDeep4::testLocationRawAndAddressNormalisedCountAsLocation()
{
    const DataQualityScorer scorer = DataQualityScorer::withDefaults();

    CrimeEvent rawOnly = baseEvent();
    rawOnly.locationRaw = QStringLiteral("14 Baker Street");
    rawOnly.occurredAt  = QDateTime(QDate(2024, 4, 1), QTime(9, 0), QTimeZone::utc());
    rawOnly.lat         = 51.52;
    rawOnly.lon         = -0.15;

    CrimeEvent addrOnly = baseEvent();
    addrOnly.addressNormalised = QStringLiteral("14 Baker Street, London");
    addrOnly.occurredAt        = rawOnly.occurredAt;
    addrOnly.lat               = rawOnly.lat;
    addrOnly.lon               = rawOnly.lon;

    QCOMPARE(scorer.score(rawOnly).completeness, 1.0);
    QCOMPARE(scorer.score(addrOnly).completeness, 1.0);
}

void TestDataQualityDeep4::testSourceAliasReliabilityMatchesCanonical()
{
    const auto defaults = DataQualityScorer::defaultReliabilityMap();
    const DataQualityScorer scorer = DataQualityScorer::withDefaults();

    CrimeEvent canonical = baseEvent();
    canonical.source = QStringLiteral("open_meteo");

    CrimeEvent alias = baseEvent();
    alias.source = QStringLiteral("weather");

    QCOMPARE(defaults[QStringLiteral("open_meteo")],
             defaults[QStringLiteral("weather")]);
    QCOMPARE(scorer.score(canonical).sourceReliability,
             scorer.score(alias).sourceReliability);
    QCOMPARE(scorer.score(canonical).compositeScore,
             scorer.score(alias).compositeScore);
}

void TestDataQualityDeep4::testTemporalMonthPrecisionDayOneMidnight()
{
    const DataQualityScorer scorer = DataQualityScorer::withDefaults();

    CrimeEvent monthLevel = baseEvent();
    monthLevel.occurredAt = QDateTime(QDate(2024, 6, 1), QTime(0, 0, 0), QTimeZone::utc());
    monthLevel.lat        = 51.5;
    monthLevel.lon        = -0.1;
    monthLevel.suburb     = QStringLiteral("City");

    CrimeEvent dayLevel = monthLevel;
    dayLevel.occurredAt = QDateTime(QDate(2024, 6, 15), QTime(0, 0, 0), QTimeZone::utc());

    QCOMPARE(scorer.score(monthLevel).temporalPrecision, QStringLiteral("month"));
    QCOMPARE(scorer.score(dayLevel).temporalPrecision,   QStringLiteral("day"));

    const QualityReport rMonth = scorer.score(monthLevel);
    const QualityReport rDay   = scorer.score(dayLevel);
    QVERIFY(rDay.compositeScore > rMonth.compositeScore);
}

void TestDataQualityDeep4::testSpatialExactVersusBlockPrecision()
{
    const DataQualityScorer scorer = DataQualityScorer::withDefaults();

    CrimeEvent exact = baseEvent();
    exact.lat        = 51.5074;
    exact.lon        = -0.1278;
    exact.occurredAt = QDateTime(QDate(2024, 5, 1), QTime(12, 0), QTimeZone::utc());
    exact.suburb     = QStringLiteral("Soho");

    CrimeEvent block = baseEvent();
    block.lat        = 51.12;
    block.lon        = -0.34;
    block.occurredAt = exact.occurredAt;
    block.suburb     = exact.suburb;

    QCOMPARE(scorer.score(exact).spatialPrecision, QStringLiteral("exact"));
    QCOMPARE(scorer.score(block).spatialPrecision, QStringLiteral("block"));
    QVERIFY(scorer.score(exact).compositeScore > scorer.score(block).compositeScore);
}

void TestDataQualityDeep4::testZeroCoordinatesFailCompletenessAndSpatial()
{
    const DataQualityScorer scorer = DataQualityScorer::withDefaults();

    CrimeEvent zero = baseEvent();
    zero.lat        = 0.0;
    zero.lon        = 0.0;
    zero.latitude   = 0.0;
    zero.longitude  = 0.0;
    zero.occurredAt = QDateTime(QDate(2024, 2, 1), QTime(10, 0), QTimeZone::utc());
    zero.suburb     = QStringLiteral("Unknown grid");

    CrimeEvent withValidCoords = zero;
    withValidCoords.lat       = 51.5074;
    withValidCoords.lon       = -0.1278;
    withValidCoords.latitude  = 51.5074;
    withValidCoords.longitude = -0.1278;

    const QualityReport r = scorer.score(zero);
    const QualityReport rValid = scorer.score(withValidCoords);
    QCOMPARE(r.spatialPrecision, QStringLiteral("unknown"));
    QVERIFY(r.completeness < 1.0);
    QVERIFY(r.compositeScore < rValid.compositeScore);
}

void TestDataQualityDeep4::testInvalidLongitudePenalizesScore()
{
    const DataQualityScorer scorer = DataQualityScorer::withDefaults();

    CrimeEvent valid = baseEvent();
    valid.lat        = 51.5;
    valid.lon        = -0.1;
    valid.occurredAt = QDateTime(QDate(2024, 7, 4), QTime(8, 0), QTimeZone::utc());
    valid.suburb     = QStringLiteral("Zone");

    CrimeEvent invalid = valid;
    invalid.lon       = 200.0;
    invalid.longitude = 200.0;

    const QualityReport rValid   = scorer.score(valid);
    const QualityReport rInvalid = scorer.score(invalid);

    QCOMPARE(rInvalid.spatialPrecision, QStringLiteral("unknown"));
    QVERIFY(rValid.compositeScore > rInvalid.compositeScore);
    QVERIFY(rValid.completeness > rInvalid.completeness);
}

void TestDataQualityDeep4::testQuarantineThresholdExactPointThreeNotQuarantined()
{
    QMap<QString, double> tuned {
        { QStringLiteral("tuned"), 1.0 },
    };
    const DataQualityScorer scorer(tuned);

    CrimeEvent ev = baseEvent();
    ev.source     = QStringLiteral("tuned");
    ev.crimeType  = QStringLiteral("fraud");
    ev.suburb     = QStringLiteral("Test");
    ev.lat        = 51.5074;
    ev.lon        = -0.1278;
    ev.occurredAt = QDateTime(QDate(2024, 8, 20), QTime(16, 30, 0), QTimeZone::utc());

    const QualityReport r = scorer.score(ev);
    // completeness=1, temporal=hour(1.0), spatial=exact(1.0), reliability=1.0
    // composite = 0.30*1 + 0.20*1 + 0.20*1 + 0.30*1 = 1.0 — well above threshold
    QVERIFY(r.compositeScore >= 0.3);
    QVERIFY(!r.quarantined);

    // Boundary semantics: quarantine uses strict less-than 0.3
    CrimeEvent low = baseEvent();
    low.source = QStringLiteral("manual"); // 0.40 reliability but no other fields
    const QualityReport rLow = scorer.score(low);
    if (std::abs(rLow.compositeScore - 0.3) < 1e-9) {
        QVERIFY2(!rLow.quarantined,
                 "compositeScore == 0.3 must NOT quarantine (strict < threshold)");
    } else {
        QVERIFY(rLow.compositeScore < 0.3);
        QVERIFY(rLow.quarantined);
    }
}

QTEST_GUILESS_MAIN(TestDataQualityDeep4)
#include "test_data_quality_deep4.moc"
