#include "inference/GeographicProfiler.h"

#include <cmath>
#include <algorithm>
#include <numeric>
#include <limits>

static constexpr double GEO_PI = 3.14159265358979323846;

GeographicProfiler::GeographicProfiler(double f, double g,
                                         double bufferKm, int gridN)
    : m_f(f)
    , m_g(g)
    , m_bufferDeg(bufferKm / 111.0)
    , m_gridN(std::max(gridN, 2))
{}

double GeographicProfiler::rossmoContrib(double gridLat, double gridLon,
                                           double crimeLat, double crimeLon) const
{
    const double dLat = gridLat - crimeLat;
    const double dLon = gridLon - crimeLon;
    const double dist = std::sqrt(dLat * dLat + dLon * dLon);

    if (dist <= m_bufferDeg) {
        // Near zone (safety buffer) — Rossmo 1/d^f
        const double safeDist = std::max(dist, 1e-8);
        return 1.0 / std::pow(safeDist, m_f);
    } else if (dist < 2.0 * m_bufferDeg) {
        // Far zone (B < d < 2B) — Rossmo B^(g-f)/(2B-d)^g
        const double denom = std::max(2.0 * m_bufferDeg - dist, 1e-10);
        return std::pow(m_bufferDeg, m_g - m_f) / std::pow(denom, m_g);
    } else {
        // Beyond 2B — formula is undefined; use negligible decay
        return 0.0;
    }
}

std::vector<std::vector<double>> GeographicProfiler::gaussianSmooth(
    const std::vector<std::vector<double>>& grid, double sigma) const
{
    const int rows = static_cast<int>(grid.size());
    const int cols = rows > 0 ? static_cast<int>(grid[0].size()) : 0;
    if (rows == 0 || cols == 0) return grid;

    // Build 1-D Gaussian kernel
    const int halfK = static_cast<int>(std::ceil(3.0 * sigma));
    const int kSize = 2 * halfK + 1;
    std::vector<double> kernel(kSize);
    double kSum = 0.0;
    for (int i = 0; i < kSize; ++i) {
        const double x = i - halfK;
        kernel[i] = std::exp(-0.5 * (x * x) / (sigma * sigma));
        kSum += kernel[i];
    }
    for (auto& v : kernel) v /= kSum;

    // Horizontal pass
    std::vector<std::vector<double>> tmp(rows, std::vector<double>(cols, 0.0));
    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c) {
            double acc = 0.0;
            for (int k = 0; k < kSize; ++k) {
                const int cc = std::clamp(c + k - halfK, 0, cols - 1);
                acc += kernel[k] * grid[r][cc];
            }
            tmp[r][c] = acc;
        }
    }

    // Vertical pass
    std::vector<std::vector<double>> out(rows, std::vector<double>(cols, 0.0));
    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c) {
            double acc = 0.0;
            for (int k = 0; k < kSize; ++k) {
                const int rr = std::clamp(r + k - halfK, 0, rows - 1);
                acc += kernel[k] * tmp[rr][c];
            }
            out[r][c] = acc;
        }
    }
    return out;
}

double GeographicProfiler::searchArea(const std::vector<std::vector<double>>& surface,
                                        const std::vector<double>& lats,
                                        const std::vector<double>& lons,
                                        double threshold) const
{
    const int rows = static_cast<int>(surface.size());
    const int cols = rows > 0 ? static_cast<int>(surface[0].size()) : 0;
    if (rows < 2 || cols < 2) return 0.0;

    const double cellLatKm = std::abs(lats[1] - lats[0]) * 111.0;
    const double midLat    = (lats.front() + lats.back()) / 2.0;
    const double cellLonKm = std::abs(lons[1] - lons[0]) * 111.0 *
                             std::cos(midLat * GEO_PI / 180.0);
    const double cellAreaKm2 = cellLatKm * cellLonKm;

    // Flatten and sort cells by probability descending
    std::vector<double> flat;
    flat.reserve(rows * cols);
    for (const auto& row : surface)
        for (double v : row)
            flat.push_back(v);

    std::sort(flat.begin(), flat.end(), std::greater<double>());

    double cumSum = 0.0;
    int count = 0;
    for (double v : flat) {
        cumSum += v;
        ++count;
        if (cumSum >= threshold) break;
    }
    return count * cellAreaKm2;
}

GeographicProfile GeographicProfiler::profile(
    const QVector<QPair<double,double>>& crimeLocations) const
{
    GeographicProfile result;
    result.method = QStringLiteral("rossmo_cgt");

    if (crimeLocations.isEmpty()) return result;

    // Determine bounding box with margin
    double minLat =  std::numeric_limits<double>::max();
    double maxLat = -std::numeric_limits<double>::max();
    double minLon =  std::numeric_limits<double>::max();
    double maxLon = -std::numeric_limits<double>::max();

    for (const auto& loc : crimeLocations) {
        minLat = std::min(minLat, loc.first);
        maxLat = std::max(maxLat, loc.first);
        minLon = std::min(minLon, loc.second);
        maxLon = std::max(maxLon, loc.second);
    }

    const double marginDeg = std::max(0.05, m_bufferDeg * 4.0);
    minLat -= marginDeg; maxLat += marginDeg;
    minLon -= marginDeg; maxLon += marginDeg;

    const double latStep = (maxLat - minLat) / (m_gridN - 1);
    const double lonStep = (maxLon - minLon) / (m_gridN - 1);

    // Build coordinate axes
    result.gridLats.resize(m_gridN);
    result.gridLons.resize(m_gridN);
    for (int i = 0; i < m_gridN; ++i) {
        result.gridLats[i] = minLat + i * latStep;
        result.gridLons[i] = minLon + i * lonStep;
    }

    // Compute raw CGT surface
    std::vector<std::vector<double>> raw(m_gridN, std::vector<double>(m_gridN, 0.0));
    for (int r = 0; r < m_gridN; ++r) {
        for (int c = 0; c < m_gridN; ++c) {
            double total = 0.0;
            for (const auto& loc : crimeLocations) {
                total += rossmoContrib(result.gridLats[r], result.gridLons[c],
                                       loc.first, loc.second);
            }
            raw[r][c] = total;
        }
    }

    // Gaussian smoothing (sigma = 1.5 cells)
    auto smoothed = gaussianSmooth(raw, 1.5);

    // Normalise to probability surface
    double total = 0.0;
    for (const auto& row : smoothed)
        for (double v : row)
            total += v;

    if (total <= 0.0) total = 1.0;

    result.probabilitySurface.assign(m_gridN, std::vector<double>(m_gridN));
    double peakProb = 0.0;
    int peakR = 0, peakC = 0;
    for (int r = 0; r < m_gridN; ++r) {
        for (int c = 0; c < m_gridN; ++c) {
            const double p = smoothed[r][c] / total;
            result.probabilitySurface[r][c] = p;
            if (p > peakProb) {
                peakProb = p;
                peakR = r;
                peakC = c;
            }
        }
    }

    result.peakLat         = result.gridLats[peakR];
    result.peakLon         = result.gridLons[peakC];
    result.peakProbability = peakProb;

    result.searchArea50pct = searchArea(result.probabilitySurface,
                                         result.gridLats, result.gridLons, 0.50);
    result.searchArea80pct = searchArea(result.probabilitySurface,
                                         result.gridLats, result.gridLons, 0.80);
    return result;
}
