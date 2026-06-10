#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// BayesianHierarchical — Hierarchical Bayesian crime-rate model
//
// Model specification (conjugate Gamma-Poisson hierarchy):
//
//   Global hyperprior:
//     λ_global ~ Gamma(α₀, β₀)      global crime rate prior
//
//   Zone-level:
//     λ_z | α₀, β₀ ~ Gamma(α₀, β₀) zone rate drawn from global distribution
//     y_{z,t} | λ_z ~ Poisson(λ_z * E_t)  counts in time interval t
//
//   Conjugate update (zone z observed k crimes in interval of exposure E):
//     λ_z | y ~ Gamma(α₀ + k, β₀ + E)
//
//   Posterior predictive:
//     P(y* = k | data) = NegBin(k; α_post, β_post / (β_post + E*))
//
//   Partial pooling:
//     α₀, β₀ estimated via empirical Bayes (method of moments across zones):
//       α₀ = μ² / σ²    β₀ = μ / σ²    where μ,σ² are cross-zone mean/variance
//
// References:
//   Gelman & Hill (2007) Chapter 12 — Multilevel Models
//   Lawson (2013) Statistical Methods in Spatial Epidemiology
// ─────────────────────────────────────────────────────────────────────────────
#include <QVector>
#include <QMap>
#include <QString>
#include "core/CrimeEvent.h"

struct ZonePosterior {
    QString zoneId;
    double  alphaPrior  = 1.0;   // Gamma shape (hyperprior)
    double  betaPrior   = 1.0;   // Gamma rate  (hyperprior)
    double  alphaPost   = 1.0;   // Gamma shape (posterior)
    double  betaPost    = 1.0;   // Gamma rate  (posterior)
    double  posteriorMean = 0.0; // E[λ|data] = alphaPost / betaPost
    double  posteriorVar  = 0.0; // Var[λ|data] = alphaPost / betaPost²
    double  credibleLow   = 0.0; // 5th percentile of Gamma posterior
    double  credibleHigh  = 0.0; // 95th percentile of Gamma posterior
    int     observedCount = 0;
    double  exposure      = 1.0; // time window (days)
};

class BayesianHierarchical {
public:
    BayesianHierarchical() = default;

    // Fit global hyperparameters via empirical Bayes.
    // events: all crime events; crimeType: optional filter ("" = all)
    void fit(const QVector<CrimeEvent>& events,
             double exposureDays = 30.0,
             const QString& crimeType = {});

    // Get posterior for a specific zone
    ZonePosterior posteriorForZone(const QString& zoneId) const;

    // Get posteriors for all fitted zones, sorted by posteriorMean desc
    QVector<ZonePosterior> allPosteriors() const;

    // Posterior predictive: P(y_new ≥ k | zone) using Negative Binomial
    double predictiveProbability(const QString& zoneId, int minCount) const;

    // Predict expected count for a zone over a given exposure period
    double predictMean(const QString& zoneId, double exposureDays) const;

    // Shrinkage estimate: blend zone-specific and global rate
    // Returns (1-w)*globalMean + w*zoneMean where w depends on zone confidence
    double shrinkageEstimate(const QString& zoneId) const;

    bool isFitted() const { return m_fitted; }
    int  zoneCount() const { return static_cast<int>(m_posteriors.size()); }

    double globalAlpha() const { return m_alpha0; }
    double globalBeta()  const { return m_beta0; }
    double globalMean()  const { return m_fitted ? m_alpha0 / m_beta0 : 0.0; }

private:
    void estimateHyperparameters(const QVector<double>& zoneMeans,
                                  const QVector<int>& zoneCounts);
    static double gammaPPF(double alpha, double beta, double p);
    static double negBinomCDF(int k, double r, double p);

    bool   m_fitted = false;
    double m_alpha0 = 1.0;
    double m_beta0  = 1.0;
    double m_exposureDays = 30.0;
    QMap<QString, ZonePosterior> m_posteriors;
};
