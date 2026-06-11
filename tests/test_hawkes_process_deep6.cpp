// test_hawkes_process_deep6.cpp — Deep audit iteration 15 for HawkesProcess
// Intensity temporal decay, branching ratio, empty fit.

#include <QtTest>
#include <cmath>
#include "models/HawkesProcess.h"

class TestHawkesProcessDeep6 : public QObject
{
    Q_OBJECT

    static SpatiotemporalEvent ev(double tDays, double lat = 51.5, double lon = -0.1)
    {
        SpatiotemporalEvent e;
        e.tDays     = tDays;
        e.lat       = lat;
        e.lon       = lon;
        e.crimeType = QStringLiteral("Burglary");
        return e;
    }

    static HawkesParams params(double mu, double alpha, double beta, double sigma)
    {
        HawkesParams p;
        p.mu     = mu;
        p.alpha  = alpha;
        p.beta   = beta;
        p.sigma  = sigma;
        p.logLik = 0.0;
        return p;
    }

private slots:

    void testFitEmptyReturnsFalse()
    {
        HawkesProcess hp;
        QVERIFY(!hp.fit({}, 10));
        QVERIFY(!hp.isFitted());
    }

    void testBranchingRatioEqualsAlpha()
    {
        HawkesProcess hp;
        hp.setParams(params(0.2, 0.55, 3.0, 0.02));
        QCOMPARE(hp.branchingRatio(), 0.55);
        QCOMPARE(hp.branchingRatio(), hp.params().alpha);
    }

    void testIntensityMonotonicDecayWithElapsedTime()
    {
        HawkesProcess hp;
        hp.setParams(params(0.1, 0.6, 2.0, 0.01));
        hp.setHistory({ ev(0.0, 51.5, -0.1) });

        const double lamEarly = hp.intensity(0.5, 51.5, -0.1);
        const double lamMid   = hp.intensity(1.5, 51.5, -0.1);
        const double lamLate  = hp.intensity(3.0, 51.5, -0.1);

        QVERIFY2(lamEarly > lamMid,
                 qPrintable(QStringLiteral("decay: early=%1 mid=%2").arg(lamEarly).arg(lamMid)));
        QVERIFY2(lamMid > lamLate,
                 qPrintable(QStringLiteral("decay: mid=%1 late=%2").arg(lamMid).arg(lamLate)));
        QVERIFY2(lamLate > 0.1,
                 "intensity should remain above background mu after decay");
    }

    void testTriggerKernelHalfLifeAtLn2OverBeta()
    {
        HawkesProcess hp;
        const double alpha = 0.5;
        const double beta  = 4.0;
        hp.setParams(params(0.1, alpha, beta, 0.01));

        const double dtHalf = std::log(2.0) / beta;
        const double atZero = hp.triggerKernel(0.0, 0.0);
        const double atHalf = hp.triggerKernel(dtHalf, 0.0);

        QVERIFY2(std::abs(atHalf - 0.5 * atZero) < 1e-9,
                 qPrintable(QStringLiteral("half-life kernel: atHalf=%1 half*atZero=%2")
                                .arg(atHalf).arg(0.5 * atZero)));
    }

    void testIntensityEqualsMuBeforeFirstHistoryEvent()
    {
        HawkesProcess hp;
        const double mu = 0.33;
        hp.setParams(params(mu, 0.4, 2.0, 0.01));
        hp.setHistory({ ev(5.0, 51.5, -0.1) });

        QCOMPARE(hp.intensity(2.0, 51.5, -0.1), mu);
    }

    void testIntensityDecaysToMuAfterTemporalCutoff()
    {
        HawkesProcess hp;
        const double mu   = 0.12;
        const double beta = 5.0;  // tCutoff = min(20/5, 90) = 4 days
        hp.setParams(params(mu, 0.45, beta, 0.01));
        hp.setHistory({ ev(0.0, 51.5, -0.1) });

        const double withinCutoff = hp.intensity(3.5, 51.5, -0.1);
        const double beyondCutoff = hp.intensity(20.0, 51.5, -0.1);

        QVERIFY2(withinCutoff > mu,
                 qPrintable(QStringLiteral("within cutoff=%1 should exceed mu=%2")
                                .arg(withinCutoff).arg(mu)));
        QCOMPARE(beyondCutoff, mu);
    }

    void testBranchingRatioSubcriticalAfterBurstFit()
    {
        QVector<SpatiotemporalEvent> burst;
        for (int i = 0; i < 28; ++i)
            burst << ev(i * 0.06, 51.5, -0.1);

        HawkesProcess hp;
        QVERIFY(hp.fit(burst, 12));
        QVERIFY2(hp.branchingRatio() >= 0.0 && hp.branchingRatio() < 1.0,
                 qPrintable(QStringLiteral("branching ratio=%1 must lie in [0,1)")
                                .arg(hp.branchingRatio())));
        QVERIFY(hp.isFitted());
        QVERIFY(std::isfinite(hp.params().logLik));
    }

    void testBranchingRatioLowerForUniformThanCluster()
    {
        QVector<SpatiotemporalEvent> uniform;
        for (int i = 0; i < 25; ++i)
            uniform << ev(static_cast<double>(i), 51.5 + 0.01 * (i % 5), -0.1);

        QVector<SpatiotemporalEvent> cluster;
        for (int i = 0; i < 25; ++i)
            cluster << ev(i * 0.04, 51.5, -0.1);

        HawkesProcess hpUniform;
        HawkesProcess hpCluster;
        QVERIFY(hpUniform.fit(uniform, 12));
        QVERIFY(hpCluster.fit(cluster, 12));

        QVERIFY2(hpCluster.branchingRatio() > hpUniform.branchingRatio(),
                 qPrintable(QStringLiteral("cluster alpha=%1 should exceed uniform alpha=%2")
                                .arg(hpCluster.branchingRatio())
                                .arg(hpUniform.branchingRatio())));
    }
};

QTEST_GUILESS_MAIN(TestHawkesProcessDeep6)
#include "test_hawkes_process_deep6.moc"
