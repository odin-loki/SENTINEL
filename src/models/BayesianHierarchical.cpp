#include "models/BayesianHierarchical.h"
#include <cmath>
#include <algorithm>
#include <numeric>

// ─── Gamma PPF (approximate via Newton's method on regularised incomplete gamma)
// Uses the Wilson-Hilferty normal approximation for the quantile function.
double BayesianHierarchical::gammaPPF(double alpha, double beta, double p)
{
    if (p <= 0.0) return 0.0;
    if (p >= 1.0) return 1e9;
    if (alpha <= 0.0 || beta <= 0.0) return 0.0;

    // Standard Gamma quantile: Q ~ alpha * (1 - 1/(9*alpha) + z/sqrt(9*alpha))^3
    // where z is the standard normal quantile
    // Approximate z via rational approximation (Abramowitz & Stegun 26.2.23)
    double q = (p < 0.5) ? p : (1.0 - p);
    double t = std::sqrt(-2.0 * std::log(q));
    const double c0 = 2.515517, c1 = 0.802853, c2 = 0.010328;
    const double d1 = 1.432788, d2 = 0.189269, d3 = 0.001308;
    double z = t - (c0 + c1*t + c2*t*t) / (1.0 + d1*t + d2*t*t + d3*t*t*t);
    if (p < 0.5) z = -z;

    // Wilson-Hilferty transformation
    const double h = 1.0 / (9.0 * alpha);
    const double x = alpha * std::pow(std::max(0.0, 1.0 - h + z * std::sqrt(h)), 3.0);
    return x / beta;   // scale by 1/beta (Gamma(alpha, beta) parameterised as rate)
}

// ─── Negative Binomial CDF P(X ≤ k | r, p)
// Using recurrence relation for exact computation
double BayesianHierarchical::negBinomCDF(int k, double r, double p_success)
{
    if (k < 0) return 0.0;
    if (p_success <= 0.0) return 1.0;
    if (p_success >= 1.0) return 0.0;

    // P(X = j) = C(j+r-1, j) * (1-p)^r * p^j
    const double q = 1.0 - p_success;
    double prob = 0.0;
    double term = std::pow(q, r);  // P(X=0)

    for (int j = 0; j <= k && j <= 10000; ++j) {
        prob += term;
        if (term < 1e-15 * prob) break;
        // Recurrence: P(X=j+1) = P(X=j) * (j+r)/(j+1) * p
        term *= (j + r) / (j + 1) * p_success;
    }
    return std::min(1.0, prob);
}

// ─── fit ─────────────────────────────────────────────────────────────────────
void BayesianHierarchical::fit(const QVector<CrimeEvent>& events,
                                double exposureDays,
                                const QString& crimeType)
{
    m_exposureDays = std::max(exposureDays, 1.0);
    m_fitted = false;
    m_posteriors.clear();

    // Count events per zone within the exposure window
    QMap<QString, int> zoneCounts;
    for (const auto& ev : events) {
        if (!crimeType.isEmpty() && ev.crimeType != crimeType) continue;
        const QString zone = ev.suburb.isEmpty() ? QStringLiteral("Unknown") : ev.suburb;
        ++zoneCounts[zone];
    }

    if (zoneCounts.isEmpty()) return;

    // Collect per-zone rates (counts / exposure)
    QVector<double> zoneMeans;
    QVector<int>    counts;
    for (auto it = zoneCounts.constBegin(); it != zoneCounts.constEnd(); ++it) {
        zoneMeans.append(static_cast<double>(it.value()) / m_exposureDays);
        counts.append(it.value());
    }

    // Estimate global hyperparameters via empirical Bayes
    estimateHyperparameters(zoneMeans, counts);

    // Compute zone posteriors
    for (auto it = zoneCounts.constBegin(); it != zoneCounts.constEnd(); ++it) {
        const QString& zone = it.key();
        const int      k    = it.value();

        ZonePosterior zp;
        zp.zoneId       = zone;
        zp.alphaPrior   = m_alpha0;
        zp.betaPrior    = m_beta0;
        // Conjugate update: Gamma(α₀ + k, β₀ + E)
        zp.alphaPost    = m_alpha0 + k;
        zp.betaPost     = m_beta0 + m_exposureDays;
        zp.posteriorMean = zp.alphaPost / zp.betaPost;
        zp.posteriorVar  = zp.alphaPost / (zp.betaPost * zp.betaPost);
        zp.credibleLow   = gammaPPF(zp.alphaPost, zp.betaPost, 0.05);
        zp.credibleHigh  = gammaPPF(zp.alphaPost, zp.betaPost, 0.95);
        zp.observedCount = k;
        zp.exposure      = m_exposureDays;

        m_posteriors.insert(zone, zp);
    }

    m_fitted = true;
}

