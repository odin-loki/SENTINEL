// test_model_accuracy_validation.cpp
// Advanced model accuracy validation tests for SENTINEL statistical models.
// Validates: PoissonBaseline, HawkesProcess, KDEHotspot, SeriesDetector, GPRegression.

#include <QTest>
#include <QCoreApplication>
#include <QDate>
#include <QDateTime>
#include <QVector>
#include <QPair>
#include <cmath>

#include "models/PoissonBaseline.h"
#include "models/HawkesProcess.h"
#include "models/KDEHotspot.h"
#include "models/SeriesDetector.h"
#include "models/GPRegression.h"
#include "core/CrimeEvent.h"

// ─────────────────────────────────────────────────────────────────────────────
// 1. TestPoissonBaselineAccuracy
// ─────────────────────────────────────────────────────────────────────────────
class TestPoissonBaselineAccuracy : public QObject {
    Q_OBJECT

private slots:

    // Fit 200 synthetic events distributed as ~Poisson(lambda=3.5) per bucket period.
    // Strategy: generate 57 distinct Monday-in-January dates; 29 with 4 events
    // and 28 with 3 events → total 200 events, mean = 200/57 ≈ 3.51.
    // Predicted lambda must be within 15% of 3.5 (i.e. [2.975, 4.025]).
    void testPoissonLambdaAccuracy()
    {
        QVector<PoissonBaseline::EventRecord> events;

        // Find 57 Mondays in January starting from 2020
        QDate d(2020, 1, 1);
        int mondayIdx = 0;
        while (mondayIdx < 57) {
            if (d.dayOfWeek() == Qt::Monday && d.month() == 1) {
                // Alternate 4 and 3 events: 29×4 + 28×3 = 200 total, mean≈3.51
                int count = (mondayIdx < 29) ? 4 : 3;
                for (int i = 0; i < count; ++i) {
                    PoissonBaseline::EventRecord r;
                    r.zoneId     = "lambdaZone";
                    r.crimeType  = "theft";
                    r.occurredAt = QDateTime(d, QTime(10, 0, 0), Qt::UTC);
                    events.append(r);
                }
                ++mondayIdx;
            }
            d = d.addDays(1);
        }

        QCOMPARE(events.size(), 200);

        PoissonBaseline model;
        model.fit(events);
        QVERIFY2(model.isFitted(), "PoissonBaseline should be fitted after adding 200 events");

        // Query the same bucket: Monday in January at hour 10
        // 2025-01-06 is a Monday
        QDateTime queryDt(QDate(2025, 1, 6), QTime(10, 0, 0), Qt::UTC);
        PoissonPrediction pred = model.predict("lambdaZone", queryDt, "theft");

        QVERIFY2(pred.nObservations > 0,
                 qPrintable(QString("Expected observations > 0, got %1").arg(pred.nObservations)));

        const double lambda     = pred.lambda;
        const double target     = 3.5;
        const double tolerance  = target * 0.15;   // 15% = 0.525

        QVERIFY2(std::abs(lambda - target) <= tolerance,
                 qPrintable(QString("Expected lambda≈3.5 (±15%%), got %1 (diff=%2, tol=%3)")
                            .arg(lambda).arg(std::abs(lambda - target)).arg(tolerance)));
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// 2. TestHawkesProcessAccuracy
// ─────────────────────────────────────────────────────────────────────────────
class TestHawkesProcessAccuracy : public QObject {
    Q_OBJECT

private slots:

    // Fit 100 near-repeat events (10 clusters of 10 events each) with clear
    // spatiotemporal excitation structure.  After fitting, alpha > 0 and mu > 0.
    void testHawkesParamPositiveAfterFit()
    {
        QVector<SpatiotemporalEvent> events;

        // 10 clusters, each with 1 main event + 9 aftershocks
        for (int cluster = 0; cluster < 10; ++cluster) {
            double clusterLat = 51.5 + cluster * 0.02;
            double clusterLon = -0.1;
            double clusterT   = cluster * 10.0;   // clusters 10 days apart

            // Main event
            SpatiotemporalEvent main;
            main.tDays = clusterT;
            main.lat   = clusterLat;
            main.lon   = clusterLon;
            main.crimeType = "burglary";
            events.append(main);

            // 9 aftershocks: nearby in space (< 0.01 deg) and time (< 2 days)
            for (int j = 1; j <= 9; ++j) {
                SpatiotemporalEvent after;
                after.tDays    = clusterT + j * 0.2;               // 0.2–1.8 days after
                after.lat      = clusterLat + (j % 3 - 1) * 0.002; // within ~200 m
                after.lon      = clusterLon + (j % 2)     * 0.002;
                after.crimeType = "burglary";
                events.append(after);
            }
        }

        QCOMPARE(events.size(), 100);

        HawkesProcess hp;
        const bool converged = hp.fit(events, 50);

        // Even if optimiser doesn't fully converge, parameters must be positive
        Q_UNUSED(converged);
        QVERIFY2(hp.isFitted(), "HawkesProcess should be marked fitted after fit()");

        const HawkesParams& p = hp.params();
        QVERIFY2(p.mu > 0.0,
                 qPrintable(QString("mu=%1 should be > 0 after fitting excitation data").arg(p.mu)));
        QVERIFY2(p.alpha > 0.0,
                 qPrintable(QString("alpha=%1 should be > 0 after fitting excitation data").arg(p.alpha)));
        QVERIFY2(p.beta > 0.0,
                 qPrintable(QString("beta=%1 should be > 0").arg(p.beta)));
    }

    // Intensity at a recently active location must exceed background rate mu
    void testHawkesIntensityAboveBackground()
    {
        QVector<SpatiotemporalEvent> events;
        for (int cluster = 0; cluster < 10; ++cluster) {
            double t = cluster * 10.0;
            for (int j = 0; j < 10; ++j) {
                SpatiotemporalEvent e;
                e.tDays    = t + j * 0.2;
                e.lat      = 51.5;
                e.lon      = -0.1;
                e.crimeType = "burglary";
                events.append(e);
            }
        }

        HawkesProcess hp;
        hp.fit(events, 50);
        QVERIFY(hp.isFitted());

        const double mu         = hp.params().mu;
        // Intensity at last cluster location, 0.1 days after last event
        const double lastT      = 90.0 + 9 * 0.2;
        const double intensity  = hp.intensity(lastT + 0.1, 51.5, -0.1);

        QVERIFY2(intensity >= mu,
                 qPrintable(QString("intensity=%1 should be >= mu=%2 near recent activity")
                            .arg(intensity).arg(mu)));
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// 3. TestKDEHotspotAccuracy
// ─────────────────────────────────────────────────────────────────────────────
class TestKDEHotspotAccuracy : public QObject {
    Q_OBJECT

private slots:

    // Generate 50 points tightly clustered around (51.50, -0.10) within a
    // bounding box [51.40-51.60, -0.30 to 0.10].  The top hotspot region's
    // centroid must lie within 0.05 degrees of the true cluster centre.
    void testHotspotOverlapsCluster()
    {
        QVector<QPair<double,double>> locations;

        // 50 tightly clustered points near (51.50, -0.10)
        const double clusterLat = 51.50;
        const double clusterLon = -0.10;

        for (int i = 0; i < 50; ++i) {
            // Deterministic offsets within ±0.01 degrees (~1 km)
            double dlat = ((i * 7) % 20 - 10) * 0.001;
            double dlon = ((i * 3) % 20 - 10) * 0.001;
            locations.append({clusterLat + dlat, clusterLon + dlon});
        }

        // Bounding box covers a wider area so the cluster is clearly detectable
        const double latMin = 51.40, latMax = 51.60;
        const double lonMin = -0.30, lonMax =  0.10;

        KDEHotspot kde(50, 1.0);
        QVector<HotspotRegion> hotspots = kde.findHotspots(
            locations, latMin, latMax, lonMin, lonMax, 3, 0.02);

        QVERIFY2(!hotspots.isEmpty(),
                 "KDE should find at least one hotspot region for a tight cluster");

        const HotspotRegion& top = hotspots.first();

        // The top hotspot centroid must be within 0.05° of the true cluster
        const double distLat = std::abs(top.centroidLat - clusterLat);
        const double distLon = std::abs(top.centroidLon - clusterLon);

        QVERIFY2(distLat < 0.05,
                 qPrintable(QString("centroidLat=%1 should be near clusterLat=%2 (diff=%3)")
                            .arg(top.centroidLat).arg(clusterLat).arg(distLat)));
        QVERIFY2(distLon < 0.05,
                 qPrintable(QString("centroidLon=%1 should be near clusterLon=%2 (diff=%3)")
                            .arg(top.centroidLon).arg(clusterLon).arg(distLon)));
    }

    // Peak density in the hotspot region must be positive
    void testHotspotDensityPositive()
    {
        // Points must vary in BOTH lat and lon so Silverman bandwidth > 0
        QVector<QPair<double,double>> locations;
        for (int i = 0; i < 25; ++i) {
            double dlat = ((i * 7) % 10 - 5) * 0.002;
            double dlon = ((i * 3) % 10 - 5) * 0.002;
            locations.append({51.5 + dlat, -0.1 + dlon});
        }

        KDEHotspot kde;
        auto hotspots = kde.findHotspots(locations, 51.4, 51.6, -0.2, 0.0, 2, 0.02);

        QVERIFY2(!hotspots.isEmpty(), "Should detect at least one hotspot");
        QVERIFY2(hotspots.first().peakDensity > 0.0,
                 "Peak density must be positive");
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// 4. TestSeriesDetectorAccuracy
// ─────────────────────────────────────────────────────────────────────────────
class TestSeriesDetectorAccuracy : public QObject {
    Q_OBJECT

private:
    // Build a SeriesEvent with the given params
    static SeriesEvent makeSeriesEvent(const QString& id,
                                       double lat, double lon,
                                       double tDays,
                                       const QString& moText = "forced_entry residential")
    {
        SeriesEvent e;
        e.eventId   = id;
        e.lat       = lat;
        e.lon       = lon;
        e.tDays     = tDays;
        e.crimeType = "burglary";
        e.moText    = moText;
        return e;
    }

private slots:

    // Create 3 distinct crime series, each with 5 events within 0.2 km and 5 days.
    // The three clusters are ~15 km apart.  SeriesDetector should find ≥ 2.
    void testDetectsAtLeastTwoOfThreeSeries()
    {
        QVector<SeriesEvent> events;

        // Series A: centred at (51.50, -0.10)
        for (int i = 0; i < 5; ++i)
            events.append(makeSeriesEvent(
                QString("A%1").arg(i),
                51.5000 + i * 0.001, -0.1000,
                0.0 + i * 1.0));

        // Series B: centred at (51.63, -0.20) — ~15 km from A
        for (int i = 0; i < 5; ++i)
            events.append(makeSeriesEvent(
                QString("B%1").arg(i),
                51.6300 + i * 0.001, -0.2000,
                0.0 + i * 1.0));

        // Series C: centred at (51.76, -0.30) — ~15 km from B
        for (int i = 0; i < 5; ++i)
            events.append(makeSeriesEvent(
                QString("C%1").arg(i),
                51.7600 + i * 0.001, -0.3000,
                0.0 + i * 1.0));

        // epsKm=0.5 (500 m), epsDays=7, minSamples=4 — each series has 5 tight events
        SeriesDetector det(0.5, 7.0, 4);
        const auto series = det.detectSeries(events);

        QVERIFY2(series.size() >= 2,
                 qPrintable(QString("Expected ≥ 2 series detected, got %1").arg(series.size())));
    }

    // Each detected series must have at least 4 members
    void testSeriesMemberCountSufficient()
    {
        QVector<SeriesEvent> events;

        for (int s = 0; s < 3; ++s) {
            double baseLat = 51.50 + s * 0.15;
            double baseLon = -0.10 - s * 0.15;
            for (int i = 0; i < 6; ++i)
                events.append(makeSeriesEvent(
                    QString("S%1_E%2").arg(s).arg(i),
                    baseLat + i * 0.001, baseLon,
                    s * 30.0 + i * 1.0));  // temporal separation between series
        }

        SeriesDetector det(0.5, 7.0, 4);
        const auto series = det.detectSeries(events);

        for (const auto& cs : series) {
            QVERIFY2(cs.members.size() >= 4,
                     qPrintable(QString("Series %1 has only %2 members, expected ≥ 4")
                                .arg(cs.seriesId).arg(cs.members.size())));
        }
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// 5. TestGPRegressionAccuracy
// ─────────────────────────────────────────────────────────────────────────────
class TestGPRegressionAccuracy : public QObject {
    Q_OBJECT

private slots:

    // Fit GP on 20 training points from sin(x), x ∈ [0, 2π].
    // Predictions at 8 interior test points must be within 0.3 of true sin(x).
    void testGPFitsSinFunction()
    {
        GPRegression gp;
        gp.setKernelParams(1.0, 1.0, 1e-6);

        // Training: 21 evenly spaced points over [0, 2π]
        constexpr double kPi = 3.14159265358979323846;
        QVector<QPair<double,double>> X;
        QVector<double>               y;
        for (int i = 0; i <= 20; ++i) {
            double x = i * kPi / 10.0;   // 0 … 2π
            X.append({x, 0.0});
            y.append(std::sin(x));
        }
        gp.fit(X, y);

        QVERIFY2(gp.isFitted(), "GP should be fitted after providing training data");
        QCOMPARE(gp.nTrainingPoints(), 21);

        // Test at 8 midpoints between consecutive training points
        int within = 0;
        const double tol = 0.3;
        for (int i = 0; i < 8; ++i) {
            double x    = (2 * i + 1) * kPi / 20.0;  // midpoints
            double pred = gp.predict(x, 0.0);
            double truth = std::sin(x);
            if (std::abs(pred - truth) < tol) ++within;

            QVERIFY2(std::isfinite(pred),
                     qPrintable(QString("Prediction at x=%1 is not finite: %2").arg(x).arg(pred)));
        }

        QVERIFY2(within >= 6,
                 qPrintable(QString("Only %1/8 predictions within 0.3 of sin(x); expected ≥ 6")
                            .arg(within)));
    }

    // Posterior variance at a training point must be less than prior variance (sigma2=1)
    void testGPVarianceReducedAtTrainingPoints()
    {
        GPRegression gp;
        gp.setKernelParams(1.0, 1.0, 1e-6);

        QVector<QPair<double,double>> X = {{0.0,0.0},{1.0,0.0},{2.0,0.0},{3.0,0.0}};
        QVector<double> y = {0.0, 0.841, 0.909, 0.141};  // ≈ sin values
        gp.fit(X, y);

        for (const auto& pt : X) {
            const auto [mean, var] = gp.predictWithUncertainty(pt.first, pt.second);
            Q_UNUSED(mean)
            QVERIFY2(var < 0.5,
                     qPrintable(QString("Posterior var=%1 at training point should be < 0.5").arg(var)));
        }
    }

    // GP predictions must be bounded when extrapolating far outside training range
    void testGPPredictionsBoundedOutside()
    {
        GPRegression gp;
        gp.setKernelParams(1.0, 1.0, 0.01);

        QVector<QPair<double,double>> X;
        QVector<double> y;
        for (int i = 0; i <= 10; ++i) {
            X.append({static_cast<double>(i), 0.0});
            y.append(std::sin(static_cast<double>(i)));
        }
        gp.fit(X, y);

        // Extrapolate far outside training range
        for (double x : {-5.0, -10.0, 20.0, 50.0}) {
            double pred = gp.predict(x, 0.0);
            QVERIFY2(std::isfinite(pred),
                     qPrintable(QString("Extrapolation at x=%1 gave non-finite %2").arg(x).arg(pred)));
            QVERIFY2(std::abs(pred) < 10.0,
                     qPrintable(QString("Extrapolation at x=%1 gave extreme value %2").arg(x).arg(pred)));
        }
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// main
// ─────────────────────────────────────────────────────────────────────────────
static int runTest(QObject* obj, const char* logFile)
{
    QStringList args = { "test", "-o", QString("%1,txt").arg(logFile) };
    return QTest::qExec(obj, args);
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    int r = 0;
    { TestPoissonBaselineAccuracy t; r |= runTest(&t, "acc_poisson.txt"); }
    { TestHawkesProcessAccuracy   t; r |= runTest(&t, "acc_hawkes.txt"); }
    { TestKDEHotspotAccuracy      t; r |= runTest(&t, "acc_kde.txt"); }
    { TestSeriesDetectorAccuracy  t; r |= runTest(&t, "acc_series.txt"); }
    { TestGPRegressionAccuracy    t; r |= runTest(&t, "acc_gp.txt"); }
    return r;
}

#include "test_model_accuracy_validation.moc"
