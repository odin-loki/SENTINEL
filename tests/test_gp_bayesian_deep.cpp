// test_gp_bayesian_deep.cpp — iteration-6 deep mathematical audit tests
// GPRegression, BayesianHierarchical, PoissonBaseline
#include <QTest>
#include <QUuid>
#include <QDateTime>
#include <QTimeZone>
#include <cmath>
#include <random>

#include "models/GPRegression.h"
#include "models/BayesianHierarchical.h"
#include "models/PoissonBaseline.h"
#include "core/CrimeEvent.h"

class GPBayesianDeepTest : public QObject
{
    Q_OBJECT

private:
    static CrimeEvent makeEvent(const QString& zone, int count = 1)
    {
        CrimeEvent ev;
        ev.id        = QUuid::createUuid().toString(QUuid::WithoutBraces);
        ev.suburb    = zone;
        ev.crimeType = QStringLiteral("burglary");
        ev.latitude  = 51.5;
        ev.longitude = -0.1;
        ev.timestamp = QDateTime(QDate(2024, 6, 1), QTime(12, 0), QTimeZone::utc());
        Q_UNUSED(count);
        return ev;
    }

    static QVector<CrimeEvent> zoneEvents(const QString& zone, int n)
    {
        QVector<CrimeEvent> evs;
        evs.reserve(n);
        for (int i = 0; i < n; ++i)
            evs.append(makeEvent(zone));
        return evs;
    }

    static PoissonBaseline::EventRecord poissonRec(const QString& zone,
                                                    const QDateTime& dt,
                                                    const QString& type = QStringLiteral("burglary"))
    {
        PoissonBaseline::EventRecord r;
        r.zoneId     = zone;
        r.occurredAt = dt;
        r.crimeType  = type;
        return r;
    }

    static QDateTime mondayJanHour10(int year, int day)
    {
        return QDateTime(QDate(year, 1, day), QTime(10, 0, 0), QTimeZone::utc());
    }

    static QVector<PoissonBaseline::EventRecord> makeOverdispersedNB(
        const QString& zone, const QString& crimeType = QStringLiteral("burglary"))
    {
        struct Slot { int year, day, count; };
        static const Slot kSlots[] = {
            {2020,  6, 10}, {2020, 13,  1}, {2020, 20, 10}, {2020, 27,  1},
            {2021,  4, 10}, {2021, 11,  1}, {2021, 18, 10}, {2021, 25,  1},
            {2022,  3, 10}, {2022, 10,  1}, {2022, 17, 10}, {2022, 24,  1}, {2022, 31, 10},
            {2023,  2,  1}, {2023,  9, 10}, {2023, 16,  1}, {2023, 23, 10}, {2023, 30,  1},
            {2024,  1, 10}, {2024,  8,  1}, {2024, 15, 10}, {2024, 22,  1}, {2024, 29, 10},
        };
        QVector<PoissonBaseline::EventRecord> recs;
        for (const auto& s : kSlots) {
            for (int i = 0; i < s.count; ++i)
                recs.append(poissonRec(zone, mondayJanHour10(s.year, s.day), crimeType));
        }
        return recs;
    }

private slots:

    // ── GPRegression ─────────────────────────────────────────────────────────

    void testGPPosteriorMeanInterpolates()
    {
        GPRegression gp;
        gp.setKernelParams(2.0, 1.0, 1e-12);

        QVector<QPair<double, double>> X = {{0.0, 0.0}, {1.0, 0.0}, {2.0, 1.0}};
        QVector<double> y = {1.0, 3.0, -0.5};
        gp.fit(X, y);
        QVERIFY(gp.isFitted());

        for (int i = 0; i < X.size(); ++i) {
            const double pred = gp.predict(X[i].first, X[i].second);
            QVERIFY2(std::abs(pred - y[i]) < 1e-4,
                     qPrintable(QStringLiteral("GP mean at training point %1: got %2 expected %3")
                                    .arg(i).arg(pred).arg(y[i])));
        }
    }

