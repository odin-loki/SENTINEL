// test_bias_auditor_comprehensive.cpp
// Comprehensive tests for BiasAuditor: disparate impact, equal opportunity,
// group stats, feedback loop, and derived metrics.

#include <QTest>
#include <QCoreApplication>
#include <QString>
#include <QVector>
#include <QMap>
#include <cmath>
#include <algorithm>

#include "benchmark/BiasAuditor.h"

class TestBiasAuditorComprehensive : public QObject
{
    Q_OBJECT
private slots:

    // ── Disparate impact tests ────────────────────────────────────────────────

    // Equal positive prediction rates across two groups → DI ratio = 1.0, not flagged
    void testEqualGroupsNoBias()
    {
        QVector<QString> groups;
        QVector<double>  yPred;

        // Both groups: 2/4 = 0.5 positive rate
        for (int i = 0; i < 4; ++i) {
            groups << QStringLiteral("A");
            yPred  << (i < 2 ? 0.8 : 0.2);
        }
        for (int i = 0; i < 4; ++i) {
            groups << QStringLiteral("B");
            yPred  << (i < 2 ? 0.8 : 0.2);
        }

        const auto reports = BiasAuditor::disparateImpact(groups, yPred);
        QVERIFY(!reports.isEmpty());

        for (const BiasReport& r : reports) {
            QVERIFY2(!r.flagged,
                     qPrintable(QStringLiteral("equal groups should not be flagged, ratio=%1")
                                    .arg(r.ratio)));
            QVERIFY2(std::abs(r.ratio - 1.0) < 0.01,
                     qPrintable(QStringLiteral("expected DI ratio ~1.0, got %1").arg(r.ratio)));
        }
    }

    // Group "High" has 100% positive rate, Group "Low" has 0% → DI ratio > 1.2 and flagged
    void testDisparateImpact()
    {
        QVector<QString> groups;
        QVector<double>  yPred;

        for (int i = 0; i < 5; ++i) { groups << QStringLiteral("High"); yPred << 1.0; }
        for (int i = 0; i < 5; ++i) { groups << QStringLiteral("Low");  yPred << 0.0; }

        const auto reports = BiasAuditor::disparateImpact(groups, yPred);
        QVERIFY(!reports.isEmpty());

        // At least one pair must be flagged and have ratio > 1.2 (or infinity)
        bool foundFlagged = false;
        for (const BiasReport& r : reports) {
            if (r.flagged) {
                foundFlagged = true;
                QVERIFY2(r.ratio > 1.2 || std::isinf(r.ratio),
                         qPrintable(QStringLiteral("flagged report should have ratio > 1.2, got %1")
                                        .arg(r.ratio)));
            }
        }
        QVERIFY2(foundFlagged, "expected at least one flagged BiasReport for disparate groups");
    }

    // Any non-trivial two-group setup produces a non-empty report and a non-empty formatted string
    void testBiasReportNonEmpty()
    {
        const QVector<QString> groups = {
            QStringLiteral("A"), QStringLiteral("A"),
            QStringLiteral("B"), QStringLiteral("B")
        };
        const QVector<double> yPred = { 0.8, 0.2, 0.9, 0.1 };
        const QVector<double> yTrue = { 1.0, 0.0, 1.0, 0.0 };

        const auto diReports = BiasAuditor::disparateImpact(groups, yPred);
        const auto eoReports = BiasAuditor::equalOpportunity(groups, yTrue, yPred);

        QVERIFY2(!diReports.isEmpty(), "disparateImpact should return at least one report");
        QVERIFY2(!eoReports.isEmpty(), "equalOpportunity should return at least one report");

        const QString formatted = BiasAuditor::formatReports(diReports);
        QVERIFY2(!formatted.isEmpty(), "formatReports should not return empty string");
        QVERIFY2(!formatted.contains(QStringLiteral("No bias reports")),
                 "formatReports should not say 'No bias reports' when reports exist");
    }

    // ── GroupStats correctness ────────────────────────────────────────────────

