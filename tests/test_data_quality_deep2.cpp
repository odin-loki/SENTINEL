// test_data_quality_deep2.cpp — Second-pass deep tests for DataQualityScorer
#include <QTest>
#include <QTimeZone>
#include "ingest/DataQualityScorer.h"
#include "core/CrimeEvent.h"
#include <cmath>

class TestDataQualityDeep2 : public QObject
{
    Q_OBJECT

private:
    static CrimeEvent emptyEvent()
    {
        CrimeEvent ev;
        ev.eventId = QStringLiteral("EMPTY");
        return ev;
    }

    static CrimeEvent fullEvent()
    {
        CrimeEvent ev;
        ev.eventId   = QStringLiteral("FULL");
        ev.crimeType = QStringLiteral("burglary");
        ev.suburb    = QStringLiteral("Soho");
        ev.lat       = 51.5074;
        ev.lon       = -0.1278;
        ev.latitude  = 51.5074;
        ev.longitude = -0.1278;
        const QDateTime dt = QDateTime(QDate(2024, 3, 15), QTime(22, 10, 0), QTimeZone::utc());
        ev.occurredAt = dt;
        ev.timestamp  = dt;
        ev.source     = QStringLiteral("uk_police");
        return ev;
    }

private slots:

    // ── 1. Empty CrimeEvent scores < 0.5 ─────────────────────────────────────
    void testEmptyEventLowScore()
    {
        DataQualityScorer scorer;
        const auto r = scorer.score(emptyEvent());
        // Completeness=0, temporal=0, spatial=0, reliability=0.5 (default)
        // Score = 0.30*0 + 0.20*0 + 0.20*0 + 0.30*0.5 = 0.15
        QVERIFY2(r.compositeScore < 0.5,
                 qPrintable(QStringLiteral("Empty event score %1 should be < 0.5").arg(r.compositeScore)));
    }

    // ── 2. Fully populated event scores > 0.8 ─────────────────────────────────
    void testFullEventHighScore()
    {
        DataQualityScorer scorer = DataQualityScorer::withDefaults();
        const auto r = scorer.score(fullEvent());
        QVERIFY2(r.compositeScore > 0.8,
                 qPrintable(QStringLiteral("Full event score %1 should be > 0.8").arg(r.compositeScore)));
    }

    // ── 3. Invalid lat (> 90) is penalized ────────────────────────────────────
    void testInvalidLatPenalized()
    {
        DataQualityScorer scorer = DataQualityScorer::withDefaults();

        CrimeEvent valid = fullEvent();
        CrimeEvent invalid = fullEvent();
        invalid.lat = 200.0;   // out of range
        invalid.latitude = 200.0;

        const auto rValid   = scorer.score(valid);
        const auto rInvalid = scorer.score(invalid);

        QVERIFY2(rValid.compositeScore > rInvalid.compositeScore,
                 qPrintable(QStringLiteral("Valid lat (%1) should score higher than invalid lat (%2)")
                    .arg(rValid.compositeScore).arg(rInvalid.compositeScore)));
    }

    // ── 4. Valid timestamp increases score ────────────────────────────────────
    void testValidTimestampIncreasesScore()
    {
        DataQualityScorer scorer;

        CrimeEvent withTs = emptyEvent();
        withTs.occurredAt = QDateTime(QDate(2024, 6, 1), QTime(14, 30, 0), QTimeZone::utc());

        CrimeEvent noTs = emptyEvent();

        const auto rWith = scorer.score(withTs);
        const auto rNo   = scorer.score(noTs);

        QVERIFY2(rWith.compositeScore > rNo.compositeScore,
                 qPrintable(QStringLiteral("Event with timestamp (%1) should score higher than event without (%2)")
                    .arg(rWith.compositeScore).arg(rNo.compositeScore)));
    }

    // ── 5. withDefaults() returns scorer with non-empty reliability map ────────
    void testWithDefaultsNonEmptyMap()
    {
        const auto reliabilityMap = DataQualityScorer::defaultReliabilityMap();
        QVERIFY2(!reliabilityMap.isEmpty(),
                 "defaultReliabilityMap() should not be empty");
        QVERIFY2(reliabilityMap.contains(QStringLiteral("uk_police")),
                 "defaultReliabilityMap() should contain 'uk_police'");

        DataQualityScorer scorer = DataQualityScorer::withDefaults();
        // Verify the scorer uses the map: uk_police source should give reliability > default 0.5
        CrimeEvent ev = emptyEvent();
        ev.source = QStringLiteral("uk_police");
        const auto r = scorer.score(ev);
        QVERIFY2(r.sourceReliability >= 0.8,
                 qPrintable(QStringLiteral("uk_police reliability %1 should be >= 0.8").arg(r.sourceReliability)));
    }

    // ── 6. compositeScore is in [0, 1] ────────────────────────────────────────
    void testCompositeScoreInRange()
    {
        DataQualityScorer scorer = DataQualityScorer::withDefaults();
        const auto r1 = scorer.score(emptyEvent());
        const auto r2 = scorer.score(fullEvent());
        QVERIFY(r1.compositeScore >= 0.0 && r1.compositeScore <= 1.0);
        QVERIFY(r2.compositeScore >= 0.0 && r2.compositeScore <= 1.0);
    }
};

QTEST_GUILESS_MAIN(TestDataQualityDeep2)
#include "test_data_quality_deep2.moc"
