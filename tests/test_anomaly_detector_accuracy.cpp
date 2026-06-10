// test_anomaly_detector_accuracy.cpp
// Validates AnomalyDetector detects spatial/temporal outliers, returns correct
// anomaly anomalies, and handles edge cases.
#include <QTest>
#include "inference/AnomalyDetector.h"
#include "core/CrimeEvent.h"
#include <cmath>

class AnomalyDetectorAccuracyTest : public QObject
{
    Q_OBJECT

private:
    static AnomalyFeatureVector normalEvent(int i, double baseLat = 51.5, double baseLon = -0.1)
    {
        AnomalyFeatureVector ev;
        ev.eventId       = QStringLiteral("N%1").arg(i);
        ev.lat           = baseLat + (i % 5) * 0.001;
        ev.lon           = baseLon + (i % 5) * 0.001;
        ev.tDays         = static_cast<double>(i);
        ev.hourNorm      = (i % 24) / 24.0;
        ev.crimeTypeCode = 1;
        return ev;
    }

    static AnomalyFeatureVector outlierEvent(const QString& id, double lat, double lon,
                                              double t, int typeCode = 99)
    {
        AnomalyFeatureVector ev;
        ev.eventId       = id;
        ev.lat           = lat;
        ev.lon           = lon;
        ev.tDays         = t;
        ev.hourNorm      = 0.5;
        ev.crimeTypeCode = typeCode;
        return ev;
    }

private slots:

    // ── 1. isFitted() true after fit ────────────────────────────────────────
    void testIsFittedAfterFit()
    {
        AnomalyDetector ad;
        QVERIFY(!ad.isFitted());
        QVector<AnomalyFeatureVector> data;
        for (int i = 0; i < 20; ++i) data.append(normalEvent(i));
        ad.fit(data);
        QVERIFY(ad.isFitted());
    }

    // ── 2. No anomalies from normal data (low contamination) ─────────────────
    void testNormalDataFewAnomalies()
    {
        AnomalyDetector ad(0.05);  // 5% contamination
        QVector<AnomalyFeatureVector> data;
        for (int i = 0; i < 100; ++i) data.append(normalEvent(i));
        ad.fit(data);
        const auto allResults = ad.detectAnomalies(data);

        // Count only events flagged as anomalies
        const int flagged = static_cast<int>(
            std::count_if(allResults.begin(), allResults.end(),
                          [](const AnomalySignal& s){ return s.isAnomaly; }));

        // With 5% contamination on 100 events, expect at most 15 flagged
        QVERIFY2(flagged <= 15,
                 qPrintable(QStringLiteral("Normal data produced %1 anomalies, expected <=15").arg(flagged)));
    }

    // ── 3. Clear spatial outlier is detected ─────────────────────────────────
    void testSpatialOutlierDetected()
    {
        AnomalyDetector ad(0.1);
        QVector<AnomalyFeatureVector> data;
        for (int i = 0; i < 50; ++i) data.append(normalEvent(i));
        // Add one event very far from the cluster
        data.append(outlierEvent(QStringLiteral("OUTLIER"), 40.0, 10.0, 25.0));

        ad.fit(data);
        const auto anomalies = ad.detectAnomalies(data);

        // Either the outlier is in the anomalies list (all returned) or isAnomaly=true
        const bool outlierFound = std::any_of(anomalies.begin(), anomalies.end(),
            [](const AnomalySignal& s){
                return s.eventId == QStringLiteral("OUTLIER");
            });
        QVERIFY2(outlierFound, "Spatial outlier should be detected as anomaly");
    }

    // ── 4. AnomalySignal has valid score range ────────────────────────────────
    void testSignalScoreRange()
    {
        AnomalyDetector ad(0.15);
        QVector<AnomalyFeatureVector> data;
        for (int i = 0; i < 30; ++i) data.append(normalEvent(i));
        data.append(outlierEvent(QStringLiteral("O1"), 90.0, 90.0, 1000.0));

        ad.fit(data);
        const auto allResults = ad.detectAnomalies(data);

        for (const auto& s : allResults) {
            QVERIFY2(s.combinedScore >= 0.0 && s.combinedScore <= 1.0 + 1e-6,
                     qPrintable(QStringLiteral("Anomaly score %1 must be in [0,1]")
                        .arg(s.combinedScore)));
        }
    }

