// Deep audit iteration 16 — DataQualityScorer passRate, batch scoring, thresholds
#include <QTest>
#include <QTimeZone>
#include "ingest/DataQualityScorer.h"
#include "core/CrimeEvent.h"

class TestDataQualityDeep3 : public QObject
{
    Q_OBJECT

private:
    static CrimeEvent minimalEvent(const QString& source = QStringLiteral("test"))
    {
        CrimeEvent ev;
        ev.eventId   = QStringLiteral("MIN");
        ev.crimeType = QStringLiteral("theft");
        ev.source    = source;
        return ev;
    }

    static CrimeEvent richEvent()
    {
        CrimeEvent ev;
        ev.eventId    = QStringLiteral("RICH");
        ev.crimeType  = QStringLiteral("burglary");
        ev.suburb     = QStringLiteral("Westminster");
        ev.lat        = 51.50740;
        ev.lon        = -0.12780;
        ev.occurredAt = QDateTime(QDate(2024, 6, 15), QTime(14, 30, 0), QTimeZone::utc());
        ev.source     = QStringLiteral("uk_police_v1");
        return ev;
    }

private slots:

    // ── 1. passRate on empty vector returns 0.0 ─────────────────────────────
    void testPassRateEmptyReturnsZero()
    {
        const double rate = DataQualityScorer::passRate({});
        QCOMPARE(rate, 0.0);
    }

    // ── 2. passRate all non-quarantined returns 1.0 ─────────────────────────
    void testPassRateAllPassReturnsOne()
    {
        const DataQualityScorer scorer = DataQualityScorer::withDefaults();
        QVector<QualityReport> reports;
        for (int i = 0; i < 5; ++i)
            reports.append(scorer.score(richEvent()));

        const double rate = DataQualityScorer::passRate(reports);
        QCOMPARE(rate, 1.0);
        for (const QualityReport& r : reports)
            QVERIFY(!r.quarantined);
    }

    // ── 3. passRate all quarantined returns 0.0 ─────────────────────────────
    void testPassRateAllQuarantinedReturnsZero()
    {
        const DataQualityScorer scorer = DataQualityScorer::withDefaults();
        QVector<QualityReport> reports;
        CrimeEvent empty;
        empty.source = QStringLiteral("unknown");
        for (int i = 0; i < 4; ++i)
            reports.append(scorer.score(empty));

        const double rate = DataQualityScorer::passRate(reports);
        QCOMPARE(rate, 0.0);
        for (const QualityReport& r : reports)
            QVERIFY(r.quarantined);
    }

    // ── 4. passRate mixed batch returns exact fraction ──────────────────────
    void testPassRateMixedFraction()
    {
        const DataQualityScorer scorer = DataQualityScorer::withDefaults();
        QVector<QualityReport> reports;
        reports.append(scorer.score(richEvent()));
        reports.append(scorer.score(richEvent()));
        reports.append(scorer.score(richEvent()));

        CrimeEvent empty;
        empty.source = QStringLiteral("unknown");
        reports.append(scorer.score(empty));
        reports.append(scorer.score(empty));

        const double rate = DataQualityScorer::passRate(reports);
        QCOMPARE(rate, 0.6);
    }

    // ── 5. scoreBatch preserves order and eventId mapping ───────────────────
    void testBatchScorePreservesOrderAndIds()
    {
        const DataQualityScorer scorer = DataQualityScorer::withDefaults();
        QVector<CrimeEvent> events;
        for (int i = 0; i < 6; ++i) {
            CrimeEvent ev = minimalEvent();
            ev.eventId = QStringLiteral("EVT_%1").arg(i);
            events.append(ev);
        }

        const auto reports = scorer.scoreBatch(events);
        QCOMPARE(reports.size(), events.size());
        for (int i = 0; i < events.size(); ++i)
            QCOMPARE(reports[i].eventId, events[i].eventId);
    }

    // ── 6. scoreBatch results match individual score() calls ────────────────
    void testBatchScoreMatchesIndividual()
    {
        const DataQualityScorer scorer = DataQualityScorer::withDefaults();
        QVector<CrimeEvent> events { richEvent(), minimalEvent(), richEvent() };

        const auto batch = scorer.scoreBatch(events);
        for (int i = 0; i < events.size(); ++i) {
            const QualityReport single = scorer.score(events[i]);
            QCOMPARE(batch[i].compositeScore, single.compositeScore);
            QCOMPARE(batch[i].quarantined, single.quarantined);
            QCOMPARE(batch[i].completeness, single.completeness);
        }
    }

    // ── 7. Quarantine threshold: compositeScore < 0.3 quarantines ─────────
    void testQuarantineThresholdBoundary()
    {
        const DataQualityScorer scorer = DataQualityScorer::withDefaults();

        CrimeEvent low;
        low.source = QStringLiteral("manual"); // reliability 0.40 → composite ~0.12
        const QualityReport rLow = scorer.score(low);
        QVERIFY(rLow.compositeScore < 0.3);
        QVERIFY(rLow.quarantined);

        CrimeEvent high = richEvent();
        const QualityReport rHigh = scorer.score(high);
        QVERIFY(rHigh.compositeScore >= 0.3);
        QVERIFY(!rHigh.quarantined);
    }

    // ── 8. Custom reliability map shifts pass rate ──────────────────────────
    void testCustomReliabilityMapAffectsPassRate()
    {
        QMap<QString, double> lowReliability {
            { QStringLiteral("test"), 0.10 },
        };
        const DataQualityScorer strictScorer(lowReliability);
        const DataQualityScorer lenientScorer = DataQualityScorer::withDefaults();

        const CrimeEvent ev = minimalEvent(QStringLiteral("test"));

        const QualityReport strictReport = strictScorer.score(ev);
        const QualityReport lenientReport = lenientScorer.score(ev);

        QVERIFY(strictReport.compositeScore < lenientReport.compositeScore);

        QVector<QualityReport> strictBatch { strictReport };
        QVector<QualityReport> lenientBatch { lenientReport };
        // Lenient default (0.50 reliability) may pass where strict (0.10) quarantines
        if (strictReport.quarantined && !lenientReport.quarantined) {
            QCOMPARE(DataQualityScorer::passRate(strictBatch), 0.0);
            QCOMPARE(DataQualityScorer::passRate(lenientBatch), 1.0);
        } else {
            QVERIFY(strictReport.sourceReliability < lenientReport.sourceReliability);
        }
    }
};

QTEST_GUILESS_MAIN(TestDataQualityDeep3)
#include "test_data_quality_deep3.moc"
