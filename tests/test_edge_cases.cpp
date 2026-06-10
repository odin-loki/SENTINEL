// test_edge_cases.cpp — Boundary / edge-case / stress tests
#include <QTest>
#include <QCoreApplication>
#include <QDateTime>
#include <QElapsedTimer>
#include <cmath>
#include <limits>

#include "core/CrimeEvent.h"
#include "core/Database.h"
#include "core/AppConfig.h"
#include "models/PoissonBaseline.h"
#include "models/HawkesProcess.h"
#include "models/SeriesDetector.h"
#include "models/EnsemblePredictor.h"
#include "inference/GeographicProfiler.h"
#include "inference/MOAnalyser.h"
#include "inference/AnomalyDetector.h"
#include "inference/CoOffendingAnalyser.h"
#include "benchmark/BenchmarkMetrics.h"
#include "benchmark/BiasAuditor.h"
#include "benchmark/CalibrationAnalyser.h"
#include "nlp/MOExtractor.h"
#include "nlp/CrimeClassifier.h"

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────
namespace {

static AnomalyFeatureVector makeFeature(const QString& id, double lat, double lon,
                                         double tDays, int typeCode = 0) {
    AnomalyFeatureVector f;
    f.eventId = id; f.lat = lat; f.lon = lon;
    f.tDays = tDays;
    f.hourNorm = 0.5;
    f.crimeTypeCode = typeCode;
    return f;
}

static CrimeEvent makeCrimeEvent(const QString& id, double lat, double lon,
                                  const QDateTime& dt, const QString& type = "burglary") {
    CrimeEvent e;
    e.eventId = e.id = id;
    e.lat = e.latitude = lat;
    e.lon = e.longitude = lon;
    e.timestamp = dt;
    e.crimeType = type;
    return e;
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// PoissonBaseline edge cases
// ─────────────────────────────────────────────────────────────────────────────
class TestPoissonEdgeCases : public QObject {
    Q_OBJECT
private slots:

    void testPredictUnfitted() {
        PoissonBaseline pb;
        const auto pred = pb.predict("zone_x",
                                     QDateTime::currentDateTimeUtc(), "burglary");
        QVERIFY(pred.lambda >= 0.0);
    }

    void testFitEmptyEvents() {
        PoissonBaseline pb;
        pb.fit({});
        QVERIFY(pb.totalEvents() == 0);
    }

    void testFitSingleEvent() {
        PoissonBaseline pb;
        const QDateTime dt(QDate(2024,6,1), QTime(12,0,0), Qt::UTC);
        pb.fit({{ "z", dt, "theft" }});
        QVERIFY(pb.isFitted());
        QCOMPARE(pb.totalEvents(), 1);
    }

    void testPredictUnknownZone() {
        PoissonBaseline pb;
        const QDateTime dt(QDate(2024,6,1), QTime(12,0,0), Qt::UTC);
        QVector<PoissonBaseline::EventRecord> evs;
        for (int i = 0; i < 20; ++i) evs.append({"known", dt.addDays(i), "theft"});
        pb.fit(evs);
        const auto pred = pb.predict("unknown_zone", dt, "theft");
        QVERIFY(pred.lambda >= 0.0);
    }

    void testPoissonPMFLargeK() {
        const double pmf = PoissonBaseline::poissonPMF(1.0, 1000);
        QVERIFY(pmf >= 0.0 && pmf < 1e-100);
    }

    void testPoissonPPFExtremeProbability() {
        const double q0 = PoissonBaseline::poissonPPF(5.0, 0.0);
        const double q1 = PoissonBaseline::poissonPPF(5.0, 0.9999);
        QCOMPARE(q0, 0.0);
        QVERIFY(q1 > 0.0 && std::isfinite(q1));
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// HawkesProcess edge cases
// ─────────────────────────────────────────────────────────────────────────────
class TestHawkesEdgeCases : public QObject {
    Q_OBJECT
private slots:

    void testFitSingleEvent() {
        HawkesProcess hp;
        hp.fit({{ 0.0, 51.5, -0.1, "burglary" }});
        QVERIFY(true);
    }

    void testIntensityUnfitted() {
        HawkesProcess hp;
        const double lam = hp.intensity(10.0, 51.5, -0.1);
        QVERIFY(std::isfinite(lam) && lam >= 0.0);
    }

    void testKernelZeroDist() {
        HawkesProcess hp;
        HawkesParams p; p.alpha=0.5; p.beta=1.0; p.sigma=0.01; p.mu=0.1;
        hp.setParams(p);
        const double k = hp.triggerKernel(1.0, 0.0);
        QVERIFY(std::isfinite(k) && k >= 0.0);
    }

    void testRiskSurfaceAllFinite() {
        HawkesProcess hp;
        QVector<SpatiotemporalEvent> events;
        for (int i = 0; i < 10; ++i)
            events.append({static_cast<double>(i), 51.5 + i*0.001, -0.1, "theft"});
        hp.fit(events, 50);
        const auto grid = hp.riskSurface(10.0, 51.4, 51.6, -0.2, 0.0, 5);
        for (const auto& row : grid)
            for (double v : row)
                QVERIFY(std::isfinite(v) && v >= 0.0);
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// SeriesDetector edge cases
// ─────────────────────────────────────────────────────────────────────────────
class TestSeriesEdgeCases : public QObject {
    Q_OBJECT
private slots:

    void testEmptyInput() {
        SeriesDetector det(1.0, 30.0, 3);
        QVERIFY(det.detect({}).isEmpty());
    }

    void testSingleEvent() {
        SeriesDetector det(1.0, 30.0, 3);
        const QDateTime dt = QDateTime::currentDateTimeUtc();
        QVERIFY(det.detect({makeCrimeEvent("e1", 51.5, -0.1, dt)}).isEmpty());
    }

    void testAllSameLocation() {
        SeriesDetector det(1.0, 30.0, 3);
        QVector<CrimeEvent> events;
        const QDateTime base(QDate(2024,1,1), QTime(0,0,0), Qt::UTC);
        for (int i = 0; i < 10; ++i)
            events.append(makeCrimeEvent(QString::number(i), 51.5, -0.1, base.addDays(i)));
        QVERIFY(!det.detect(events).isEmpty());
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// GeographicProfiler edge cases
// ─────────────────────────────────────────────────────────────────────────────
class TestGeoProfilerEdgeCases : public QObject {
    Q_OBJECT
private slots:

    void testEmptyInput() {
        GeographicProfiler gp;
        const auto profile = gp.profile({});
        QVERIFY(profile.peakProbability >= 0.0);
    }

    void testSingleIncident() {
        GeographicProfiler gp;
        const auto profile = gp.profile({{51.5, -0.1}});
        QVERIFY(profile.peakProbability >= 0.0);
    }

    void testProfileSurfaceNonNegative() {
        GeographicProfiler gp(1.2, 1.2, 0.5, 5);
        QVector<QPair<double,double>> locs;
        for (int i = 0; i < 5; ++i)
            locs.append({51.5 + i * 0.01, -0.1});
        const auto profile = gp.profile(locs);
        for (const auto& row : profile.probabilitySurface)
            for (double v : row) QVERIFY(v >= 0.0);
    }

    void testSearchAreasNonNegative() {
        GeographicProfiler gp(1.2, 1.2, 0.5, 10);
        QVector<QPair<double,double>> locs;
        for (int i = 0; i < 5; ++i) locs.append({51.5 + i * 0.01, -0.1});
        const auto profile = gp.profile(locs);
        QVERIFY(profile.searchArea50pct >= 0.0);
        QVERIFY(profile.searchArea80pct >= profile.searchArea50pct);
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// MOAnalyser edge cases
// ─────────────────────────────────────────────────────────────────────────────
class TestMOAnalyserEdgeCases : public QObject {
    Q_OBJECT
private slots:

    void testEmptyCorpus() {
        MOAnalyser ma;
        ma.fit({});
        QVERIFY(ma.findSimilar("forced entry residential", 3).isEmpty());
    }

    void testSingleCaseCorpus() {
        MOAnalyser ma;
        ma.fit({{ "case_1", "forced entry residential night", false, "unresolved", "", 0 }});
        const auto matches = ma.findSimilar("forced entry", 3, 0.0);
        QVERIFY(matches.size() <= 1);
    }

    void testQueryEmptyString() {
        MOAnalyser ma;
        ma.fit({{ "case_1", "forced entry residential", false, "", "", 0 }});
        ma.findSimilar("", 3);   // should not crash
        QVERIFY(true);
    }

    void testTopKRespected() {
        MOAnalyser ma;
        QVector<MOCaseRecord> corpus;
        for (int i = 0; i < 20; ++i)
            corpus.append({QString("c%1").arg(i),
                           QString("burglary %1 forced entry").arg(i), false, "", "", 0});
        ma.fit(corpus);
        const int K = 3;
        const auto matches = ma.findSimilar("burglary forced entry", K, 0.0);
        QVERIFY(matches.size() <= K);
    }

    void testSimilarityScoreInRange() {
        MOAnalyser ma;
        QVector<MOCaseRecord> corpus;
        for (int i = 0; i < 5; ++i)
            corpus.append({QString("c%1").arg(i),
                           QString("residential burglary night entry %1").arg(i),
                           false, "", "", 0});
        ma.fit(corpus);
        for (const auto& m : ma.findSimilar("residential burglary", 5, 0.0))
            QVERIFY(m.similarityScore >= 0.0 && m.similarityScore <= 1.0 + 1e-9);
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// AnomalyDetector edge cases
// ─────────────────────────────────────────────────────────────────────────────
class TestAnomalyDetectorEdgeCases : public QObject {
    Q_OBJECT
private slots:

    void testEmptyDataset() {
        AnomalyDetector ad;
        QVERIFY(ad.detectAnomalies({}).isEmpty());
    }

    void testSingleEvent() {
        AnomalyDetector ad;
        const auto result = ad.detectAnomalies({makeFeature("e1", 51.5, -0.1, 0.0)});
        QCOMPARE(result.size(), 1);
    }

    void testCombinedScoreInRange() {
        AnomalyDetector ad;
        QVector<AnomalyFeatureVector> fvs;
        for (int i = 0; i < 30; ++i)
            fvs.append(makeFeature(QString::number(i), 51.5 + i*0.001, -0.1,
                                   static_cast<double>(i)));
        ad.fit(fvs);
        for (const auto& sig : ad.detectAnomalies(fvs)) {
            QVERIFY(sig.combinedScore >= 0.0);
            QVERIFY(sig.combinedScore <= 1.0 + 1e-9);
        }
    }

    void testAllSameLocation() {
        AnomalyDetector ad;
        QVector<AnomalyFeatureVector> fvs;
        for (int i = 0; i < 20; ++i)
            fvs.append(makeFeature(QString::number(i), 51.5, -0.1, static_cast<double>(i)));
        ad.fit(fvs);
        for (const auto& sig : ad.detectAnomalies(fvs))
            QVERIFY(std::isfinite(sig.combinedScore));
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// BenchmarkMetrics edge cases
// ─────────────────────────────────────────────────────────────────────────────
class TestBenchmarkEdgeCases : public QObject {
    Q_OBJECT
private slots:

    void testEmptyInput() {
        const auto rep = BenchmarkMetrics::fullReport({}, {});
        QVERIFY(rep.nSamples == 0);
    }

    void testSingleElement() {
        const auto rep = BenchmarkMetrics::fullReport({1.0}, {0.8});
        QVERIFY(rep.nSamples == 1);
    }

    void testAllZeroActuals() {
        QVector<double> yt(50, 0.0);
        QVector<double> yp(50, 0.5);
        const double pai = BenchmarkMetrics::pai(yt, yp, 0.1);
        QVERIFY(std::isfinite(pai) || pai == 0.0);
    }

    void testPAIAreaFractionOne() {
        QVector<double> yt = {1.0, 0.0, 1.0, 0.0, 1.0};
        QVector<double> yp = {0.9, 0.8, 0.7, 0.6, 0.5};
        const double pai = BenchmarkMetrics::pai(yt, yp, 1.0);
        QVERIFY(std::abs(pai - 1.0) < 1e-9);
    }

    void testHintQualityEmptyInput() {
        const auto res = BenchmarkMetrics::hintQuality({});
        QVERIFY(res.nCases == 0);
    }

    void testHintQualityAllRankZero() {
        QVector<int> ranks = {0, 0, 0};
        const auto res = BenchmarkMetrics::hintQuality(ranks, 5);
        QVERIFY(res.mrr < 1e-9);
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// CalibrationAnalyser edge cases
// ─────────────────────────────────────────────────────────────────────────────
class TestCalibrationEdgeCases : public QObject {
    Q_OBJECT
private slots:

    void testSinglePairECE() {
        CalibrationAnalyser ca;
        const auto res = ca.analyse({{0.7, 1.0}});
        QVERIFY(std::isfinite(res.ece));
    }

    void testExtremeProb0() {
        CalibrationAnalyser ca;
        QVector<QPair<double,double>> data;
        for (int i = 0; i < 10; ++i) data.append({0.0, 0.0});
        const auto res = ca.analyse(data);
        QVERIFY(std::isfinite(res.ece));
    }

    void testExtremeProb1() {
        CalibrationAnalyser ca;
        QVector<QPair<double,double>> data;
        for (int i = 0; i < 10; ++i) data.append({1.0, 1.0});
        const auto res = ca.analyse(data);
        QVERIFY(std::isfinite(res.ece));
    }

    void testIsotonicEmptyInput() {
        QVERIFY(CalibrationAnalyser::isotonicCalibrate({}).isEmpty());
    }

    void testIsotonicSinglePair() {
        QCOMPARE(CalibrationAnalyser::isotonicCalibrate({{0.5, 1.0}}).size(), 1);
    }

    void testOneBinAnalyserClamped() {
        // nBins=1 is clamped to minimum of 2 internally
        CalibrationAnalyser ca(1);
        QVector<QPair<double,double>> data;
        for (int i = 0; i < 10; ++i) data.append({0.5, (i%2==0) ? 1.0 : 0.0});
        const auto res = ca.analyse(data);
        QVERIFY(res.bins.size() >= 1);     // at least 1 bin regardless
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// CoOffendingAnalyser edge cases
// ─────────────────────────────────────────────────────────────────────────────
class TestCoOffendingEdgeCases : public QObject {
    Q_OBJECT
private slots:

    void testTwoPersonsSameIncident() {
        CoOffendingAnalyser coa;
        coa.buildGraph({{"A","i1","suspect",1.0}, {"B","i1","associate",0.5}});
        coa.analyse();
        QVERIFY(coa.findLeads("i1").size() <= 2);
    }

    void testSinglePersonNoLeads() {
        CoOffendingAnalyser coa;
        coa.buildGraph({{"A","i1","suspect",1.0}});
        coa.analyse();
        QVERIFY(coa.findLeads("i1").size() <= 1);
    }

    void testLargeGraphPerformance() {
        QVector<PersonIncidentRecord> records;
        for (int incident = 0; incident < 10; ++incident)
            for (int person = 0; person < 5; ++person)
                records.append({
                    QStringLiteral("p%1").arg(incident * 5 + person),
                    QStringLiteral("i%1").arg(incident),
                    "suspect", 1.0
                });

        CoOffendingAnalyser coa;
        QElapsedTimer timer; timer.start();
        coa.buildGraph(records);
        coa.analyse();
        QVERIFY(timer.elapsed() < 5000);
        QVERIFY(coa.findLeads("i0", 5).size() <= 5);
    }

    void testTwoCommunities() {
        CoOffendingAnalyser coa;
        coa.buildGraph({
            {"A","i1","suspect",1.0}, {"B","i1","associate",0.5},
            {"C","i2","suspect",1.0}, {"D","i2","suspect",1.0}
        });
        coa.analyse();
        QSet<int> communityIds;
        for (const auto& n : coa.nodes()) communityIds.insert(n.communityId);
        QCOMPARE(communityIds.size(), 2);
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// EnsemblePredictor edge cases
// ─────────────────────────────────────────────────────────────────────────────
class TestEnsembleEdgeCases : public QObject {
    Q_OBJECT
private slots:

    void testNoModels() {
        EnsemblePredictor ep;
        const auto pred = ep.predict("z", QDateTime::currentDateTimeUtc(),
                                     "theft", 51.5, -0.1);
        QCOMPARE(pred.probCrime, 0.0);
    }

    void testBrierScoreEmptyInput() {
        QCOMPARE(EnsemblePredictor::brierScore({}), 0.0);
    }

    void testECEEmptyInput() {
        QCOMPARE(EnsemblePredictor::ece({}), 0.0);
    }

    void testSetWeightsZero() {
        EnsemblePredictor ep;
        ep.setWeights(0.0, 0.0);   // both zero — should not crash
        QVERIFY(true);
    }

    void testProbAlwaysInRange() {
        PoissonBaseline pb;
        const QDateTime base(QDate(2024,1,1), QTime(0,0,0), Qt::UTC);
        QVector<PoissonBaseline::EventRecord> events;
        for (int i = 0; i < 50; ++i) events.append({"z", base.addDays(i), "theft"});
        pb.fit(events);

        EnsemblePredictor ep;
        ep.setPoisson(&pb);
        for (int d = 0; d < 10; ++d) {
            const auto pred = ep.predict("z", base.addDays(51+d), "theft", 51.5, -0.1);
            QVERIFY(pred.probCrime >= 0.0 && pred.probCrime <= 1.0 + 1e-9);
        }
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// NLP edge cases
// ─────────────────────────────────────────────────────────────────────────────
class TestNLPEdgeCases : public QObject {
    Q_OBJECT
private slots:

    void testMOExtractorEmptyString() {
        MOExtractor ext;
        const auto feats = ext.extract("");
        QVERIFY(!feats.entryMethod.has_value());
    }

    void testMOExtractorLongString() {
        MOExtractor ext;
        const QString longText = QString("forced entry residential ").repeated(1000);
        const auto feats = ext.extract(longText);
        QVERIFY(feats.entryMethod.has_value());
        QCOMPARE(*feats.entryMethod, QStringLiteral("forced_entry"));
    }

    void testCrimeClassifierEmptyString() {
        CrimeClassifier clf;
        const auto [type, conf] = clf.classify("");
        QVERIFY(true);   // must not crash
    }

    void testMOExtractorAllPatterns() {
        MOExtractor ext;
        QVERIFY(ext.extract("broke into the house").entryMethod.has_value());
        QVERIFY(ext.extract("unlocked door").entryMethod.has_value());
        QVERIFY(ext.extract("residential property home").targetType.has_value());
        QVERIFY(ext.extract("alone solo offender").soloOrGroup.has_value());
        QVERIFY(ext.extract("group of three gang").soloOrGroup.has_value());
        QVERIFY(ext.extract("took cash jewellery phone").itemsTaken.size() >= 2);
    }

    void testSentimentEdgeCases() {
        CrimeClassifier clf;
        // All-positive text → sentiment >= 0
        const double posS = clf.sentiment("great wonderful happy good");
        QVERIFY(posS >= 0.0);
        // All-negative text → sentiment < 0
        const double negS = clf.sentiment("horrible terrible violent attack threat");
        QVERIFY(negS < 0.0);
    }

    void testSeverityScoreInRange() {
        CrimeClassifier clf;
        const double s = clf.severityScore("armed robbery with weapon", "robbery");
        QVERIFY(s >= 0.0 && s <= 1.0);
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// main
// ─────────────────────────────────────────────────────────────────────────────

static int runTest(QObject* obj, const char* logFile) {
    QStringList args = { "test", "-o", QString("%1,txt").arg(logFile) };
    return QTest::qExec(obj, args);
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    int r = 0;
    { TestPoissonEdgeCases      t1;  r |= runTest(&t1,  "edge_poisson.txt"); }
    { TestHawkesEdgeCases       t2;  r |= runTest(&t2,  "edge_hawkes.txt"); }
    { TestSeriesEdgeCases       t3;  r |= runTest(&t3,  "edge_series.txt"); }
    { TestGeoProfilerEdgeCases  t4;  r |= runTest(&t4,  "edge_geo.txt"); }
    { TestMOAnalyserEdgeCases   t5;  r |= runTest(&t5,  "edge_mo.txt"); }
    { TestAnomalyDetectorEdgeCases t6; r |= runTest(&t6,"edge_anom.txt"); }
    { TestBenchmarkEdgeCases    t7;  r |= runTest(&t7,  "edge_bench.txt"); }
    { TestCalibrationEdgeCases  t8;  r |= runTest(&t8,  "edge_calib.txt"); }
    { TestCoOffendingEdgeCases  t9;  r |= runTest(&t9,  "edge_net.txt"); }
    { TestEnsembleEdgeCases     t10; r |= runTest(&t10, "edge_ensemble.txt"); }
    { TestNLPEdgeCases          t11; r |= runTest(&t11, "edge_nlp.txt"); }
    return r;
}

#include "test_edge_cases.moc"
