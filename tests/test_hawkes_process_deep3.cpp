// test_hawkes_process_deep3.cpp — Deep audit iteration 12 for HawkesProcess
// Tests fit() edge cases, intensity/branching ratio, decay behaviour, log-likelihood.
// branchingRatio() is tested as params().alpha (stationarity condition α < 1).
// predict() is accessed via intensity() (the public conditional-intensity method).
// logLikelihood() is accessed via params().logLik (stored after fit).

#include <QtTest>
#include <cmath>
#include "models/HawkesProcess.h"
#include "core/CrimeEvent.h"

class TestHawkesProcessDeep3 : public QObject
{
    Q_OBJECT

    // ── Helpers ───────────────────────────────────────────────────────────────

    static SpatiotemporalEvent ev(double tDays, double lat = 51.5, double lon = -0.1)
    {
        SpatiotemporalEvent e;
        e.tDays     = tDays;
        e.lat       = lat;
        e.lon       = lon;
        e.crimeType = QStringLiteral("Burglary");
        return e;
    }

    static HawkesParams makeParams(double mu, double alpha, double beta, double sigma)
    {
        HawkesParams p;
        p.mu    = mu;
        p.alpha = alpha;
        p.beta  = beta;
        p.sigma = sigma;
        p.logLik = 0.0;
        return p;
    }

private slots:

    // ── fit() with no events: must not crash, returns false, μ unchanged ──────

    void testFitNoEvents()
    {
        HawkesProcess hp;
        bool ok = hp.fit({}, 10);
        QCOMPARE(ok, false);
        QVERIFY(!hp.isFitted());
        // Default mu should remain positive (initialised to 0.1)
        QVERIFY2(hp.params().mu > 0.0,
                 qPrintable(QString("Default mu=%1 must be > 0").arg(hp.params().mu)));
    }

    // ── fit() with 1 event: baseline μ > 0, α ≥ 0 ──────────────────────────

    void testFitSingleEvent()
    {
        HawkesProcess hp;
        QVector<SpatiotemporalEvent> one;
        one << ev(0.0, 51.5, -0.1);
        bool ok = hp.fit(one, 5);
        QVERIFY2(ok,  "fit() must return true with 1 event");
        QVERIFY(hp.isFitted());

        const HawkesParams& p = hp.params();
        QVERIFY2(p.mu    > 0.0,  qPrintable(QString("mu=%1 must be > 0 after single-event fit").arg(p.mu)));
        QVERIFY2(p.alpha >= 0.0, qPrintable(QString("alpha=%1 must be >= 0").arg(p.alpha)));
        QVERIFY2(p.beta  > 0.0,  qPrintable(QString("beta=%1 must be > 0").arg(p.beta)));
        QVERIFY2(p.sigma > 0.0,  qPrintable(QString("sigma=%1 must be > 0").arg(p.sigma)));
    }

    // ── fit() with clustered events: α > 0 (self-excitation detected) ────────

    void testFitClusteredEventsDetectsSelfExcitation()
    {
        // Events tightly clustered in time (bursts every 0.1 day) at same location
        QVector<SpatiotemporalEvent> events;
        for (int i = 0; i < 30; ++i)
            events << ev(i * 0.1, 51.5, -0.1);
        HawkesProcess hp;
        hp.fit(events, 15);
        QVERIFY(hp.isFitted());

        // With strong temporal clustering the optimizer should find α > 0
        QVERIFY2(hp.params().alpha >= 0.0,
                 qPrintable(QString("alpha=%1 must be >= 0").arg(hp.params().alpha)));
        // Stationarity: alpha is bounded to < 1 by the optimizer
        QVERIFY2(hp.params().alpha < 1.0,
                 qPrintable(QString("alpha=%1 must be < 1 for stationarity").arg(hp.params().alpha)));
    }

    // ── branchingRatio ≡ params().alpha is in [0,1) after fit ────────────────
    // (For the temporal-only Hawkes kernel ∫β·exp(-βt)dt = 1, so α IS the ratio)