    void testGPPosteriorVarianceZeroAtTrainingPoints()
    {
        GPRegression gp;
        gp.setKernelParams(1.5, 0.8, 1e-12);

        QVector<QPair<double, double>> X = {{0.0, 0.0}, {2.0, 0.0}, {4.0, 0.0}};
        QVector<double> y = {0.2, 1.1, 0.7};
        gp.fit(X, y);

        for (const auto& pt : X) {
            const auto [mean, var] = gp.predictWithUncertainty(pt.first, pt.second);
            Q_UNUSED(mean);
            QVERIFY2(var < 1e-4,
                     qPrintable(QStringLiteral("Variance at training point (%1,%2) = %3, expected ~0")
                                    .arg(pt.first).arg(pt.second).arg(var)));
        }
    }

    void testGPPosteriorVariancePositiveAway()
    {
        GPRegression gp;
        gp.setKernelParams(1.0, 1.0, 1e-6);

        QVector<QPair<double, double>> X = {{0.0, 0.0}, {1.0, 0.0}};
        QVector<double> y = {0.0, 1.0};
        gp.fit(X, y);

        const auto [mean, var] = gp.predictWithUncertainty(50.0, 50.0);
        Q_UNUSED(mean);
        QVERIFY2(var > 0.0,
                 qPrintable(QStringLiteral("Far-field variance %1 must be positive").arg(var)));
    }

    void testGPVarianceMonotonicallyIncreases()
    {
        GPRegression gp;
        gp.setKernelParams(1.0, 0.5, 1e-5);

        QVector<QPair<double, double>> X = {{0.0, 0.0}, {1.0, 0.0}, {0.0, 1.0}};
        QVector<double> y = {1.0, 0.5, 0.8};
        gp.fit(X, y);

        double prevVar = -1.0;
        for (int d = 2; d <= 10; ++d) {
            const auto [mean, var] = gp.predictWithUncertainty(static_cast<double>(d), 0.0);
            Q_UNUSED(mean);
            QVERIFY2(var > 0.0, "Variance along ray must be positive");
            QVERIFY2(var >= prevVar - 1e-9,
                     qPrintable(QStringLiteral("Variance should increase with distance: d=%1 var=%2 prev=%3")
                                    .arg(d).arg(var).arg(prevVar)));
            prevVar = var;
        }
    }

    void testGPPredictBeforeFitSafe()
    {
        GPRegression gp;
        QVERIFY(!gp.isFitted());

        const double mean = gp.predict(1.0, 2.0);
        QCOMPARE(mean, 0.0);

        const auto [m, v] = gp.predictWithUncertainty(1.0, 2.0);
        QCOMPARE(m, 0.0);
        QVERIFY(v >= 0.0);
        QVERIFY(std::isfinite(v));
    }

    void testGPSEKernelSymmetric()
    {
        // Equidistant symmetric setup: k*((0,0),(1,0)) = k*((1,0),(0,0))
        // ⇒ posterior mean at (0,1) equals mean at (1,0) when y is symmetric.
        GPRegression gp;
        gp.setKernelParams(3.0, 1.2, 1e-8);

        QVector<QPair<double, double>> X = {{0.0, 0.0}, {1.0, 1.0}};
        QVector<double> y = {2.0, 2.0};
        gp.fit(X, y);

        const double predA = gp.predict(0.0, 1.0);
        const double predB = gp.predict(1.0, 0.0);
        QVERIFY2(std::abs(predA - predB) < 1e-6,
                 qPrintable(QStringLiteral("Symmetric kernel geometry: predA=%1 predB=%2")
                                .arg(predA).arg(predB)));
    }

