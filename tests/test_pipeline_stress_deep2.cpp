// test_pipeline_stress_deep2.cpp — Deep audit iteration 14: 2000-event model chain.

#include <QTest>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QDateTime>
#include <QTimeZone>
#include <cmath>

#include "core/CrimeEvent.h"
#include "models/PoissonBaseline.h"
#include "models/HawkesProcess.h"
#include "models/KDEHotspot.h"
#include "models/SeriesDetector.h"
#include "models/EnsemblePredictor.h"
#include "models/RiskForecaster.h"

namespace {

static QVector<CrimeEvent> generateEvents(int count)
{
    QVector<CrimeEvent> events;
    events.reserve(count);

    const QStringList zones = {
        QStringLiteral("Z1"), QStringLiteral("Z2"), QStringLiteral("Z3"),
        QStringLiteral("Z4"), QStringLiteral("Z5")
    };
    const QStringList types = {
        QStringLiteral("burglary"), QStringLiteral("theft"), QStringLiteral("assault"),
        QStringLiteral("robbery"), QStringLiteral("vehicle_crime")
    };

    const double latCenter = 51.5074;
    const double lonCenter = -0.1278;
    const QDateTime epoch(QDate(2024, 1, 1), QTime(0, 0, 0), QTimeZone::utc());

    for (int i = 0; i < count; ++i) {
        CrimeEvent e;
        e.eventId = QStringLiteral("DEEP2_%1").arg(i, 5, 10, QChar('0'));
        e.id = e.eventId;
        e.suburb = zones[i % zones.size()];
        e.crimeType = types[i % types.size()];

        const double angle = i * 0.37;
        const double radius = (i % 40) * 0.0002;
        e.lat = latCenter + radius * std::cos(angle);
        e.lon = lonCenter + radius * std::sin(angle);
        e.latitude = e.lat.value_or(0.0);
        e.longitude = e.lon.value_or(0.0);

        e.occurredAt = epoch.addDays(i % 90).addSecs((i % 24) * 3600);
        e.timestamp = e.occurredAt.value_or(QDateTime{});
        e.qualityScore = 0.75 + (i % 4) * 0.05;
        events.append(e);
    }
    return events;
}

static QVector<SpatiotemporalEvent> toSpatiotemporal(const QVector<CrimeEvent>& events)
{
    QVector<SpatiotemporalEvent> sts;
    sts.reserve(events.size());
    const QDateTime epoch(QDate(2024, 1, 1), QTime(0, 0, 0), QTimeZone::utc());
    for (const CrimeEvent& e : events) {
        if (!e.occurredAt || !e.lat || !e.lon)
            continue;
        SpatiotemporalEvent st;
        st.tDays = epoch.daysTo(*e.occurredAt);
        st.lat = *e.lat;
        st.lon = *e.lon;
        st.crimeType = e.crimeType;
        sts.append(st);
    }
    return sts;
}

} // namespace

class TestPipelineStressDeep2 : public QObject {
    Q_OBJECT

private slots:
    void testFullModelChain2000EventsUnder10Seconds()
    {
        const QVector<CrimeEvent> events = generateEvents(2000);
        QVERIFY(events.size() == 2000);

        QElapsedTimer timer;
        timer.start();

        QVector<PoissonBaseline::EventRecord> poissonRecs;
        poissonRecs.reserve(events.size());
        for (const CrimeEvent& e : events) {
            if (!e.occurredAt)
                continue;
            PoissonBaseline::EventRecord r;
            r.zoneId = e.suburb;
            r.occurredAt = *e.occurredAt;
            r.crimeType = e.crimeType;
            poissonRecs.append(r);
        }

        PoissonBaseline poisson;
        poisson.fit(poissonRecs);
        QVERIFY(poisson.isFitted());

        const QVector<SpatiotemporalEvent> sts = toSpatiotemporal(events);
        QVERIFY(!sts.isEmpty());

        HawkesProcess hawkes;
        QVERIFY(hawkes.fit(sts, 5));
        QVERIFY(hawkes.isFitted());

        QVector<QPair<double, double>> locs;
        locs.reserve(events.size());
        for (const CrimeEvent& e : events) {
            if (e.lat && e.lon)
                locs.append({*e.lat, *e.lon});
        }
        QVERIFY(!locs.isEmpty());

        KDEHotspot kde(30);
        const auto hotspots = kde.findHotspots(locs, 51.45, 51.55, -0.17, -0.08, 5);
        QVERIFY(!hotspots.isEmpty());

        SeriesDetector seriesDetector(0.5, 14.0, 3);
        const auto series = seriesDetector.detect(events);
        Q_UNUSED(series)

        EnsemblePredictor ensemble;
        ensemble.setPoisson(&poisson);
        ensemble.setHawkes(&hawkes);
        ensemble.setWeights(0.6, 0.4);
        const auto prediction = ensemble.predict(
            events.first().suburb,
            QDateTime::currentDateTimeUtc(),
            events.first().crimeType,
            events.first().lat.value_or(51.5),
            events.first().lon.value_or(-0.1));
        QVERIFY(prediction.probCrime >= 0.0);
        QVERIFY(prediction.probCrime <= 1.0);

        RiskForecaster forecaster(7);
        forecaster.fit(events);
        QVERIFY(forecaster.isFitted());
        const auto forecasts = forecaster.forecast(QDateTime::currentDateTimeUtc());
        QVERIFY(!forecasts.isEmpty());

        const qint64 elapsedMs = timer.elapsed();
        QVERIFY2(elapsedMs < 10000,
                 qPrintable(QStringLiteral("2000-event model chain took %1 ms, expected < 10000 ms")
                                .arg(elapsedMs)));
    }
};

QTEST_GUILESS_MAIN(TestPipelineStressDeep2)

#include "test_pipeline_stress_deep2.moc"
