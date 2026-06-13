#include "inference/AnomalyDetector.h"

#include <cmath>
#include <algorithm>
#include <numeric>
#include <limits>

AnomalyDetector::AnomalyDetector(double contamination)
    : m_contamination(std::clamp(contamination, 0.01, 0.49))
    , m_anomalyThreshold(std::clamp(1.0 - m_contamination * 7.0, 0.4, 0.85))
{}

static double safeSqrt(double x) { return x > 0.0 ? std::sqrt(x) : 0.0; }

static double stddevOf(const QVector<double>& vals, double mean)
{
    if (vals.size() < 2) return 1.0;
    double sumSq = 0.0;
    for (double v : vals) {
        const double d = v - mean;
        sumSq += d * d;
    }
    return safeSqrt(sumSq / static_cast<double>(vals.size() - 1));
}

void AnomalyDetector::fit(const QVector<AnomalyFeatureVector>& data)
{
    if (data.isEmpty()) return;

    // Feature order: lat, lon, tDays, hourNorm, crimeTypeCode
    constexpr int NDIM = 5;
    QVector<QVector<double>> feats(NDIM);
    for (const auto& ev : data) {
        feats[0].append(ev.lat);
        feats[1].append(ev.lon);
        feats[2].append(ev.tDays);
        feats[3].append(ev.hourNorm);
        feats[4].append(static_cast<double>(ev.crimeTypeCode));
    }

    m_dimStats.resize(NDIM);
    for (int d = 0; d < NDIM; ++d) {
        double mean = 0.0;
        for (double v : feats[d]) mean += v;
        mean /= feats[d].size();
        m_dimStats[d].mean   = mean;
        m_dimStats[d].stddev = std::max(stddevOf(feats[d], mean), 1e-9);
    }

    m_trainLatMean = m_dimStats[0].mean;
    m_trainLatStd  = m_dimStats[0].stddev;
    m_trainLonMean = m_dimStats[1].mean;
    m_trainLonStd  = m_dimStats[1].stddev;
    m_trainTMean   = m_dimStats[2].mean;
    m_trainTStd    = m_dimStats[2].stddev;

    m_fitted = true;
}

double AnomalyDetector::zScoreTemporal(double tDays) const
{
    return std::abs((tDays - m_trainTMean) / m_trainTStd);
}

double AnomalyDetector::zScoreSpatial(double lat, double lon) const
{
    const double zLat = (lat - m_trainLatMean) / m_trainLatStd;
    const double zLon = (lon - m_trainLonMean) / m_trainLonStd;
    return safeSqrt(zLat * zLat + zLon * zLon);
}

double AnomalyDetector::featureDistance(const AnomalyFeatureVector& a,
                                         const AnomalyFeatureVector& b) const
{
    constexpr int NDIM = 5;
    if (m_dimStats.size() < NDIM) {
        const double dLat = a.lat - b.lat;
        const double dLon = a.lon - b.lon;
        const double dT   = a.tDays - b.tDays;
        const double dH   = a.hourNorm - b.hourNorm;
        const double dC   = static_cast<double>(a.crimeTypeCode - b.crimeTypeCode);
        return safeSqrt(dLat*dLat + dLon*dLon + dT*dT + dH*dH + dC*dC);
    }

    auto normDelta = [&](double av, double bv, int dim) {
        const double scale = std::max(m_dimStats[dim].stddev, 1e-9);
        return (av - bv) / scale;
    };

    const double dLat = normDelta(a.lat, b.lat, 0);
    const double dLon = normDelta(a.lon, b.lon, 1);
    const double dT   = normDelta(a.tDays, b.tDays, 2);
    const double dH   = normDelta(a.hourNorm, b.hourNorm, 3);
    const double dC   = normDelta(static_cast<double>(a.crimeTypeCode),
                                  static_cast<double>(b.crimeTypeCode), 4);
    return safeSqrt(dLat*dLat + dLon*dLon + dT*dT + dH*dH + dC*dC);
}

