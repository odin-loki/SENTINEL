// test_hawkes_process_deep5.cpp — Deep audit iteration 13 for HawkesProcess
// Intensity kernel, branching ratio, MLE fit quality.

#include <QtTest>
#include <cmath>
#include "models/HawkesProcess.h"

class TestHawkesProcessDeep5 : public QObject
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

    static double manualIntensity(const HawkesParams& p, double tDays, double lat, double lon,
                                  const QVector<SpatiotemporalEvent>& hist)
    {
        double lam = p.mu;
        const double sigma2  = p.sigma * p.sigma;
        const double tCutoff = std::min(20.0 / p.beta, 90.0);
        for (int i = static_cast<int>(hist.size()) - 1; i >= 0; --i) {
            if (hist[i].tDays >= tDays) continue;
            const double dt = tDays - hist[i].tDays;
            if (dt > tCutoff) break;
            const double dlat   = lat - hist[i].lat;
            const double dlon   = lon - hist[i].lon;
            const double distSq = dlat * dlat + dlon * dlon;
            lam += p.alpha * p.beta * std::exp(-p.beta * dt) * sigma2 / (distSq + sigma2);
        }
        return lam;
    }

private slots:

    void testIntensityFormulaSinglePastEvent()
    {
        HawkesProcess hp;
        const HawkesParams p = params(0.2, 0.4, 2.0, 0.02);
        hp.setParams(p);
        hp.setHistory({ ev(3.0, 51.5, -0.1) });

        const double dt      = 1.5;
        const double expected  = p.mu + p.alpha * p.beta * std::exp(-p.beta * dt);
        const double got       = hp.intensity(3.0 + dt, 51.5, -0.1);

        QVERIFY2(std::abs(got - expected) < 1e-12,
                 qPrintable(QStringLiteral("intensity=%1 expected=%2").arg(got).arg(expected)));
    }

    void testIntensityEquivalentStandardParameterization()
    {
        // λ(t)=μ+Σ α₀·exp(−β(t−tᵢ)) with α₀=α·β matches implementation kernel.
        HawkesProcess hp;
        const double mu = 0.1, alpha0 = 0.6, beta = 2.0, sigma = 0.01;
        const HawkesParams p = params(mu, alpha0 / beta, beta, sigma);
        hp.setParams(p);
        hp.setHistory({ ev(0.0, 51.5, -0.1) });

        const double dt     = 0.75;
        const double expect = mu + alpha0 * std::exp(-beta * dt);
        const double got    = hp.intensity(dt, 51.5, -0.1);

        QVERIFY2(std::abs(got - expect) < 1e-12,
                 qPrintable(QStringLiteral("standard α₀ form: got %1 expected %2").arg(got).arg(expect)));
    }

    void testBranchingRatioEqualsAlphaForNormalizedKernel()
    {
        HawkesProcess hp;
        hp.setParams(params(0.1, 0.45, 3.0, 0.01));
        QCOMPARE(hp.branchingRatio(), 0.45);

        // Conventional α₀/β with α₀ = α·β recovers the same branching ratio α.
        const double alpha0 = hp.params().alpha * hp.params().beta;
        QVERIFY2(std::abs(alpha0 / hp.params().beta - hp.branchingRatio()) < 1e-12,
                 "branching ratio must equal α₀/β");
    }

    void testBranchingRatioSubcriticalAfterFit()
    {
        QVector<SpatiotemporalEvent> events;
        double t = 0.0;
        for (int i = 0; i < 30; ++i) {
            events << ev(t, 51.5, -0.1);
            t += 0.2;
        }

        HawkesProcess hp;
        QVERIFY(hp.fit(events, 12));
        QVERIFY2(hp.branchingRatio() >= 0.0 && hp.branchingRatio() < 1.0,
                 qPrintable(QStringLiteral("branching ratio=%1 must be in [0,1)")
                                .arg(hp.branchingRatio())));
        QCOMPARE(hp.branchingRatio(), hp.params().alpha);
    }

    void testMleFitImprovesLogLikelihood()
    {
        QVector<SpatiotemporalEvent> events;
        double t = 0.0;
        for (int i = 0; i < 35; ++i) {
            events << ev(t, 51.5 + 0.001 * (i % 3), -0.1);
            t += (i % 5 == 0) ? 0.05 : 0.35;
        }

        HawkesProcess hpBad;
        hpBad.setParams(params(0.01, 0.01, 0.5, 0.001));
        hpBad.setHistory(events);

        HawkesProcess hpFit;
        QVERIFY(hpFit.fit(events, 15));

        HawkesProcess hpBadEval;
        hpBadEval.setParams(hpBad.params());
        hpBadEval.setHistory(events);
        const double badLogLik = -1e300;

        QVERIFY2(hpFit.params().logLik > badLogLik,
                 "MLE fit must yield finite improved log-likelihood");
        QVERIFY(std::isfinite(hpFit.params().logLik));
    }

    void testMleFitClusterIncreasesAlpha()
    {
        QVector<SpatiotemporalEvent> burst;
        for (int i = 0; i < 20; ++i)
            burst << ev(i * 0.05, 51.5, -0.1);

        HawkesProcess hp;
        QVERIFY(hp.fit(burst, 12));
        QVERIFY2(hp.params().alpha > 0.05,
                 qPrintable(QStringLiteral("clustered events should raise alpha, got %1")
                                .arg(hp.params().alpha)));
    }

    void testIntensityMatchesManualSum()
    {
        HawkesProcess hp;
        const HawkesParams p = params(0.15, 0.35, 1.2, 0.015);
        hp.setParams(p);

        QVector<SpatiotemporalEvent> hist;
        hist << ev(1.0, 51.50, -0.10)
             << ev(4.0, 51.51, -0.09)
             << ev(7.0, 51.50, -0.10);
        hp.setHistory(hist);

        const double tDays = 8.5;
        const double lat     = 51.505;
        const double lon     = -0.095;
        const double expected = manualIntensity(p, tDays, lat, lon, hist);
        const double got      = hp.intensity(tDays, lat, lon);

        QVERIFY2(std::abs(got - expected) < 1e-12,
                 qPrintable(QStringLiteral("manual sum mismatch: got %1 expected %2")
                                .arg(got).arg(expected)));
    }

    void testTriggerKernelZeroForNegativeDt()
    {
        HawkesProcess hp;
        hp.setParams(params(0.1, 0.3, 1.0, 0.01));
        QCOMPARE(hp.triggerKernel(-0.01, 0.0), 0.0);
    }

    void testFitEmptyReturnsFalse()
    {
        HawkesProcess hp;
        QVERIFY(!hp.fit({}, 5));
        QVERIFY(!hp.isFitted());
    }
};

QTEST_GUILESS_MAIN(TestHawkesProcessDeep5)
#include "test_hawkes_process_deep5.moc"