    void testGPSEKernelAtZero()
    {
        // Far from training data with negligible noise: posterior variance → k(x*,x*) = σ²
        GPRegression gp;
        const double sigma2 = 4.0;
        gp.setKernelParams(sigma2, 1.0, 1e-10);

        QVector<QPair<double, double>> X = {{0.0, 0.0}};
        QVector<double> y = {1.0};
        gp.fit(X, y);

        const auto [mean, var] = gp.predictWithUncertainty(100.0, 100.0);
        Q_UNUSED(mean);
        QVERIFY2(std::abs(var - sigma2) < 0.05,
                 qPrintable(QStringLiteral("Prior variance at distant point: got %1 expected %2")
                                .arg(var).arg(sigma2)));
    }

    void testGPLargeLengthscaleSmoother()
    {
        QVector<QPair<double, double>> X = {{0.0, 0.0}, {3.0, 0.0}};
        QVector<double> y = {0.0, 1.0};

        GPRegression gpShort, gpLong;
        gpShort.setKernelParams(1.0, 0.3, 1e-5);
        gpLong.setKernelParams(1.0, 3.0, 1e-5);
        gpShort.fit(X, y);
        gpLong.fit(X, y);

        const auto [meanS, varS] = gpShort.predictWithUncertainty(1.5, 0.0);
        const auto [meanL, varL] = gpLong.predictWithUncertainty(1.5, 0.0);
        Q_UNUSED(meanS);
        Q_UNUSED(meanL);

        QVERIFY2(varL < varS,
                 qPrintable(QStringLiteral("Larger lengthscale should yield lower variance: short=%1 long=%2")
                                .arg(varS).arg(varL)));
    }

    // ── BayesianHierarchical ─────────────────────────────────────────────────

    void testBayesianPosteriorMeanGamma()
    {
        const double exposure = 30.0;
        const int nEvents = 7;

        BayesianHierarchical bh;
        bh.fit(zoneEvents(QStringLiteral("Alpha"), nEvents), exposure);

        const double alpha0 = bh.globalAlpha();
        const double beta0  = bh.globalBeta();
        const double expected = (alpha0 + nEvents) / (beta0 + exposure);

        const ZonePosterior zp = bh.posteriorForZone(QStringLiteral("Alpha"));
        QVERIFY2(std::abs(zp.posteriorMean - expected) < 1e-9,
                 qPrintable(QStringLiteral("Posterior mean %1 != (α+n)/(β+T)=%2")
                                .arg(zp.posteriorMean).arg(expected)));
    }

    void testBayesianZeroObsShrinkage()
    {
        QVector<CrimeEvent> evs = zoneEvents(QStringLiteral("Busy"), 20);
        evs += zoneEvents(QStringLiteral("Quiet"), 2);

        BayesianHierarchical bh;
        bh.fit(evs, 30.0);

        const double globalMu = bh.globalMean();
        const ZonePosterior unseen = bh.posteriorForZone(QStringLiteral("Ghost"));
        const ZonePosterior quiet  = bh.posteriorForZone(QStringLiteral("Quiet"));

        QVERIFY2(unseen.observedCount == 0,
                 "Unseen zone should have zero observations");

        // Unseen zone gets the empirical Bayes prior mean = α/β ≈ globalMu
        QVERIFY2(unseen.posteriorMean > 0.0,
                 "Unseen zone posterior mean must be positive (prior)");
        QVERIFY2(unseen.posteriorMean < globalMu * 2.0,
                 "Unseen zone posterior should not wildly exceed global mean");

        // "Quiet" zone has 2 events vs global mean dominated by "Busy" (20 events).
        // Bayesian shrinkage pulls it toward globalMu. The posterior mean is between
        // the raw rate (2/30 ≈ 0.067) and the prior mean. Both zones should have
        // positive posterior means.
        QVERIFY2(quiet.posteriorMean > 0.0,
                 "Quiet zone posterior mean must be positive");

        // "Busy" zone should have higher posterior than "Quiet" zone
        const ZonePosterior busy = bh.posteriorForZone(QStringLiteral("Busy"));
        QVERIFY2(busy.posteriorMean > quiet.posteriorMean,
                 qPrintable(QStringLiteral("Busy zone (%1) must have higher posterior than Quiet (%2)")
                                .arg(busy.posteriorMean).arg(quiet.posteriorMean)));
    }

