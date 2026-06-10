#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// KDEHotspot — kernel density estimation for spatial hotspot detection
//
// Computes a 2D Gaussian KDE surface from crime locations, then identifies
// the top-N hotspot regions by peak probability mass.
//
// Bandwidth selection:
//   Silverman's rule-of-thumb:  h = 1.06 * σ * n^(-1/5)
//   where σ = standard deviation of coordinates across both dimensions.
//
// Design reference: Chainey & Ratcliffe "GIS and Crime Mapping" (2005)
// ─────────────────────────────────────────────────────────────────────────────
#include <QVector>
#include <QPair>
#include <QString>
#include <vector>

struct HotspotRegion {
    double centroidLat  = 0.0;
    double centroidLon  = 0.0;
    double latMin       = 0.0;
    double latMax       = 0.0;
    double lonMin       = 0.0;
    double lonMax       = 0.0;
    double peakDensity  = 0.0;  // kernel density at centroid
    double totalMass    = 0.0;  // integrated density in this region
    int    crimeCount   = 0;    // crimes within the bounding box
    int    rank         = 0;    // 1 = hottest
};

class KDEHotspot {
public:
    explicit KDEHotspot(int gridN = 50, double bandwidthMultiplier = 1.0);

    // Compute KDE surface from (lat, lon) pairs.
    // Returns normalised density surface [gridN][gridN], row = lat, col = lon.
    std::vector<std::vector<double>> compute(
        const QVector<QPair<double,double>>& locations,
        double latMin, double latMax,
        double lonMin, double lonMax) const;

    // Identify top-k distinct hotspot regions via greedy peak selection
    // (each region = bounding box around a local maximum that is not
    //  within suppressionRadius degrees of a higher-ranked peak).
    QVector<HotspotRegion> findHotspots(
        const QVector<QPair<double,double>>& locations,
        double latMin, double latMax,
        double lonMin, double lonMax,
        int topK = 5,
        double suppressionRadius = 0.02) const;

    // Predictive Accuracy Index area threshold (fraction of area)
    // Returns the smallest fraction of cells that contain pFrac
    // of the total predicted crime (for PAI computation)
    double paiAreaFraction(
        const std::vector<std::vector<double>>& surface,
        double pFrac = 0.5) const;

    // Silverman bandwidth from data
    static double silvermanBandwidth(const QVector<double>& values,
                                      double multiplier = 1.0);

private:
    int    m_gridN;
    double m_bwMultiplier;

    static double gaussianKernel(double dx, double dy, double hx, double hy);
};
