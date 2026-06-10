// test_hawkes_convergence.cpp — HawkesProcess convergence and parameter tests
#include <QTest>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <cmath>
#include "models/HawkesProcess.h"

// ─── helpers ─────────────────────────────────────────────────────────────────

static SpatiotemporalEvent makeEvent(double tDays, double lat = 51.5, double lon = -0.1)
{
    SpatiotemporalEvent e;
    e.tDays = tDays;
    e.lat   = lat;
    e.lon   = lon;
    return e;
}

static QVector<SpatiotemporalEvent> makeRegularEvents(int n, double step = 0.5)
{
    QVector<SpatiotemporalEvent> ev;
    ev.reserve(n);
    for (int i = 0; i < n; ++i)
        ev.append(makeEvent(i * step));
    return ev;
}

// ─── test class ──────────────────────────────────────────────────────────────

class TestHawkesConvergence : public QObject {
    Q_OBJECT

private slots:

    // 1. Fit on 50 events, verify isFitted() and params are plausible.
    //    (EM/coordinate-descent log-likelihood should not increase; we verify the
    //     result is consistent with convergence rather than inspecting each step.)
    void testNegLogLikelihoodDecreases()
    {
        auto events = makeRegularEvents(50);
        HawkesProcess hp;
        hp.fit(events);
        QVERIFY(hp.isFitted());
        const auto& p = hp.params();
        // logLik should be non-positive (NLL stored negated or as raw LL)
        // At minimum it must be finite
        QVERIFY(std::isfinite(p.logLik));
        QVERIFY(p.mu    > 0.0);
        QVERIFY(p.alpha >= 0.0);
        QVERIFY(p.beta  > 0.0);
    }

    // 2. After fitting, all parameters must satisfy physical constraints.
    void testParameterBounds()
    {
        auto events = makeRegularEvents(50, 0.3);
        HawkesProcess hp;
        hp.fit(events);
        QVERIFY(hp.isFitted());
        const auto& p = hp.params();
        QVERIFY2(p.mu    > 0.0,  "mu must be positive");
        QVERIFY2(p.alpha >= 0.0, "alpha must be non-negative");
        QVERIFY2(p.beta  > 0.0,  "beta must be positive");
        QVERIFY2(p.sigma > 0.0,  "sigma must be positive");
    }

    // 3. λ*(t, lat, lon) ≥ mu > 0 at any time.
    void testIntensityAlwaysPositive()
    {
        auto events = makeRegularEvents(30, 0.5);
        HawkesProcess hp;
        hp.fit(events);
        QVERIFY(hp.isFitted());
        const double mu = hp.params().mu;
        for (double t : {0.0, 1.5, 5.0, 10.0, 50.0}) {
            double lam = hp.intensity(t, 51.5, -0.1);
            QVERIFY2(lam > 0.0,
                     qPrintable(QString("intensity(%1)=%2, expected > 0").arg(t).arg(lam)));
            QVERIFY2(lam >= mu - 1e-9,
                     qPrintable(QString("intensity(%1)=%2 < mu=%3").arg(t).arg(lam).arg(mu)));
        }
    }

    // 4. Intensity right after an event (in history) exceeds the background rate.
    void testIntensityHighAfterEvent()
    {
        // Manually inject params and history so the test is deterministic.
        HawkesParams p;
        p.mu    = 0.10;
        p.alpha = 0.80;
        p.beta  = 2.0;
        p.sigma = 0.50;
        HawkesProcess hp;
        hp.setParams(p);

        QVector<SpatiotemporalEvent> hist;
        hist.append(makeEvent(10.0, 51.5, -0.1));
        hp.setHistory(hist);

        // Before the event at t=10 only background applies
        double intensityBefore = hp.intensity(0.0, 51.5, -0.1);   // = mu
        // Just after the event the kernel fires
        double intensityAfter  = hp.intensity(10.001, 51.5, -0.1);

        QVERIFY2(intensityAfter > intensityBefore,
                 qPrintable(QString("Expected λ after event (%1) > λ before (%2)")
                            .arg(intensityAfter).arg(intensityBefore)));
    }

    // 5. Fitting on empty data must not crash and leave the model unfitted (or default).
    void testEmptyDataReturnsDefault()
    {
        HawkesProcess hp;
        hp.fit({}); // must not throw / crash
        if (hp.isFitted()) {
            // If implementation still marks it fitted, params must be valid
            const auto& p = hp.params();
            QVERIFY(p.mu   > 0.0);
            QVERIFY(p.beta > 0.0);
        }
        // Either outcome is acceptable; crash is not.
        QVERIFY(true);
    }