double AnomalyDetector::isolationScore(const AnomalyFeatureVector& ev,
                                        const QVector<AnomalyFeatureVector>& context) const
{
    // Normalised Euclidean distance from centroid of context (simplified isolation forest)
    if (context.isEmpty()) return 0.0;
    // A single-event context means the event is trivially isolated (no peers to form a cluster).
    if (context.size() == 1) return 1.0;

    constexpr int NDIM = 5;
    double means[NDIM] = {};
    for (const auto& e : context) {
        means[0] += e.lat;
        means[1] += e.lon;
        means[2] += e.tDays;
        means[3] += e.hourNorm;
        means[4] += static_cast<double>(e.crimeTypeCode);
    }
    const double n = static_cast<double>(context.size());
    for (int d = 0; d < NDIM; ++d)
        means[d] /= n;

    auto normDelta = [&](double val, double mean, int dim) {
        const double scale = (m_dimStats.size() > dim)
                             ? std::max(m_dimStats[dim].stddev, 1e-9)
                             : 1.0;
        return (val - mean) / scale;
    };

    const double dLat = normDelta(ev.lat, means[0], 0);
    const double dLon = normDelta(ev.lon, means[1], 1);
    const double dT   = normDelta(ev.tDays, means[2], 2);
    const double dH   = normDelta(ev.hourNorm, means[3], 3);
    const double dC   = normDelta(static_cast<double>(ev.crimeTypeCode), means[4], 4);

    const double dist = safeSqrt(dLat*dLat + dLon*dLon + dT*dT + dH*dH + dC*dC);
    // Expected average path length for isolation forest with n samples ≈ 2*H(n-1)
    // Simplified: score = dist / (2 * sqrt(NDIM)), clamped to [0,1]
    return std::min(dist / (2.0 * std::sqrt(static_cast<double>(NDIM))), 1.0);
}

double AnomalyDetector::localDensityRatio(const AnomalyFeatureVector& ev,
                                           const QVector<AnomalyFeatureVector>& data,
                                           int k) const
{
    if (data.size() < 2) return 1.0;

    // Distances from ev to all other points (exclude self — zero distance
    // would otherwise deflate k-NN averages and cap LOF at ~1.0).
    QVector<double> dists;
    dists.reserve(data.size());
    for (const auto& d : data) {
        const double dist = featureDistance(ev, d);
        if (dist < 1e-12) continue;
        dists.append(dist);
    }

    if (dists.isEmpty()) return 1.0;

    const int actualK = std::min(k, static_cast<int>(dists.size()));
    std::partial_sort(dists.begin(), dists.begin() + actualK, dists.end());

    double avgKNN = 0.0;
    for (int i = 0; i < actualK; ++i)
        avgKNN += dists[i];
    avgKNN /= actualK;

    // Average of each neighbour's own kNN distances (LOF approximation)
    // We use the global average NN distance as surrogate for local reachability
    double globalAvgDist = 0.0;
    for (double d : dists) globalAvgDist += d;
    globalAvgDist /= dists.size();

    return (globalAvgDist > 1e-12) ? avgKNN / globalAvgDist : 1.0;
}

QVector<AnomalySignal> AnomalyDetector::detectAnomalies(
    const QVector<AnomalyFeatureVector>& events)
{
    if (events.isEmpty()) return {};

    // Auto-fit on the provided batch if no prior training data is available.
    if (!m_fitted)
        fit(events);

    QVector<AnomalySignal> results;
    results.reserve(events.size());

    for (const auto& ev : events) {
        AnomalySignal sig;
        sig.eventId = ev.eventId;

        const double zTemporal = zScoreTemporal(ev.tDays);
        const double zSpatial  = zScoreSpatial(ev.lat, ev.lon);
        const double isoScore  = isolationScore(ev, events);
        const double lofScore  = localDensityRatio(ev, events);

        // Normalise LOF to [0,1]: values >> 1 indicate anomaly
        const double lofNorm = std::min((lofScore - 1.0) / 4.0 + 0.25, 1.0);
        // Normalise Z-scores: treat Z > 3 as maximum anomaly
        const double zTNorm  = std::min(zTemporal / 3.0, 1.0);
        const double zSNorm  = std::min(zSpatial  / 3.0, 1.0);

        const double combined = 0.4 * isoScore +
                                0.4 * std::max(lofNorm, 0.0) +
                                0.1 * zTNorm +
                                0.1 * zSNorm;

        sig.isolationScore   = isoScore;
        sig.lofScore         = lofScore;
        sig.zScoreTemporal   = zTemporal;
        sig.zScoreSpatial    = zSpatial;
        sig.combinedScore    = combined;
        sig.isAnomaly        = combined > m_anomalyThreshold;

        if (zTemporal > 2.0)
            sig.signalReasons.push_back(QStringLiteral("temporal_outlier"));
        if (zSpatial > 2.0)
            sig.signalReasons.push_back(QStringLiteral("spatial_outlier"));
        if (isoScore > 0.6)
            sig.signalReasons.push_back(QStringLiteral("isolation_outlier"));
        if (lofNorm > 0.5)
            sig.signalReasons.push_back(QStringLiteral("low_density_region"));

        results.append(sig);
    }
    return results;
}
