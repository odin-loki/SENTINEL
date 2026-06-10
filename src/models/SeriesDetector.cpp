#include "models/SeriesDetector.h"
#include "core/SentinelLogger.h"
#include <cmath>
#include <algorithm>
#include <numeric>
#include <array>
#include <QStringList>
#include <QTimeZone>

static constexpr double SERIES_PI = 3.14159265358979323846;

// ---------------------------------------------------------------------------
// Near-repeat lookup table (Ratcliffe 2009; Johnson et al. 2007)
// ---------------------------------------------------------------------------

const QMap<QString, SeriesDetector::NearRepeatParams>&
SeriesDetector::nearRepeatTable()
{
    static const QMap<QString, NearRepeatParams> table = {
        { QStringLiteral("burglary"),       { 200.0, 14.0, 4.5 } },
        { QStringLiteral("robbery"),        { 400.0,  7.0, 3.2 } },
        { QStringLiteral("assault"),        { 150.0,  3.0, 2.8 } },
        { QStringLiteral("vehicle_crime"),  { 300.0, 21.0, 3.0 } },
        { QStringLiteral("drug_offence"),   { 100.0,  7.0, 2.1 } },
    };
    return table;
}

SeriesDetector::NearRepeatParams
SeriesDetector::nearRepeatFor(const QString& crimeType)
{
    QString key = crimeType.toLower().replace(QLatin1Char(' '), QLatin1Char('_'));
    const auto& tbl = nearRepeatTable();
    auto it = tbl.constFind(key);
    if (it != tbl.constEnd()) return it.value();
    return NearRepeatParams{};   // default values
}

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------

SeriesDetector::SeriesDetector(double epsKm, double epsDays, int minSamples)
    : m_epsKm(epsKm), m_epsDays(epsDays), m_minSamples(minSamples)
{}

// ---------------------------------------------------------------------------
// 3D Euclidean distance helper
// ---------------------------------------------------------------------------

