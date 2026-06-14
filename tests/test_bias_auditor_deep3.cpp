// test_bias_auditor_deep3.cpp — Deep audit iteration 23: BiasAuditor
// Probes: feedback-loop detection, disparate-impact thresholds, equalized
// odds on degenerate groups, formatReports output, and trendSlope behaviour.

#include <QTest>
#include <cmath>
#include "benchmark/BiasAuditor.h"

class TestBiasAuditorDeep3 : public QObject
{
    Q_OBJECT

private:
    static QVector<QString> makeGroups(const QString& a, int nA,
                                       const QString& b, int nB)
    {
        QVector<QString> g;
        g.reserve(nA + nB);
        for (int i = 0; i < nA; ++i) g.append(a);
        for (int i = 0; i < nB; ++i) g.append(b);
        return g;
    }

private slots:
    void testFeedbackLoopRisingPredFlatActual();
    void testFeedbackLoopIgnoresBalancedTrends();
    void testMaxDisparateImpactEmptyGroupsReturnsZero();
    void testEqualizedOddsDiffSingleGroup();
    void testFormatReportsNonEmptyOutput();
    void testDisparateImpactCustomThreshold();
    void testTrendSlopeMonotonicIncreasingPositive();
};

// ─── feedbackLoopCheck ───────────────────────────────────────────────────────

void TestBiasAuditorDeep3::testFeedbackLoopRisingPredFlatActual()
{
    QMap<QString, QVector<QPair<double, double>>> trendData;
    QVector<QPair<double, double>> series;
    series.reserve(8);
    for (int i = 0; i < 8; ++i)
        series.append({0.05 + 0.04 * i, 0.42}); // pred rises, actual flat
    trendData.insert(QStringLiteral("DistrictNorth"), series);

    const auto flagged = BiasAuditor::feedbackLoopCheck(trendData, 0.01);
    QVERIFY2(flagged.contains(QStringLiteral("DistrictNorth")),
             "Rising prediction rate with flat actual rate should be flagged");
}

void TestBiasAuditorDeep3::testFeedbackLoopIgnoresBalancedTrends()
{
    QMap<QString, QVector<QPair<double, double>>> trendData;
    QVector<QPair<double, double>> series;
    series.reserve(6);
    for (int i = 0; i < 6; ++i) {
        const double rate = 0.15 + 0.03 * i;
        series.append({rate, rate}); // both pred and actual rise together
    }
    trendData.insert(QStringLiteral("BalancedLGA"), series);

    const auto flagged = BiasAuditor::feedbackLoopCheck(trendData, 0.01);
    QVERIFY2(!flagged.contains(QStringLiteral("BalancedLGA")),
             "Balanced rising trends should not trigger feedback-loop flag");
}

// ─── maxDisparateImpact / equalizedOddsDiff ──────────────────────────────────

void TestBiasAuditorDeep3::testMaxDisparateImpactEmptyGroupsReturnsZero()
{
    BiasAuditor auditor;

    QCOMPARE(auditor.maxDisparateImpact({}), 0.0);

    GroupStats empty;
    empty.nEvents = 0;
    empty.nFlagged = 0;
    GroupStats valid;
    valid.nEvents = 12;
    valid.nFlagged = 6;
    QCOMPARE(auditor.maxDisparateImpact({valid, empty}), 0.0);
}

void TestBiasAuditorDeep3::testEqualizedOddsDiffSingleGroup()
{
    GroupStats only;
    only.groupId    = QStringLiteral("Solo");
    only.nEvents    = 20;
    only.nActualPos = 10;
    only.nTP        = 7;
    only.nFN        = 3;

    BiasAuditor auditor;
    QCOMPARE(auditor.equalizedOddsDiff({only}), 0.0);

    only.nTP = 10;
    only.nFN = 0;
    QCOMPARE(auditor.equalizedOddsDiff({only}), 0.0);
}

// ─── formatReports ───────────────────────────────────────────────────────────

void TestBiasAuditorDeep3::testFormatReportsNonEmptyOutput()
{
    const auto groups = makeGroups(QStringLiteral("Alpha"), 40,
                                   QStringLiteral("Beta"), 40);
    QVector<double> preds;
    preds.reserve(80);
    for (int i = 0; i < 40; ++i) preds.append(0.85);
    for (int i = 0; i < 40; ++i) preds.append(0.35);

    const auto reports = BiasAuditor::disparateImpact(groups, preds);
    QVERIFY(!reports.isEmpty());

    const QString formatted = BiasAuditor::formatReports(reports);
    QVERIFY2(!formatted.trimmed().isEmpty(),
             "formatReports must produce non-empty text for non-empty input");
    QVERIFY(formatted.contains(QStringLiteral("Bias Audit Report")));
    QVERIFY(formatted.contains(QStringLiteral("disparate_impact")));
    QVERIFY(formatted.contains(QStringLiteral("Alpha")));
    QVERIFY(formatted.contains(QStringLiteral("Beta")));
}

// ─── disparateImpact threshold ───────────────────────────────────────────────

void TestBiasAuditorDeep3::testDisparateImpactCustomThreshold()
{
    const auto groups = makeGroups(QStringLiteral("High"), 20,
                                   QStringLiteral("Low"), 20);
    QVector<double> preds;
    preds.reserve(40);
    for (int i = 0; i < 20; ++i) preds.append(0.90); // positive at 0.5 and 0.7
    for (int i = 0; i < 20; ++i) preds.append(0.65); // positive at 0.5, not at 0.7

    const auto atDefault = BiasAuditor::disparateImpact(groups, preds, 0.5);
    const auto atStrict  = BiasAuditor::disparateImpact(groups, preds, 0.7);

    QVERIFY(!atDefault.isEmpty());
    QVERIFY(!atStrict.isEmpty());
    QVERIFY2(std::abs(atDefault.first().ratio - 1.0) < 1e-9,
             "Both groups selected at threshold 0.5 should yield ratio 1.0");
    QVERIFY2(atStrict.first().flagged,
             "Custom threshold 0.7 should expose disparate impact between groups");
    QVERIFY2(atStrict.first().valueB < 1e-9,
             qPrintable(QStringLiteral(
                 "Low group rate at 0.7 threshold expected 0, got %1")
                            .arg(atStrict.first().valueB)));
}

// ─── trendSlope (via feedbackLoopCheck boundary) ───────────────────────────────

void TestBiasAuditorDeep3::testTrendSlopeMonotonicIncreasingPositive()
{
    // Monotonic pred series [1,2,3,4,5] has positive OLS slope; flat actual at 0.
    QMap<QString, QVector<QPair<double, double>>> trendData;
    QVector<QPair<double, double>> series;
    for (int i = 1; i <= 5; ++i)
        series.append({static_cast<double>(i), 0.0});
    trendData.insert(QStringLiteral("Monotonic"), series);

    const auto flaggedLowSens  = BiasAuditor::feedbackLoopCheck(trendData, 0.01);
    const auto flaggedHighSens = BiasAuditor::feedbackLoopCheck(trendData, 5.0);

    QVERIFY2(flaggedLowSens.contains(QStringLiteral("Monotonic")),
             "Positive pred slope with flat actual should flag at low sensitivity");
    QVERIFY2(!flaggedHighSens.contains(QStringLiteral("Monotonic")),
             "Slope below high sensitivity threshold should not flag");
}

QTEST_GUILESS_MAIN(TestBiasAuditorDeep3)
#include "test_bias_auditor_deep3.moc"
