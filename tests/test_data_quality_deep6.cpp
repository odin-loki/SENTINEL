// test_data_quality_deep6.cpp — Deep audit iteration 27: DataQualityScorer
// passRate, batch scoring order, custom reliability map, quarantine flag.
#include <QTest>
#include "ingest/DataQualityScorer.h"
#include "core/CrimeEvent.h"

class TestDataQualityDeep6 : public QObject
{
    Q_OBJECT

    static CrimeEvent event(const QString& id, const QString& source,
                            bool fullFields = true)
    {
        CrimeEvent ev;
        ev.eventId   = id;
        ev.crimeType = QStringLiteral("theft");
        ev.source    = source;
        if (fullFields) {
            ev.occurredAt = QDateTime(QDate(2024, 8, 1), QTime(10, 0), Qt::UTC);
            ev.lat        = 51.5;
            ev.lon        = -0.1;
            ev.suburb     = QStringLiteral("Test");
            ev.narrative  = QStringLiteral("Incident detail");
        }
        return ev;
    }

private slots:

    void testPassRateAllGood()
    {
        const DataQualityScorer scorer = DataQualityScorer::withDefaults();
        const auto reports = scorer.scoreBatch({
            event(QStringLiteral("G1"), QStringLiteral("uk_police_v1")),
            event(QStringLiteral("G2"), QStringLiteral("uk_police_v1")),
        });
        const double rate = DataQualityScorer::passRate(reports);
        QVERIFY(rate >= 0.5);
    }

    void testBatchPreservesOrder()
    {
        const DataQualityScorer scorer = DataQualityScorer::withDefaults();
        const auto reports = scorer.scoreBatch({
            event(QStringLiteral("ORD-1"), QStringLiteral("uk_police_v1")),
            event(QStringLiteral("ORD-2"), QStringLiteral("uk_police_v1")),
        });
        QCOMPARE(reports.size(), 2);
        QCOMPARE(reports[0].eventId, QStringLiteral("ORD-1"));
        QCOMPARE(reports[1].eventId, QStringLiteral("ORD-2"));
    }

    void testCustomReliabilityMap()
    {
        QMap<QString, double> rel;
        rel[QStringLiteral("low_trust")] = 0.1;
        DataQualityScorer scorer(rel);

        const auto report = scorer.score(event(QStringLiteral("L1"), QStringLiteral("low_trust")));
        QVERIFY(report.sourceReliability <= 0.2);
    }

    void testSparseEventMayQuarantine()
    {
        const DataQualityScorer scorer = DataQualityScorer::withDefaults();
        const auto report = scorer.score(
            event(QStringLiteral("SPARSE"), QStringLiteral("unknown_source_xyz"), false));
        QVERIFY(report.compositeScore < 0.8);
    }

    void testCompositeScoreInRange()
    {
        const DataQualityScorer scorer = DataQualityScorer::withDefaults();
        const auto report = scorer.score(
            event(QStringLiteral("RNG"), QStringLiteral("uk_police_v1")));
        QVERIFY(report.compositeScore >= 0.0 && report.compositeScore <= 1.0);
    }
};

QTEST_GUILESS_MAIN(TestDataQualityDeep6)
#include "test_data_quality_deep6.moc"