// ─── estimateHyperparameters — method of moments ──────────────────────────────
void BayesianHierarchical::estimateHyperparameters(const QVector<double>& zoneMeans,
                                                     const QVector<int>& /*unused*/)
{
    const int n = static_cast<int>(zoneMeans.size());
    if (n == 0) { m_alpha0 = 1.0; m_beta0 = 1.0; return; }

    // Sample mean and variance of zone rates
    const double mu = std::accumulate(zoneMeans.begin(), zoneMeans.end(), 0.0) / n;
    double var = 0.0;
    for (double r : zoneMeans) var += (r - mu) * (r - mu);
    var = (n > 1) ? var / (n - 1) : 1.0;

    if (var < 1e-12 || mu < 1e-12) {
        m_alpha0 = std::max(mu * mu / std::max(var, 1e-9), 0.1);
        m_beta0  = std::max(mu / std::max(var, 1e-9), 0.1);
        return;
    }

    // Method of moments: E[λ] = α/β, Var[λ] = α/β²
    // → α = μ²/σ², β = μ/σ²
    m_alpha0 = std::max(mu * mu / var, 0.1);
    m_beta0  = std::max(mu / var, 0.1);
}

// ─── posteriorForZone ─────────────────────────────────────────────────────────
ZonePosterior BayesianHierarchical::posteriorForZone(const QString& zoneId) const
{
    auto it = m_posteriors.find(zoneId);
    if (it == m_posteriors.end()) {
        // Return prior for a zone with no observations (no exposure adjustment)
        ZonePosterior zp;
        zp.zoneId       = zoneId;
        zp.alphaPrior   = m_alpha0;
        zp.betaPrior    = m_beta0;
        zp.alphaPost    = m_alpha0;
        zp.betaPost     = m_beta0;
        zp.posteriorMean = zp.alphaPost / zp.betaPost;
        zp.posteriorVar  = zp.alphaPost / (zp.betaPost * zp.betaPost);
        zp.credibleLow  = gammaPPF(zp.alphaPost, zp.betaPost, 0.05);
        zp.credibleHigh = gammaPPF(zp.alphaPost, zp.betaPost, 0.95);
        return zp;
    }
    return it.value();
}

// ─── allPosteriors ────────────────────────────────────────────────────────────
QVector<ZonePosterior> BayesianHierarchical::allPosteriors() const
{
    QVector<ZonePosterior> result;
    for (const auto& zp : m_posteriors) result.append(zp);
    std::sort(result.begin(), result.end(),
              [](const ZonePosterior& a, const ZonePosterior& b) {
                  return a.posteriorMean > b.posteriorMean;
              });
    return result;
}

// ─── predictiveProbability ───────────────────────────────────────────────────
double BayesianHierarchical::predictiveProbability(const QString& zoneId,
                                                    int minCount) const
{
    const ZonePosterior zp = posteriorForZone(zoneId);
    // Posterior predictive: NegBin(r=alphaPost, p=E*/(betaPost+E*))
    const double E_star = m_exposureDays;
    const double p_neg  = E_star / (zp.betaPost + E_star);
    // P(Y ≥ minCount) = 1 - P(Y ≤ minCount-1)
    if (minCount <= 0) return 1.0;
    return 1.0 - negBinomCDF(minCount - 1, zp.alphaPost, p_neg);
}

// ─── predictMean ─────────────────────────────────────────────────────────────
double BayesianHierarchical::predictMean(const QString& zoneId,
                                          double exposureDays) const
{
    const ZonePosterior zp = posteriorForZone(zoneId);
    // E[Y | future exposure E*] = E[λ] * E* = posteriorMean * exposure
    return zp.posteriorMean * std::max(exposureDays, 0.0);
}

// ─── shrinkageEstimate ────────────────────────────────────────────────────────
double BayesianHierarchical::shrinkageEstimate(const QString& zoneId) const
{
    const ZonePosterior zp = posteriorForZone(zoneId);
    const double globalMu  = m_alpha0 / m_beta0;
    const double zoneMu    = zp.posteriorMean;
    // Shrinkage weight: proportional to zone confidence (count-based)
    // w = k / (k + β₀) — more data → more weight on zone estimate
    const double w = static_cast<double>(zp.observedCount) /
                     (zp.observedCount + m_beta0);
    return (1.0 - w) * globalMu + w * zoneMu;
}
