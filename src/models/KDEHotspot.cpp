#include "models/KDEHotspot.h"

#include <algorithm>
#include <cmath>
#include <numeric>

// ─────────────────────────────────────────────────────────────────────────────

KDEHotspot::KDEHotspot(int gridN, double bandwidthMultiplier)
    : m_gridN(std::max(gridN, 4))
    , m_bwMultiplier(std::max(bandwidthMultiplier, 0.1))
{
}

// ─── Gaussian kernel ─────────────────────────────────────────────────────────

double KDEHotspot::gaussianKernel(double dx, double dy, double hx, double hy)
{
    const double u = dx / hx;
    const double v = dy / hy;
    return std::exp(-0.5 * (u * u + v * v));
}

// ─── Silverman bandwidth ─────────────────────────────────────────────────────

double KDEHotspot::silvermanBandwidth(const QVector<double>& values, double multiplier)
{
    if (values.size() < 2) return 0.01;

    const double n = static_cast<double>(values.size());
    double mean = 0.0;
    for (double v : values) mean += v;
    mean /= n;

    double var = 0.0;
    for (double v : values) var += (v - mean) * (v - mean);
    var /= (n - 1.0);
    const double sigma = std::sqrt(var);

    // h = 1.06 * σ * n^(-1/5); clamp to avoid zero bandwidth on identical points
    const double h = multiplier * 1.06 * sigma * std::pow(n, -0.2);
    return std::max(h, 1e-6);
}

// ─── KDE surface ─────────────────────────────────────────────────────────────

std::vector<std::vector<double>> KDEHotspot::compute(
    const QVector<QPair<double,double>>& locations,
    double latMin, double latMax,
    double lonMin, double lonMax) const
{
    const int N = m_gridN;
    std::vector<std::vector<double>> surface(N, std::vector<double>(N, 0.0));

    if (locations.isEmpty()) return surface;

    // Compute bandwidths via Silverman's rule
    QVector<double> lats, lons;
    lats.reserve(locations.size());
    lons.reserve(locations.size());
    for (const auto& [lat, lon] : locations) {
        lats.append(lat);
        lons.append(lon);
    }
    const double hLat = silvermanBandwidth(lats, m_bwMultiplier);
    const double hLon = silvermanBandwidth(lons, m_bwMultiplier);

    const double dLat = (latMax - latMin) / N;
    const double dLon = (lonMax - lonMin) / N;

    // Clamp bandwidth to at least half a grid cell so that co-located or
    // near-identical events still produce a visible peak in the surface.
    const double safeHLat = std::max(hLat, dLat * 0.5);
    const double safeHLon = std::max(hLon, dLon * 0.5);

    // Evaluate KDE at each grid cell
    for (int r = 0; r < N; ++r) {
        const double gridLat = latMin + (r + 0.5) * dLat;
        for (int c = 0; c < N; ++c) {
            const double gridLon = lonMin + (c + 0.5) * dLon;
            double density = 0.0;
            for (const auto& [lat, lon] : locations)
                density += gaussianKernel(gridLat - lat, gridLon - lon,
                                           safeHLat, safeHLon);
            surface[r][c] = density / locations.size();
        }
    }

    // Normalise so sum of all cells = 1
    double total = 0.0;
    for (const auto& row : surface) for (double v : row) total += v;
    if (total > 0.0)
        for (auto& row : surface) for (double& v : row) v /= total;

    return surface;
}

// ─── Find hotspots ───────────────────────────────────────────────────────────

QVector<HotspotRegion> KDEHotspot::findHotspots(
    const QVector<QPair<double,double>>& locations,
    double latMin, double latMax,
    double lonMin, double lonMax,
    int topK, double suppressionRadius) const
{
    if (locations.isEmpty()) return {};

    const int effectiveTopK = std::min(topK, static_cast<int>(locations.size()));

    const auto surface = compute(locations, latMin, latMax, lonMin, lonMax);
    const int N = m_gridN;
    const double dLat = (latMax - latMin) / N;
    const double dLon = (lonMax - lonMin) / N;

    // Collect all (density, row, col) pairs
    struct Cell { double density; int r, c; };
    QVector<Cell> cells;
    cells.reserve(N * N);
    for (int r = 0; r < N; ++r)
        for (int c = 0; c < N; ++c)
            cells.append({surface[r][c], r, c});

    std::sort(cells.begin(), cells.end(),
              [](const Cell& a, const Cell& b) { return a.density > b.density; });

    // Greedy peak selection with suppression
    QVector<HotspotRegion> regions;
    QVector<QPair<double,double>> selectedCentroids;

    for (const auto& cell : cells) {
        if (regions.size() >= effectiveTopK) break;
        if (cell.density <= 0.0) break;

        const double peakLat = latMin + (cell.r + 0.5) * dLat;
        const double peakLon = lonMin + (cell.c + 0.5) * dLon;

        // Suppression: skip if within suppressionRadius of an existing peak
        bool suppressed = false;
        for (const auto& [sLat, sLon] : selectedCentroids) {
            const double dist = std::hypot(peakLat - sLat, peakLon - sLon);
            if (dist < suppressionRadius) { suppressed = true; break; }
        }
        if (suppressed) continue;

        selectedCentroids.append({peakLat, peakLon});

        // Build bounding box: radius = suppressionRadius/2 from centroid
        const double bboxR = suppressionRadius / 2.0;
        HotspotRegion region;
        region.centroidLat = peakLat;
        region.centroidLon = peakLon;
        region.latMin = peakLat - bboxR;
        region.latMax = peakLat + bboxR;
        region.lonMin = peakLon - bboxR;
        region.lonMax = peakLon + bboxR;
        region.peakDensity = cell.density;
        region.rank = static_cast<int>(regions.size()) + 1;

        // Integrate mass and count crimes within bounding box
        double mass = 0.0;
        for (int r = 0; r < N; ++r) {
            const double gLat = latMin + (r + 0.5) * dLat;
            if (gLat < region.latMin || gLat > region.latMax) continue;
            for (int c = 0; c < N; ++c) {
                const double gLon = lonMin + (c + 0.5) * dLon;
                if (gLon < region.lonMin || gLon > region.lonMax) continue;
                mass += surface[r][c];
            }
        }
        region.totalMass = mass;

        for (const auto& [lat, lon] : locations) {
            if (lat >= region.latMin && lat <= region.latMax &&
                lon >= region.lonMin && lon <= region.lonMax)
                ++region.crimeCount;
        }

        regions.append(region);
    }

    return regions;
}

// ─── PAI area fraction ───────────────────────────────────────────────────────

double KDEHotspot::paiAreaFraction(
    const std::vector<std::vector<double>>& surface, double pFrac) const
{
    if (surface.empty()) return 1.0;

    const int N = static_cast<int>(surface.size());
    const int M = N > 0 ? static_cast<int>(surface[0].size()) : 0;
    if (M == 0) return 1.0;

    std::vector<double> flat;
    flat.reserve(N * M);
    for (const auto& row : surface)
        for (double v : row) flat.push_back(v);

    std::sort(flat.rbegin(), flat.rend());  // descending

    const double total = std::accumulate(flat.begin(), flat.end(), 0.0);
    if (total <= 0.0) return 1.0;

    double cumulative = 0.0;
    for (int k = 0; k < static_cast<int>(flat.size()); ++k) {
        cumulative += flat[k];
        if (cumulative >= pFrac * total)
            return static_cast<double>(k + 1) / flat.size();
    }
    return 1.0;
}