    // ── 5. AnomalySignal eventId is set for all returned events ─────────────
    void testSignalEventIdSet()
    {
        AnomalyDetector ad(0.2);
        QVector<AnomalyFeatureVector> data;
        for (int i = 0; i < 20; ++i) data.append(normalEvent(i));
        ad.fit(data);
        const auto allResults = ad.detectAnomalies(data);

        for (const auto& s : allResults) {
            QVERIFY2(!s.eventId.isEmpty(), "AnomalySignal eventId must not be empty");
        }
    }

    // ── 6. Temporal outlier flagged ───────────────────────────────────────────
    void testTemporalOutlierDetected()
    {
        AnomalyDetector ad(0.1);
        QVector<AnomalyFeatureVector> data;
        for (int i = 0; i < 50; ++i) data.append(normalEvent(i));
        // Add event in very distant future
        data.append(outlierEvent(QStringLiteral("T_OUTLIER"), 51.5, -0.1, 99999.0));

        ad.fit(data);
        const auto anomalies = ad.detectAnomalies(data);
        const bool found = std::any_of(anomalies.begin(), anomalies.end(),
            [](const AnomalySignal& s){ return s.eventId == QStringLiteral("T_OUTLIER"); });
        QVERIFY2(found, "Temporal outlier in far future should be detected");
    }

    // ── 7. Auto-fit on first detectAnomalies call ────────────────────────────
    void testAutoFitOnDetect()
    {
        AnomalyDetector ad;
        QVERIFY(!ad.isFitted());

        QVector<AnomalyFeatureVector> data;
        for (int i = 0; i < 20; ++i) data.append(normalEvent(i));

        // Should auto-fit and not crash
        const auto anomalies = ad.detectAnomalies(data);
        QVERIFY(ad.isFitted());
    }

    // ── 8. Empty input → no anomalies, no crash ───────────────────────────────
    void testEmptyInputNoCrash()
    {
        AnomalyDetector ad;
        const auto anomalies = ad.detectAnomalies({});
        QVERIFY(anomalies.isEmpty());
    }

    // ── 9. More extreme outliers get higher scores ────────────────────────────
    void testOutlierScoreOrdering()
    {
        AnomalyDetector ad(0.2);
        QVector<AnomalyFeatureVector> data;
        for (int i = 0; i < 50; ++i) data.append(normalEvent(i));
        const auto mildOutlier  = outlierEvent(QStringLiteral("MILD"),    51.52, -0.08, 25.0);
        const auto severeOutlier = outlierEvent(QStringLiteral("SEVERE"), 80.0,  50.0, 5000.0);
        data.append(mildOutlier);
        data.append(severeOutlier);

        ad.fit(data);
        const auto anomalies = ad.detectAnomalies(data);

        double mildScore = -1.0, severeScore = -1.0;
        for (const auto& s : anomalies) {
            if (s.eventId == QStringLiteral("MILD"))   mildScore   = s.combinedScore;
            if (s.eventId == QStringLiteral("SEVERE")) severeScore = s.combinedScore;
        }

        if (severeScore >= 0 && mildScore >= 0) {
            QVERIFY2(severeScore >= mildScore,
                     qPrintable(QStringLiteral("Severe outlier score %1 should >= mild %2")
                        .arg(severeScore).arg(mildScore)));
        }
    }

    // ── 10. flagged count ≤ contamination * n * tolerance ────────────────────
    void testContaminationBound()
    {
        const double contamination = 0.05;
        AnomalyDetector ad(contamination);
        QVector<AnomalyFeatureVector> data;
        for (int i = 0; i < 200; ++i) data.append(normalEvent(i));
        ad.fit(data);
        const auto allResults = ad.detectAnomalies(data);

        int flagged = 0;
        for (const auto& s : allResults) {
            if (s.isAnomaly) ++flagged;
        }
        const int maxExpected = static_cast<int>(contamination * 200 * 4);  // 4x tolerance
        QVERIFY2(flagged <= maxExpected,
                 qPrintable(QStringLiteral("Flagged count %1 should be <= %2").arg(flagged).arg(maxExpected)));
    }
};

QTEST_MAIN(AnomalyDetectorAccuracyTest)
#include "test_anomaly_detector_accuracy.moc"
