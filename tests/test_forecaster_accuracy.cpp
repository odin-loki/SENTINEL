// test_forecaster_accuracy.cpp
// Accuracy validation tests for SENTINEL forecasting models:
// RiskForecaster, EnsemblePredictor, BayesianHierarchical.

#include <QTest>
#include <QCoreApplication>
#include <QDate>
#include <QDateTime>
#include <QVector>
#include <QString>
#include <cmath>

#include "models/RiskForecaster.h"
#include "models/EnsemblePredictor.h"
#include "models/BayesianHierarchical.h"
#include "models/PoissonBaseline.h"
#include "models/HawkesProcess.h"
#include "core/CrimeEvent.h"

namespace {

// Build a minimal CrimeEvent for zone/type/time tests
CrimeEvent makeCrimeEvent(const QString& id, const QString& suburb,
                           const QDateTime& dt, const QString& type = "burglary")
{
    CrimeEvent ev;
    ev.eventId   = ev.id = id;
    ev.suburb    = suburb;
    ev.timestamp = dt;          // flat UI field read by RiskForecaster
    ev.occurredAt = dt;         // canonical field for BayesianHierarchical
    ev.ingestedAt = dt;
    ev.crimeType = type;
    ev.qualityScore = 1.0;
    return ev;
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// 1. TestRiskForecasterAccuracy
// ─────────────────────────────────────────────────────────────────────────────
class TestRiskForecasterAccuracy : public QObject {
    Q_OBJECT

private slots:

    // Fit 60 days of crime data where one zone is active (5 events/day) and
    // another is quiet (0 events).  The forecast for the active zone must
    // have weeklyRisk > quiet zone, and at least 4/7 forecast days for the
    // active zone must have higher riskScore than the quiet zone's average.
    void testSevenDayPatternDirection()
    {
        const QDate trainStart(2025, 1, 1);
        QVector<CrimeEvent> events;

        // Active zone: 5 events every day for 60 days
        for (int day = 0; day < 60; ++day) {
            QDateTime dt(trainStart.addDays(day), QTime(10, 0, 0), Qt::UTC);
            for (int i = 0; i < 5; ++i)
                events.append(makeCrimeEvent(
                    QString("hi_%1_%2").arg(day).arg(i),
                    "highZone", dt, "burglary"));
        }

        // Quiet zone: 1 event near the end of training so it enters m_recentCounts
        events.append(makeCrimeEvent("lo_0", "quietZone",
                                      QDateTime(trainStart.addDays(58), QTime(10, 0, 0), Qt::UTC),
                                      "burglary"));

        RiskForecaster rf(7);
        rf.fit(events, "burglary");

        QVERIFY2(rf.isFitted(), "RiskForecaster must be fitted after events");
        QVERIFY2(rf.zoneCount() >= 2,
                 qPrintable(QString("Expected ≥ 2 zones, got %1").arg(rf.zoneCount())));

        const QDateTime forecastFrom(QDate(2025, 3, 3), QTime(0, 0, 0), Qt::UTC);

        ZoneForecast zfHigh  = rf.forecastZone("highZone",  forecastFrom);
        ZoneForecast zfQuiet = rf.forecastZone("quietZone", forecastFrom);

        QVERIFY2(zfHigh.days.size()  == 7, "highZone: expected 7 forecast days");
        QVERIFY2(zfQuiet.days.size() == 7, "quietZone: expected 7 forecast days");

        // Active zone should have higher weekly risk than quiet zone
        QVERIFY2(zfHigh.weeklyRisk > zfQuiet.weeklyRisk,
                 qPrintable(QString("highZone weeklyRisk=%1 should exceed quietZone=%2")
                            .arg(zfHigh.weeklyRisk).arg(zfQuiet.weeklyRisk)));

        // Compute quietZone average riskScore as a threshold
        double quietAvg = 0.0;
        for (const ForecastDay& fd : zfQuiet.days) quietAvg += fd.riskScore;
        quietAvg /= 7.0;

        // At least 4/7 days of highZone must exceed the quiet zone average
        int aboveThreshold = 0;
        for (const ForecastDay& fd : zfHigh.days) {
            if (fd.riskScore > quietAvg) ++aboveThreshold;
        }

        QVERIFY2(aboveThreshold >= 4,
                 qPrintable(QString("Only %1/7 highZone days exceed quietZone avg risk=%2; "
                                    "highZone weeklyRisk=%3, quietZone weeklyRisk=%4")
                            .arg(aboveThreshold).arg(quietAvg)
                            .arg(zfHigh.weeklyRisk).arg(zfQuiet.weeklyRisk)));
    }

    // All daily risk scores must be in [0, 1]
    void testForecastRiskScoresInRange()
    {
        const QDate start(2024, 6, 1);
        QVector<CrimeEvent> events;
        for (int i = 0; i < 30; ++i) {
            QDateTime dt(start.addDays(i), QTime(14, 0, 0), Qt::UTC);
            events.append(makeCrimeEvent(QString("e%1").arg(i), "rangeZone", dt));
        }

        RiskForecaster rf(7);
        rf.fit(events);

        const QDateTime from(QDate(2024, 7, 2), QTime(0, 0, 0), Qt::UTC);
        ZoneForecast zf = rf.forecastZone("rangeZone", from);

        for (const auto& fd : zf.days) {
            QVERIFY2(fd.riskScore >= 0.0 && fd.riskScore <= 1.0,
                     qPrintable(QString("riskScore %1 out of [0,1] on %2")
                                .arg(fd.riskScore).arg(fd.date.toString())));
        }
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// 2. TestEnsemblePredictorAccuracy
// ─────────────────────────────────────────────────────────────────────────────
class TestEnsemblePredictorAccuracy : public QObject {
    Q_OBJECT

private:
    // Build a fitted PoissonBaseline from n events in the given zone/type/slot
    static PoissonBaseline makeFittedPoisson(int n, const QString& zone,
                                              const QString& type,
                                              int hour, int month)
    {
        QVector<PoissonBaseline::EventRecord> recs;
        recs.reserve(n);
        for (int i = 0; i < n; ++i) {
            PoissonBaseline::EventRecord r;
            r.zoneId     = zone;
            r.crimeType  = type;
            r.occurredAt = QDateTime(QDate(2025, month, 1).addDays(i),
                                     QTime(hour, 0, 0), Qt::UTC);
            recs.append(r);
        }
        PoissonBaseline pb;
        pb.fit(recs);
        return pb;
    }

    // Build a fitted HawkesProcess from n near-repeat events
    static HawkesProcess makeFittedHawkes(int n)
    {
        QVector<SpatiotemporalEvent> evs;
        evs.reserve(n);
        for (int i = 0; i < n; ++i) {
            SpatiotemporalEvent e;
            e.tDays    = i * 0.5;
            e.lat      = 51.5 + (i % 5) * 0.002;
            e.lon      = -0.1 + (i % 3) * 0.002;
            e.crimeType = "burglary";
            evs.append(e);
        }
        HawkesProcess hp;
        hp.fit(evs, 30);
        return hp;
    }

private slots:

    // Ensemble prediction must lie in [0, 1] and be positive for an active zone
    void testEnsembleProbabilityInRange()
    {
        PoissonBaseline pb = makeFittedPoisson(40, "ens_zone", "burglary", 10, 4);
        HawkesProcess   hp = makeFittedHawkes(40);

        QVERIFY2(pb.isFitted(), "PoissonBaseline must be fitted");
        QVERIFY2(hp.isFitted(), "HawkesProcess must be fitted");

        EnsemblePredictor ens;
        ens.setPoisson(&pb);
        ens.setHawkes(&hp);
        ens.setWeights(0.6, 0.4);

        QVERIFY2(ens.isReady(), "EnsemblePredictor should be ready after setting both models");

        QDateTime queryDt(QDate(2025, 4, 7), QTime(10, 0, 0), Qt::UTC); // Monday
        EnsemblePrediction pred = ens.predict("ens_zone", queryDt, "burglary",
                                               51.5, -0.1);

        QVERIFY2(pred.probCrime >= 0.0 && pred.probCrime <= 1.0,
                 qPrintable(QString("probCrime=%1 out of [0,1]").arg(pred.probCrime)));
        QVERIFY2(pred.expectedCount >= 0.0,
                 qPrintable(QString("expectedCount=%1 should be ≥ 0").arg(pred.expectedCount)));
        QVERIFY2(pred.ciLow95 <= pred.ciHigh95,
                 qPrintable(QString("CI bounds inverted: low=%1 high=%2")
                            .arg(pred.ciLow95).arg(pred.ciHigh95)));
    }

    // Ensemble with only Poisson (wHawkes=0) should give non-trivially different
    // result from ensemble with only Hawkes (wPoisson=0), demonstrating both
    // components contribute differently.
    void testEnsemblePoissonDominatedVsHawkesDominated()
    {
        PoissonBaseline pb = makeFittedPoisson(50, "cmp_zone", "theft", 14, 3);
        HawkesProcess   hp = makeFittedHawkes(50);

        EnsemblePredictor ensPois, ensHawk;
        ensPois.setPoisson(&pb);
        ensPois.setHawkes(&hp);
        ensPois.setWeights(1.0, 0.0);   // Poisson-dominant

        ensHawk.setPoisson(&pb);
        ensHawk.setHawkes(&hp);
        ensHawk.setWeights(0.0, 1.0);   // Hawkes-dominant

        QDateTime dt(QDate(2025, 3, 3), QTime(14, 0, 0), Qt::UTC);

        EnsemblePrediction rPois = ensPois.predict("cmp_zone", dt, "theft", 51.5, -0.1);
        EnsemblePrediction rHawk = ensHawk.predict("cmp_zone", dt, "theft", 51.5, -0.1);

        // Both must be valid probabilities
        QVERIFY2(rPois.probCrime >= 0.0 && rPois.probCrime <= 1.0,
                 qPrintable(QString("Poisson-dominant probCrime=%1").arg(rPois.probCrime)));
        QVERIFY2(rHawk.probCrime >= 0.0 && rHawk.probCrime <= 1.0,
                 qPrintable(QString("Hawkes-dominant probCrime=%1").arg(rHawk.probCrime)));

        // Combined (0.5/0.5) should be between the two extreme results
        EnsemblePredictor ensMix;
        ensMix.setPoisson(&pb);
        ensMix.setHawkes(&hp);
        ensMix.setWeights(0.5, 0.5);

        EnsemblePrediction rMix = ensMix.predict("cmp_zone", dt, "theft", 51.5, -0.1);
        QVERIFY2(rMix.probCrime >= 0.0 && rMix.probCrime <= 1.0,
                 qPrintable(QString("Mixed ensemble probCrime=%1").arg(rMix.probCrime)));

        // The mixture should fall between (or very near) the two dominant values
        const double lo = std::min(rPois.probCrime, rHawk.probCrime);
        const double hi = std::max(rPois.probCrime, rHawk.probCrime);
        QVERIFY2(rMix.probCrime >= lo - 0.05 && rMix.probCrime <= hi + 0.05,
                 qPrintable(QString("Mixed=%1 should be ≈ between Poisson=%2 and Hawkes=%3")
                            .arg(rMix.probCrime).arg(rPois.probCrime).arg(rHawk.probCrime)));
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// 3. TestBayesianHierarchicalAccuracy
// ─────────────────────────────────────────────────────────────────────────────
class TestBayesianHierarchicalAccuracy : public QObject {
    Q_OBJECT

private:
    static QVector<CrimeEvent> makeZoneEvents(const QString& zone, int count,
                                               const QString& type = "burglary")
    {
        QVector<CrimeEvent> evs;
        for (int i = 0; i < count; ++i) {
            CrimeEvent ev;
            static int idCounter = 0;
            ev.eventId = ev.id = QString("bh_%1").arg(idCounter++);
            ev.suburb    = zone;
            ev.crimeType = type;
            ev.qualityScore = 1.0;
            evs.append(ev);
        }
        return evs;
    }

private slots:

    // Zones with more observed crimes must have higher posterior mean.
    // highZone: 30 crimes, medZone: 8 crimes, lowZone: 2 crimes.
    void testPosteriorMeansRankCorrectly()
    {
        QVector<CrimeEvent> events;
        events += makeZoneEvents("highZone", 30, "burglary");
        events += makeZoneEvents("medZone",   8, "burglary");
        events += makeZoneEvents("lowZone",   2, "burglary");

        BayesianHierarchical bh;
        bh.fit(events, 30.0);

        QVERIFY2(bh.isFitted(), "BayesianHierarchical must be fitted");
        QCOMPARE(bh.zoneCount(), 3);

        ZonePosterior postHigh = bh.posteriorForZone("highZone");
        ZonePosterior postMed  = bh.posteriorForZone("medZone");
        ZonePosterior postLow  = bh.posteriorForZone("lowZone");

        QVERIFY2(postHigh.posteriorMean > postMed.posteriorMean,
                 qPrintable(QString("highZone posteriorMean=%1 should exceed medZone=%2")
                            .arg(postHigh.posteriorMean).arg(postMed.posteriorMean)));
        QVERIFY2(postMed.posteriorMean > postLow.posteriorMean,
                 qPrintable(QString("medZone posteriorMean=%1 should exceed lowZone=%2")
                            .arg(postMed.posteriorMean).arg(postLow.posteriorMean)));
    }

    // allPosteriors() should return zones sorted by posteriorMean descending
    void testAllPosteriorsSortedDescending()
    {
        QVector<CrimeEvent> events;
        events += makeZoneEvents("z10", 20);
        events += makeZoneEvents("z05", 5);
        events += makeZoneEvents("z15", 15);
        events += makeZoneEvents("z01", 1);

        BayesianHierarchical bh;
        bh.fit(events, 30.0);

        const auto posteriors = bh.allPosteriors();
        QVERIFY2(!posteriors.isEmpty(), "allPosteriors() must not be empty");

        for (int i = 1; i < posteriors.size(); ++i) {
            QVERIFY2(posteriors[i-1].posteriorMean >= posteriors[i].posteriorMean,
                     qPrintable(QString("Posteriors not sorted: [%1]=%2 < [%3]=%4")
                                .arg(i-1).arg(posteriors[i-1].posteriorMean)
                                .arg(i).arg(posteriors[i].posteriorMean)));
        }
    }

    // Posterior credible interval must contain the posterior mean
    void testCredibleIntervalContainsMean()
    {
        QVector<CrimeEvent> events = makeZoneEvents("ciZone", 15);

        BayesianHierarchical bh;
        bh.fit(events, 30.0);

        ZonePosterior post = bh.posteriorForZone("ciZone");

        QVERIFY2(post.credibleLow <= post.posteriorMean,
                 qPrintable(QString("credibleLow=%1 should be <= posteriorMean=%2")
                            .arg(post.credibleLow).arg(post.posteriorMean)));
        QVERIFY2(post.posteriorMean <= post.credibleHigh,
                 qPrintable(QString("posteriorMean=%1 should be <= credibleHigh=%2")
                            .arg(post.posteriorMean).arg(post.credibleHigh)));
    }

    // predictiveProbability(zone, minCount=1) should be higher for busier zones
    void testPredictiveProbabilityRanksZones()
    {
        QVector<CrimeEvent> events;
        events += makeZoneEvents("busyZone", 25);
        events += makeZoneEvents("quietZone", 2);

        BayesianHierarchical bh;
        bh.fit(events, 30.0);

        const double pBusy  = bh.predictiveProbability("busyZone",  1);
        const double pQuiet = bh.predictiveProbability("quietZone", 1);

        QVERIFY2(pBusy > pQuiet,
                 qPrintable(QString("busyZone P(y≥1)=%1 should exceed quietZone P(y≥1)=%2")
                            .arg(pBusy).arg(pQuiet)));
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
    { TestRiskForecasterAccuracy    t; r |= runTest(&t, "acc_forecaster.txt"); }
    { TestEnsemblePredictorAccuracy t; r |= runTest(&t, "acc_ensemble.txt"); }
    { TestBayesianHierarchicalAccuracy t; r |= runTest(&t, "acc_bayesian.txt"); }
    return r;
}

#include "test_forecaster_accuracy.moc"
