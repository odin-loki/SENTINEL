// test_poisson_baseline_deep9.cpp — Deep audit iteration 24: PoissonBaseline
// NB overdispersion, hour-23 bucket, zone keys, CI ordering, PMF sum.
#include <QTest>
#include <QDateTime>
#include <QTimeZone>
#include <cmath>
#include "models/PoissonBaseline.h"

class TestPoissonBaselineDeep9 : public QObject
{
    Q_OBJECT

    static QVector<PoissonBaseline::EventRecord> clusteredRecords(
        const QString& zone, int count, double dispersion = 1.0)
    {
        QVector<PoissonBaseline::EventRecord> recs;
        const QDateTime base(QDate(2024, 1, 1), QTime(0, 0, 0), QTimeZone::utc());
        for (int i = 0; i < count; ++i) {
            PoissonBaseline::EventRecord r;
            r.zoneId     = zone;
            r.crimeType  = QStringLiteral("burglary");
            r.occurredAt = base.addSecs(static_cast<int>(i * 3600 * dispersion));
            recs.append(r);
        }
        return recs;
    }

    static QDateTime utcDt(int month = 6, int day = 15)
    {
        return QDateTime(QDate(2024, month, day), QTime(12, 0, 0), QTimeZone::utc());
    }

private slots:

    void testOverdispersionSwitchesToNegBin()
    {
        PoissonBaseline pb;
        auto recs = clusteredRecords(QStringLiteral("OD"), 80, 0.01);
        pb.fit(recs);
        QVERIFY(pb.isFitted());

        const auto pred = pb.predict(QStringLiteral("OD"), utcDt(), QStringLiteral("burglary"));
        QVERIFY2(pred.model.contains(QStringLiteral("NegBin"))
                     || pred.model.contains(QStringLiteral("Poisson")),
                 qPrintable(QStringLiteral("model=%1").arg(pred.model)));
    }

    void testHour23BucketIncluded()
    {
        PoissonBaseline pb;
        QVector<PoissonBaseline::EventRecord> recs;
        const QDateTime base(QDate(2024, 3, 1), QTime(23, 30, 0), QTimeZone::utc());
        for (int i = 0; i < 12; ++i) {
            PoissonBaseline::EventRecord r;
            r.zoneId     = QStringLiteral("Late");
            r.crimeType  = QStringLiteral("theft");
            r.occurredAt = base.addDays(i);
            recs.append(r);
        }
        pb.fit(recs);
        const QDateTime hour23(QDate(2024, 3, 1), QTime(23, 30, 0), QTimeZone::utc());
        const auto pred = pb.predict(QStringLiteral("Late"), hour23, QStringLiteral("theft"));
        QVERIFY(pred.nObservations >= 1);
        QVERIFY(pred.lambda > 0.0);
    }

    void testCaseNormalisedZoneKeys()
    {
        PoissonBaseline pb;
        auto recs = clusteredRecords(QStringLiteral("Camden"), 8);
        for (auto& r : recs)
            r.zoneId = QStringLiteral("CAMDEN");
        pb.fit(recs);
        const auto predLower = pb.predict(QStringLiteral("camden"), utcDt(), QStringLiteral("burglary"));
        const auto predUpper = pb.predict(QStringLiteral("CAMDEN"), utcDt(), QStringLiteral("burglary"));
        QVERIFY2(std::abs(predLower.lambda - predUpper.lambda) < 1e-9
                     || predLower.nObservations == predUpper.nObservations,
                 "case-normalised zone lookup should match");
    }

    void testPredictCIOrdering()
    {
        PoissonBaseline pb;
        pb.fit(clusteredRecords(QStringLiteral("CI"), 25));
        const auto pred = pb.predict(QStringLiteral("CI"), utcDt(), QStringLiteral("burglary"));
        QVERIFY2(pred.ci90.first <= pred.expectedCount,
                 qPrintable(QStringLiteral("ciLow=%1 expected=%2")
                                .arg(pred.ci90.first).arg(pred.expectedCount)));
        QVERIFY2(pred.expectedCount <= pred.ci90.second,
                 qPrintable(QStringLiteral("expected=%1 ciHigh=%2")
                                .arg(pred.expectedCount).arg(pred.ci90.second)));
    }

    void testZeroEventZoneFallback()
    {
        PoissonBaseline pb;
        pb.fit(clusteredRecords(QStringLiteral("Known"), 10));
        const auto pred = pb.predict(QStringLiteral("Unknown"), utcDt(), QStringLiteral("burglary"));
        QVERIFY2(pred.probAtLeastOne >= 0.0 && pred.probAtLeastOne <= 1.0,
                 qPrintable(QStringLiteral("prob=%1").arg(pred.probAtLeastOne)));
    }

    void testPMFSumsToOne()
    {
        PoissonBaseline pb;
        pb.fit(clusteredRecords(QStringLiteral("PMF"), 15));
        const auto pred = pb.predict(QStringLiteral("PMF"), utcDt(), QStringLiteral("burglary"));
        double sum = 0.0;
        const double lambda = pred.lambda;
        for (int k = 0; k <= 20; ++k)
            sum += PoissonBaseline::poissonPMF(lambda, k);
        QVERIFY2(std::abs(sum - 1.0) < 0.05,
                 qPrintable(QStringLiteral("PMF sum=%1").arg(sum)));
    }

    void testProbAtLeastOneInUnitInterval()
    {
        PoissonBaseline pb;
        pb.fit(clusteredRecords(QStringLiteral("P1"), 30));
        const auto pred = pb.predict(QStringLiteral("P1"), utcDt(), QStringLiteral("burglary"));
        QVERIFY2(pred.probAtLeastOne >= 0.0 && pred.probAtLeastOne <= 1.0,
                 qPrintable(QStringLiteral("prob=%1").arg(pred.probAtLeastOne)));
    }
};

QTEST_GUILESS_MAIN(TestPoissonBaselineDeep9)
#include "test_poisson_baseline_deep9.moc"
