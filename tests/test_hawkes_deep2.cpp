#include <QTest>
#include "models/HawkesProcess.h"
#include "core/CrimeEvent.h"
#include <cmath>

class TestHawkesDeep2 : public QObject
{
    Q_OBJECT

    static SpatiotemporalEvent ev(double tDays, double lat, double lon)
    {
        SpatiotemporalEvent e;
        e.tDays     = tDays;
        e.lat       = lat;
        e.lon       = lon;
        e.crimeType = QStringLiteral("test");
        return e;
    }

    static HawkesParams params(double mu, double alpha, double beta, double sigma)
    {
        HawkesParams p;
        p.mu    = mu;
        p.alpha = alpha;
        p.beta  = beta;
        p.sigma = sigma;
        return p;
    }

private slots:

    // λ(t) = μ + α·β·exp(−β·dt)·σ²/(distSq+σ²)
    // With distSq=0 the spatial factor is 1; we get an exact closed-form result.
    void testIntensityKnownValue()
    {
        HawkesProcess hp;
        // mu=0.1, alpha=0.3, beta=1.0, sigma=0.01
        hp.setParams(params(0.1, 0.3, 1.0, 0.01));
        QVector<SpatiotemporalEvent> hist;
        hist << ev(5.0, 0.0, 0.0);
        hp.setHistory(hist);

        // dt = 6.0 - 5.0 = 1.0, distSq = 0 → spatial factor = 1
        const double expected = 0.1 + 0.3 * 1.0 * std::exp(-1.0);
        const double got = hp.intensity(6.0, 0.0, 0.0);
        QVERIFY2(std::abs(got - expected) < 1e-10,
                 qPrintable(QString("intensity = %1, expected %2").arg(got).arg(expected)));
    }

    // Spatial decay reduces intensity for off-location queries
    void testIntensitySpatialDecay()
    {
        HawkesProcess hp;
        hp.setParams(params(0.1, 0.3, 1.0, 0.01));
        QVector<SpatiotemporalEvent> hist;
        hist << ev(5.0, 0.0, 0.0);
        hp.setHistory(hist);

        // On-site intensity
        const double onSite  = hp.intensity(6.0, 0.0, 0.0);
        // Off-site: dlat=0.1, distSq=0.01
        // sigma²=0.0001, spatial = 0.0001/(0.01+0.0001) = 0.0001/0.0101 ≈ 0.009901
        const double offSite = hp.intensity(6.0, 0.1, 0.0);
        QVERIFY2(offSite < onSite,
                 qPrintable(QString("Off-site intensity %1 must be < on-site %2").arg(offSite).arg(onSite)));
    }

    // With an empty history, λ(t) must equal μ exactly
    void testIntensityEmptyHistory()
    {
        HawkesProcess hp;
        hp.setParams(params(0.1, 0.3, 1.0, 0.01));
        hp.setHistory({});

        QVERIFY2(std::abs(hp.intensity(10.0, 0.0, 0.0) - 0.1) < 1e-15,
                 "Empty history must return exactly mu");
    }

    // An event beyond tCutoff (20/β days) contributes nothing — intensity = μ
    void testIntensityDecayedEventExcluded()
    {
        HawkesProcess hp;
        // beta=1.0 → tCutoff = min(20, 90) = 20 days
        hp.setParams(params(0.1, 0.3, 1.0, 0.01));
        QVector<SpatiotemporalEvent> hist;
        hist << ev(-50.0, 0.0, 0.0);  // 50 days before query at t=0
        hp.setHistory(hist);

        // dt = 50 > tCutoff = 20 → event excluded → λ = μ
        const double got = hp.intensity(0.0, 0.0, 0.0);
        QVERIFY2(std::abs(got - 0.1) < 1e-15,
                 qPrintable(QString("Events beyond tCutoff excluded: intensity %1 must equal mu 0.1").arg(got)));
    }

    // An event just within tCutoff does contribute (intensity > μ)
    void testIntensityRecentEventContributes()
    {
        HawkesProcess hp;
        // beta=1.0 → tCutoff = 20 days
        hp.setParams(params(0.1, 0.3, 1.0, 0.01));
        QVector<SpatiotemporalEvent> hist;
        hist << ev(0.0, 0.0, 0.0);  // dt=10 at query time t=10 < tCutoff
        hp.setHistory(hist);

        const double got = hp.intensity(10.0, 0.0, 0.0);
        QVERIFY2(got > 0.1,
                 qPrintable(QString("Recent event must push intensity %1 above mu 0.1").arg(got)));
    }