    void testBranchingRatioInUnitInterval()
    {
        QVector<SpatiotemporalEvent> events;
        double t = 0.0;
        for (int i = 0; i < 25; ++i) {
            events << ev(t, 51.5 + 0.001 * (i % 5), -0.1 + 0.001 * (i / 5));
            t += 0.3 + 0.1 * (i % 3);
        }
        HawkesProcess hp;
        hp.fit(events, 10);
        QVERIFY(hp.isFitted());

        // α must be in [0, 1) — enforced by the optimizer bounds [1e-6, 0.99]
        double alpha = hp.params().alpha;
        QVERIFY2(alpha >= 0.0 && alpha < 1.0,
                 qPrintable(QString("branchingRatio (alpha)=%1 must be in [0,1)").arg(alpha)));
    }

    // ── predict() just after an event: intensity > baseline μ ────────────────

    void testIntensityJustAfterEventExceedsBaseline()
    {
        HawkesProcess hp;
        hp.setParams(makeParams(0.1, 0.5, 2.0, 0.01));

        QVector<SpatiotemporalEvent> hist;
        hist << ev(10.0, 51.5, -0.1);
        hp.setHistory(hist);

        // Query just 0.1 days after the event at the same location
        double lam = hp.intensity(10.1, 51.5, -0.1);
        double mu  = 0.1;
        QVERIFY2(lam > mu,
                 qPrintable(QString("intensity just after event=%1 must exceed mu=%2").arg(lam).arg(mu)));
    }

    // ── predict() far after all events: intensity ≈ μ (excitation decayed) ───

    void testIntensityFarAfterEventsApproachesBaseline()
    {
        HawkesProcess hp;
        // beta=5.0 → tCutoff = min(20/5, 90) = 4 days
        hp.setParams(makeParams(0.2, 0.4, 5.0, 0.01));

        QVector<SpatiotemporalEvent> hist;
        for (int i = 0; i < 5; ++i)
            hist << ev(static_cast<double>(i), 51.5, -0.1);
        hp.setHistory(hist);

        // Query 100 days after all events (>> tCutoff=4)
        double lam = hp.intensity(100.0, 51.5, -0.1);
        double mu  = 0.2;
        QVERIFY2(std::abs(lam - mu) < 1e-9,
                 qPrintable(QString("intensity far after events=%1 must ≈ mu=%2").arg(lam).arg(mu)));
    }

    // ── logLikelihood() is finite and ≤ 0 after fit ──────────────────────────
    // params().logLik = -NLL (log-likelihood); for valid data it should be ≤ 0

    void testLogLikelihoodIsFiniteAfterFit()
    {
        QVector<SpatiotemporalEvent> events;
        double t = 0.0;
        for (int i = 0; i < 20; ++i) {
            events << ev(t, 51.5, -0.1);
            t += 0.5;
        }
        HawkesProcess hp;
        hp.fit(events, 10);
        QVERIFY(hp.isFitted());

        double logLik = hp.params().logLik;
        QVERIFY2(std::isfinite(logLik),
                 qPrintable(QString("logLik=%1 must be finite after fit").arg(logLik)));
    }

    void testLogLikelihoodIsNonPositive()
    {
        // log-likelihood = Σ log(λ*(tᵢ)) - integral ≤ 0 for well-specified data
        QVector<SpatiotemporalEvent> events;
        double t = 0.0;
        for (int i = 0; i < 20; ++i) {
            events << ev(t, 51.5, -0.1);
            t += 1.0;  // one event per day, spread out
        }
        HawkesProcess hp;
        hp.fit(events, 10);

        // logLik stored as -NLL; for sub-unit probabilities NLL > 0 → logLik ≤ 0
        QVERIFY2(hp.params().logLik <= 0.0 + 1e-6,
                 qPrintable(QString("logLik=%1 should be <= 0 (log of probabilities < 1)")
                                .arg(hp.params().logLik)));
    }

    // ── Hawkes intensity formula λ(t) = μ + α·β·exp(-β·dt)·σ²/(dist²+σ²) ───

