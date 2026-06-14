// test_hawkes_process_deep10.cpp — Deep audit iteration 28: HawkesProcess
// empty fit guard, history ordering, params accessors, mu non-negative.
#include <QTest>
#include <cmath>
#include "models/HawkesProcess.h"

class TestHawkesProcessDeep10 : public QObject
{
    Q_OBJECT

    static SpatiotemporalEvent ev(double t, double lat = 51.5, double lon = -0.1)
    {
        SpatiotemporalEvent e;
        e.tDays     = t;
        e.lat       = lat;
        e.lon       = lon;
        e.crimeType = QStringLiteral("burglary");
        return e;
    }

private slots:

    void testEmptyEventsFitFails()
    {
        HawkesProcess hp;
        QVERIFY(!hp.fit({}, 5));
    }

    void testParamsMuNonNegativeAfterFit()
    {
        QVector<SpatiotemporalEvent> events;
        for (int i = 0; i < 15; ++i)
            events.append(ev(i * 0.3));

        HawkesProcess hp;
        QVERIFY(hp.fit(events, 8));
        QVERIFY(hp.params().mu >= 0.0);
        QVERIFY(hp.params().alpha >= 0.0);
        QVERIFY(hp.params().beta > 0.0);
    }

    void testSetHistoryEnablesIntensity()
    {
        HawkesProcess hp;
        HawkesParams p;
        p.mu = 0.05; p.alpha = 0.2; p.beta = 1.5; p.sigma = 0.02; p.logLik = 0.0;
        hp.setParams(p);
        hp.setHistory({ ev(0.0), ev(1.0), ev(2.0) });
        const double lam = hp.intensity(2.5, 51.5, -0.1);
        QVERIFY(std::isfinite(lam));
        QVERIFY(lam >= p.mu - 1e-9);
    }

    void testBranchingRatioFinite()
    {
        QVector<SpatiotemporalEvent> events;
        for (int i = 0; i < 20; ++i)
            events.append(ev(i * 0.4));
        HawkesProcess hp;
        QVERIFY(hp.fit(events, 10));
        QVERIFY(std::isfinite(hp.branchingRatio()));
    }

    void testRiskSurfaceDimensions()
    {
        HawkesProcess hp;
        HawkesParams p;
        p.mu = 0.1; p.alpha = 0.3; p.beta = 2.0; p.sigma = 0.02; p.logLik = 0.0;
        hp.setParams(p);
        hp.setHistory({ ev(0.0), ev(0.5) });
        const auto grid = hp.riskSurface(1.0, 51.48, 51.52, -0.12, -0.08, 6);
        QCOMPARE(grid.size(), 6);
        for (const auto& row : grid)
            QCOMPARE(row.size(), 6);
    }
};

QTEST_GUILESS_MAIN(TestHawkesProcessDeep10)
#include "test_hawkes_process_deep10.moc"
