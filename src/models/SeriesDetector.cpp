#include "models/SeriesDetector.h"
#include "core/AppConfig.h"
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

SeriesDetector::SeriesDetector(double epsKm, double epsDays, int minSamples,
                               const QMap<QString, double>& epsOverridesKm)
    : m_epsKm(epsKm)
    , m_epsDays(epsDays)
    , m_minSamples(minSamples)
    , m_epsOverridesKm(epsOverridesKm)
{}

SeriesDetector SeriesDetector::fromConfig(const AppConfig& cfg)
{
    return SeriesDetector(cfg.seriesEpsKm, cfg.seriesEpsDays, cfg.seriesMinEvents,
                          cfg.seriesEpsByCrimeType);
}

QString SeriesDetector::crimeBucket(const QString& crimeType)
{
    const QString key = crimeType.toLower().replace(QLatin1Char(' '), QLatin1Char('_'));
    if (key.contains(QStringLiteral("burgl")))
        return QStringLiteral("burglary");
    if (key.contains(QStringLiteral("theft")) || key.contains(QStringLiteral("larcen"))
        || key.contains(QStringLiteral("shoplift")))
        return QStringLiteral("theft");
    if (key.contains(QStringLiteral("assault")) || key.contains(QStringLiteral("violent"))
        || key.contains(QStringLiteral("robbery")) || key.contains(QStringLiteral("homicide"))
        || key.contains(QStringLiteral("weapon")))
        return QStringLiteral("violent");
    return QStringLiteral("other");
}

double SeriesDetector::epsKmFor(const QString& crimeType) const
{
    const QString bucket = crimeBucket(crimeType);
    const auto it = m_epsOverridesKm.constFind(bucket);
    if (it != m_epsOverridesKm.constEnd() && it.value() > 0.0)
        return it.value();
    return m_epsKm;
}

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
                    if (labels[nb] == -2) {
                        seeds.append(nb);  // unvisited → queue for expansion
                    } else if (labels[nb] == -1) {
                        labels[nb] = clusterId;  // noise → border point
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

QVector<CrimeSeries> SeriesDetector::clusterSubset(const QVector<SeriesEvent>& events,
                                                    double epsKm,
                                                    int seriesIdOffset) const
{
    if (events.isEmpty())
        return {};

    const double epsDeg = epsKm / 111.0;
    const double zScale = (m_epsDays > 0.0) ? (epsDeg / m_epsDays) : 1.0;

    QVector<std::array<double, 3>> pts;
    pts.reserve(events.size());
    for (const auto& ev : events)
        pts.append({ ev.lat, ev.lon, ev.tDays * zScale });

    const QVector<int> labels = dbscan(pts, epsDeg, m_minSamples);

    QMap<int, QVector<int>> clusters;
    for (int i = 0; i < labels.size(); ++i) {
        if (labels[i] >= 0)
            clusters[labels[i]].append(i);
    }

    QVector<CrimeSeries> result;
    result.reserve(clusters.size());

    int localId = 0;
    for (auto it = clusters.constBegin(); it != clusters.constEnd(); ++it) {
        const QVector<int>& idxs = it.value();
        CrimeSeries series;
        series.seriesId = QStringLiteral("SERIES-%1")
                              .arg(seriesIdOffset + localId++, 4, 10, QChar('0'));

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

        std::sort(series.members.begin(), series.members.end(),
                  [](const SeriesEvent& a, const SeriesEvent& b) {
                      return a.tDays < b.tDays;
                  });

        series.centroidLat = sumLat / idxs.size();
        series.centroidLon = sumLon / idxs.size();
        series.firstDays   = minT;
        series.lastDays    = maxT;

        int maxCount = 0;
        for (auto tc = typeCounts.constBegin(); tc != typeCounts.constEnd(); ++tc) {
            if (tc.value() > maxCount) {
                maxCount = tc.value();
                series.dominantCrimeType = tc.key();
            }
        }

        result.append(series);
    }

    return result;
}

QVector<CrimeSeries> SeriesDetector::detectSeries(
    const QVector<SeriesEvent>& events)
{
    if (events.isEmpty()) return {};

    // Deduplicate by eventId — keep first occurrence, skip later duplicates
    QVector<SeriesEvent> uniqueEvents;
    uniqueEvents.reserve(events.size());
    QSet<QString> seenIds;
    for (const auto& ev : events) {
        if (!ev.eventId.isEmpty() && seenIds.contains(ev.eventId))
            continue;
        if (!ev.eventId.isEmpty())
            seenIds.insert(ev.eventId);
        uniqueEvents.append(ev);
    }

    // Group by crime-type bucket so each cluster uses the appropriate eps override
    QMap<QString, QVector<SeriesEvent>> buckets;
    for (const auto& ev : uniqueEvents)
        buckets[crimeBucket(ev.crimeType)].append(ev);

    QVector<CrimeSeries> result;
    int seriesOffset = 0;

    const QStringList bucketOrder = {
        QStringLiteral("burglary"),
        QStringLiteral("theft"),
        QStringLiteral("violent"),
        QStringLiteral("other"),
    };

    for (const QString& bucket : bucketOrder) {
        if (!buckets.contains(bucket))
            continue;
        const QVector<SeriesEvent>& bucketEvents = buckets[bucket];
        const double epsKm = epsKmFor(bucketEvents.first().crimeType);
        const auto bucketSeries = clusterSubset(bucketEvents, epsKm, seriesOffset);
        seriesOffset += bucketSeries.size();
        for (const auto& s : bucketSeries)
            result.append(s);
    }

    qCInfo(lcModels) << "Series detection found" << result.size() << "series from"
                     << uniqueEvents.size() << "events";
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
        double distM  = haversineKm(newEvent.lat, newEvent.lon, mem.lat, mem.lon) * 1000.0;
        double dtDays = std::abs(newEvent.tDays - mem.tDays);
        minDistM  = std::min(minDistM,  distM);
        minDtDays = std::min(minDtDays, dtDays);
    }

    match.spatialDistanceM      = minDistM;
    match.temporalDistanceDays  = minDtDays;

    // Component scores (0–1)
    double sSpatial  = std::max(0.0, 1.0 - minDistM  / nr.distM);
    double sTemporal = std::max(0.0, 1.0 - minDtDays / nr.days);

    const double moClamped = std::clamp(moSimilarity, 0.0, 1.0);
    double composite = 0.35 * sSpatial + 0.35 * sTemporal + 0.30 * moClamped;
    match.compositeScore = std::clamp(composite, 0.0, 1.0);

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

    if (setA.isEmpty() || setB.isEmpty()) return 0.0;

    QSet<QString> intersection = setA & setB;
    QSet<QString> unionSet     = setA | setB;

    return static_cast<double>(intersection.size())
         / static_cast<double>(unionSet.size());
}