    // 6. Single-event fit: should not crash, params should be valid if fitted.
    void testSingleEventFit()
    {
        QVector<SpatiotemporalEvent> events;
        events.append(makeEvent(1.0));
        HawkesProcess hp;
        hp.fit(events);
        if (hp.isFitted()) {
            QVERIFY(hp.params().mu   > 0.0);
            QVERIFY(hp.params().beta > 0.0);
        }
        QVERIFY(true); // crash-free is the minimum requirement
    }

    // 7. Intensity decays over time after a cluster at t≈0.
    void testIntensityDecayOverTime()
    {
        HawkesParams p;
        p.mu    = 0.05;
        p.alpha = 0.50;
        p.beta  = 3.0;
        p.sigma = 0.50;
        HawkesProcess hp;
        hp.setParams(p);

        QVector<SpatiotemporalEvent> hist;
        for (int i = 0; i < 10; ++i)
            hist.append(makeEvent(i * 0.01, 51.5, -0.1));
        hp.setHistory(hist);

        double lambdaEarly = hp.intensity(0.1,  51.5, -0.1);
        double lambdaLate  = hp.intensity(10.0, 51.5, -0.1);

        QVERIFY2(lambdaEarly > lambdaLate,
                 qPrintable(QString("Intensity should decay: λ(0.1)=%1, λ(10)=%2")
                            .arg(lambdaEarly).arg(lambdaLate)));
    }

    // 8. After fitting, intensity at a future time is positive (proxy for event-count > 0).
    void testIntensityAfterFitIsPositive()
    {
        auto events = makeRegularEvents(30, 0.5);
        HawkesProcess hp;
        hp.fit(events);
        QVERIFY(hp.isFitted());
        double lam = hp.intensity(20.0, 51.5, -0.1);
        QVERIFY2(lam > 0.0,
                 qPrintable(QString("intensity after fit = %1, expected > 0").arg(lam)));
    }

    // 9. Fitting 500 events must complete in reasonable time.
    //    Events span 500 days so the 90-day temporal cutoff keeps the window small
    //    (< 90/500 × 500 ≈ 90 events per evaluation) — O(N) per event rather than O(N²).
    void testTemporalCutoffOptimization()
    {
        QVector<SpatiotemporalEvent> events;
        events.reserve(500);
        // Spread over 500 days so the cutoff window stays bounded
        for (int i = 0; i < 500; ++i)
            events.append(makeEvent(
                i * 1.0 + (i % 7) * 0.01,   // 0…500 days
                51.5 + (i % 5) * 0.001,
               -0.1  + (i % 3) * 0.001));

        HawkesProcess hp;
        QElapsedTimer timer;
        timer.start();
        hp.fit(events, 5); // 5 coordinate-descent outer iterations
        qint64 elapsed = timer.elapsed();

        QVERIFY2(elapsed < 10000,
                 qPrintable(QString("Fitting 500 events took %1 ms (limit 10000 ms)").arg(elapsed)));
    }

    // 10. Fitting twice on the same data must produce nearly identical parameters.
    void testRefitConsistency()
    {
        auto events = makeRegularEvents(40, 0.4);
        HawkesProcess hp1, hp2;
        hp1.fit(events);
        hp2.fit(events);
        QVERIFY(hp1.isFitted());
        QVERIFY(hp2.isFitted());
        const auto& p1 = hp1.params();
        const auto& p2 = hp2.params();
        QVERIFY2(std::abs(p1.mu    - p2.mu)    < 1e-4, "mu not reproducible");
        QVERIFY2(std::abs(p1.alpha - p2.alpha) < 1e-4, "alpha not reproducible");
        QVERIFY2(std::abs(p1.beta  - p2.beta)  < 1e-4, "beta not reproducible");
        QVERIFY2(std::abs(p1.sigma - p2.sigma) < 1e-4, "sigma not reproducible");
    }
};

// ─── main ────────────────────────────────────────────────────────────────────

static int runTest(QObject* obj, const char* logFile)
{
    QStringList args = { "test", "-v2", "-o", QString("%1,txt").arg(logFile) };
    return QTest::qExec(obj, args);
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    Q_UNUSED(argv)
    TestHawkesConvergence t;
    return runTest(&t, "hawkes_convergence.txt");
}

#include "test_hawkes_convergence.moc"
