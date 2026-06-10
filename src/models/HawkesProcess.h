#pragma once
#include <QVector>
#include <QDateTime>
#include <functional>
#include <optional>
#include <vector>
#include "core/CrimeEvent.h"

struct SpatiotemporalEvent {
    double tDays;       // days since reference epoch
    double lat;
    double lon;
    QString crimeType;
};

class HawkesProcess {
public:
    HawkesProcess() = default;

    // Fit parameters by coordinate-descent with golden-section line search.
    // Returns true if the optimizer converged (log-likelihood improved).
    bool fit(const QVector<SpatiotemporalEvent>& events,
             int maxIterations = 10);

    // Conditional intensity λ*(t, lat, lon)
    double intensity(double tDays, double lat, double lon) const;

    // Risk surface over grid at time t.
    // Returns (gridN × gridN) nested vector, row = lat, col = lon.
    std::vector<std::vector<double>> riskSurface(
        double tDays,
        double latMin, double latMax,
        double lonMin, double lonMax,
        int gridN = 50) const;

    bool isFitted() const { return m_fitted; }
    const HawkesParams& params() const { return m_params; }

    // ── Test / inspection helpers ─────────────────────────────────────────
    // Allow callers to directly set parameters (e.g. for unit testing the
    // kernel and intensity functions without running the optimizer).
    void setParams(const HawkesParams& p) { m_params = p; }
    void setHistory(const QVector<SpatiotemporalEvent>& h) { m_history = h; }

    // Triggering kernel (public for testability):
    // φ(Δt, Δx) = α · β · exp(−β·Δt) · σ²/(‖Δx‖²+σ²)
    double triggerKernel(double dt, double distSq) const;

private:
    HawkesParams m_params;
    QVector<SpatiotemporalEvent> m_history;
    bool m_fitted = false;

    // Negative log-likelihood (Mohler et al. 2011)
    double negLogLikelihood(double mu, double alpha,
                             double beta, double sigma,
                             const QVector<SpatiotemporalEvent>& events) const;

    // Coordinate-descent optimizer using golden-section search per parameter
    void optimise(const QVector<SpatiotemporalEvent>& events, int maxIter);

    static double goldenSectionSearch(
        std::function<double(double)> f,
        double lo, double hi, double tol = 1e-4);
};
