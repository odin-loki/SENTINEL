// test_data_quality_boundary.cpp
// DataQualityScorer boundary tests: minimal events, near-threshold scoring,
// passRate computation, and source reliability integration.
#include <QTest>
#include <QTimeZone>
#include "ingest/DataQualityScorer.h"
#include "core/CrimeEvent.h"

class DataQualityBoundaryTest : public QObject
{
    Q_OBJECT

private:
    static CrimeEvent minimalEvent(const QString& id)
    {
        CrimeEvent ev;
        ev.id        = id;
        ev.eventId   = id;
        ev.crimeType = QStringLiteral("burglary");
        return ev;
    }

    static CrimeEvent fullEvent(const QString& id)
    {
        CrimeEvent ev;
        ev.id           = id;
        ev.eventId      = id;
        ev.crimeType    = QStringLiteral("burglary");
        ev.source       = QStringLiteral("uk_police_v1");
        ev.timestamp    = QDateTime(QDate::currentDate().addDays(-1), QTime(10, 0), QTimeZone::utc());
        ev.occurredAt   = ev.timestamp;
        ev.lat          = 51.5;
        ev.lon          = -0.1;
        ev.latitude     = 51.5;
        ev.longitude    = -0.1;
        ev.suburb       = QStringLiteral("Brixton");
        ev.narrative    = QStringLiteral("Full description of the incident.");
        ev.outcome      = QStringLiteral("resolved");
        return ev;
    }

private slots:

    // 1. Minimal event: score in [0, 1]
    void testMinimalEventScoreRange()
    {
        DataQualityScorer scorer;
        const auto r = scorer.score(minimalEvent(QStringLiteral("E1")));
        QVERIFY2(r.compositeScore >= 0.0 && r.compositeScore <= 1.0,
                 qPrintable(QStringLiteral("Minimal event score %1 must be in [0,1]")
                    .arg(r.compositeScore)));
    }

    // 2. Full event: score > minimal event score
    void testFullEventHigherScore()
    {
        DataQualityScorer scorer;
        const auto minR  = scorer.score(minimalEvent(QStringLiteral("E1")));
        const auto fullR = scorer.score(fullEvent(QStringLiteral("E2")));
        QVERIFY2(fullR.compositeScore >= minR.compositeScore,
                 qPrintable(QStringLiteral("Full event %1 should score >= minimal event %2")
                    .arg(fullR.compositeScore).arg(minR.compositeScore)));
    }

    // 3. Minimal event: quarantined if score < 0.3
    void testMinimalEventMayBeQuarantined()
    {
        DataQualityScorer scorer;
        const auto r = scorer.score(minimalEvent(QStringLiteral("E3")));
        if (r.compositeScore < 0.3) {
            QVERIFY2(r.quarantined, "Score < 0.3 should be quarantined");
        } else {
            QVERIFY2(!r.quarantined, "Score >= 0.3 should not be quarantined");
        }
    }

    // 4. Full event from reliable source: not quarantined
    void testFullEventFromReliableSourceNotQuarantined()
    {
        DataQualityScorer scorer({ { QStringLiteral("uk_police_v1"), 0.95 } });
        const auto r = scorer.score(fullEvent(QStringLiteral("E4")));
        QVERIFY2(!r.quarantined,
                 qPrintable(QStringLiteral("Full event from reliable source (score=%1) should not be quarantined")
                    .arg(r.compositeScore)));
    }

    // 5. scoreBatch: returns same count as input
    void testScoreBatchCount()
    {
        DataQualityScorer scorer;
        QVector<CrimeEvent> evs = {
            fullEvent(QStringLiteral("A")),
            minimalEvent(QStringLiteral("B")),
            fullEvent(QStringLiteral("C"))
        };
        const auto reports = scorer.scoreBatch(evs);
        QCOMPARE(reports.size(), 3);
    }

    // 6. passRate: all good events -> near 1.0
    void testPassRateAllGood()
    {
        DataQualityScorer scorer({ { QStringLiteral("uk_police_v1"), 0.95 } });
        QVector<CrimeEvent> evs;
        for (int i = 0; i < 10; ++i) evs.append(fullEvent(QStringLiteral("E%1").arg(i)));
        const auto reports = scorer.scoreBatch(evs);
        const double rate = DataQualityScorer::passRate(reports);
        QVERIFY2(rate > 0.8,
                 qPrintable(QStringLiteral("Pass rate %1 should be > 0.8 for good events").arg(rate)));
    }

    // 7. eventId in report matches input
    void testEventIdInReport()
    {
        DataQualityScorer scorer;
        const auto r = scorer.score(fullEvent(QStringLiteral("MYID123")));
        QVERIFY2(r.eventId == QStringLiteral("MYID123"),
                 qPrintable(QStringLiteral("Report eventId '%1' should be 'MYID123'").arg(r.eventId)));
    }

    // 8. completeness is in [0, 1]
    void testCompletenessRange()
    {
        DataQualityScorer scorer;
        for (int i = 0; i < 3; ++i) {
            const auto r = scorer.score(i % 2 == 0 ? fullEvent(QStringLiteral("E%1").arg(i))
                                                    : minimalEvent(QStringLiteral("E%1").arg(i)));
            QVERIFY2(r.completeness >= 0.0 && r.completeness <= 1.0,
                     qPrintable(QStringLiteral("completeness %1 must be in [0,1]").arg(r.completeness)));
        }
    }

    // 9. passRate on empty vector is 0.0 (no data to pass)
    void testPassRateEmptyVectorIsZero()
    {
        const double rate = DataQualityScorer::passRate({});
        QVERIFY2(rate == 0.0, "passRate on empty vector should be 0.0 (implementation-defined)");
    }

    // 10. Low source reliability reduces score
    void testLowSourceReliabilityReducesScore()
    {
        DataQualityScorer highReliability({ { QStringLiteral("uk_police_v1"), 0.95 } });
        DataQualityScorer lowReliability({ { QStringLiteral("uk_police_v1"), 0.1 } });

        const auto highR = highReliability.score(fullEvent(QStringLiteral("E1")));
        const auto lowR  = lowReliability.score(fullEvent(QStringLiteral("E1")));

        QVERIFY2(highR.compositeScore >= lowR.compositeScore,
                 qPrintable(QStringLiteral("High reliability score %1 should >= low reliability score %2")
                    .arg(highR.compositeScore).arg(lowR.compositeScore)));
    }
};

QTEST_MAIN(DataQualityBoundaryTest)
#include "test_data_quality_boundary.moc"
