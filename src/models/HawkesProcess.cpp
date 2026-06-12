#include "models/HawkesProcess.h"
#include "core/SentinelLogger.h"
#include <cmath>
#include <algorithm>
#include <functional>
#include <limits>
#include <numeric>

static constexpr double PI_VAL = 3.14159265358979323846;

void HawkesProcess::setHistory(const QVector<SpatiotemporalEvent>& h)
{
    m_history = h;
    std::sort(m_history.begin(), m_history.end(),
              [](const SpatiotemporalEvent& a, const SpatiotemporalEvent& b) {
                  return a.tDays < b.tDays;
              });
}

// ---------------------------------------------------------------------------
// Triggering kernel
// φ(Δt, Δx) = α · β · exp(−β·Δt) · σ²/(‖Δx‖²+σ²)
// distSq is in squared degrees (lat/lon), Δt in days.
// ---------------------------------------------------------------------------

double HawkesProcess::triggerKernel(double dt, double distSq) const
{
    if (dt < 0.0) return 0.0;
    double sigma2 = m_params.sigma * m_params.sigma;
    double temporal = m_params.alpha * m_params.beta
                    * std::exp(-m_params.beta * dt);
    double spatial  = sigma2 / (distSq + sigma2);
    return temporal * spatial;
}

// ---------------------------------------------------------------------------
// Conditional intensity λ*(t, lat, lon)
// λ*(t,x) = μ + Σᵢ: tᵢ < t  φ(t−tᵢ, x−xᵢ)
// ---------------------------------------------------------------------------

double HawkesProcess::intensity(double tDays, double lat, double lon) const
{
    double lam = m_params.mu;
    const double tCutoff = std::min(20.0 / m_params.beta, 90.0);
    // Walk backward from the end to exploit time ordering
    for (int i = static_cast<int>(m_history.size()) - 1; i >= 0; --i) {
        if (m_history[i].tDays >= tDays) continue;
        double dt = tDays - m_history[i].tDays;
        if (dt > tCutoff) break;
        double dlat   = lat - m_history[i].lat;
        double dlon   = lon - m_history[i].lon;
        double distSq = dlat * dlat + dlon * dlon;
        lam += triggerKernel(dt, distSq);
    }
    return lam;
}

// ---------------------------------------------------------------------------
// Negative log-likelihood  (Mohler et al. 2011, eq. 3)
//
//  NLL = -Σᵢ log λ*(tᵢ,xᵢ) + ∫₀ᵀ ∫ λ*(t,x) dx dt
//
// Integral approximation:
//   Background integral: μ · T
//   Trigger integral: Σᵢ α · (1 − exp(−β(T−tᵢ)))
//     (integral of β·exp(−β·Δt) over [0,T−tᵢ] = 1−exp(−β(T−tᵢ)))
//     The spatial integral of σ²/(‖x‖²+σ²) over R² = π·σ²·(π) → normalised
//     kernel already satisfies ∫ g(x)dx = π for our form, but we absorb this
//     into α.  In practice this is a standard approximation.
// ---------------------------------------------------------------------------

double HawkesProcess::negLogLikelihood(double mu, double alpha, double beta,
                                        double sigma,
                                        const QVector<SpatiotemporalEvent>& events) const
{
    if (mu <= 0.0 || alpha < 0.0 || alpha >= 1.0 || beta <= 0.0 || sigma <= 0.0)
        return std::numeric_limits<double>::max();

    int n = events.size();
    if (n == 0) return 0.0;

    double T = events.last().tDays - events.first().tDays;
    if (T <= 0.0) T = 1.0;

    // Temporal cutoff: only consider past events within this window.
    // Kernel is alpha*beta*exp(-beta*dt); contributions < 1e-6 are negligible
    // when beta*dt > -ln(1e-6/alpha*beta) ≈ 14 + ln(alpha*beta)
    // Cap at 90 days to bound complexity even when beta is small.
    const double tCutoff = std::min(20.0 / beta, 90.0);

    double sigma2 = sigma * sigma;

    // Sum of log intensities using a sliding window back-pointer
    double sumLogLam = 0.0;
    for (int i = 0; i < n; ++i) {
        double lam = mu;
        // Walk backward from i-1 while within temporal cutoff
        for (int j = i - 1; j >= 0; --j) {
            double dt = events[i].tDays - events[j].tDays;
            if (dt > tCutoff) break;  // sorted, so all earlier j will also exceed cutoff
            double dlat   = events[i].lat   - events[j].lat;
            double dlon   = events[i].lon   - events[j].lon;
            double distSq = dlat * dlat + dlon * dlon;
            lam += alpha * beta * std::exp(-beta * dt) * sigma2 / (distSq + sigma2);
        }
        if (lam <= 0.0) lam = 1e-300;
        sumLogLam += std::log(lam);
    }

    // Integral approximation
    double integralBg = mu * T;
    double integralTrigger = 0.0;
    for (int i = 0; i < n; ++i) {
        double rem = events.last().tDays - events[i].tDays;
        integralTrigger += alpha * (1.0 - std::exp(-beta * rem));
    }

    return -(sumLogLam) + integralBg + integralTrigger;
}

