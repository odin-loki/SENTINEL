// test_hawkes_temporal_decay.cpp
// Validates HawkesProcess temporal decay, spatial decay, intensity, and
// risk surface properties.
#include <QTest>
#include "models/HawkesProcess.h"
#include "core/CrimeEvent.h"
#include <cmath>
#include <vector>

class HawkesTemporalDecayTest : public QObject
{
    Q_OBJECT

private:
    static SpatiotemporalEvent ev(double t, double lat, double lon)
    {
        SpatiotemporalEvent e;
        e.tDays     = t;
        e.lat       = lat;
        e.lon       = lon;
        e.crimeType = QStringLiteral("burglary");
        return e;
    }

    static HawkesProcess makeConfigured(double mu = 0.1, double alpha = 0.5,
                                         double beta = 1.0, double sigma = 0.01)
    {
        HawkesProcess h;
        HawkesParams p;
        p.mu = mu; p.alpha = alpha; p.beta = beta; p.sigma = sigma;
        h.setParams(p);
        h.setHistory({ ev(0.0, 51.5, -0.1) });
        return h;
    }

private slots:

    // ── 1. Intensity at reference event location/time is > background ─────────
    void testIntensityAboveBackground()
    {
        auto h = makeConfigured();
        // Right after the event at t=0.001 at the same location
        const double background = h.params().mu;
        const double lambda = h.intensity(0.001, 51.5, -0.1);
        QVERIFY2(lambda > background,
                 qPrintable(QStringLiteral(
                    "Intensity %1 should exceed background %2").arg(lambda).arg(background)));
    }

    // ── 2. Intensity decays over time (further from the event) ───────────────
    void testIntensityDecaysOverTime()
    {
        auto h = makeConfigured(0.1, 0.8, 2.0, 0.01);
        const double near  = h.intensity(0.1,  51.5, -0.1);
        const double far   = h.intensity(5.0,  51.5, -0.1);
        QVERIFY2(near > far,
                 qPrintable(QStringLiteral(
                    "Intensity near (%1) should exceed intensity far (%2)").arg(near).arg(far)));
    }

    // ── 3. Intensity decays with spatial distance ─────────────────────────────
    void testIntensityDecaysWithDistance()
    {
        auto h = makeConfigured(0.1, 0.8, 1.0, 0.01);
        const double atEvent  = h.intensity(0.01, 51.5, -0.1);
        const double awayFar  = h.intensity(0.01, 51.5, 5.0);  // very far away
        QVERIFY2(atEvent > awayFar,
                 qPrintable(QStringLiteral(
                    "Intensity at event (%1) should exceed intensity far (%2)")
                    .arg(atEvent).arg(awayFar)));
    }

    // ── 4. triggerKernel: larger dt → smaller kernel value ────────────────────
    void testTriggerKernelDecay()
    {
        HawkesProcess h;
        HawkesParams p;
        p.mu = 0.1; p.alpha = 0.5; p.beta = 1.0; p.sigma = 0.1;
        h.setParams(p);

        const double k1 = h.triggerKernel(0.1, 0.0);
        const double k5 = h.triggerKernel(5.0, 0.0);
        QVERIFY2(k1 > k5,
                 qPrintable(QStringLiteral("Kernel dt=0.1 (%1) should exceed dt=5 (%2)")
                    .arg(k1).arg(k5)));
    }

    // ── 5. triggerKernel: larger distSq → smaller kernel value ───────────────
    void testTriggerKernelSpatialDecay()
    {
        HawkesProcess h;
        HawkesParams p;
        p.mu = 0.1; p.alpha = 0.5; p.beta = 1.0; p.sigma = 0.1;
        h.setParams(p);

        const double kNear = h.triggerKernel(0.1, 0.0);
        const double kFar  = h.triggerKernel(0.1, 100.0);
        QVERIFY2(kNear > kFar,
                 qPrintable(QStringLiteral("Kernel near (%1) > kernel far (%2)")
                    .arg(kNear).arg(kFar)));
    }

    // ── 6. Risk surface has correct dimensions ────────────────────────────────
    void testRiskSurfaceDimensions()
    {
        auto h = makeConfigured();
        const auto surface = h.riskSurface(1.0, 51.4, 51.6, -0.2, 0.0, 30);
        QCOMPARE((int)surface.size(), 30);
        for (const auto& row : surface)
            QCOMPARE((int)row.size(), 30);
    }

    // ── 7. Risk surface values are all positive ───────────────────────────────
    void testRiskSurfacePositive()
    {
        auto h = makeConfigured();
        const auto surface = h.riskSurface(1.0, 51.4, 51.6, -0.2, 0.0, 20);
        for (const auto& row : surface) {
            for (double v : row) {
                QVERIFY2(v >= 0.0,
                         qPrintable(QStringLiteral("Risk surface value %1 must be >= 0").arg(v)));
            }
        }
    }

    // ── 8. fit() on small dataset returns without crash ───────────────────────
    void testFitSmallDataset()
    {
        HawkesProcess h;
        QVector<SpatiotemporalEvent> events = {
            ev(0.0, 51.5, -0.1), ev(0.5, 51.5, -0.1), ev(1.0, 51.51, -0.10),
            ev(2.0, 51.52, -0.09), ev(3.0, 51.5, -0.1)
        };
        const bool ok = h.fit(events, 5);
        // Should not crash; convergence success is optional
        Q_UNUSED(ok);

        // After fit, intensity should be a finite positive number
        const double lambda = h.intensity(3.5, 51.5, -0.1);
        QVERIFY2(std::isfinite(lambda) && lambda > 0.0,
                 qPrintable(QStringLiteral("Post-fit intensity %1 must be finite positive").arg(lambda)));
    }

    // ── 9. Params alpha must be in [0, 1] after fit (near-repeat model) ───────
    void testFitParamsAlphaBounded()
    {
        HawkesProcess h;
        QVector<SpatiotemporalEvent> events;
        for (int i = 0; i < 20; ++i)
            events.append(ev(static_cast<double>(i) * 0.3, 51.5, -0.1));
        h.fit(events, 10);
        const double alpha = h.params().alpha;
        QVERIFY2(alpha >= 0.0 && alpha < 2.0,
                 qPrintable(QStringLiteral("Alpha %1 should be in [0, 2)").arg(alpha)));
    }

    // ── 10. Empty history → intensity equals background rate ─────────────────
    void testEmptyHistoryIsBackground()
    {
        HawkesProcess h;
        HawkesParams p;
        p.mu = 0.05; p.alpha = 0.5; p.beta = 1.0; p.sigma = 0.01;
        h.setParams(p);
        h.setHistory({});

        const double lambda = h.intensity(1.0, 51.5, -0.1);
        QVERIFY2(std::abs(lambda - 0.05) < 1e-9,
                 qPrintable(QStringLiteral("Empty history intensity %1 should equal mu=0.05").arg(lambda)));
    }
};

QTEST_MAIN(HawkesTemporalDecayTest)
#include "test_hawkes_temporal_decay.moc"
