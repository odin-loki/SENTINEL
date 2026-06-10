// test_data_quality_deep.cpp
// Deep tests for DataQualityScorer: completeness, spatial/temporal precision,
// quarantine logic, passRate, and source reliability.
#include <QTest>
#include "ingest/DataQualityScorer.h"
#include "core/CrimeEvent.h"

class DataQualityDeepTest : public QObject
{
    Q_OBJECT

private:
    static CrimeEvent fullEvent()
    {
        CrimeEvent ev;
        ev.eventId   = QStringLiteral("E1");
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
        ev.narrative  = QStringLiteral("Residential burglary reported by witness.");
        return ev;
    }

    static CrimeEvent minimalEvent()
    {
        CrimeEvent ev;
        ev.eventId = QStringLiteral("E_MIN");
        return ev;
    }

private slots:

    // ── 1. Full event has high composite score ────────────────────────────────
    void testFullEventHighScore()
    {
        DataQualityScorer scorer;
        const auto r = scorer.score(fullEvent());
        QVERIFY2(r.compositeScore > 0.7,
                 qPrintable(QStringLiteral("Full event composite score %1 should be > 0.7")
                    .arg(r.compositeScore)));
    }

    // ── 2. Minimal event has low composite score ──────────────────────────────
    void testMinimalEventLowScore()
    {
        DataQualityScorer scorer;
        const auto r = scorer.score(minimalEvent());
        QVERIFY2(r.compositeScore < 0.7,
                 qPrintable(QStringLiteral("Minimal event composite score %1 should be < 0.7")
                    .arg(r.compositeScore)));
    }

    // ── 3. compositeScore is in [0, 1] ────────────────────────────────────────
    void testCompositeScoreRange()
    {
        DataQualityScorer scorer;
        const auto r1 = scorer.score(fullEvent());
        const auto r2 = scorer.score(minimalEvent());

        QVERIFY2(r1.compositeScore >= 0.0 && r1.compositeScore <= 1.0,
                 qPrintable(QStringLiteral("Full event composite %1 must be in [0,1]").arg(r1.compositeScore)));
        QVERIFY2(r2.compositeScore >= 0.0 && r2.compositeScore <= 1.0,
                 qPrintable(QStringLiteral("Minimal event composite %1 must be in [0,1]").arg(r2.compositeScore)));
    }

    // ── 4. Event missing lat/lon has lower score than event with coordinates ──
    void testMissingCoordinatesLowerScore()
    {
        DataQualityScorer scorer;

        CrimeEvent withCoords = fullEvent();
        CrimeEvent noCoords   = fullEvent();
        noCoords.latitude  = 0.0;
        noCoords.longitude = 0.0;

        const auto r1 = scorer.score(withCoords);
        const auto r2 = scorer.score(noCoords);
        QVERIFY2(r1.compositeScore >= r2.compositeScore,
                 qPrintable(QStringLiteral("Event with coords (%1) should score >= no coords (%2)")
                    .arg(r1.compositeScore).arg(r2.compositeScore)));
    }

    // ── 5. Quarantine flag: minimal event is quarantined ─────────────────────
    void testMinimalEventQuarantined()
    {
        DataQualityScorer scorer;
        const auto r = scorer.score(minimalEvent());
        QVERIFY2(r.quarantined, "Minimal event with low score should be quarantined");
    }

    // ── 6. Full event is NOT quarantined ──────────────────────────────────────
    void testFullEventNotQuarantined()
    {
        DataQualityScorer scorer;
        const auto r = scorer.score(fullEvent());
        QVERIFY2(!r.quarantined, "Full event should not be quarantined");
    }

    // ── 7. passRate with all-passing events → 1.0 ────────────────────────────
    void testPassRateAllPass()
    {
        DataQualityScorer scorer;
        QVector<QualityReport> reports;
        for (int i = 0; i < 10; ++i) {
            auto r = scorer.score(fullEvent());
            reports.append(r);
        }
        const double rate = DataQualityScorer::passRate(reports);
        QVERIFY2(std::abs(rate - 1.0) < 0.01,
                 qPrintable(QStringLiteral("All-passing passRate %1 should be ~1.0").arg(rate)));
    }

    // ── 8. passRate with half quarantined → ~0.5 ─────────────────────────────
    void testPassRateHalf()
    {
        DataQualityScorer scorer;
        QVector<QualityReport> reports;
        for (int i = 0; i < 5; ++i) reports.append(scorer.score(fullEvent()));
        for (int i = 0; i < 5; ++i) reports.append(scorer.score(minimalEvent()));

        const double rate = DataQualityScorer::passRate(reports);
        QVERIFY2(rate >= 0.3 && rate <= 0.7,
                 qPrintable(QStringLiteral("Half quarantined passRate %1 should be ~0.5").arg(rate)));
    }

    // ── 9. scoreBatch returns same count as input ─────────────────────────────
    void testScoreBatchCount()
    {
        DataQualityScorer scorer;
        QVector<CrimeEvent> evs;
        for (int i = 0; i < 20; ++i) evs.append(fullEvent());

        const auto reports = scorer.scoreBatch(evs);
        QCOMPARE(reports.size(), evs.size());
    }

    // ── 10. Source reliability influences score ───────────────────────────────
    void testSourceReliabilityInfluence()
    {
        QMap<QString, double> highRel;
        highRel[QStringLiteral("uk_police")] = 0.99;

        QMap<QString, double> lowRel;
        lowRel[QStringLiteral("uk_police")] = 0.1;

        DataQualityScorer highScorer(highRel);
        DataQualityScorer lowScorer(lowRel);

        const auto rHigh = highScorer.score(fullEvent());
        const auto rLow  = lowScorer.score(fullEvent());

        QVERIFY2(rHigh.compositeScore >= rLow.compositeScore,
                 qPrintable(QStringLiteral("High reliability (%1) should score >= low reliability (%2)")
                    .arg(rHigh.compositeScore).arg(rLow.compositeScore)));
    }
};

QTEST_MAIN(DataQualityDeepTest)
#include "test_data_quality_deep.moc"
