// test_hawkes_advanced.cpp
// Advanced tests for HawkesProcess: intensity computation, risk surface sum,
// spatial/temporal decay effects, and parameter bounds.
#include <QTest>
#include "models/HawkesProcess.h"
#include "core/CrimeEvent.h"
#include <cmath>
#include <algorithm>
#include <numeric>

class HawkesAdvancedTest : public QObject
{
    Q_OBJECT

private:
    static SpatiotemporalEvent stev(double t, double lat = 51.5, double lon = -0.1)
    {
        SpatiotemporalEvent e;
        e.tDays     = t;
        e.lat       = lat;
        e.lon       = lon;
        e.crimeType = QStringLiteral("burglary");
        return e;
    }

    static QVector<SpatiotemporalEvent> cluster(double lat, double lon, int n, double start = 0.0)
    {
        QVector<SpatiotemporalEvent> evs;
        for (int i = 0; i < n; ++i) evs.append(stev(start + i, lat, lon));
        return evs;
    }

    static HawkesProcess fittedHP(const QVector<SpatiotemporalEvent>& history)
    {
        HawkesProcess hp;
        hp.setHistory(history);
        hp.fit(history, 5);
        return hp;
    }

private slots:

    // ── 1. Intensity at cluster location > intensity far away ────────────────
    void testIntensityHigherAtCluster()
    {
        auto history = cluster(51.5, -0.1, 8, 0.0);
        auto hp = fittedHP(history);

        const double iCluster = hp.intensity(8.5, 51.5, -0.1);
        const double iFar     = hp.intensity(8.5, 55.0,  5.0);

        QVERIFY2(iCluster >= iFar,
                 qPrintable(QStringLiteral("Cluster intensity %1 should >= far %2")
                    .arg(iCluster).arg(iFar)));
    }

    // ── 2. Intensity decays with time (same location) ────────────────────────
    void testIntensityDecaysWithTime()
    {
        auto history = cluster(51.5, -0.1, 5, 0.0);
        auto hp = fittedHP(history);

        const double i5  = hp.intensity(5.1, 51.5, -0.1);
        const double i10 = hp.intensity(10.0, 51.5, -0.1);
        const double i30 = hp.intensity(30.0, 51.5, -0.1);

        QVERIFY2(i5 >= i10,
                 qPrintable(QStringLiteral("t=5.1 (%1) >= t=10 (%2)").arg(i5).arg(i10)));
        QVERIFY2(i10 >= i30,
                 qPrintable(QStringLiteral("t=10 (%1) >= t=30 (%2)").arg(i10).arg(i30)));
    }

    // ── 3. Intensity is non-negative ─────────────────────────────────────────
    void testIntensityNonNegative()
    {
        auto history = cluster(51.5, -0.1, 6, 0.0);
        auto hp = fittedHP(history);

        for (double t = 0.0; t <= 20.0; t += 2.0)
            QVERIFY2(hp.intensity(t, 51.5, -0.1) >= 0.0,
                     qPrintable(QStringLiteral("Intensity at t=%1 should be >= 0").arg(t)));
    }

    // ── 4. triggerKernel decays with time ────────────────────────────────────
    void testTriggerKernelDecay()
    {
        HawkesProcess hp;
        hp.setParams({0.1, 0.5, 2.0, 0.01, 0.0});  // mu, alpha, beta, sigma
        const double k1 = hp.triggerKernel(0.5, 0.0);  // small dt
        const double k2 = hp.triggerKernel(2.0, 0.0);  // larger dt
        QVERIFY2(k1 >= k2,
                 qPrintable(QStringLiteral("Kernel at dt=0.5 (%1) should >= dt=2.0 (%2)")
                    .arg(k1).arg(k2)));
    }

    // ── 5. triggerKernel decays with spatial distance ────────────────────────
    void testTriggerKernelSpatialDecay()
    {
        HawkesProcess hp;
        hp.setParams({0.1, 0.5, 1.0, 0.01, 0.0});
        const double kNear = hp.triggerKernel(1.0, 0.0);    // at same location
        const double kFar  = hp.triggerKernel(1.0, 100.0);  // far away
        QVERIFY2(kNear >= kFar,
                 qPrintable(QStringLiteral("Kernel near (%1) should >= far (%2)")
                    .arg(kNear).arg(kFar)));
    }

    // ── 6. Risk surface dimensions correct ───────────────────────────────────
    void testRiskSurfaceDimensions()
    {
        auto hp = fittedHP(cluster(51.5, -0.1, 5, 0.0));
        const int n = 10;
        const auto surface = hp.riskSurface(5.0, 51.4, 51.6, -0.2, 0.0, n);
        QVERIFY2(static_cast<int>(surface.size()) == n,
                 qPrintable(QStringLiteral("Rows %1 expected %2")
                    .arg(surface.size()).arg(n)));
        for (const auto& row : surface)
            QVERIFY2(static_cast<int>(row.size()) == n,
                     qPrintable(QStringLiteral("Cols %1 expected %2")
                        .arg(row.size()).arg(n)));
    }

    // ── 7. Risk surface values non-negative ──────────────────────────────────
    void testRiskSurfaceNonNegative()
    {
        auto hp = fittedHP(cluster(51.5, -0.1, 5, 0.0));
        const auto surface = hp.riskSurface(5.0, 51.4, 51.6, -0.2, 0.0, 8);
        for (const auto& row : surface)
            for (double v : row)
                QVERIFY2(v >= 0.0,
                         qPrintable(QStringLiteral("Risk surface value %1 must be >= 0").arg(v)));
    }

    // ── 8. Risk surface sum is finite and positive ────────────────────────────
    void testRiskSurfaceSumPositive()
    {
        auto hp = fittedHP(cluster(51.5, -0.1, 8, 0.0));
        const auto surface = hp.riskSurface(8.5, 51.4, 51.6, -0.2, 0.0, 10);
        double total = 0.0;
        for (const auto& row : surface) for (double v : row) total += v;
        QVERIFY2(std::isfinite(total) && total > 0.0,
                 qPrintable(QStringLiteral("Risk surface total %1 must be finite and > 0").arg(total)));
    }

    // ── 9. alpha in [0, 1] after fit ─────────────────────────────────────────
    void testAlphaRange()
    {
        auto hp = fittedHP(cluster(51.5, -0.1, 10, 0.0));
        QVERIFY2(hp.params().alpha >= 0.0 && hp.params().alpha <= 1.0,
                 qPrintable(QStringLiteral("alpha %1 must be in [0,1]").arg(hp.params().alpha)));
    }

    // ── 10. mu positive after fit ─────────────────────────────────────────────
    void testMuPositive()
    {
        auto hp = fittedHP(cluster(51.5, -0.1, 8, 0.0));
        QVERIFY2(hp.params().mu > 0.0,
                 qPrintable(QStringLiteral("mu %1 must be > 0").arg(hp.params().mu)));
    }
};

QTEST_MAIN(HawkesAdvancedTest)
#include "test_hawkes_advanced.moc"
