// test_sla_accuracy.cpp — Performance SLA and model accuracy validation tests
#include <QTest>
#include <QCoreApplication>
#include <QDateTime>
#include <QTimeZone>
#include <QElapsedTimer>
#include <QVector>
#include <QPair>
#include <QString>
#include <cmath>
#include <algorithm>

#include "core/CrimeEvent.h"
#include "models/PoissonBaseline.h"
#include "models/HawkesProcess.h"
#include "models/SeriesDetector.h"
#include "models/KDEHotspot.h"
#include "models/GPRegression.h"
#include "models/BayesianHierarchical.h"
#include "models/EnsemblePredictor.h"
#include "inference/HintEngine.h"
#include "inference/GeographicProfiler.h"
#include "nlp/CrimeClassifier.h"
#include "benchmark/BenchmarkMetrics.h"

namespace {

static const QTimeZone kUtc = QTimeZone::utc();

static QDateTime utcDt(const QDate& date, const QTime& time = QTime(10, 0, 0))
{
    return QDateTime(date, time, kUtc);
}

static CrimeEvent makeCrimeEvent(const QString& zone, int index = 0)
{
    CrimeEvent ev;
    ev.eventId   = QStringLiteral("ev_%1").arg(index);
    ev.id        = ev.eventId;
    ev.suburb    = zone;
    ev.crimeType = QStringLiteral("burglary");
    ev.occurredAt = utcDt(QDate(2024, 1, 1).addDays(index % 30));
    ev.timestamp  = ev.occurredAt.value_or(QDateTime());
    ev.lat        = 51.5;
    ev.lon        = -0.1;
    ev.latitude   = 51.5;
    ev.longitude  = -0.1;
    ev.qualityScore = 0.9;
    return ev;
}

static double sampleGridDensity(const std::vector<std::vector<double>>& grid,
                                double lat, double lon,
                                double latMin, double latMax,
                                double lonMin, double lonMax)
{
    const int n = static_cast<int>(grid.size());
    if (n == 0) return 0.0;
    const double latFrac = (lat - latMin) / (latMax - latMin);
    const double lonFrac = (lon - lonMin) / (lonMax - lonMin);
    const int r = std::clamp(static_cast<int>(latFrac * (n - 1)), 0, n - 1);
    const int c = std::clamp(static_cast<int>(lonFrac * (n - 1)), 0, n - 1);
    return grid[static_cast<size_t>(r)][static_cast<size_t>(c)];
}

static HintEngineInput makeFullHintInput()
{
    HintEngineInput input;

    CrimeEvent ev;
    ev.eventId    = QStringLiteral("sla_hint_001");
    ev.id         = ev.eventId;
    ev.crimeType  = QStringLiteral("burglary");
    ev.suburb     = QStringLiteral("Westminster");
    ev.lat        = 51.505;
    ev.lon        = -0.128;
    ev.latitude   = 51.505;
    ev.longitude  = -0.128;
    ev.occurredAt = utcDt(QDate(2024, 6, 1), QTime(22, 30, 0));
    ev.timestamp  = ev.occurredAt.value_or(QDateTime());
    ev.narrative  = QStringLiteral("forced entry rear window residential night");
    ev.qualityScore = 0.9;
    input.event = ev;

    for (int i = 0; i < 5; ++i) {
        SeriesMatch sm;
        sm.seriesId             = QStringLiteral("series_%1").arg(i);
        sm.memberCount          = 4 + i;
        sm.linkProbability      = 0.55 + i * 0.08;
        sm.spatialDistanceM     = 120.0 + i * 40.0;
        sm.temporalDistanceDays = 2.0 + i;
        sm.moSimilarity         = 0.7 + i * 0.04;
        sm.compositeScore       = 0.6 + i * 0.05;
        sm.method               = QStringLiteral("dbscan");
        input.seriesMatches.append(sm);
    }

    for (int i = 0; i < 5; ++i) {
        MOMatch mm;
        mm.caseId          = QStringLiteral("case_%1").arg(i);
        mm.similarityScore = 0.65 + i * 0.05;
        mm.resolved        = (i % 2 == 0);
        mm.outcome         = mm.resolved ? QStringLiteral("convicted")
                                         : QStringLiteral("open");
        mm.suspectProfile  = QStringLiteral("male 25-40");
        input.moMatches.append(mm);
    }

    QVector<QPair<double, double>> locs;
    for (int i = 0; i < 10; ++i)
        locs.append({51.505 + i * 0.001, -0.128 + i * 0.001});
    GeographicProfiler profiler(1.2, 1.2, 0.5, 20);
    input.geoProfile = profiler.profile(locs);

    for (int i = 0; i < 3; ++i) {
        NetworkLead nl;
        nl.personId        = QStringLiteral("person_%1").arg(i);
        nl.connectionType  = QStringLiteral("direct_cooffender");
        nl.sharedIncidents = 2 + i;
        nl.centralityScore = 0.4 + i * 0.15;
        nl.communityId     = i;
        nl.riskScore       = 0.55 + i * 0.1;
        nl.reasoning       = QStringLiteral("Shared incidents in series");
        input.networkLeads.append(nl);
    }

    for (int i = 0; i < 4; ++i) {
        EvidenceWeight ew;
        ew.evidenceType         = QStringLiteral("forensic_%1").arg(i);
        ew.likelihoodRatio      = 2.0 + i;
        ew.posteriorOdds        = 1.5 + i * 0.5;
        ew.posteriorProbability = 0.55 + i * 0.08;
        ew.reliability          = 0.8;
        input.evidenceWeights.append(ew);
    }

    AnomalySignal sig;
    sig.eventId          = ev.eventId;
    sig.isolationScore   = 0.72;
    sig.lofScore         = 1.8;
    sig.zScoreTemporal   = 2.1;
    sig.zScoreSpatial    = 1.6;
    sig.combinedScore    = 0.68;
    sig.isAnomaly        = true;
    sig.signalReasons    = {QStringLiteral("temporal_outlier"),
                            QStringLiteral("spatial_outlier")};
    input.anomalySignal  = sig;
    input.dataQuality    = 0.85;
    return input;
}

} // namespace

