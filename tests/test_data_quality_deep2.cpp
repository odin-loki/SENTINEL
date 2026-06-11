#include <QtTest>
#include "ingest/DataQualityScorer.h"
#include "core/CrimeEvent.h"

static CrimeEvent perfectEvent()
{
    CrimeEvent ev;
    ev.eventId   = QStringLiteral("PERF-1");
    ev.crimeType = QStringLiteral("burglary");
    ev.suburb    = QStringLiteral("Soho");
    ev.lat       = 51.5074;
    ev.lon       = -0.1278;
    ev.source    = QStringLiteral("uk_police");
    ev.occurredAt = QDateTime(QDate(2024, 3, 15), QTime(22, 10, 0), QTimeZone::utc());
    ev.qualityScore = 0.9;
    return ev;
}

// Only crimeType + suburb populated; no coordinates, no timestamp
static CrimeEvent halfPopulatedEvent()
{
    CrimeEvent ev;
    ev.eventId   = QStringLiteral("HALF-1");
    ev.crimeType = QStringLiteral("theft");
    ev.suburb    = QStringLiteral("Camden");
    ev.source    = QStringLiteral("csv_import");
    return ev;
}

class TestDataQualityDeep2 : public QObject
{
    Q_OBJECT

private slots:

    // ── 1. completeness = 1.0 when all 4 fields populated ────────────────────
    void testCompletenessPerfectEvent()
    {
        DataQualityScorer scorer;
        const auto r = scorer.score(perfectEvent());
        QVERIFY2(std::abs(r.completeness - 1.0) < 0.001,
                 qPrintable(QStringLiteral("Perfect event completeness should be 1.0, got %1")
                     .arg(r.completeness)));
    }

    // ── 2. completeness ≈ 0.5 when 2 of 4 fields populated ──────────────────
    void testCompletenessHalfPopulated()
    {
        DataQualityScorer scorer;
        const auto r = scorer.score(halfPopulatedEvent());
        QVERIFY2(std::abs(r.completeness - 0.5) < 0.001,
                 qPrintable(QStringLiteral("Half-populated event completeness should be 0.5, got %1")
                     .arg(r.completeness)));
    }

    // ── 3. completeness = 0.0 for fully empty event ───────────────────────────
    void testCompletenessEmptyEvent()
    {
        DataQualityScorer scorer;
        CrimeEvent ev;
        ev.eventId = QStringLiteral("EMPTY-1");
        const auto r = scorer.score(ev);
        QVERIFY2(r.completeness < 0.001,
                 qPrintable(QStringLiteral("Empty event completeness should be 0.0, got %1")
                     .arg(r.completeness)));
    }

    // ── 4. Invalid coordinates (lat=200) lower composite vs valid lat ─────────
    void testInvalidCoordinatesLowerComposite()
    {
        DataQualityScorer scorer;

        CrimeEvent withValid = perfectEvent();

        CrimeEvent withInvalid = perfectEvent();
        withInvalid.lat = 200.0;
        withInvalid.lon = 200.0;

        const auto rValid   = scorer.score(withValid);
        const auto rInvalid = scorer.score(withInvalid);

        QVERIFY2(rValid.compositeScore > rInvalid.compositeScore,
                 qPrintable(QStringLiteral(
                     "Valid coords composite (%1) should exceed invalid coords composite (%2)")
                     .arg(rValid.compositeScore).arg(rInvalid.compositeScore)));
    }

    // ── 5. Missing timestamp lowers composite vs event with timestamp ─────────
    void testMissingTimestampLowersComposite()
    {
        DataQualityScorer scorer;

        CrimeEvent withTimestamp = perfectEvent();

        CrimeEvent noTimestamp = perfectEvent();
        noTimestamp.occurredAt = std::nullopt;

        const auto rWith    = scorer.score(withTimestamp);
        const auto rWithout = scorer.score(noTimestamp);

        QVERIFY2(rWith.compositeScore > rWithout.compositeScore,
                 qPrintable(QStringLiteral(
                     "Event with timestamp (%1) should score > no timestamp (%2)")
                     .arg(rWith.compositeScore).arg(rWithout.compositeScore)));
    }

    // ── 6. defaultReliabilityMap returns non-empty map with expected sources ──
    void testDefaultReliabilityMapNonEmpty()
    {
        const auto map = DataQualityScorer::defaultReliabilityMap();
        QVERIFY2(!map.isEmpty(), "defaultReliabilityMap must be non-empty");
        QVERIFY2(map.contains(QStringLiteral("uk_police")),
                 "defaultReliabilityMap must contain 'uk_police'");
        QVERIFY2(map.contains(QStringLiteral("csv_import")),
                 "defaultReliabilityMap must contain 'csv_import'");

        for (auto it = map.constBegin(); it != map.constEnd(); ++it) {
            QVERIFY2(it.value() >= 0.0 && it.value() <= 1.0,
                     qPrintable(QStringLiteral("Reliability for '%1' = %2 must be in [0,1]")
                         .arg(it.key()).arg(it.value())));
        }
    }