// ---------------------------------------------------------------------------
// Golden-section search (minimisation on [lo, hi])
// ---------------------------------------------------------------------------

double HawkesProcess::goldenSectionSearch(std::function<double(double)> f,
                                           double lo, double hi, double tol)
{
    constexpr double phi = 1.6180339887498948482;   // golden ratio
    constexpr double resphi = 2.0 - phi;             // = 1/phi²

    double x1 = lo + resphi * (hi - lo);
    double x2 = hi - resphi * (hi - lo);
    double f1 = f(x1);
    double f2 = f(x2);

    while (std::abs(hi - lo) > tol) {
        if (f1 < f2) {
            hi = x2;
            x2 = x1; f2 = f1;
            x1 = lo + resphi * (hi - lo);
            f1 = f(x1);
        } else {
            lo = x1;
            x1 = x2; f1 = f2;
            x2 = hi - resphi * (hi - lo);
            f2 = f(x2);
        }
    }
    return (lo + hi) / 2.0;
}

// ---------------------------------------------------------------------------
// Coordinate-descent optimizer
// Parameter bounds:
//   μ  ∈ [1e-6, 10]
//   α  ∈ [0,    0.99]
//   β  ∈ [0.01, 20]
//   σ  ∈ [1e-5, 0.5]
// ---------------------------------------------------------------------------

void HawkesProcess::optimise(const QVector<SpatiotemporalEvent>& events,
                               int maxIter)
{
    double mu    = m_params.mu;
    double alpha = m_params.alpha;
    double beta  = m_params.beta;
    double sigma = m_params.sigma;

    double prevNLL = std::numeric_limits<double>::max();

    for (int iter = 0; iter < maxIter; ++iter) {
        // Optimise μ
        mu = goldenSectionSearch(
            [&](double v) {
                return negLogLikelihood(v, alpha, beta, sigma, events);
            }, 1e-6, 10.0);

        // Optimise α
        alpha = goldenSectionSearch(
            [&](double v) {
                return negLogLikelihood(mu, v, beta, sigma, events);
            }, 1e-6, 0.99);

        // Optimise β — lower bound 0.1 prevents tCutoff from exploding to O(N^2)
        beta = goldenSectionSearch(
            [&](double v) {
                return negLogLikelihood(mu, alpha, v, sigma, events);
            }, 0.1, 20.0);

        // Optimise σ
        sigma = goldenSectionSearch(
            [&](double v) {
                return negLogLikelihood(mu, alpha, beta, v, events);
            }, 1e-5, 0.5);

        double nll = negLogLikelihood(mu, alpha, beta, sigma, events);
        if (std::abs(prevNLL - nll) < 1e-5) break;
        prevNLL = nll;
    }

    m_params.mu     = mu;
    m_params.alpha  = alpha;
    m_params.beta   = beta;
    m_params.sigma  = sigma;
    m_params.logLik = -negLogLikelihood(mu, alpha, beta, sigma, events);
    qCDebug(lcModels) << "Hawkes fit: mu=" << mu << "alpha=" << alpha
                      << "beta=" << beta << "sigma=" << sigma;
}

// ---------------------------------------------------------------------------
// fit()
// ---------------------------------------------------------------------------

bool HawkesProcess::fit(const QVector<SpatiotemporalEvent>& events,
                         int maxIterations)
{
    m_fitted = false;
    if (events.isEmpty()) return false;

    // Sort by time
    QVector<SpatiotemporalEvent> sorted = events;
    std::sort(sorted.begin(), sorted.end(),
              [](const SpatiotemporalEvent& a, const SpatiotemporalEvent& b) {
                  return a.tDays < b.tDays;
              });

    m_history = sorted;

    // Initialise parameters with sensible defaults
    m_params = HawkesParams{};

    optimise(sorted, maxIterations);

    m_fitted = true;
    return true;
}

// ---------------------------------------------------------------------------
// riskSurface()
// Returns a (gridN × gridN) grid of conditional intensities.
// Row i → lat, column j → lon.
// ---------------------------------------------------------------------------

std::vector<std::vector<double>>
HawkesProcess::riskSurface(double tDays,
                             double latMin, double latMax,
                             double lonMin, double lonMax,
                             int gridN) const
{
    std::vector<std::vector<double>> grid(
        static_cast<std::size_t>(gridN),
        std::vector<double>(static_cast<std::size_t>(gridN), 0.0));

    double latStep = (latMax - latMin) / std::max(gridN - 1, 1);
    double lonStep = (lonMax - lonMin) / std::max(gridN - 1, 1);

    for (int i = 0; i < gridN; ++i) {
        double lat = latMin + i * latStep;
        for (int j = 0; j < gridN; ++j) {
            double lon = lonMin + j * lonStep;
            grid[static_cast<std::size_t>(i)][static_cast<std::size_t>(j)]
                = intensity(tDays, lat, lon);
        }
    }
    return grid;
}