class SLAAccuracyTest : public QObject {
    Q_OBJECT

private slots:

    // ── Performance SLA tests ────────────────────────────────────────────────

    void testPoissonFitSpeed()
    {
        QVector<PoissonBaseline::EventRecord> events;
        events.reserve(1000);
        const QDateTime base = utcDt(QDate(2024, 1, 1));
        for (int i = 0; i < 1000; ++i) {
            PoissonBaseline::EventRecord r;
            r.zoneId     = QStringLiteral("zone_%1").arg(i % 10);
            r.crimeType  = QStringLiteral("burglary");
            r.occurredAt = base.addSecs(i * 3600);
            events.append(r);
        }

        PoissonBaseline model;
        QElapsedTimer timer;
        timer.start();
        model.fit(events);
        const qint64 elapsed = timer.elapsed();

        QVERIFY(model.isFitted());
        QVERIFY2(elapsed < 1000,
                 qPrintable(QStringLiteral("Poisson fit took %1 ms (limit 1000 ms)").arg(elapsed)));
    }

    void testPoissonPredictSpeed()
    {
        QVector<PoissonBaseline::EventRecord> events;
        const QDateTime base = utcDt(QDate(2024, 1, 1));
        for (int i = 0; i < 1000; ++i) {
            PoissonBaseline::EventRecord r;
            r.zoneId     = QStringLiteral("zone_%1").arg(i % 10);
            r.crimeType  = QStringLiteral("theft");
            r.occurredAt = base.addSecs(i * 3600);
            events.append(r);
        }

        PoissonBaseline model;
        model.fit(events);
        QVERIFY(model.isFitted());

        QElapsedTimer timer;
        timer.start();
        for (int i = 0; i < 1000; ++i) {
            const auto pred = model.predict(
                QStringLiteral("zone_%1").arg(i % 10),
                base.addSecs(i * 3600),
                QStringLiteral("theft"));
            Q_UNUSED(pred)
        }
        const qint64 elapsed = timer.elapsed();

        QVERIFY2(elapsed < 200,
                 qPrintable(QStringLiteral("Poisson predict took %1 ms (limit 200 ms)").arg(elapsed)));
    }

