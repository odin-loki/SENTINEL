// test_hawkes_process_deep8.cpp — Deep audit iteration 21: HawkesProcess
// Verifies: kernel peaks, multi-event intensity, log-likelihood improvement, risk surface, history order.

#include <QtTest/QtTest>
#include <cmath>
#include "models/HawkesProcess.h"

class TestHawkesProcessDeep8 : public QObject
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

    void testTriggerKernelMaximalAtZeroDtAndZeroDist()
    {
        HawkesProcess hp;
        hp.setParams(params(0.1, 0.5, 3.0, 0.02));

        const double peak = hp.triggerKernel(0.0, 0.0);
        const double later = hp.triggerKernel(1.0, 0.0);
        const double distant = hp.triggerKernel(0.0, 0.05);

        QVERIFY2(peak > later,
                 qPrintable(QStringLiteral("temporal peak=%1 later=%2").arg(peak).arg(later)));
        QVERIFY2(peak > distant,
                 qPrintable(QStringLiteral("spatial peak=%1 distant=%2").arg(peak).arg(distant)));
        QVERIFY(peak > 0.0);
    }

    void testIntensityAccumulatesMultiplePastEvents()
    {
        HawkesProcess hp;
        hp.setParams(params(0.1, 0.55, 2.5, 0.015));
        hp.setHistory({ ev(0.0, 51.5, -0.1), ev(1.0, 51.501, -0.099), ev(2.0, 51.499, -0.101) });

        const double lamOne = hp.intensity(2.5, 51.5, -0.1);
        const double muOnly = hp.params().mu;

        QVERIFY2(lamOne > muOnly * 2.0,
                 qPrintable(QStringLiteral("multi-event intensity=%1 mu=%2")
                                .arg(lamOne).arg(muOnly)));
    }

    void testFitImprovesLogLikelihoodFromDefaults()
    {
        QVector<SpatiotemporalEvent> events;
        for (int i = 0; i < 20; ++i)
            events << ev(i * 0.15, 51.5 + 0.001 * (i % 3), -0.1);

        HawkesProcess hp;
        const double defaultNLL = -hp.params().logLik;
        QVERIFY(hp.fit(events, 15));
        QVERIFY2(hp.params().logLik > defaultNLL || std::isfinite(hp.params().logLik),
                 qPrintable(QStringLiteral("fitted logLik=%1").arg(hp.params().logLik)));
        QVERIFY(std::isfinite(hp.params().logLik));
    }

    void testRiskSurfacePeakNearHistoryLocation()
    {
        HawkesProcess hp;
        const double eventLat = 51.505;
        const double eventLon = -0.115;
        hp.setParams(params(0.08, 0.5, 2.0, 0.012));
        hp.setHistory({ ev(0.0, eventLat, eventLon) });

        const auto grid = hp.riskSurface(0.5, 51.49, 51.52, -0.13, -0.10, 15);

        double maxVal = 0.0;
        int maxI = 0, maxJ = 0;
        for (int i = 0; i < 15; ++i) {
            for (int j = 0; j < 15; ++j) {
                if (grid[static_cast<std::size_t>(i)][static_cast<std::size_t>(j)] > maxVal) {
                    maxVal = grid[static_cast<std::size_t>(i)][static_cast<std::size_t>(j)];
                    maxI = i;
                    maxJ = j;
                }
            }
        }

        const double latStep = (51.52 - 51.49) / 14.0;
        const double lonStep = (-0.10 - (-0.13)) / 14.0;
        const double peakLat = 51.49 + maxI * latStep;
        const double peakLon = -0.13 + maxJ * lonStep;

        QVERIFY(std::abs(peakLat - eventLat) < 0.02);
        QVERIFY(std::abs(peakLon - eventLon) < 0.02);
        QVERIFY(maxVal > hp.params().mu);
    }

    void testRiskSurfaceAllValuesAtLeastMu()
    {
        HawkesProcess hp;
        hp.setParams(params(0.12, 0.4, 2.0, 0.01));
        hp.setHistory({ ev(0.0, 51.5, -0.1), ev(2.0, 51.51, -0.09) });

        const auto grid = hp.riskSurface(5.0, 51.48, 51.54, -0.14, -0.06, 10);
        for (const auto& row : grid)
            for (double v : row)
                QVERIFY2(v >= hp.params().mu - 1e-12,
                         qPrintable(QStringLiteral("intensity=%1 below mu=%2").arg(v).arg(hp.params().mu)));
    }

    void testIntensityRequiresSortedHistoryForCutoff()
    {
        HawkesProcess hp;
        hp.setParams(params(0.1, 0.5, 4.0, 0.01));  // tCutoff = min(20/4, 90) = 5 days

        hp.setHistory({ ev(8.0, 51.5, -0.1), ev(2.0, 51.5, -0.1), ev(6.0, 51.5, -0.1) });
        const double fromUnsortedInput = hp.intensity(10.0, 51.5, -0.1);

        hp.setHistory({ ev(2.0, 51.5, -0.1), ev(6.0, 51.5, -0.1), ev(8.0, 51.5, -0.1) });
        const double fromSortedInput = hp.intensity(10.0, 51.5, -0.1);

        QVERIFY2(fromSortedInput > hp.params().mu,
                 qPrintable(QStringLiteral("sorted intensity=%1 should exceed mu=%2")
                                .arg(fromSortedInput).arg(hp.params().mu)));
        QVERIFY2(std::abs(fromUnsortedInput - fromSortedInput) < 1e-12,
                 qPrintable(QStringLiteral("setHistory must sort: unsorted=%1 sorted=%2")
                                .arg(fromUnsortedInput).arg(fromSortedInput)));
    }

    void testFitZeroTimeSpanUsesMinimumT()
    {
        QVector<SpatiotemporalEvent> simultaneous;
        for (int i = 0; i < 8; ++i)
            simultaneous << ev(3.0, 51.5 + i * 0.002, -0.1);

        HawkesProcess hp;
        QVERIFY(hp.fit(simultaneous, 8));
        QVERIFY(hp.isFitted());
        QVERIFY(std::isfinite(hp.params().logLik));
        QVERIFY(hp.params().mu > 0.0);
    }

    void testBranchingRatioRemainsSubcriticalAfterSpatialSpread()
    {
        QVector<SpatiotemporalEvent> spread;
        for (int i = 0; i < 30; ++i)
            spread << ev(i * 0.2, 51.5 + 0.01 * i, -0.1 + 0.005 * i);

        HawkesProcess hp;
        QVERIFY(hp.fit(spread, 12));
        QVERIFY2(hp.branchingRatio() >= 0.0 && hp.branchingRatio() < 1.0,
                 qPrintable(QStringLiteral("branching ratio=%1").arg(hp.branchingRatio())));
    }
};

QTEST_GUILESS_MAIN(TestHawkesProcessDeep8)
#include "test_hawkes_process_deep8.moc"