    // Known input: 4 events, TP=1, FP=1, FN=1, TN=1 → verify all GroupStats fields
    void testGroupStatsComputed()
    {
        const QVector<QString> groups = {
            QStringLiteral("A"), QStringLiteral("A"),
            QStringLiteral("A"), QStringLiteral("A")
        };
        // yTrue[0]=1, yTrue[1]=1 (actual positives); yTrue[2..3]=0
        // yPred[0]=0.8 (>=0.5), yPred[1]=0.3 (<0.5), yPred[2]=0.8 (>=0.5), yPred[3]=0.3
        const QVector<double> yTrue = { 1.0, 1.0, 0.0, 0.0 };
        const QVector<double> yPred = { 0.8, 0.3, 0.8, 0.3 };

        const auto stats = BiasAuditor::groupStats(groups, yTrue, yPred);
        QVERIFY(stats.contains(QStringLiteral("A")));

        const GroupStats& s = stats[QStringLiteral("A")];
        QCOMPARE(s.nEvents,    4);
        QCOMPARE(s.nFlagged,   2);   // idx 0 and 2 have yPred >= 0.5
        QCOMPARE(s.nActualPos, 2);   // yTrue[0] and yTrue[1] = 1
        QCOMPARE(s.nTP,        1);   // only idx 0: yTrue=1 AND yPred>=0.5

        QVERIFY2(std::abs(s.flagRate   - 0.5) < 1e-9, "expected flagRate = 0.5");
        QVERIFY2(std::abs(s.actualRate - 0.5) < 1e-9, "expected actualRate = 0.5");
    }

    // Two groups with different TPRs → equalizedOddsDiff > 0
    void testMaxTPRDifference()
    {
        GroupStats high;
        high.groupId    = QStringLiteral("HighTPR");
        high.nActualPos = 4;
        high.nTP        = 4;   // TPR = 1.0
        high.nEvents    = 4;
        high.nFlagged   = 4;

        GroupStats low;
        low.groupId    = QStringLiteral("LowTPR");
        low.nActualPos = 4;
        low.nTP        = 2;   // TPR = 0.5
        low.nEvents    = 4;
        low.nFlagged   = 2;

        BiasAuditor auditor;
        const double diff = auditor.equalizedOddsDiff({ high, low });

        QVERIFY2(diff > 0.0,
                 qPrintable(QStringLiteral("expected maxTPRDiff > 0, got %1").arg(diff)));
        QVERIFY2(std::abs(diff - 0.5) < 1e-9,
                 qPrintable(QStringLiteral("expected maxTPRDiff = 0.5, got %1").arg(diff)));
    }

    // ── Edge cases ────────────────────────────────────────────────────────────

    // Single group → no pairs → empty reports, no crash
    void testSingleGroup()
    {
        const QVector<QString> groups = {
            QStringLiteral("A"), QStringLiteral("A"), QStringLiteral("A")
        };
        const QVector<double> yPred = { 0.8, 0.9, 0.7 };
        const QVector<double> yTrue = { 1.0, 1.0, 0.0 };

        const auto diReports = BiasAuditor::disparateImpact(groups, yPred);
        QVERIFY2(diReports.isEmpty(),
                 "single group should produce no disparate-impact pairs");

        const auto eoReports = BiasAuditor::equalOpportunity(groups, yTrue, yPred);
        QVERIFY2(eoReports.isEmpty(),
                 "single group should produce no equal-opportunity pairs");
    }

    // Empty inputs → no crash, empty results
    void testEmptyGroups()
    {
        const QVector<QString> groups;
        const QVector<double>  yPred;
        const QVector<double>  yTrue;

        const auto diReports = BiasAuditor::disparateImpact(groups, yPred);
        QVERIFY(diReports.isEmpty());

        const auto eoReports = BiasAuditor::equalOpportunity(groups, yTrue, yPred);
        QVERIFY(eoReports.isEmpty());

        const auto stats = BiasAuditor::groupStats(groups, yTrue, yPred);
        QVERIFY(stats.isEmpty());
    }

    // ── Metric derivation correctness ─────────────────────────────────────────

