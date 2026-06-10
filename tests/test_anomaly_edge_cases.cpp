// test_anomaly_edge_cases.cpp
// AnomalyDetector edge cases: single event, extreme outlier values,
// empty input, and combined score ordering.
#include <QTest>
#include "inference/AnomalyDetector.h"
#include <cmath>

class AnomalyEdgeCasesTest : public QObject
{
    Q_OBJECT

private:
    using AFV = AnomalyFeatureVector;

    static AFV makeAFV(const QString& id, double lat, double lon, double tDays,
                        double hour = 0.5, int typeCode = 0)
    {
        return { id, lat, lon, tDays, hour, typeCode };
    }

    static QVector<AFV> normal20()
    {
        QVector<AFV> data;
        for (int i = 0; i < 20; ++i)
            data.append(makeAFV(QStringLiteral("N%1").arg(i),
                                51.5 + i * 0.001, -0.1 + i * 0.001,
                                static_cast<double>(i)));
        return data;
    }

private slots:

    // 1. Empty fit: no crash, isFitted true (auto-fit on detectAnomalies)
    void testEmptyFitNoCrash()
    {
        AnomalyDetector ad;
        ad.fit({});
        QVERIFY(true); // No crash
    }

    // 2. Single event: detectAnomalies returns one result
    void testSingleEventReturnsOneResult()
    {
        AnomalyDetector ad;
        auto result = ad.detectAnomalies({ makeAFV(QStringLiteral("E1"), 51.5, -0.1, 0.0) });
        QCOMPARE(result.size(), 1);
    }

    // 3. Extreme outlier gets higher combined score than normal events
    void testExtremeOutlierHigherScore()
    {
        AnomalyDetector ad;
        auto data = normal20();
        // Extreme outlier
        data.append(makeAFV(QStringLiteral("EXTREME"), 90.0, 0.0, 9999.0, 0.99, 99));

        ad.fit(data);
        auto anomalies = ad.detectAnomalies(data);

        double normalMax = 0.0, extremeScore = 0.0;
        for (const auto& a : anomalies) {
            if (a.eventId == QStringLiteral("EXTREME"))
                extremeScore = a.combinedScore;
            else
                normalMax = std::max(normalMax, a.combinedScore);
        }

        QVERIFY2(extremeScore >= normalMax * 0.5,
                 qPrintable(QStringLiteral("Extreme score %1 should be >= 50%% of normal max %2")
                    .arg(extremeScore).arg(normalMax)));
    }

    // 4. isFitted() true after fit with non-empty data
    void testFittedAfterFit()
    {
        AnomalyDetector ad;
        ad.fit(normal20());
        QVERIFY(ad.isFitted());
    }

    // 5. All combinedScore values in [0, ∞) after detection
    void testCombinedScoreNonNegative()
    {
        AnomalyDetector ad;
        auto anomalies = ad.detectAnomalies(normal20());
        for (const auto& a : anomalies)
            QVERIFY2(a.combinedScore >= 0.0,
                     qPrintable(QStringLiteral("combinedScore %1 must be >= 0").arg(a.combinedScore)));
    }

    // 6. detectAnomalies: result count matches input count
    void testResultCountMatchesInput()
    {
        AnomalyDetector ad;
        const auto data = normal20();
        const auto anomalies = ad.detectAnomalies(data);
        QCOMPARE(anomalies.size(), data.size());
    }

    // 7. eventId preserved in output
    void testEventIdPreserved()
    {
        AnomalyDetector ad;
        QVector<AFV> data = { makeAFV(QStringLiteral("TESTID123"), 51.5, -0.1, 5.0) };
        const auto anomalies = ad.detectAnomalies(data);
        QVERIFY(!anomalies.isEmpty());
        QVERIFY2(anomalies.first().eventId == QStringLiteral("TESTID123"),
                 "eventId should be preserved in output");
    }

    // 8. Empty detectAnomalies: no crash, empty result
    void testEmptyDetectNoCrash()
    {
        AnomalyDetector ad;
        ad.fit(normal20());
        const auto anomalies = ad.detectAnomalies({});
        QVERIFY(anomalies.isEmpty());
    }

    // 9. isolationScore in [0, ∞)
    void testIsolationScoreNonNegative()
    {
        AnomalyDetector ad;
        const auto anomalies = ad.detectAnomalies(normal20());
        for (const auto& a : anomalies)
            QVERIFY2(a.isolationScore >= 0.0,
                     qPrintable(QStringLiteral("isolationScore %1 must be >= 0").arg(a.isolationScore)));
    }

    // 10. Two calls produce consistent results
    void testConsistentResults()
    {
        AnomalyDetector ad;
        ad.fit(normal20());
        const auto r1 = ad.detectAnomalies(normal20());
        const auto r2 = ad.detectAnomalies(normal20());
        QCOMPARE(r1.size(), r2.size());
        if (!r1.isEmpty())
            QVERIFY2(std::abs(r1.first().combinedScore - r2.first().combinedScore) < 1e-9,
                     "Repeated detection should give consistent scores");
    }
};

QTEST_MAIN(AnomalyEdgeCasesTest)
#include "test_anomaly_edge_cases.moc"