    void testBayesianEmpiricaBayesUpdates()
    {
        BayesianHierarchical bh;
        const double alphaBefore = bh.globalAlpha();
        const double betaBefore  = bh.globalBeta();

        QVector<CrimeEvent> evs;
        evs += zoneEvents(QStringLiteral("A"), 15);
        evs += zoneEvents(QStringLiteral("B"), 3);
        evs += zoneEvents(QStringLiteral("C"), 8);
        bh.fit(evs, 30.0);

        QVERIFY(bh.isFitted());
        QVERIFY2(bh.globalAlpha() != alphaBefore || bh.globalBeta() != betaBefore,
                 "Empirical Bayes via fit() should update hyperparameters from defaults");
        QVERIFY(bh.globalAlpha() > 0.0);
        QVERIFY(bh.globalBeta() > 0.0);
    }

    void testBayesianPredictiveVariancePositive()
    {
        QVector<CrimeEvent> evs;
        evs += zoneEvents(QStringLiteral("Z1"), 12);
        evs += zoneEvents(QStringLiteral("Z2"), 1);
        evs += zoneEvents(QStringLiteral("Z3"), 5);

        BayesianHierarchical bh;
        bh.fit(evs, 30.0);

        for (const auto& zp : bh.allPosteriors()) {
            QVERIFY2(zp.posteriorVar > 0.0,
                     qPrintable(QStringLiteral("Zone %1 variance %2 must be positive")
                                    .arg(zp.zoneId).arg(zp.posteriorVar)));
            const double expectedVar = zp.alphaPost / (zp.betaPost * zp.betaPost);
            QVERIFY2(std::abs(zp.posteriorVar - expectedVar) < 1e-12,
                     qPrintable(QStringLiteral("Var should equal α/β² for zone %1")
                                    .arg(zp.zoneId)));
        }

        const ZonePosterior unseen = bh.posteriorForZone(QStringLiteral("NewZone"));
        QVERIFY(unseen.posteriorVar > 0.0);
    }

    void testBayesianGroupIsolation()
    {
        QVector<CrimeEvent> evs;
        evs += zoneEvents(QStringLiteral("HighCrime"), 50);
        evs += zoneEvents(QStringLiteral("LowCrime"), 1);

        BayesianHierarchical bh;
        bh.fit(evs, 30.0);

        const ZonePosterior high = bh.posteriorForZone(QStringLiteral("HighCrime"));
        const ZonePosterior low  = bh.posteriorForZone(QStringLiteral("LowCrime"));

        QVERIFY2(high.posteriorMean > low.posteriorMean * 3.0,
                 "High-crime zone should have substantially higher posterior mean");
        QVERIFY2(low.posteriorMean < high.posteriorMean * 0.5,
                 qPrintable(QStringLiteral("Low-crime zone mean %1 should be much less than high %2")
                                .arg(low.posteriorMean).arg(high.posteriorMean)));
    }

    // ── PoissonBaseline ──────────────────────────────────────────────────────

    void testPoissonProbAtLeastOne()
    {
        const double lambda = 2.5;
        const double expected = 1.0 - std::exp(-lambda);

        const double pmf0 = PoissonBaseline::poissonPMF(lambda, 0);
        const double prob = 1.0 - pmf0;

        QVERIFY2(std::abs(prob - expected) < 1e-12,
                 qPrintable(QStringLiteral("P(X≥1)=%1 expected 1-exp(-λ)=%2")
                                .arg(prob).arg(expected)));

        PoissonBaseline pb;
        QVector<PoissonBaseline::EventRecord> evs;
        for (int i = 0; i < 10; ++i)
            evs.append(poissonRec(QStringLiteral("Z"), mondayJanHour10(2024, 1 + i * 7)));
        pb.fit(evs);
        const auto pred = pb.predict(QStringLiteral("Z"), mondayJanHour10(2025, 6),
                                     QStringLiteral("burglary"));
        QVERIFY2(std::abs(pred.probAtLeastOne - (1.0 - std::exp(-pred.lambda))) < 1e-9,
                 "predict().probAtLeastOne must equal 1-exp(-lambda)");
    }