    // Perfect predictions: TP=2, FP=0, TN=2, FN=0 → accuracy = 1.0
    void testAccuracyCalculation()
    {
        const QVector<QString> groups = {
            QStringLiteral("X"), QStringLiteral("X"),
            QStringLiteral("X"), QStringLiteral("X")
        };
        const QVector<double> yTrue = { 1.0, 1.0, 0.0, 0.0 };
        const QVector<double> yPred = { 0.8, 0.8, 0.2, 0.2 };  // all correct

        const auto stats = BiasAuditor::groupStats(groups, yTrue, yPred);
        QVERIFY(stats.contains(QStringLiteral("X")));
        const GroupStats& s = stats[QStringLiteral("X")];

        // TP=2, nFlagged=2, nActualPos=2, nEvents=4
        QCOMPARE(s.nTP,        2);
        QCOMPARE(s.nFlagged,   2);
        QCOMPARE(s.nActualPos, 2);
        QCOMPARE(s.nEvents,    4);

        // TN = nEvents - nFlagged - nActualPos + nTP
        const int tn = s.nEvents - s.nFlagged - s.nActualPos + s.nTP;
        QCOMPARE(tn, 2);

        const double accuracy = static_cast<double>(s.nTP + tn) / s.nEvents;
        QVERIFY2(std::abs(accuracy - 1.0) < 1e-9,
                 qPrintable(QStringLiteral("expected accuracy=1.0, got %1").arg(accuracy)));
    }

    // Known TP=1, FP=1, TN=1, FN=1 → FPR = FP / actual_negative = 1/2 = 0.5
    void testFPRCalculation()
    {
        const QVector<QString> groups = {
            QStringLiteral("Y"), QStringLiteral("Y"),
            QStringLiteral("Y"), QStringLiteral("Y")
        };
        // yTrue[0]=1, yTrue[2]=1 (actual positives)
        // yPred[0]=0.8 >=0.5 (TP), yPred[1]=0.8 >=0.5 (FP),
        // yPred[2]=0.2 <0.5 (FN), yPred[3]=0.2 <0.5 (TN)
        const QVector<double> yTrue = { 1.0, 0.0, 1.0, 0.0 };
        const QVector<double> yPred = { 0.8, 0.8, 0.2, 0.2 };

        const auto stats = BiasAuditor::groupStats(groups, yTrue, yPred);
        QVERIFY(stats.contains(QStringLiteral("Y")));
        const GroupStats& s = stats[QStringLiteral("Y")];

        QCOMPARE(s.nTP,        1);
        QCOMPARE(s.nFlagged,   2);
        QCOMPARE(s.nActualPos, 2);
        QCOMPARE(s.nEvents,    4);

        const int fp        = s.nFlagged - s.nTP;          // 1
        const int actualNeg = s.nEvents  - s.nActualPos;   // 2
        QCOMPARE(fp,        1);
        QCOMPARE(actualNeg, 2);

        const double fpr = static_cast<double>(fp) / actualNeg;
        QVERIFY2(std::abs(fpr - 0.5) < 1e-9,
                 qPrintable(QStringLiteral("expected FPR=0.5, got %1").arg(fpr)));
    }

    // ── Flagging threshold ────────────────────────────────────────────────────

    // High group (100% positive rate) vs Low group (25% positive rate)
    // → DI ratio = 4.0, outside [0.80, 1.25] → should be flagged
    void testBiasThresholdFlagging()
    {
        QVector<QString> groups;
        QVector<double>  yPred;

        for (int i = 0; i < 4; ++i) { groups << QStringLiteral("High"); yPred << 0.9; }
        // Only 1 of 4 positive for the low group
        groups << QStringLiteral("Low"); yPred << 0.9;
        groups << QStringLiteral("Low"); yPred << 0.1;
        groups << QStringLiteral("Low"); yPred << 0.1;
        groups << QStringLiteral("Low"); yPred << 0.1;

        const auto reports = BiasAuditor::disparateImpact(groups, yPred);
        QVERIFY(!reports.isEmpty());

        const bool anyFlagged =
            std::any_of(reports.begin(), reports.end(),
                        [](const BiasReport& r) { return r.flagged; });
        QVERIFY2(anyFlagged,
                 "expected at least one report to be flagged when DI ratio = 4.0");
    }
};

QTEST_MAIN(TestBiasAuditorComprehensive)
#include "test_bias_auditor_comprehensive.moc"
