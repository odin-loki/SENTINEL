# SENTINEL — Statistical Models Reference

## 1. PoissonBaseline

**File**: `src/models/PoissonBaseline.h/cpp`

### Purpose
Predict the probability that at least one crime will occur in a given zone, time bucket, and crime type.

### Method
Non-homogeneous Poisson process with historical rates per bucket key `zone|hour|dow|month|crimeType`.

- If observed variance > mean (overdispersion), automatically switches to Negative Binomial parameterisation.
- Provides PMF, PPF (quantile function), 90% credible intervals via Poisson/NegBin distributions.

### Key Output: `PoissonPrediction`
```cpp
struct PoissonPrediction {
    double lambda;                        // λ (expected count per window)
    double probAtLeastOne;                // P(X ≥ 1) = 1 - exp(-λ)
    double expectedCount;                 // E[N]
    std::pair<double,double> ci90;        // (5th, 95th) percentile on count
    int    nObservations;                 // number of training windows
    QString model;                        // "poisson" | "negative_binomial"
};
```

---

## 2. HawkesProcess

**File**: `src/models/HawkesProcess.h/cpp`

### Purpose
Model the self-exciting (contagion) nature of crime: after a crime occurs, the probability of a near-repeat incident temporarily increases.

### Method
Hawkes conditional intensity:
```
λ(t, x) = μ + Σᵢ:tᵢ<t  α · exp(-β(t - tᵢ)) · G(x - xᵢ, σ)
```
where:
- `μ` = background intensity
- `α` = excitation magnitude
- `β` = temporal decay rate
- `G(Δx, σ)` = 2D isotropic Gaussian kernel

**Fitting**: Maximum likelihood estimation of `(μ, α, β)` over the training event set.

### Key Output: `HawkesParams`
```cpp
struct HawkesParams {
    double mu;     // background rate
    double alpha;  // excitation
    double beta;   // decay
};
```

---

## 3. SeriesDetector

**File**: `src/models/SeriesDetector.h/cpp`

### Purpose
Identify crime series — clusters of events likely committed by the same offender(s).

### Method
1. Compute pairwise distances in a 3D normalised space: `(spatial_km, temporal_days, mo_dissimilarity)`
2. Apply DBSCAN with per-crime-type epsilon from calibrated near-repeat literature
3. Assign `linkProbability` using a logistic function of composite score

**MO distance**: Jaccard dissimilarity on MO feature sets  
**Spatial distance**: Haversine formula (metres)

### Key Output: `SeriesMatch`
```cpp
struct SeriesMatch {
    QString seriesId;            // cluster identifier
    int     memberCount;         // events in this cluster
    double  linkProbability;     // [0,1] — logistic(compositeScore)
    double  spatialDistanceM;    // metres from query event
    double  temporalDistanceDays;
    double  moSimilarity;        // Jaccard [0,1]
    double  compositeScore;
    QString method;              // "dbscan_3d"
};
```

---

## 4. KDEHotspot

**File**: `src/models/KDEHotspot.h/cpp`

### Purpose
Estimate continuous spatial crime density and identify hotspot regions.

### Method
2D Gaussian Kernel Density Estimation:
```
f(x,y) = (1/n) Σᵢ K_h(x-xᵢ, y-yᵢ)
```
where `K_h` is a bivariate Gaussian with bandwidth `h`.

**Bandwidth**: Silverman's rule of thumb `h = 0.9 · σ · n^(-1/5)`, or user-specified.

**Hotspot extraction**: Local maxima with non-maximum suppression, ranked by density.

**PAI (Predictive Accuracy Index)**: `PAI = (n_cap/n_total) / (a_cap/a_total)` — higher is better.

### Key Output: `HotspotRegion`
```cpp
struct HotspotRegion {
    double lat, lon;
    double density;
    double radiusKm;
    int rank;
};
```

---

## 5. GPRegression

**File**: `src/models/GPRegression.h/cpp`

### Purpose
Non-parametric regression for temporal or spatial crime trend modelling.

### Method
Gaussian Process with squared-exponential (RBF) kernel:
```
k(x, x') = σ² · exp(-||x-x'||² / (2l²))
```

**Training**: Cholesky decomposition of `(K + σₙ²I)` for numerical stability.

**Prediction**:
```
μ* = K*ᵀ α         (posterior mean)
σ*² = K** - K*ᵀ v  (posterior variance)
```

**Model selection**: Log marginal likelihood `log p(y|X) = -½ yᵀα - Σ log Lᵢᵢ - n/2 log 2π`

### Key Output
```cpp
double predict(double x);                              // mean
std::pair<double,double> predictWithUncertainty(x);   // (mean, variance)
double logMarginalLikelihood();
```

---

## 6. BayesianHierarchical

**File**: `src/models/BayesianHierarchical.h/cpp`

### Purpose
Estimate zone-specific crime rates with partial pooling across zones — borrowing statistical strength from similar zones.