    // ── 7. uk_police reliability in default map is >= 0.85 ───────────────────
    void testDefaultReliabilityUkPoliceSane()
    {
        const auto map = DataQualityScorer::defaultReliabilityMap();
        const double rel = map.value(QStringLiteral("uk_police"), -1.0);
        QVERIFY2(rel >= 0.85,
                 qPrintable(QStringLiteral("uk_police reliability %1 should be >= 0.85").arg(rel)));
    }

    // ── 8. withDefaults() creates a scorer that scores a uk_police event well ─
    void testWithDefaultsCreatesValidScorer()
    {
        const DataQualityScorer scorer = DataQualityScorer::withDefaults();
        const auto r = scorer.score(perfectEvent());

        QVERIFY2(r.compositeScore > 0.7,
                 qPrintable(QStringLiteral(
                     "withDefaults() scorer on perfect uk_police event should score > 0.7, got %1")
                     .arg(r.compositeScore)));
        QVERIFY2(!r.quarantined,
                 "withDefaults() scorer: perfect event must not be quarantined");
    }

    // ── 9. withDefaults() uses uk_police reliability correctly ───────────────
    void testWithDefaultsSourceReliabilityApplied()
    {
        const DataQualityScorer scorer = DataQualityScorer::withDefaults();
        const auto r = scorer.score(perfectEvent());
        QVERIFY2(r.sourceReliability >= 0.85,
                 qPrintable(QStringLiteral(
                     "uk_police sourceReliability should be >= 0.85, got %1")
                     .arg(r.sourceReliability)));
    }

    // ── 10. Perfect event composite score is high (> 0.7) ────────────────────
    void testPerfectEventHighComposite()
    {
        const DataQualityScorer scorer = DataQualityScorer::withDefaults();
        const auto r = scorer.score(perfectEvent());
        QVERIFY2(r.compositeScore > 0.7,
                 qPrintable(QStringLiteral("Perfect event composite %1 must be > 0.7")
                     .arg(r.compositeScore)));
    }

    // ── 11. temporalPrecision label is "hour" for event with non-midnight time
    void testTemporalPrecisionHour()
    {
        DataQualityScorer scorer;
        const auto r = scorer.score(perfectEvent());
        QCOMPARE(r.temporalPrecision, QStringLiteral("hour"));
    }

    // ── 12. temporalPrecision label is "unknown" when no timestamp ────────────
    void testTemporalPrecisionUnknown()
    {
        DataQualityScorer scorer;
        CrimeEvent ev = halfPopulatedEvent();
        const auto r = scorer.score(ev);
        QCOMPARE(r.temporalPrecision, QStringLiteral("unknown"));
    }

    // ── 13. spatialPrecision label is "exact" for 4+ decimal-place coords ─────
    void testSpatialPrecisionExact()
    {
        DataQualityScorer scorer;
        const auto r = scorer.score(perfectEvent());
        QCOMPARE(r.spatialPrecision, QStringLiteral("exact"));
    }

    // ── 14. compositeScore is in [0,1] for any event ─────────────────────────
    void testCompositeScoreAlwaysInRange()
    {
        DataQualityScorer scorer;
        const QVector<CrimeEvent> evs = {
            perfectEvent(),
            halfPopulatedEvent(),
            CrimeEvent{},
        };
        for (const auto& ev : evs) {
            const auto r = scorer.score(ev);
            QVERIFY2(r.compositeScore >= 0.0 && r.compositeScore <= 1.0,
                     qPrintable(QStringLiteral("compositeScore %1 must be in [0,1]")
                         .arg(r.compositeScore)));
        }
    }

    // ── 15. scoreBatch matches individual score() calls ───────────────────────
    void testScoreBatchMatchesIndividual()
    {
        DataQualityScorer scorer;
        QVector<CrimeEvent> evs = { perfectEvent(), halfPopulatedEvent() };
        const auto batch = scorer.scoreBatch(evs);
        QCOMPARE(batch.size(), 2);
        for (int i = 0; i < 2; ++i) {
            const auto single = scorer.score(evs[i]);
            QVERIFY2(std::abs(batch[i].compositeScore - single.compositeScore) < 0.0001,
                     qPrintable(QStringLiteral("scoreBatch[%1] composite %2 != single %3")
                         .arg(i).arg(batch[i].compositeScore).arg(single.compositeScore)));
        }
    }
};

QTEST_GUILESS_MAIN(TestDataQualityDeep2)
#include "test_data_quality_deep2.moc"
