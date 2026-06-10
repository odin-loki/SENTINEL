// test_calibration_bias_improvements.cpp
// Tests for CalibrationAnalyser::reliabilityDiagram(bins) and
// BiasAuditor::maxDisparateImpact / equalizedOddsDiff
#include <QCoreApplication>
#include <QTest>
#include <cmath>

#include "benchmark/CalibrationAnalyser.h"
#include "benchmark/BiasAuditor.h"

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

static CalibrationBin makeBin(double midpoint, double empirical, int count)
{
    CalibrationBin b;
    b.midpoint  = midpoint;
    b.empirical = empirical;
    b.count     = count;
    return b;
}

static GroupStats makeGroup(int nEvents, int nFlagged, int nTP, int nActualPos)
{
    GroupStats g;
    g.nEvents    = nEvents;
    g.nFlagged   = nFlagged;
    g.nTP        = nTP;
    g.nActualPos = nActualPos;
    g.flagRate   = (nEvents > 0) ? static_cast<double>(nFlagged) / nEvents : 0.0;
    return g;
}

// ─────────────────────────────────────────────────────────────────────────────

class TestCalibrationReliabilityDiagram : public QObject
{
    Q_OBJECT

private slots:

    void emptyBinsReturnsEmpty()
    {
        CalibrationAnalyser ca;
        auto pts = ca.reliabilityDiagram(QVector<CalibrationBin>{});
        QVERIFY(pts.isEmpty());
    }

    void singleBinWithCountReturnsOnePoint()
    {
        CalibrationAnalyser ca;
        QVector<CalibrationBin> bins;
        bins.append(makeBin(0.25, 0.3, 10));

        auto pts = ca.reliabilityDiagram(bins);
        QCOMPARE(pts.size(), 1);
        QCOMPARE(pts[0].count, 10);
        QVERIFY(std::abs(pts[0].meanPredicted    - 0.25) < 1e-9);
        QVERIFY(std::abs(pts[0].fractionPositive - 0.3)  < 1e-9);
    }

    void zeroCountBinsAreSkipped()
    {
        CalibrationAnalyser ca;
        QVector<CalibrationBin> bins;
        bins.append(makeBin(0.05, 0.0, 0));   // skipped
        bins.append(makeBin(0.15, 0.2, 5));   // included
        bins.append(makeBin(0.25, 0.0, 0));   // skipped
        bins.append(makeBin(0.35, 0.4, 8));   // included

        auto pts = ca.reliabilityDiagram(bins);
        QCOMPARE(pts.size(), 2);
    }

    void pointsAreInMidpointOrder()
    {
        CalibrationAnalyser ca;
        QVector<CalibrationBin> bins;
        bins.append(makeBin(0.05, 0.1, 4));
        bins.append(makeBin(0.15, 0.2, 3));
        bins.append(makeBin(0.25, 0.3, 6));

        auto pts = ca.reliabilityDiagram(bins);
        QCOMPARE(pts.size(), 3);
        QVERIFY(pts[0].meanPredicted < pts[1].meanPredicted);
        QVERIFY(pts[1].meanPredicted < pts[2].meanPredicted);
    }

    void fractionPositiveMatchesBinObservedFreq()
    {
        CalibrationAnalyser ca;
        QVector<CalibrationBin> bins;
        bins.append(makeBin(0.45, 0.62, 20));

        auto pts = ca.reliabilityDiagram(bins);
        QCOMPARE(pts.size(), 1);
        QVERIFY(std::abs(pts[0].fractionPositive - 0.62) < 1e-9);
    }
};

// ─────────────────────────────────────────────────────────────────────────────

class TestBiasAuditorFairness : public QObject
{
    Q_OBJECT

private:
    BiasAuditor m_auditor;

private slots:

    // ── maxDisparateImpact ───────────────────────────────────────────────────

    void equalGroupsDisparateImpactIsOne()
    {
        QVector<GroupStats> groups;
        groups.append(makeGroup(100, 50, 0, 0));
        groups.append(makeGroup(100, 50, 0, 0));

        QVERIFY(std::abs(m_auditor.maxDisparateImpact(groups) - 1.0) < 1e-9);
    }

    void oneFullOneHalfSelectionRateRatioIsTwo()
    {
        // Group A: 100/100 → 1.0; Group B: 50/100 → 0.5 → ratio = 2.0
        QVector<GroupStats> groups;
        groups.append(makeGroup(100, 100, 0, 0));
        groups.append(makeGroup(100, 50,  0, 0));

        QVERIFY(std::abs(m_auditor.maxDisparateImpact(groups) - 2.0) < 1e-9);
    }

    void emptyGroupsDisparateImpactReturnsZero()
    {
        QVERIFY(std::abs(m_auditor.maxDisparateImpact(QVector<GroupStats>{})) < 1e-9);

        // Group with nEvents == 0 also triggers zero-return
        QVector<GroupStats> withEmpty;
        withEmpty.append(makeGroup(0,   0, 0, 0));
        withEmpty.append(makeGroup(100, 50, 0, 0));
        QVERIFY(std::abs(m_auditor.maxDisparateImpact(withEmpty)) < 1e-9);
    }

    // ── equalizedOddsDiff ────────────────────────────────────────────────────

    void equalTprAcrossGroupsReturnsZero()
    {
        QVector<GroupStats> groups;
        groups.append(makeGroup(20, 10, 8, 10));
        groups.append(makeGroup(20, 10, 8, 10));

        QVERIFY(std::abs(m_auditor.equalizedOddsDiff(groups)) < 1e-9);
    }

    void differentTprReturnsCorrectMaxAbsDiff()
    {
        // TPR: 0.8, 0.6, 0.5 → max diff = 0.8 - 0.5 = 0.3
        QVector<GroupStats> groups;
        groups.append(makeGroup(20, 10, 8, 10));
        groups.append(makeGroup(20,  8, 6, 10));
        groups.append(makeGroup(20,  7, 5, 10));

        QVERIFY(std::abs(m_auditor.equalizedOddsDiff(groups) - 0.3) < 1e-9);
    }

    void groupWithNoPositivesReturnsZero()
    {
        QVector<GroupStats> groups;
        groups.append(makeGroup(20, 10, 8, 10));
        groups.append(makeGroup(20,  5, 0,  0));  // no actual positives

        QVERIFY(std::abs(m_auditor.equalizedOddsDiff(groups)) < 1e-9);
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// main: run both test suites
// ─────────────────────────────────────────────────────────────────────────────

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);

    int failures = 0;
    {
        TestCalibrationReliabilityDiagram t;
        failures += QTest::qExec(&t, argc, argv);
    }
    {
        TestBiasAuditorFairness t;
        failures += QTest::qExec(&t, argc, argv);
    }
    return failures;
}

#include "test_calibration_bias_improvements.moc"
