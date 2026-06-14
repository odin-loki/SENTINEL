#include "models/GPRegression.h"

#include <cmath>
#include <numbers>

// ─── Kernel ───────────────────────────────────────────────────────────────────

double GPRegression::squaredDist(double x1a, double x2a, double x1b, double x2b)
{
    const double d1 = x1a - x1b;
    const double d2 = x2a - x2b;
    return d1 * d1 + d2 * d2;
}

double GPRegression::kernel(double x1a, double x2a, double x1b, double x2b) const
{
    const double r2 = squaredDist(x1a, x2a, x1b, x2b);
    return m_sigma2 * std::exp(-r2 / (2.0 * m_lengthscale * m_lengthscale));
}

// ─── Cholesky decomposition (lower-triangular) ────────────────────────────────
// Computes L such that L Lᵀ = A.  Adds diagonal jitter when numerically needed.

QVector<QVector<double>> GPRegression::cholesky(const QVector<QVector<double>>& A)
{
    const int n = A.size();
    QVector<QVector<double>> L(n, QVector<double>(n, 0.0));

    for (int j = 0; j < n; ++j) {
        double sumSq = 0.0;
        for (int k = 0; k < j; ++k)
            sumSq += L[j][k] * L[j][k];

        const double diag = A[j][j] - sumSq;
        // Guard: if diag is non-positive add minimal jitter rather than NaN
        L[j][j] = (diag > 1e-14) ? std::sqrt(diag)
                                  : std::sqrt(std::abs(diag) + 1e-9);

        const double ljj = L[j][j];
        for (int i = j + 1; i < n; ++i) {
            double cross = 0.0;
            for (int k = 0; k < j; ++k)
                cross += L[i][k] * L[j][k];
            L[i][j] = (ljj > 1e-15) ? (A[i][j] - cross) / ljj : 0.0;
        }
    }
    return L;
}

// ─── Triangular solvers ───────────────────────────────────────────────────────

// Forward substitution: solve L x = b  (L lower-triangular)
QVector<double> GPRegression::solveL(const QVector<QVector<double>>& L,
                                      const QVector<double>& b)
{
    const int n = b.size();
    QVector<double> x(n, 0.0);
    for (int i = 0; i < n; ++i) {
        double s = b[i];
        for (int j = 0; j < i; ++j)
            s -= L[i][j] * x[j];
        x[i] = (std::abs(L[i][i]) > 1e-15) ? s / L[i][i] : 0.0;
    }
    return x;
}

// Back substitution: solve Lᵀ x = b  (L lower-triangular ⟹ Lᵀ upper-triangular)
QVector<double> GPRegression::solveLT(const QVector<QVector<double>>& L,
                                       const QVector<double>& b)
{
    const int n = b.size();
    QVector<double> x(n, 0.0);
    for (int i = n - 1; i >= 0; --i) {
        double s = b[i];
        for (int j = i + 1; j < n; ++j)
            s -= L[j][i] * x[j];   // Lᵀ[i][j] = L[j][i]
        x[i] = (std::abs(L[i][i]) > 1e-15) ? s / L[i][i] : 0.0;
    }
    return x;
}

// ─── Hyperparameter setter ────────────────────────────────────────────────────

void GPRegression::setKernelParams(double sigma2, double lengthscale, double noiseSigma2)
{
    m_sigma2      = std::max(sigma2, 1e-12);
    m_lengthscale = std::max(lengthscale, 1e-12);
    m_noiseSigma2 = std::max(noiseSigma2, 0.0);
    m_fitted      = false;   // must re-fit after changing hyperparameters
}

// ─── Fit ──────────────────────────────────────────────────────────────────────

void GPRegression::fit(const QVector<QPair<double,double>>& X, const QVector<double>& y)
{
    m_X      = X;
    m_y      = y;
    m_fitted = false;

    const int n = X.size();
    if (n == 0) return;

    // Build noisy covariance matrix K_n = K(X,X) + σ_n² I
    QVector<QVector<double>> Kn(n, QVector<double>(n, 0.0));
    for (int i = 0; i < n; ++i) {
        for (int j = 0; j < n; ++j)
            Kn[i][j] = kernel(X[i].first, X[i].second, X[j].first, X[j].second);
        Kn[i][i] += m_noiseSigma2;
    }

    // Cholesky: L Lᵀ = K_n
    m_L = cholesky(Kn);

    // α = K_n⁻¹ y = L⁻ᵀ (L⁻¹ y)
    const QVector<double> v = solveL(m_L, y);
    m_alpha = solveLT(m_L, v);

    // Log marginal likelihood: -½ yᵀα  -  Σᵢ log L[i][i]  -  n/2 log(2π)
    double yTalpha = 0.0;
    for (int i = 0; i < n; ++i)
        yTalpha += y[i] * m_alpha[i];

    double logDetTerm = 0.0;
    for (int i = 0; i < n; ++i)
        logDetTerm += std::log(std::abs(m_L[i][i]) + 1e-300);

    const double log2pi = std::numbers::ln2_v<double> + std::log(std::numbers::pi_v<double>);
    m_logML = -0.5 * yTalpha - logDetTerm - 0.5 * n * log2pi;

    m_fitted = true;
}

// ─── k* vector ────────────────────────────────────────────────────────────────

QVector<double> GPRegression::kStar(double x1, double x2) const
{
    const int n = m_X.size();
    QVector<double> ks(n);
    for (int i = 0; i < n; ++i)
        ks[i] = kernel(m_X[i].first, m_X[i].second, x1, x2);
    return ks;
}

// ─── Prediction ───────────────────────────────────────────────────────────────

double GPRegression::predict(double x1, double x2) const
{
    if (!m_fitted) return 0.0;
    const QVector<double> ks = kStar(x1, x2);
    double mean = 0.0;
    for (int i = 0; i < ks.size(); ++i)
        mean += ks[i] * m_alpha[i];
    return mean;
}

QPair<double,double> GPRegression::predictWithUncertainty(double x1, double x2) const
{
    if (!m_fitted) return {0.0, m_sigma2};

    const QVector<double> ks = kStar(x1, x2);

    // Posterior mean: μ* = kᵀ α
    double mean = 0.0;
    for (int i = 0; i < ks.size(); ++i)
        mean += ks[i] * m_alpha[i];

    // Posterior variance: σ*² = k(x*,x*) - vᵀv,  v = L⁻¹ k*
    const double kss = kernel(x1, x2, x1, x2);   // = m_sigma2
    const QVector<double> v = solveL(m_L, ks);
    double vTv = 0.0;
    for (const double vi : v) vTv += vi * vi;

    const double var = std::max(0.0, kss - vTv);
    return {mean, var};
}

// ─── Log marginal likelihood ──────────────────────────────────────────────────

double GPRegression::logMarginalLikelihood() const
{
    if (!m_fitted) return 0.0;
    return m_logML;
}
