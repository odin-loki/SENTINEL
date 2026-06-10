#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// EnsemblePredictor — weighted ensemble of Poisson baseline + Hawkes process
//
// Combines the two probabilistic model outputs with calibrated weights:
//   P_ensemble(x > 0) = w_P * P_poisson + w_H * P_hawkes + w_R * P_recent
//
// Uncertainty decomposition:
//   aleatoric  = inherent randomness (from Poisson CI width)
//   epistemic  = model disagreement (spread between Poisson and Hawkes)
//   combined   = sqrt(aleatoric² + epistemic²)
//
// Calibration: isotonic regression over historical holdout
// (simplified: linear interpolation from calibration table)
// ─────────────────────────────────────────────────────────────────────────────
#include <QVector>
#include <QString>
#include <QDateTime>
#include <QPair>
#include "core/CrimeEvent.h"
#include "models/PoissonBaseline.h"
#include "models/HawkesProcess.h"

struct EnsemblePrediction {
    double probCrime             = 0.0;  // P(at least 1 crime) [0,1]
    double expectedCount         = 0.0;  // E[N]
    QPair<double, double> ci90  {};      // 90% credible interval on count
    double ciLow95               = 0.0;  // 95% CI lower bound on probCrime
    double ciHigh95              = 1.0;  // 95% CI upper bound on probCrime
    double uncertaintyAleatoric  = 0.0;  // irreducible randomness
    double uncertaintyEpistemic  = 0.0;  // model uncertainty
    double poissonWeight         = 0.0;  // contribution fraction
    double hawkesWeight          = 0.0;
    bool   calibrated            = false;
    QString dominantModel;               // "poisson" | "hawkes" | "equal"
};

class EnsemblePredictor {
public:
    EnsemblePredictor() = default;

    // Set the component models (must already be fitted)
    void setPoisson(const PoissonBaseline* poisson) { m_poisson = poisson; }
    void setHawkes(const HawkesProcess*   hawkes)  { m_hawkes  = hawkes; }

    // Set ensemble weights (must sum to 1.0)
    void setWeights(double wPoisson, double wHawkes);

    // Calibrate from validation data.
    // calibrationData: list of (predicted_prob, actual_binary)
    void calibrate(const QVector<QPair<double, double>>& calibrationData);

    // Predict at a specific (zone, time, lat, lon)
    EnsemblePrediction predict(const QString& zoneId,
                                const QDateTime& dt,
                                const QString& crimeType,
                                double lat, double lon) const;

    // Risk surface over grid (ensemble weighted)
    // Returns n×n grid of EnsemblePredictions
    QVector<QVector<EnsemblePrediction>> riskGrid(
        const QDateTime& dt,
        double latMin, double latMax,
        double lonMin, double lonMax,
        int gridN = 20) const;

    bool isReady() const { return m_poisson != nullptr || m_hawkes != nullptr; }

    // Expected Calibration Error (ECE) on held-out data
    // bins: number of probability buckets
    static double ece(const QVector<QPair<double, double>>& predActual,
                      int bins = 10);

    // Brier score
    static double brierScore(const QVector<QPair<double, double>>& predActual);

private:
    double applyCal(double rawProb) const;

    const PoissonBaseline* m_poisson = nullptr;
    const HawkesProcess*   m_hawkes  = nullptr;

    double m_wPoisson = 0.6;
    double m_wHawkes  = 0.4;

    // Calibration table: list of (raw_prob, calibrated_prob) breakpoints
    // sorted by raw_prob ascending
    QVector<QPair<double, double>> m_calTable;
    bool m_calibrated = false;
};