    void testHawkesFitSpeed()
    {
        QVector<SpatiotemporalEvent> events;
        events.reserve(200);
        for (int i = 0; i < 200; ++i) {
            SpatiotemporalEvent ev;
            ev.tDays     = i * 0.5;
            ev.lat       = 51.5 + (i % 7) * 0.002;
            ev.lon       = -0.1 + (i % 5) * 0.002;
            ev.crimeType = QStringLiteral("burglary");
            events.append(ev);
        }

        HawkesProcess model;
        QElapsedTimer timer;
        timer.start();
        model.fit(events, 10);
        const qint64 elapsed = timer.elapsed();

        QVERIFY(model.isFitted());
        QVERIFY2(elapsed < 5000,
                 qPrintable(QStringLiteral("Hawkes fit took %1 ms (limit 5000 ms)").arg(elapsed)));
    }

    void testSeriesDetectorSpeed()
    {
        QVector<SeriesEvent> events;
        events.reserve(500);
        for (int i = 0; i < 500; ++i) {
            SeriesEvent ev;
            ev.eventId   = QStringLiteral("sd_%1").arg(i);
            ev.lat       = 51.5 + (i % 20) * 0.001;
            ev.lon       = -0.1 + (i % 15) * 0.001;
            ev.tDays     = i * 0.2;
            ev.crimeType = QStringLiteral("burglary");
            ev.moText    = QStringLiteral("forced entry residential");
            events.append(ev);
        }

        SeriesDetector detector(0.3, 14.0, 3);
        QElapsedTimer timer;
        timer.start();
        const auto series = detector.detectSeries(events);
        const qint64 elapsed = timer.elapsed();

        Q_UNUSED(series)
        QVERIFY2(elapsed < 3000,
                 qPrintable(QStringLiteral("SeriesDetector took %1 ms (limit 3000 ms)").arg(elapsed)));
    }

    void testGPRegressionFitSpeed()
    {
        QVector<QPair<double, double>> X;
        QVector<double> y;
        X.reserve(50);
        y.reserve(50);
        for (int i = 0; i < 50; ++i) {
            const double x = static_cast<double>(i) * 0.1;
            X.append({x, 0.0});
            y.append(std::sin(x));
        }

        GPRegression gp;
        QElapsedTimer timer;
        timer.start();
        gp.fit(X, y);
        const qint64 elapsed = timer.elapsed();

        QVERIFY(gp.isFitted());
        QVERIFY2(elapsed < 2000,
                 qPrintable(QStringLiteral("GP fit took %1 ms (limit 2000 ms)").arg(elapsed)));
    }

    void testKDEFitSpeed()
    {
        QVector<QPair<double, double>> locs;
        locs.reserve(500);
        for (int i = 0; i < 500; ++i) {
            const double dlat = ((i * 7) % 20 - 10) * 0.001;
            const double dlon = ((i * 3) % 20 - 10) * 0.001;
            locs.append({51.5 + dlat, -0.1 + dlon});
        }

        KDEHotspot kde(50, 1.0);
        QElapsedTimer timer;
        timer.start();
        const auto surface = kde.compute(locs, 51.4, 51.6, -0.2, 0.0);
        const qint64 elapsed = timer.elapsed();

        QVERIFY(!surface.empty());
        QVERIFY2(elapsed < 500,
                 qPrintable(QStringLiteral("KDE fit took %1 ms (limit 500 ms)").arg(elapsed)));
    }

    void testBayesianFitSpeed()
    {
        QVector<CrimeEvent> events;
        events.reserve(500);
        for (int i = 0; i < 500; ++i)
            events.append(makeCrimeEvent(QStringLiteral("zone_%1").arg(i % 10), i));

        BayesianHierarchical model;
        QElapsedTimer timer;
        timer.start();
        model.fit(events, 30.0);
        const qint64 elapsed = timer.elapsed();

        QVERIFY(model.isFitted());
        QCOMPARE(model.zoneCount(), 10);
        QVERIFY2(elapsed < 500,
                 qPrintable(QStringLiteral("Bayesian fit took %1 ms (limit 500 ms)").arg(elapsed)));
    }

    void testHintEngineSpeed()
    {
        const HintEngineInput input = makeFullHintInput();
        HintEngine engine;

        QElapsedTimer timer;
        timer.start();
        const auto leads = engine.generate(input);
        const qint64 elapsed = timer.elapsed();

        QVERIFY(!leads.isEmpty());
        QVERIFY2(elapsed < 50,
                 qPrintable(QStringLiteral("HintEngine generate took %1 ms (limit 50 ms)").arg(elapsed)));
    }

