// test_data_quality_deep7.cpp — Deep audit iteration 29: DataQualityScorer
// quarantine sparse event, default reliability map, precision labels, composite bounds.
#include <QTest>
#include <QTimeZone>
#include "ingest/DataQualityScorer.h"
#include "core/CrimeEvent.h"

class TestDataQualityDeep7 : public QObject
{
    Q_OBJECT

    static CrimeEvent sparse(const QString& id)
    {
        CrimeEvent ev;
        ev.eventId = id;
        return ev;
    }

    static CrimeEvent rich(const QString& id)
    {
        CrimeEvent ev;
        ev.eventId    = id;
        ev.crimeType  = QStringLiteral("theft");
        ev.source     = QStringLiteral("uk_police_v1");
        ev.occurredAt = QDateTime(QDate(2024, 9, 1), QTime(14, 30), QTimeZone::utc());
        ev.lat        = 51.5;
        ev.lon        = -0.1;
        ev.suburb     = QStringLiteral("Central");
        ev.narrative  = QStringLiteral("Detailed narrative");
        return ev;
    }

private slots:

    void testSparseEventQuarantined()
    {
        const auto report = DataQualityScorer::withDefaults().score(sparse(QStringLiteral("SP7")));
        QVERIFY(report.quarantined);
        QVERIFY(report.compositeScore < 0.3);
    }

    void testDefaultReliabilityMapHasUkPolice()
    {
        const auto map = DataQualityScorer::defaultReliabilityMap();
        QVERIFY(map.contains(QStringLiteral("uk_police_v1")));
        QVERIFY(map.value(QStringLiteral("uk_police_v1")) > 0.0);
    }

    void testRichEventPrecisionLabels()
    {
        const auto report = DataQualityScorer::withDefaults().score(rich(QStringLiteral("R7")));
        QVERIFY(!report.temporalPrecision.isEmpty());
        QVERIFY(!report.spatialPrecision.isEmpty());
        QVERIFY(!report.quarantined);
    }

    void testCompositeScoreBounded()
    {
        const auto report = DataQualityScorer::withDefaults().score(rich(QStringLiteral("B7")));
        QVERIFY(report.compositeScore >= 0.0);
        QVERIFY(report.compositeScore <= 1.0);
    }

    void testCustomSourceReliabilityUsed()
    {
        QMap<QString, double> rel;
        rel[QStringLiteral("custom_feed")] = 0.95;
        const DataQualityScorer scorer(rel);

        CrimeEvent ev = rich(QStringLiteral("C7"));
        ev.source = QStringLiteral("custom_feed");
        const auto report = scorer.score(ev);
        QVERIFY(report.sourceReliability >= 0.9);
    }
};

QTEST_GUILESS_MAIN(TestDataQualityDeep7)
#include "test_data_quality_deep7.moc"