### Method
Gamma-Poisson conjugate hierarchy:
```
Yₖ | λₖ ~ Poisson(λₖ · Eₖ)
λₖ | α, β ~ Gamma(α, β)
```

**Empirical Bayes**: hyperparameters `(α₀, β₀)` estimated from observed zone rates:
```
α₀ = μ̄² / σ̄²
β₀ = μ̄ / σ̄²
```

**Posterior**: `λₖ | data ~ Gamma(α₀ + kₖ, β₀ + Eₖ)`

**Shrinkage**: Zones with sparse data shrink towards the global mean; zones with many events are estimated from their local data.

### Key Output: `ZonePosterior`
```cpp
struct ZonePosterior {
    QString zoneId;
    double  alphaPrior, betaPrior;    // hyperprior parameters
    double  alphaPost,  betaPost;     // posterior parameters
    double  posteriorMean;            // E[λ|data] = alphaPost / betaPost
    double  posteriorVar;             // Var[λ|data]
    double  credibleLow;              // 5th percentile (Wilson-Hilferty approx)
    double  credibleHigh;             // 95th percentile
    int     observedCount;
    double  exposure;                 // training window (days)
};
```

---

## 7. RiskForecaster

**File**: `src/models/RiskForecaster.h/cpp`

### Purpose
Produce multi-day zone risk forecasts with alert level classification.

### Method
Combines:
- Bayesian posterior mean rate from `BayesianHierarchical`
- Poisson baseline trend from `PoissonBaseline`
- Hawkes process near-term excitation
- Rolling 7-day event count escalation signal
- `TemporalFeatures` (weekend, payday, holiday proximity)

Output is normalised to `[0,1]` and thresholded against configurable alert levels.

### Key Output: `ForecastDay` / `ZoneForecast`
```cpp
struct ForecastDay {
    QDate date;
    double riskScore;         // [0,1]
    AlertLevel alertLevel;    // NORMAL | ELEVATED | HIGH | CRITICAL
    double lowerCI, upperCI;
};
```

---

## 8. EnsemblePredictor

**File**: `src/models/EnsemblePredictor.h/cpp`

### Purpose
Combine Poisson and Hawkes predictions into a single calibrated forecast with uncertainty decomposition.

### Method
1. Weighted linear combination: `p̂ = w₁ · p̂_poisson + w₂ · p̂_hawkes`
2. Post-hoc calibration via isotonic regression (PAVA)
3. Uncertainty decomposition:
   - **Aleatoric**: mean of individual model variances  
   - **Epistemic**: variance of individual model means

### Key Output: `EnsemblePrediction`
```cpp
struct EnsemblePrediction {
    double probCrime;              // P(≥1 crime) [0,1], post-calibration
    double expectedCount;          // E[N]
    QPair<double,double> ci90;     // 90% CI on count
    double ciLow95, ciHigh95;      // 95% CI on probCrime
    double uncertaintyAleatoric;   // irreducible randomness (CI width / 3.29)
    double uncertaintyEpistemic;   // model disagreement |p_poi - p_hawk| / 2
    double poissonWeight;          // Poisson contribution fraction [0,1]
    double hawkesWeight;           // Hawkes contribution fraction [0,1]
    bool   calibrated;             // true if isotonic calibration applied
    QString dominantModel;         // "poisson" | "hawkes" | "equal"
};
```

---

## 9. TemporalFeatures

**File**: `src/models/TemporalFeatures.h/cpp`

### Purpose
Convert raw timestamps into a feature vector suitable for statistical models.

### Features
| Feature | Encoding |
|---|---|
| Hour of day | `sin(2π·h/24)`, `cos(2π·h/24)` |
| Day of week | `sin(2π·d/7)`, `cos(2π·d/7)` |
| Month | `sin(2π·m/12)`, `cos(2π·m/12)` |
| Lunar phase | `fmod(days_since_2000-01-06, 29.53) / 29.53` (0=new, 0.5=full) |
| Sun altitude | `cos(hourAngle) · latitude_factor` |
| Weekend flag | Binary |
| Night flag | Binary (22:00–06:00) |
| Payday proximity | `min(doy%14, 14-doy%14)` � days to nearest fortnightly payday |

---

## Calibration Analysis

**File**: `src/benchmark/CalibrationAnalyser.h/cpp`

### Metrics
- **ECE** (Expected Calibration Error): `Σ_b (|b| / n) · |acc(b) - conf(b)|`
- **MCE** (Maximum Calibration Error): `max_b |acc(b) - conf(b)|`
- **ACE** (Average Calibration Error): `(1/B) Σ_b |acc(b) - conf(b)|`
- **Brier Score**: `(1/n) Σ (p̂ᵢ - yᵢ)²`
- **Log Loss**: `-(1/n) Σ [yᵢ log(p̂ᵢ) + (1-yᵢ) log(1-p̂ᵢ)]`

### Recalibration
Isotonic regression (PAVA algorithm) fitted to `(predicted_prob, observed_outcome)` pairs. Monotonicity constraint enforced post-fit.