    void testNLPClassifierSpeed()
    {
        CrimeClassifier classifier;
        const QStringList texts = {
            QStringLiteral("armed robbery at convenience store with knife"),
            QStringLiteral("vehicle break-in window smashed electronics stolen"),
            QStringLiteral("assault victim punched outside nightclub"),
            QStringLiteral("residential burglary forced rear door entry"),
            QStringLiteral("shoplifting suspect detained by security guard"),
            QStringLiteral("vandalism graffiti spray paint on public building"),
            QStringLiteral("theft bicycle stolen from train station rack"),
            QStringLiteral("sexual assault reported in park at night"),
            QStringLiteral("drug possession cannabis found during search"),
            QStringLiteral("fraud online scam elderly victim bank transfer"),
        };

        QElapsedTimer timer;
        timer.start();
        for (int i = 0; i < 100; ++i) {
            const auto result = classifier.classify(texts.at(i % texts.size()));
            Q_UNUSED(result)
        }
        const qint64 elapsed = timer.elapsed();

        QVERIFY2(elapsed < 100,
                 qPrintable(QStringLiteral("CrimeClassifier took %1 ms (limit 100 ms)").arg(elapsed)));
    }

    // ── Model accuracy validation tests ──────────────────────────────────────

    void testPoissonProbAtLeastOneMonotone()
    {
        const QDateTime query = utcDt(QDate(2025, 1, 6)); // Monday, January, hour 10
        const QString zone = QStringLiteral("mono_zone");
        const QString type = QStringLiteral("theft");

        QVector<QDate> mondays;
        for (int year = 2015; year <= 2024 && mondays.size() < 10; ++year) {
            for (int day = 1; day <= 31; ++day) {
                const QDate d(year, 1, day);
                if (d.isValid() && d.dayOfWeek() == Qt::Monday) {
                    mondays.append(d);
                    if (mondays.size() >= 10) break;
                }
            }
        }
        QVERIFY(mondays.size() >= 5);

        QVector<PoissonBaseline::EventRecord> sparse;
        for (const QDate& d : mondays) {
            PoissonBaseline::EventRecord r;
            r.zoneId     = zone;
            r.crimeType  = type;
            r.occurredAt = utcDt(d);
            sparse.append(r);
        }

        QVector<PoissonBaseline::EventRecord> dense;
        for (const QDate& d : mondays) {
            for (int k = 0; k < 4; ++k) {
                PoissonBaseline::EventRecord r;
                r.zoneId     = zone;
                r.crimeType  = type;
                r.occurredAt = utcDt(d);
                dense.append(r);
            }
        }

        PoissonBaseline sparseModel;
        sparseModel.fit(sparse);
        const double pSparse = sparseModel.predict(zone, query, type).probAtLeastOne;

        PoissonBaseline denseModel;
        denseModel.fit(dense);
        const double pDense = denseModel.predict(zone, query, type).probAtLeastOne;

        QVERIFY2(pDense > pSparse,
                 qPrintable(QStringLiteral("Expected P(X>=1) to increase: sparse=%1 dense=%2")
                                .arg(pSparse).arg(pDense)));
    }

    void testPoissonExpectedCountPositive()
    {
        QVector<PoissonBaseline::EventRecord> events;
        for (int i = 0; i < 30; ++i) {
            PoissonBaseline::EventRecord r;
            r.zoneId     = QStringLiteral("pos_zone");
            r.crimeType  = QStringLiteral("burglary");
            r.occurredAt = utcDt(QDate(2024, 1, 1).addDays(i));
            events.append(r);
        }

        PoissonBaseline model;
        model.fit(events);
        const auto pred = model.predict(
            QStringLiteral("pos_zone"),
            utcDt(QDate(2025, 1, 6)),
            QStringLiteral("burglary"));

        QVERIFY2(pred.expectedCount > 0.0,
                 qPrintable(QStringLiteral("expectedCount=%1 should be > 0")
                                .arg(pred.expectedCount)));
    }

