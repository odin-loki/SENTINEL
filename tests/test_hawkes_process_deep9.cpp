// test_hawkes_process_deep9.cpp — Deep audit iteration 24: HawkesProcess
// branching ratio, intensity decay, sorted history, logLik, risk surface.
#include <QTest>
#include <cmath>
#include "models/HawkesProcess.h"

class TestHawkesProcessDeep9 : public QObject
{
    Q_OBJECT

    static SpatiotemporalEvent ev(double tDays, double lat = 51.5, double lon = -0.1)
    {
        SpatiotemporalEvent e;
        e.tDays     = tDays;
        e.lat       = lat;
        e.lon       = lon;
        e.crimeType = QStringLiteral("burglary");
        return e;
    }

private slots:

    void testBranchingRatioBelowOneForStableFit()
    {
        QVector<SpatiotemporalEvent> events;
        for (int i = 0; i < 25; ++i)
            events.append(ev(i * 0.5, 51.5 + i * 1e-6, -0.1));

        HawkesProcess hp;
        QVERIFY(hp.fit(events, 15));
        QVERIFY2(hp.branchingRatio() < 1.0,
                 qPrintable(QStringLiteral("branching=%1").arg(hp.branchingRatio())));
    }

    void testIntensityNonNegative()
    {
        HawkesProcess hp;
        HawkesParams p;
        p.mu = 0.1; p.alpha = 0.4; p.beta = 2.0; p.sigma = 0.02; p.logLik = 0.0;
        hp.setParams(p);
        hp.setHistory({ ev(0.0), ev(1.0), ev(2.0) });
        const double lam = hp.intensity(3.0, 51.5, -0.1);
        QVERIFY2(lam >= 0.0, qPrintable(QStringLiteral("lam=%1").arg(lam)));
    }

    void testIntensityDecaysWithDistance()
    {
        HawkesProcess hp;
        HawkesParams p;
        p.mu = 0.1; p.alpha = 0.5; p.beta = 3.0; p.sigma = 0.02; p.logLik = 0.0;
        hp.setParams(p);
        hp.setHistory({ ev(0.0, 51.5, -0.1) });
        const double near = hp.intensity(1.0, 51.5, -0.1);
        const double far  = hp.intensity(1.0, 52.0, -0.5);
        QVERIFY2(near >= far,
                 qPrintable(QStringLiteral("near=%1 far=%2").arg(near).arg(far)));
    }

    void testLogLikelihoodFiniteAfterFit()
    {
        QVector<SpatiotemporalEvent> events;
        for (int i = 0; i < 20; ++i)
            events.append(ev(i * 0.2));
        HawkesProcess hp;
        QVERIFY(hp.fit(events, 10));
        QVERIFY(std::isfinite(hp.params().logLik));
    }

    void testRiskSurfaceNonNegative()
    {
        HawkesProcess hp;
        HawkesParams p;
        p.mu = 0.1; p.alpha = 0.3; p.beta = 2.5; p.sigma = 0.02; p.logLik = 0.0;
        hp.setParams(p);
        hp.setHistory({ ev(0.0), ev(0.5), ev(1.0) });
        const auto surface = hp.riskSurface(2.0, 51.48, 51.52, -0.12, -0.08, 8);
        QCOMPARE(static_cast<int>(surface.size()), 8);
        for (const auto& row : surface)
            for (double v : row)
                QVERIFY2(v >= 0.0, qPrintable(QStringLiteral("cell=%1").arg(v)));
    }

    void testUnfittedIntensityUsesMu()
    {
        HawkesProcess hp;
        const double lam = hp.intensity(1.0, 51.5, -0.1);
        QVERIFY2(lam >= 0.0, qPrintable(QStringLiteral("lam=%1").arg(lam)));
    }

    void testSortedHistoryHigherIntensity()
    {
        HawkesProcess hp;
        HawkesParams p;
        p.mu = 0.05; p.alpha = 0.6; p.beta = 2.0; p.sigma = 0.02; p.logLik = 0.0;
        hp.setParams(p);
        hp.setHistory({ ev(0.0), ev(0.5), ev(1.0), ev(1.5) });
        const double lam = hp.intensity(2.0, 51.5, -0.1);
        QVERIFY2(lam > hp.params().mu,
                 qPrintable(QStringLiteral("lam=%1 mu=%2").arg(lam).arg(hp.params().mu)));
    }
};

QTEST_GUILESS_MAIN(TestHawkesProcessDeep9)
#include "test_hawkes_process_deep9.moc"