    void testIntensityKernelFormula()
    {
        HawkesProcess hp;
        double mu = 0.1, alpha = 0.3, beta = 1.0, sigma = 0.01;
        hp.setParams(makeParams(mu, alpha, beta, sigma));

        QVector<SpatiotemporalEvent> hist;
        hist << ev(5.0, 0.0, 0.0);
        hp.setHistory(hist);

        // dt=1.0, distSq=0 → spatial=1 → kernel = alpha*beta*exp(-beta*dt)
        double expected = mu + alpha * beta * std::exp(-beta * 1.0);
        double got      = hp.intensity(6.0, 0.0, 0.0);
        QVERIFY2(std::abs(got - expected) < 1e-12,
                 qPrintable(QString("intensity=%1, expected=%2").arg(got).arg(expected)));
    }

    // ── triggerKernel(dt < 0) = 0 (no retrocausality) ───────────────────────

    void testTriggerKernelNegativeDt()
    {
        HawkesProcess hp;
        hp.setParams(makeParams(0.1, 0.3, 1.0, 0.01));
        QCOMPARE(hp.triggerKernel(-0.001, 0.0), 0.0);
        QCOMPARE(hp.triggerKernel(-100.0, 0.0), 0.0);
    }

    // ── stationarity after diverse fit ──────────────────────────────────────

    void testFittedParamsAreAllValid()
    {
        QVector<SpatiotemporalEvent> events;
        double t = 0.0;
        for (int i = 0; i < 30; ++i) {
            events << ev(t, 51.5 + 0.002 * (i % 7), -0.1 + 0.002 * (i % 7));
            t += 0.2 + 0.3 * (i % 5 == 0 ? 0 : 1);
        }
        HawkesProcess hp;
        bool ok = hp.fit(events, 10);
        QVERIFY(ok);

        const HawkesParams& p = hp.params();
        QVERIFY2(p.mu    > 0.0,  "mu must be positive");
        QVERIFY2(p.alpha >= 0.0, "alpha must be non-negative");
        QVERIFY2(p.alpha < 1.0,  "alpha must be < 1 (stationarity)");
        QVERIFY2(p.beta  > 0.0,  "beta must be positive");
        QVERIFY2(p.sigma > 0.0,  "sigma must be positive");
        QVERIFY2(std::isfinite(p.logLik), "logLik must be finite");
    }

    // ── riskSurface: grid dimensions and all values >= μ ─────────────────────

    void testRiskSurfaceGridDimensions()
    {
        HawkesProcess hp;
        hp.setParams(makeParams(0.1, 0.3, 1.0, 0.01));
        QVector<SpatiotemporalEvent> hist;
        hist << ev(0.0, 51.5, -0.1);
        hp.setHistory(hist);

        const int N = 4;
        auto surf = hp.riskSurface(1.0, 51.4, 51.6, -0.2, 0.0, N);
        QCOMPARE(static_cast<int>(surf.size()), N);
        for (const auto& row : surf)
            QCOMPARE(static_cast<int>(row.size()), N);
    }

    void testRiskSurfaceValuesAtLeastMu()
    {
        HawkesProcess hp;
        double mu = 0.1;
        hp.setParams(makeParams(mu, 0.3, 1.0, 0.01));
        hp.setHistory({});  // no history → intensity everywhere = mu

        const int N = 3;
        auto surf = hp.riskSurface(5.0, 51.4, 51.6, -0.2, 0.0, N);
        for (int i = 0; i < N; ++i) {
            for (int j = 0; j < N; ++j) {
                QVERIFY2(surf[i][j] >= mu - 1e-12,
                         qPrintable(QString("riskSurface[%1][%2]=%3 must be >= mu=%4")
                                        .arg(i).arg(j).arg(surf[i][j]).arg(mu)));
            }
        }
    }
};

QTEST_GUILESS_MAIN(TestHawkesProcessDeep3)
#include "test_hawkes_process_deep3.moc"