    void testHawkesIntensityDecayExponential()
    {
        HawkesProcess hp;
        HawkesParams p;
        p.mu    = 0.05;
        p.alpha = 0.8;
        p.beta  = 1.0;
        p.sigma = 0.01;
        hp.setParams(p);

        SpatiotemporalEvent trigger;
        trigger.tDays     = 100.0;
        trigger.lat       = 51.5;
        trigger.lon       = -0.1;
        trigger.crimeType = QStringLiteral("burglary");
        hp.setHistory({trigger});

        const double tRef = 100.0;
        const double lat  = 51.5;
        const double lon  = -0.1;
        const double i1d  = hp.intensity(tRef + 1.0, lat, lon);
        const double i10d = hp.intensity(tRef + 10.0, lat, lon);

        QVERIFY2(i10d < i1d,
                 qPrintable(QStringLiteral("intensity(t+10d)=%1 should be < intensity(t+1d)=%2")
                                .arg(i10d).arg(i1d)));
    }

    void testSeriesDetectorGroupsRelatedEvents()
    {
        QVector<SeriesEvent> events;
        const QString mo = QStringLiteral("forced entry rear window residential");
        for (int i = 0; i < 10; ++i) {
            SeriesEvent ev;
            ev.eventId   = QStringLiteral("cluster_%1").arg(i);
            ev.lat       = 51.5000;
            ev.lon       = -0.1000;
            ev.tDays     = 10.0 + i * 0.1;
            ev.crimeType = QStringLiteral("burglary");
            ev.moText    = mo;
            events.append(ev);
        }

        SeriesDetector detector(0.5, 7.0, 3);
        const auto series = detector.detectSeries(events);

        QVERIFY2(!series.isEmpty(),
                 qPrintable(QStringLiteral("Expected 1 cluster, got %1 series").arg(series.size())));

        const int maxMembers = std::max_element(
            series.begin(), series.end(),
            [](const CrimeSeries& a, const CrimeSeries& b) {
                return a.members.size() < b.members.size();
            })->members.size();

        QVERIFY2(maxMembers >= 10,
                 qPrintable(QStringLiteral("Expected cluster of 10, largest has %1")
                                .arg(maxMembers)));
    }

    void testKDEDensityHigherAtCluster()
    {
        QVector<QPair<double, double>> locs;
        const double cLat = 51.50;
        const double cLon = -0.10;
        for (int i = 0; i < 40; ++i) {
            const double dlat = ((i * 7) % 10 - 5) * 0.001;
            const double dlon = ((i * 3) % 10 - 5) * 0.001;
            locs.append({cLat + dlat, cLon + dlon});
        }

        const double latMin = 51.40, latMax = 51.70;
        const double lonMin = -0.30, lonMax = 0.10;
        const double farLat = cLat + 0.09; // ~10 km north
        const double farLon = cLon;

        KDEHotspot kde(60, 1.0);
        const auto grid = kde.compute(locs, latMin, latMax, lonMin, lonMax);

        const double densityCluster = sampleGridDensity(grid, cLat, cLon,
                                                        latMin, latMax, lonMin, lonMax);
        const double densityFar     = sampleGridDensity(grid, farLat, farLon,
                                                        latMin, latMax, lonMin, lonMax);

        QVERIFY2(densityCluster > densityFar,
                 qPrintable(QStringLiteral("Cluster density %1 should exceed far density %2")
                                .arg(densityCluster).arg(densityFar)));
    }

    void testBayesianShrinkage()
    {
        QVector<CrimeEvent> events;
        for (int i = 0; i < 200; ++i)
            events.append(makeCrimeEvent(QStringLiteral("dense_zone"), i));
        events.append(makeCrimeEvent(QStringLiteral("sparse_zone"), 999));

        BayesianHierarchical model;
        model.fit(events, 30.0);
        QVERIFY(model.isFitted());

        const double globalMean   = model.globalMean();
        const double shrinkDense  = model.shrinkageEstimate(QStringLiteral("dense_zone"));
        const double shrinkSparse = model.shrinkageEstimate(QStringLiteral("sparse_zone"));

        const double distDense  = std::abs(shrinkDense - globalMean);
        const double distSparse = std::abs(shrinkSparse - globalMean);

        QVERIFY2(distSparse < distDense,
                 qPrintable(QStringLiteral(
                     "Sparse zone should shrink more toward global mean: "
                     "distSparse=%1 distDense=%2 global=%3")
                                .arg(distSparse).arg(distDense).arg(globalMean)));
    }

