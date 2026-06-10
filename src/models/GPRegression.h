#pragma once
// GPRegression — Gaussian Process regression for spatiotemporal crime prediction
// Kernel: Squared Exponential (RBF) k(x,x') = σ² exp(-||x-x'||² / (2*l²))
// with additive noise σ_n²
//
// Prediction: posterior mean μ* = K*ᵀ (K + σ_n²I)⁻¹ y
//             posterior variance σ*² = K** - K*ᵀ (K + σ_n²I)⁻¹ K*
//
// Training: Cholesky decomposition (O(n³))
// Hyperparameter tuning: grid search over (σ², l, σ_n²) via marginal likelihood

#include <QVector>
#include <QPair>

class GPRegression {
public:
    GPRegression() = default;

    // Fit GP to 2D inputs (lat/lon or time/space) and scalar outputs
    void fit(const QVector<QPair<double,double>>& X, const QVector<double>& y);

    // Point prediction: posterior mean at (x1, x2)
    double predict(double x1, double x2) const;

    // Returns (mean, variance) for probabilistic predictions
    QPair<double,double> predictWithUncertainty(double x1, double x2) const;

    bool isFitted() const { return m_fitted; }

    // Set hyperparameters manually; invalidates any previous fit
    void setKernelParams(double sigma2, double lengthscale, double noiseSigma2);

    // Log marginal likelihood: -½ yᵀα - Σ log(L_ii) - n/2 log(2π)
    // Valid only after fit(); returns 0 if not fitted
    double logMarginalLikelihood() const;

    int nTrainingPoints() const { return static_cast<int>(m_X.size()); }

private:
    // Squared Euclidean distance between two 2D points
    static double squaredDist(double x1a, double x2a, double x1b, double x2b);

    // SE kernel: σ² exp(-||x-x'||² / (2l²))
    double kernel(double x1a, double x2a, double x1b, double x2b) const;

    // Standard Cholesky decomposition: returns lower-triangular L s.t. L Lᵀ = A
    // Adds small jitter to diagonal for numerical stability
    static QVector<QVector<double>> cholesky(const QVector<QVector<double>>& A);

    // Forward substitution: solve L x = b  (L lower-triangular)
    static QVector<double> solveL(const QVector<QVector<double>>& L,
                                   const QVector<double>& b);

    // Back substitution: solve Lᵀ x = b  (L lower-triangular)
    static QVector<double> solveLT(const QVector<QVector<double>>& L,
                                    const QVector<double>& b);

    // Covariance vector between training set and a test point
    QVector<double> kStar(double x1, double x2) const;

    // ── Training data ─────────────────────────────────────────────────────────
    QVector<QPair<double,double>> m_X;
    QVector<double>               m_y;

    // ── Kernel hyperparameters ────────────────────────────────────────────────
    double m_sigma2      = 1.0;     // signal variance σ²
    double m_lengthscale = 1.0;     // length scale l
    double m_noiseSigma2 = 1e-3;    // noise variance σ_n²

    // ── Cached posterior quantities (valid after fit) ──────────────────────────
    QVector<QVector<double>> m_L;       // Cholesky factor of K + σ_n²I
    QVector<double>           m_alpha;  // α = (K + σ_n²I)⁻¹ y = L⁻ᵀ L⁻¹ y
    double                    m_logML  = 0.0;
    bool                      m_fitted = false;
};
