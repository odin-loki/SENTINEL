#include "models/NearRepeatVictimisation.h"
#include <cmath>
#include <algorithm>
#include <limits>

static constexpr double NRV_PI = 3.14159265358979323846;

double NearRepeatVictimisation::bandwidthFor(const QString& crimeType)
{
    return SeriesDetector::nearRepeatFor(crimeType).distM;
}

NearRepeatVictimisation::NearRepeatVictimisation(double bandwidthM, double windowDays)
    : m_bandwidthM(bandwidthM), m_windowDays(windowDays)
{}

double NearRepeatVictimisation::spatialDecay(double distM, double bandwidthM)
{
    if (bandwidthM <= 0.0 || distM > bandwidthM) return 0.0;
    return std::exp(-distM / bandwidthM);
}

double NearRepeatVictimisation::temporalDecay(double dtDays, double windowDays)
{
    if (windowDays <= 0.0 || dtDays > windowDays) return 0.0;
    return std::exp(-dtDays / windowDays);
}

double NearRepeatVictimisation::alertScore(double spatialDistanceM,
                                           double temporalDistanceDays,
                                           const QString& crimeType) const
{
    double bw    = crimeType.isEmpty() ? m_bandwidthM : bandwidthFor(crimeType);
    double win   = crimeType.isEmpty()
                     ? m_windowDays
                     : SeriesDetector::nearRepeatFor(crimeType).days;
    double sDist = spatialDecay(spatialDistanceM, bw);
    double sTime = temporalDecay(temporalDistanceDays, win);
    return sDist * sTime;
}

QVector<NearRepeatAlert> NearRepeatVictimisation::analyse(
    const QVector<SeriesEvent>& events) const
{
    if (events.size() < 2) return {};
    if (m_bandwidthM <= 0.0 || m_windowDays <= 0.0) return {};

    QVector<NearRepeatAlert> alerts;
    const int n = events.size();

    for (int i = 1; i < n; ++i) {
        for (int j = 0; j < i; ++j) {
            const SeriesEvent& cur   = events[i];
            const SeriesEvent& prior = events[j];

            const double distKm = SeriesDetector::haversineKm(
                cur.lat, cur.lon, prior.lat, prior.lon);
            const double distM  = distKm * 1000.0;
            const double dtDays = std::abs(cur.tDays - prior.tDays);

            const double score = alertScore(distM, dtDays, cur.crimeType);
            if (score <= 0.0) continue;

            NearRepeatAlert alert;
            alert.eventId              = cur.eventId;
            alert.priorEventId         = prior.eventId;
            alert.alertScore           = score;
            alert.spatialDistanceM     = distM;
            alert.temporalDistanceDays = dtDays;
            alerts.append(alert);
        }
    }
    return alerts;
}

double NearRepeatVictimisation::knoxStatistic(
    const QVector<SeriesEvent>& events) const
{
    if (events.size() < 2) return 1.0;

    const int n = events.size();
    int nearPairs = 0;
    const int totalPairs = n * (n - 1) / 2;

    double latMin =  std::numeric_limits<double>::max();
    double latMax = -std::numeric_limits<double>::max();
    double lonMin =  std::numeric_limits<double>::max();
    double lonMax = -std::numeric_limits<double>::max();
    double tMin   =  std::numeric_limits<double>::max();
    double tMax   = -std::numeric_limits<double>::max();

    for (const auto& ev : events) {
        latMin = std::min(latMin, ev.lat);
        latMax = std::max(latMax, ev.lat);
        lonMin = std::min(lonMin, ev.lon);
        lonMax = std::max(lonMax, ev.lon);
        tMin   = std::min(tMin,   ev.tDays);
        tMax   = std::max(tMax,   ev.tDays);
    }

    const double meanLat = 0.5 * (latMin + latMax);
    const double cosLat  = std::cos(meanLat * NRV_PI / 180.0);

    for (int i = 0; i < n; ++i) {
        for (int j = i + 1; j < n; ++j) {
            const double distKm = SeriesDetector::haversineKm(
                events[i].lat, events[i].lon,
                events[j].lat, events[j].lon);
            const double distM  = distKm * 1000.0;
            const double dtDays = std::abs(events[i].tDays - events[j].tDays);

            if (distM <= m_bandwidthM && dtDays <= m_windowDays)
                ++nearPairs;
        }
    }

    // Study region with a minimum extent so co-located clusters are testable.
    const double latSpanM = std::max((latMax - latMin) * 111000.0, m_bandwidthM * 5.0);
    const double lonSpanM = std::max((lonMax - lonMin) * 111000.0 * cosLat,
                                     m_bandwidthM * 5.0);
    const double areaM2   = std::max(latSpanM * lonSpanM,
                                     NRV_PI * m_bandwidthM * m_bandwidthM);

    const double nearAreaM2 = NRV_PI * m_bandwidthM * m_bandwidthM;
    const double pSpace     = std::min(1.0, nearAreaM2 / areaM2);

    const double tSpan  = std::max(tMax - tMin, m_windowDays);
    const double pTime  = std::min(1.0, m_windowDays / tSpan);

    const double expected = static_cast<double>(totalPairs) * pSpace * pTime;
    if (expected < 1e-9)
        return nearPairs > 0 ? static_cast<double>(nearPairs) : 1.0;

    return static_cast<double>(nearPairs) / expected;
}