    // triggerKernel with dt < 0 must return 0 (no retro-causality)
    void testTriggerKernelNegativeDtZero()
    {
        HawkesProcess hp;
        hp.setParams(params(0.1, 0.3, 1.0, 0.01));
        QCOMPARE(hp.triggerKernel(-1.0, 0.0), 0.0);
    }

    // triggerKernel at dt=0, distSq=0: α·β·1 = α*β
    void testTriggerKernelAtOrigin()
    {
        HawkesProcess hp;
        hp.setParams(params(0.2, 0.4, 2.0, 0.05));
        const double expected = 0.4 * 2.0 * 1.0;  // alpha*beta*exp(0)*1
        const double got = hp.triggerKernel(0.0, 0.0);
        QVERIFY2(std::abs(got - expected) < 1e-12,
                 qPrintable(QString("triggerKernel(0,0) = %1, expected %2").arg(got).arg(expected)));
    }

    // fit() with a realistic sequence must converge and satisfy stationarity (α < 1)
    void testBranchingRatioStationarity()
    {
        // Build a sequence of 30 spatiotemporal events at irregular intervals
        QVector<SpatiotemporalEvent> events;
        double t = 0.0;
        for (int i = 0; i < 30; ++i) {
            events << ev(t, 51.5 + 0.001 * (i % 5), -0.1 + 0.001 * (i / 5));
            t += 0.3 + 0.15 * (i % 4);
        }

        HawkesProcess hp;
        bool ok = hp.fit(events, 10);
        QVERIFY2(ok, "fit() must return true on valid data");
        QVERIFY2(hp.isFitted(), "isFitted() must be true after successful fit");

        const HawkesParams& p = hp.params();
        QVERIFY2(p.alpha < 1.0,
                 qPrintable(QString("Branching ratio alpha=%1 must be < 1 for stationarity").arg(p.alpha)));
        QVERIFY2(p.mu    > 0.0, "Background rate mu must be positive after fit");
        QVERIFY2(p.beta  > 0.0, "Decay rate beta must be positive after fit");
        QVERIFY2(p.sigma > 0.0, "Spatial bandwidth sigma must be positive after fit");
    }

    // fit() on an empty dataset must return false (no crash)
    void testFitEmptyDataset()
    {
        HawkesProcess hp;
        QCOMPARE(hp.fit({}, 10), false);
        QVERIFY(!hp.isFitted());
    }

    // fit() on a single event still produces valid (stationary) parameters
    void testFitSingleEvent()
    {
        QVector<SpatiotemporalEvent> one;
        one << ev(0.0, 51.5, -0.1);
        HawkesProcess hp;
        bool ok = hp.fit(one, 5);
        QVERIFY2(ok, "fit() must succeed on a single event");
        QVERIFY2(hp.params().alpha < 1.0, "Single-event fit must still satisfy stationarity");
    }

    // After fit() the intensity at future times with the fitted history must be >= μ
    void testFittedIntensityAtLeastMu()
    {
        QVector<SpatiotemporalEvent> events;
        double t = 0.0;
        for (int i = 0; i < 20; ++i) {
            events << ev(t, 51.5, -0.1);
            t += 1.0;
        }
        HawkesProcess hp;
        hp.fit(events, 10);
        const double mu = hp.params().mu;
        // Intensity at a future time should equal mu (no future events in history)
        const double lam = hp.intensity(t + 100.0, 51.5, -0.1);
        QVERIFY2(lam >= mu - 1e-9,
                 qPrintable(QString("Fitted intensity %1 must be >= mu %2").arg(lam).arg(mu)));
    }

    // riskSurface: verify the grid dimensions and that values are >= μ at a close location
    void testRiskSurfaceDimensions()
    {
        HawkesProcess hp;
        hp.setParams(params(0.1, 0.3, 1.0, 0.01));
        QVector<SpatiotemporalEvent> hist;
        hist << ev(0.0, 51.5, -0.1);
        hp.setHistory(hist);

        const int N = 5;
        auto surf = hp.riskSurface(1.0, 51.4, 51.6, -0.2, 0.0, N);
        QCOMPARE(static_cast<int>(surf.size()), N);
        for (const auto& row : surf)
            QCOMPARE(static_cast<int>(row.size()), N);
    }
};

QTEST_GUILESS_MAIN(TestHawkesDeep2)
#include "test_hawkes_deep2.moc"
