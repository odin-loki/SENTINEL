// test_hawkes_process_deep7.cpp — Deep audit iteration 18 for HawkesProcess
// Spatial decay, risk surface, unsorted fit, simultaneous events.

#include <QtTest>
#include <cmath>
#include "models/HawkesProcess.h"

class TestHawkesProcessDeep7 : public QObject
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

    void testTriggerKernelNegativeDtReturnsZero()
    {
        HawkesProcess hp;
        hp.setParams(params(0.1, 0.5, 2.0, 0.01));
        QCOMPARE(hp.triggerKernel(-1.0, 0.0), 0.0);
        QCOMPARE(hp.triggerKernel(-0.001, 0.0), 0.0);
    }

    void testTriggerKernelSpatialDecayAtDistance()
    {
        HawkesProcess hp;
        hp.setParams(params(0.1, 0.6, 3.0, 0.02));

        const double atOrigin = hp.triggerKernel(0.5, 0.0);
        const double atDist   = hp.triggerKernel(0.5, 0.04);  // distSq = 0.04

        QVERIFY2(atOrigin > atDist,
                 qPrintable(QStringLiteral("spatial decay: origin=%1 distant=%2")
                                .arg(atOrigin).arg(atDist)));
        QVERIFY(atDist > 0.0);
    }

    void testFitSingleEventSucceeds()
    {
        HawkesProcess hp;
        QVERIFY(hp.fit({ ev(0.0) }, 5));
        QVERIFY(hp.isFitted());
        QVERIFY(hp.params().mu > 0.0);
        QVERIFY(hp.params().alpha >= 0.0 && hp.params().alpha < 1.0);
        QVERIFY(std::isfinite(hp.params().logLik));
    }

    void testFitSortsUnsortedInput()
    {
        QVector<SpatiotemporalEvent> unsorted;
        unsorted << ev(5.0) << ev(1.0) << ev(3.0) << ev(2.0);

        HawkesProcess hp;
        QVERIFY(hp.fit(unsorted, 8));
        QVERIFY(hp.isFitted());

        // After fit, intensity at t=4 should include contributions from t=1,2,3 not t=5
        const double beforeLast = hp.intensity(4.5, 51.5, -0.1);
        const double muOnly     = hp.params().mu;
        QVERIFY2(beforeLast > muOnly,
                 qPrintable(QStringLiteral("sorted history: intensity=%1 should exceed mu=%2")
                                .arg(beforeLast).arg(muOnly)));
    }

    void testRiskSurfaceGridDimensions()
    {
        HawkesProcess hp;
        hp.setParams(params(0.15, 0.4, 2.0, 0.01));
        hp.setHistory({ ev(0.0, 51.5, -0.1) });

        const int gridN = 12;
        const auto grid = hp.riskSurface(1.0, 51.48, 51.52, -0.12, -0.08, gridN);
        QCOMPARE(static_cast<int>(grid.size()), gridN);
        for (const auto& row : grid)
            QCOMPARE(static_cast<int>(row.size()), gridN);

        double maxVal = 0.0;
        for (const auto& row : grid)
            for (double v : row)
                maxVal = std::max(maxVal, v);
        QVERIFY(maxVal > hp.params().mu);
    }

    void testIntensityLowerAtDistantLocation()
    {
        HawkesProcess hp;
        hp.setParams(params(0.1, 0.55, 2.5, 0.015));
        hp.setHistory({ ev(0.0, 51.5, -0.1) });

        const double atEvent = hp.intensity(0.5, 51.5, -0.1);
        const double farAway = hp.intensity(0.5, 52.0, -0.5);

        QVERIFY2(atEvent > farAway,
                 qPrintable(QStringLiteral("spatial: atEvent=%1 farAway=%2")
                                .arg(atEvent).arg(farAway)));
        QVERIFY(farAway >= hp.params().mu);
    }

    void testConcurrentEventsSameTimestamp()
    {
        QVector<SpatiotemporalEvent> simultaneous;
        for (int i = 0; i < 12; ++i)
            simultaneous << ev(0.0, 51.5 + i * 0.001, -0.1);

        HawkesProcess hp;
        QVERIFY(hp.fit(simultaneous, 10));
        QVERIFY(hp.isFitted());
        QVERIFY(std::isfinite(hp.params().logLik));
        QVERIFY(hp.branchingRatio() >= 0.0 && hp.branchingRatio() < 1.0);
    }

    void testIntensitySkipsSimultaneousHistoryEvent()
    {
        HawkesProcess hp;
        const double mu = 0.2;
        hp.setParams(params(mu, 0.5, 2.0, 0.01));
        hp.setHistory({ ev(5.0, 51.5, -0.1) });

        // Query at exactly t=5: history event has tDays >= tDays, must not contribute
        QCOMPARE(hp.intensity(5.0, 51.5, -0.1), mu);
    }
};

QTEST_GUILESS_MAIN(TestHawkesProcessDeep7)
#include "test_hawkes_process_deep7.moc"