    void testEnsembleUncertaintyDecomposition()
    {
        QVector<PoissonBaseline::EventRecord> poiRecs;
        const QList<QDate> mondays = {
            QDate(2024, 1, 1), QDate(2024, 1, 8), QDate(2024, 1, 15),
            QDate(2024, 1, 22), QDate(2024, 1, 29),
        };
        for (const QDate& d : mondays) {
            for (int i = 0; i < 8; ++i) {
                PoissonBaseline::EventRecord r;
                r.zoneId     = QStringLiteral("ensemble_zone");
                r.occurredAt = utcDt(d);
                r.crimeType  = QStringLiteral("burglary");
                poiRecs.append(r);
            }
        }

        PoissonBaseline poisson;
        poisson.fit(poiRecs);

        QVector<SpatiotemporalEvent> hawkesEvents;
        for (int i = 0; i < 30; ++i) {
            SpatiotemporalEvent ev;
            ev.tDays     = static_cast<double>(i) * 2.0;
            ev.lat       = 51.48;
            ev.lon       = -0.15;
            ev.crimeType = QStringLiteral("burglary");
            hawkesEvents.append(ev);
        }
        HawkesProcess hawkes;
        hawkes.fit(hawkesEvents, 10);

        EnsemblePredictor ensemble;
        ensemble.setPoisson(&poisson);
        ensemble.setHawkes(&hawkes);
        ensemble.setWeights(0.5, 0.5);

        const auto pred = ensemble.predict(
            QStringLiteral("ensemble_zone"),
            utcDt(QDate(2024, 2, 5)),
            QStringLiteral("burglary"),
            51.5, -0.1);

        const double combined = pred.uncertaintyAleatoric + pred.uncertaintyEpistemic;
        QVERIFY2(combined > 0.0,
                 qPrintable(QStringLiteral(
                     "Aleatoric+epistemic=%1 should be > 0 (aleatoric=%2 epistemic=%3)")
                                .arg(combined)
                                .arg(pred.uncertaintyAleatoric)
                                .arg(pred.uncertaintyEpistemic)));
    }

    void testBenchmarkMetricsFullReport()
    {
        QVector<double> yTrue, yPred;
        for (int i = 0; i < 50; ++i) {
            yTrue.append(1.0);
            yPred.append(0.9 - i * 0.001);
        }
        for (int i = 0; i < 50; ++i) {
            yTrue.append(0.0);
            yPred.append(0.1 + i * 0.001);
        }

        const BenchmarkReport report = BenchmarkMetrics::fullReport(yTrue, yPred);
        const QString text = report.reportText();

        QVERIFY2(!text.isEmpty(),
                 qPrintable(QStringLiteral("fullReport reportText should be non-empty")));
        QVERIFY(text.contains(QStringLiteral("BenchmarkReport")));
        QCOMPARE(report.nSamples, 100);
    }

    void testBenchmarkPAIAboveOne()
    {
        const int n = 100;
        const int nCrimes = 10;
        QVector<double> yTrue(n, 0.0);
        QVector<double> yPred(n, 0.0);

        for (int i = 0; i < nCrimes; ++i) {
            yTrue[i] = 1.0;
            yPred[i] = 1.0;
        }
        for (int i = nCrimes; i < n; ++i)
            yPred[i] = 0.0;

        const double pai = BenchmarkMetrics::pai(yTrue, yPred, 0.10);
        QVERIFY2(pai > 1.0,
                 qPrintable(QStringLiteral("Good predictor PAI=%1 should be > 1.0").arg(pai)));
    }

    void testBenchmarkAUCROCAbove50pct()
    {
        const int n = 100;
        QVector<double> yTrue, yPred;
        for (int i = 0; i < n / 2; ++i) {
            yTrue.append(1.0);
            yPred.append(0.7 + i * 0.002);
        }
        for (int i = 0; i < n / 2; ++i) {
            yTrue.append(0.0);
            yPred.append(0.3 - i * 0.002);
        }

        const double auc = BenchmarkMetrics::aucRoc(yTrue, yPred);
        QVERIFY2(auc > 0.5,
                 qPrintable(QStringLiteral("Reasonable predictor AUC-ROC=%1 should be > 0.5")
                                .arg(auc)));
    }
};

QTEST_MAIN(SLAAccuracyTest)
#include "test_sla_accuracy.moc"