double SeriesDetector::dist3D(const std::array<double, 3>& a,
                               const std::array<double, 3>& b)
{
    double dx = a[0] - b[0];
    double dy = a[1] - b[1];
    double dz = a[2] - b[2];
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

// ---------------------------------------------------------------------------
// DBSCAN
// Standard algorithm (Ester et al. 1996).
// Returns per-point cluster labels: -1 = noise, >=0 = cluster id.
// ---------------------------------------------------------------------------

QVector<int> SeriesDetector::dbscan(
    const QVector<std::array<double, 3>>& points,
    double eps, int minPts) const
{
    int n = points.size();
    QVector<int> labels(n, -2);   // -2 = unvisited
    int clusterId = 0;

    auto regionQuery = [&](int idx) -> QVector<int> {
        QVector<int> neighbours;
        for (int j = 0; j < n; ++j) {
            if (dist3D(points[idx], points[j]) <= eps) {
                neighbours.append(j);
            }
        }
        return neighbours;
    };

    for (int i = 0; i < n; ++i) {
        if (labels[i] != -2) continue;   // already processed

        QVector<int> neighbours = regionQuery(i);

        if (neighbours.size() < minPts) {
            labels[i] = -1;   // noise
            continue;
        }

        labels[i] = clusterId;
        // Expand cluster using a queue (BFS)
        QVector<int> seeds = neighbours;
        // Remove core point from seeds to avoid revisiting immediately
        seeds.removeOne(i);

        int si = 0;
        while (si < seeds.size()) {
            int q = seeds[si++];
            if (labels[q] == -1) labels[q] = clusterId;   // border point
            if (labels[q] != -2) continue;                 // already labelled

            labels[q] = clusterId;
            QVector<int> qNeighbours = regionQuery(q);
            if (qNeighbours.size() >= minPts) {
                for (int nb : qNeighbours) {
                    if (labels[nb] == -2 || labels[nb] == -1) {
                        if (labels[nb] == -2) seeds.append(nb);
                        // Will be relabelled in next iteration if border
                    }
                }
            }
        }
        ++clusterId;
    }

    // Replace remaining -2 (unvisited isolated) with -1
    for (int& l : labels) if (l == -2) l = -1;
    return labels;
}

// ---------------------------------------------------------------------------
// detectSeries()
// ---------------------------------------------------------------------------

QVector<CrimeSeries> SeriesDetector::detectSeries(
    const QVector<SeriesEvent>& events)
{
    if (events.isEmpty()) return {};

    // Convert to normalised 3D feature space:
    //   x = lat_deg  (spatial)
    //   y = lon_deg  (spatial)
    //   z = tDays normalised to same scale as spatial epsilon
    //
    // eps_spatial_deg = epsKm / 111.0
    // eps_temporal_normalised = epsDays / (epsDays / (epsKm / 111.0)) = epsKm/111.0
    // So z_scale = (epsKm / 111.0) / epsDays  →  z = tDays * z_scale
    double epsDeg    = m_epsKm / 111.0;
    double zScale    = (m_epsDays > 0.0) ? (epsDeg / m_epsDays) : 1.0;

    QVector<std::array<double, 3>> pts;
    pts.reserve(events.size());
    for (const auto& ev : events) {
        pts.append({ ev.lat, ev.lon, ev.tDays * zScale });
    }

    QVector<int> labels = dbscan(pts, epsDeg, m_minSamples);

    // Group events by cluster id
    QMap<int, QVector<int>> clusters;
    for (int i = 0; i < labels.size(); ++i) {
        if (labels[i] >= 0) clusters[labels[i]].append(i);
    }

    QVector<CrimeSeries> result;
    result.reserve(clusters.size());

    for (auto it = clusters.constBegin(); it != clusters.constEnd(); ++it) {
        const QVector<int>& idxs = it.value();
        CrimeSeries series;
        series.seriesId = QStringLiteral("SERIES-%1").arg(it.key(), 4, 10, QChar('0'));

        double sumLat = 0.0, sumLon = 0.0;
        double minT = std::numeric_limits<double>::max();
        double maxT = std::numeric_limits<double>::lowest();

        QMap<QString, int> typeCounts;

        for (int idx : idxs) {
            const SeriesEvent& ev = events[idx];
            series.members.append(ev);
            sumLat += ev.lat;
            sumLon += ev.lon;
            minT = std::min(minT, ev.tDays);
            maxT = std::max(maxT, ev.tDays);
            typeCounts[ev.crimeType]++;
        }

        series.centroidLat = sumLat / idxs.size();
        series.centroidLon = sumLon / idxs.size();
        series.firstDays   = minT;
        series.lastDays    = maxT;

        // Dominant crime type by frequency
        int maxCount = 0;
        for (auto tc = typeCounts.constBegin(); tc != typeCounts.constEnd(); ++tc) {
            if (tc.value() > maxCount) {
                maxCount = tc.value();
                series.dominantCrimeType = tc.key();
            }
        }

        result.append(series);
    }

    qCInfo(lcModels) << "Series detection found" << result.size() << "series from"
                     << events.size() << "events";
    return result;
}

// ---------------------------------------------------------------------------
// linkProbability()
// ---------------------------------------------------------------------------

SeriesMatch SeriesDetector::linkProbability(const SeriesEvent& newEvent,
                                              const CrimeSeries& series,
                                              double moSimilarity) const
{
    SeriesMatch match;
    match.seriesId   = series.seriesId;
    match.memberCount = series.members.size();
    match.moSimilarity = moSimilarity;
    match.method      = QStringLiteral("NearRepeat-DBSCAN");

    NearRepeatParams nr = nearRepeatFor(series.dominantCrimeType);

    // Find minimum spatial and temporal distance to any series member
    double minDistM   = std::numeric_limits<double>::max();
    double minDtDays  = std::numeric_limits<double>::max();

    for (const auto& mem : series.members) {
        double dlat = newEvent.lat - mem.lat;
        double dlon = newEvent.lon - mem.lon;
        // Approx metres: 1 degree lat ≈ 111 000 m; lon scale by cos(lat)
        double cosLat   = std::cos((newEvent.lat + mem.lat) * 0.5 * SERIES_PI / 180.0);
        double distM    = std::sqrt((dlat * 111000.0) * (dlat * 111000.0)
                                  + (dlon * 111000.0 * cosLat) * (dlon * 111000.0 * cosLat));
        double dtDays   = std::abs(newEvent.tDays - mem.tDays);
        minDistM  = std::min(minDistM,  distM);
        minDtDays = std::min(minDtDays, dtDays);
    }

    match.spatialDistanceM      = minDistM;
    match.temporalDistanceDays  = minDtDays;

    // Component scores (0–1)
    double sSpatial  = std::max(0.0, 1.0 - minDistM  / nr.distM);
    double sTemporal = std::max(0.0, 1.0 - minDtDays / nr.days);

    double composite = 0.35 * sSpatial + 0.35 * sTemporal + 0.30 * moSimilarity;
    match.compositeScore = composite;

    // Link probability with near-repeat risk multiplier
    constexpr double baseRate = 0.05;
    double raw = baseRate * nr.multiplier * composite / 0.5;
    match.linkProbability = std::min(raw, 0.95);

    return match;
}

// ---------------------------------------------------------------------------
// moJaccard() — Jaccard similarity on word-token sets
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// haversineKm() — public static helper for tests and external callers
// ---------------------------------------------------------------------------

double SeriesDetector::haversineKm(double lat1, double lon1,
                                    double lat2, double lon2)
{
    constexpr double R = 6371.0;  // Earth radius in km
    const double dLat = (lat2 - lat1) * SERIES_PI / 180.0;
    const double dLon = (lon2 - lon1) * SERIES_PI / 180.0;
    const double a = std::sin(dLat / 2.0) * std::sin(dLat / 2.0)
                   + std::cos(lat1 * SERIES_PI / 180.0)
                     * std::cos(lat2 * SERIES_PI / 180.0)
                     * std::sin(dLon / 2.0) * std::sin(dLon / 2.0);
    const double c = 2.0 * std::atan2(std::sqrt(a), std::sqrt(1.0 - a));
    return R * c;
}

// ---------------------------------------------------------------------------
// detect() — CrimeEvent convenience overload
// ---------------------------------------------------------------------------

QVector<CrimeSeries> SeriesDetector::detect(const QVector<CrimeEvent>& events)
{
    static const QDateTime kEpoch =
        QDateTime(QDate(2000, 1, 1), QTime(0, 0, 0), QTimeZone::utc());

    QVector<SeriesEvent> sev;
    sev.reserve(events.size());
    for (const auto& ev : events) {
        SeriesEvent se;
        se.eventId    = ev.eventId;
        se.lat        = ev.lat.value_or(0.0);
        se.lon        = ev.lon.value_or(0.0);
        const QDateTime dt = ev.occurredAt.value_or(ev.ingestedAt);
        se.tDays      = static_cast<double>(kEpoch.daysTo(dt));
        se.crimeType  = ev.crimeType;
        se.moText     = ev.narrative.value_or(QString{});
        sev.append(se);
    }
    return detectSeries(sev);
}

// ---------------------------------------------------------------------------
// moJaccard() — Jaccard similarity on word-token sets
// ---------------------------------------------------------------------------

double SeriesDetector::moJaccard(const QString& a, const QString& b)
{
    // Both empty → no shared features → similarity 0.0
    if (a.isEmpty() && b.isEmpty()) return 0.0;
    if (a.isEmpty() || b.isEmpty()) return 0.0;

    QStringList tokA = a.toLower().split(QLatin1Char(' '), Qt::SkipEmptyParts);
    QStringList tokB = b.toLower().split(QLatin1Char(' '), Qt::SkipEmptyParts);

    QSet<QString> setA(tokA.begin(), tokA.end());
    QSet<QString> setB(tokB.begin(), tokB.end());

    if (setA.isEmpty() && setB.isEmpty()) return 1.0;

    QSet<QString> intersection = setA & setB;
    QSet<QString> unionSet     = setA | setB;

    return static_cast<double>(intersection.size())
         / static_cast<double>(unionSet.size());
}
