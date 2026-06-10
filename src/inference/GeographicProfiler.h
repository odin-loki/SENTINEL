#pragma once
#include <QVector>
#include <QPair>
#include "core/CrimeEvent.h"

class GeographicProfiler {
public:
    explicit GeographicProfiler(double f = 1.2, double g = 1.2,
                                  double bufferKm = 0.5, int gridN = 80);

    // Compute CGT probability surface given a series of crime locations.
    // Minimum 3 crimes recommended.
    GeographicProfile profile(const QVector<QPair<double,double>>& crimeLocations) const;

private:
    double m_f;         // Rossmo decay exponent (near zone)
    double m_g;         // Rossmo decay exponent (far zone)
    double m_bufferDeg; // buffer zone radius in degrees (~km/111)
    int m_gridN;        // grid resolution

    double rossmoContrib(double gridLat, double gridLon,
                          double crimeLat, double crimeLon) const;

    std::vector<std::vector<double>> gaussianSmooth(
        const std::vector<std::vector<double>>& grid, double sigma) const;

    double searchArea(const std::vector<std::vector<double>>& surface,
                      const std::vector<double>& lats,
                      const std::vector<double>& lons,
                      double threshold) const;
};