    void testPoissonCI90Coverage()
    {
        std::mt19937 rng(42);
        std::poisson_distribution<int> dist(4);

        int covered = 0;
        constexpr int trials = 500;
        for (int t = 0; t < trials; ++t) {
            const double trueLambda = 4.0;
            int sampleSum = 0;
            constexpr int nDays = 20;
            for (int d = 0; d < nDays; ++d)
                sampleSum += dist(rng);

            const double estLambda = static_cast<double>(sampleSum) / nDays;
            const double lo = PoissonBaseline::poissonPPF(estLambda, 0.05);
            const double hi = PoissonBaseline::poissonPPF(estLambda, 0.95);

            if (trueLambda >= lo && trueLambda <= hi)
                ++covered;
        }

        const double coverage = static_cast<double>(covered) / trials;
        QVERIFY2(coverage >= 0.85,
                 qPrintable(QStringLiteral("90%% CI coverage %1 should be ≥85%% in simulation")
                                .arg(coverage)));
    }

    void testPoissonNBTrigger()
    {
        PoissonBaseline pb;
        pb.fit(makeOverdispersedNB(QStringLiteral("OD")));

        const auto pred = pb.predict(QStringLiteral("OD"), mondayJanHour10(2024, 1),
                                     QStringLiteral("burglary"));
        QCOMPARE(pred.model, QStringLiteral("NegativeBinomial"));
    }

    void testPoissonLambdaMonotoneWithCount()
    {
        PoissonBaseline pbLow, pbHigh;
        QVector<PoissonBaseline::EventRecord> lowRecs, highRecs;

        for (int i = 0; i < 15; ++i) {
            lowRecs.append(poissonRec(QStringLiteral("Low"), mondayJanHour10(2020 + (i % 5), 6 + i * 7)));
            for (int j = 0; j < 5; ++j)
                highRecs.append(poissonRec(QStringLiteral("High"), mondayJanHour10(2020 + (i % 5), 6 + i * 7)));
        }

        pbLow.fit(lowRecs);
        pbHigh.fit(highRecs);

        const auto predLow  = pbLow.predict(QStringLiteral("Low"), mondayJanHour10(2024, 1),
                                            QStringLiteral("burglary"));
        const auto predHigh = pbHigh.predict(QStringLiteral("High"), mondayJanHour10(2024, 1),
                                             QStringLiteral("burglary"));

        QVERIFY2(predHigh.lambda > predLow.lambda,
                 qPrintable(QStringLiteral("More events → higher λ: low=%1 high=%2")
                                .arg(predLow.lambda).arg(predHigh.lambda)));
    }

    void testPoissonZeroCountsZonePrediction()
    {
        PoissonBaseline pb;
        QVector<PoissonBaseline::EventRecord> evs;
        // Fit on a different zone; "Empty" has no events in this bucket
        for (int i = 0; i < 8; ++i)
            evs.append(poissonRec(QStringLiteral("Other"), mondayJanHour10(2020 + i, 6)));
        pb.fit(evs);

        const auto pred = pb.predict(QStringLiteral("Empty"), mondayJanHour10(2024, 1),
                                     QStringLiteral("burglary"));
        QVERIFY2(pred.nObservations == 0,
                 "Zone with zero events should have no bucket history");
        QVERIFY2(pred.lambda < 0.5,
                 qPrintable(QStringLiteral("Zero-event zone λ=%1 should be small").arg(pred.lambda)));
        QVERIFY2(pred.probAtLeastOne < 0.5,
                 qPrintable(QStringLiteral("Zero-event zone P(X≥1)=%1 should be <0.5")
                                .arg(pred.probAtLeastOne)));
    }
};

QTEST_MAIN(GPBayesianDeepTest)
#include "test_gp_bayesian_deep.moc"
