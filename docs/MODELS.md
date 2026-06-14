# SENTINEL — Statistical Models Reference

Nine statistical models in the model stack, plus `TemporalFeatures` for feature engineering.

---

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
    double lambda;                        // — (expected count per window)
    double probAtLeastOne;                // P(X — 1) = 1 - exp(-?)
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
?(t, x) = — + ??:t?<t  — · exp(-?(t - t?)) · G(x - x?, ?)
```
where:
- `?` = background intensity
- `?` = excitation magnitude
- `?` = temporal decay rate
- `G(?x, ?)` = 2D isotropic Gaussian kernel

**Triggering kernel** (used in MLE):
```
?(?t, ?x) = — · — · exp(??·?t) · ?² / (??x?² + ?²)
```

**Fitting**: Coordinate-descent with golden-section line search over `(?, ?, ?, ?)`.

### Key Output: `HawkesParams`
```cpp
struct HawkesParams {
    double mu;     // background rate
    double alpha;  // excitation
    double beta;   // decay
    double sigma;  // spatial bandwidth (degrees)
    double logLik; // log-likelihood at fitted params
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
f(x,y) = (1/n) ?? K_h(x-x?, y-y?)
```
where `K_h` is a bivariate Gaussian with bandwidth `h`.

**Bandwidth**: Silverman's rule of thumb `h = 1.06 · — · n^(-1/5)`, or user-specified via `bandwidthMultiplier`.

**Hotspot extraction**: Greedy peak selection with non-maximum suppression, ranked by density.

**PAI (Predictive Accuracy Index)**: `PAI = (n_cap/n_total) / (a_cap/a_total)` — higher is better.

### Key Output: `HotspotRegion`
```cpp
struct HotspotRegion {
    double centroidLat, centroidLon;
    double latMin, latMax, lonMin, lonMax;
    double peakDensity;   // kernel density at centroid
    double totalMass;     // integrated density in region
    int    crimeCount;    // crimes within bounding box
    int    rank;          // 1 = hottest
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
k(x, x') = ?² · exp(-||x-x'||² / (2l²))
```

**Training**: Cholesky decomposition of `(K + ??²I)` for numerical stability.

**Prediction**:
```
?* = K*? —         (posterior mean)
?*² = K** - K*? v  (posterior variance)
```

**Model selection**: Log marginal likelihood `log p(y|X) = -½ y?? - — log L?? - n/2 log 2?`

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
Y_z | ?_z ~ Poisson(?_z · E_z)
?_z | ?, — ~ Gamma(?, ?)
```

**Empirical Bayes**: hyperparameters `(??, ??)` estimated from observed zone rates:
```
?? = ??² / ??²
?? = ?? / ??²
```

**Posterior**: `?_z | data ~ Gamma(?? + k_z, ?? + E_z)`

**Shrinkage**: Zones with sparse data shrink towards the global mean; zones with many events are estimated from their local data.

### Key Output: `ZonePosterior`
```cpp
struct ZonePosterior {
    QString zoneId;
    double  alphaPrior, betaPrior;    // hyperprior parameters
    double  alphaPost,  betaPost;     // posterior parameters
    double  posteriorMean;            // E[?|data] = alphaPost / betaPost
    double  posteriorVar;             // Var[?|data]
    double  credibleLow;              // 5th percentile
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
- Poisson baseline trend from `PoissonBaseline`
- `TemporalFeatures` cyclical multipliers (hour, DOW, month, payday, weekend)
- Rolling 14-day event count escalation signal
- Configurable alert thresholds (Elevated / High / Critical)

Output is normalised to `[0,1]` and thresholded against configurable alert levels.

### Key Output: `ForecastDay` / `ZoneForecast`
```cpp
struct ForecastDay {
    QString zoneId;
    QDate   date;
    double  riskScore;           // [0,1]
    double  baselineProb;        // Poisson P(crime > 0)
    double  escalationFactor;    // recency boost [1, ?)
    double  temporalFactor;      // cyclical multiplier
    double  expectedCount;
    int     rank;
    QString explanation;
};

struct ZoneForecast {
    QString zoneId;
    QVector<ForecastDay> days;
    double weeklyRisk;             // sum of daily risks
    int    alertLevel;             // 0=Normal, 1=Elevated, 2=High, 3=Critical
};
```

---

## 8. EnsemblePredictor

**File**: `src/models/EnsemblePredictor.h/cpp`

### Purpose
Combine Poisson and Hawkes predictions into a single calibrated forecast with uncertainty decomposition.

### Method
1. Weighted linear combination: `p? = w? · p?_poisson + w? · p?_hawkes`
2. Post-hoc calibration via isotonic regression (binned linear interpolation)
3. Uncertainty decomposition:
   - **Aleatoric**: Poisson 90% CI width / 3.29 (irreducible randomness)
   - **Epistemic**: |p_poisson — p_hawkes| / 2 (model disagreement)
   - **Combined**: `?_total = sqrt(aleatoric² + epistemic²)`

### Key Output: `EnsemblePrediction`
```cpp
struct EnsemblePrediction {
    double probCrime;              // P(?1 crime) [0,1], post-calibration
    double expectedCount;          // E[N]
    QPair<double,double> ci90;     // 90% CI on count
    double ciLow95, ciHigh95;      // 95% CI on probCrime
    double uncertaintyAleatoric;   // irreducible randomness
    double uncertaintyEpistemic;   // model disagreement
    double poissonWeight;          // Poisson contribution fraction [0,1]
    double hawkesWeight;           // Hawkes contribution fraction [0,1]
    bool   calibrated;             // true if isotonic calibration applied
    QString dominantModel;         // "poisson" | "hawkes" | "equal"
};
```

---

## 9. NearRepeatVictimisation

**File**: `src/models/NearRepeatVictimisation.h/cpp`

### Purpose
Detect space-time near-repeat victimisation patterns — pairs of crimes occurring close in space and time beyond what independence would predict. Generates proximity-weighted alerts for follow-up incidents.

### Method
For each ordered pair of events `(prior, current)` with `prior` occurring before `current`:

1. Compute Haversine spatial distance `d` (metres) and temporal gap `?t` (days).
2. Apply linear decay kernels within crime-type-calibrated bandwidths (from `SeriesDetector::nearRepeatFor()`).
3. Emit a `NearRepeatAlert` when the combined score is positive.

**Spatial decay kernel** (linear, within bandwidth `b` metres):
```
S(d) = max(0, 1 — d/b)
```

**Temporal decay kernel** (linear, within window `w` days):
```
T(?t) = max(0, 1 — ?t/w)
```

**Alert score**:
```
alertScore = S(d) · T(?t)
```
When a crime type is supplied, `b` and `w` are taken from the published near-repeat calibration table; otherwise the instance defaults (`bandwidthM`, `windowDays`) apply.

### Knox Test Statistic
Ratio of observed to expected near pairs under spatial-temporal independence:
```
Knox = N_near / E_near
```
where:
- `N_near` = count of pairs with `d — bandwidthM` and `?t — windowDays`
- `E_near = C(n,2) · p_space · p_time`
- `p_space = min(1, ?·b² / A)` — near-area fraction of study region `A`
- `p_time = min(1, w / T_span)` — near-time fraction of observation span

Values **> 1.0** indicate significant space-time clustering beyond chance.

### Key Output: `NearRepeatAlert`
```cpp
struct NearRepeatAlert {
    QString eventId;              // current event
    QString priorEventId;         // prior event in pair
    double  alertScore;           // S(d) · T(?t) in [0,1]
    double  spatialDistanceM;     // Haversine distance (metres)
    double  temporalDistanceDays; // absolute time gap (days)
};
```

### References
- Sherman, L.W., Gartin, P.R., & Buerger, M.E. (1989). *Hot spots of predatory crime.* Criminology 27(1):27–56.
- Johnson, S.D. et al. (2007). *Space-time patterns of risk.* British Journal of Criminology 47(3):363–383.

---

## Feature Engineering: TemporalFeatures

**File**: `src/models/TemporalFeatures.h/cpp`

Not counted in the nine-model stack — lives in the feature-engineering layer but feeds `RiskForecaster` and other models.

### Purpose
Convert raw timestamps into a feature vector suitable for statistical models.

### Features
| Feature | Encoding |
|---|---|
| Hour of day | `sin(2?·h/24)`, `cos(2?·h/24)` |
| Day of week | `sin(2?·d/7)`, `cos(2?·d/7)` |
| Month | `sin(2?·m/12)`, `cos(2?·m/12)` |
| Day of year | `sin(2?·doy/365)`, `cos(2?·doy/365)` |
| Lunar phase | `fmod(days_since_2000-01-06, 29.53) / 29.53` (0=new, 0.5=full) |
| Sun altitude | Approximate altitude at latitude 51.5°N |
| Weekend flag | Binary |
| Night flag | Binary (22:00–05:59) |
| Public holiday | Binary |
| Payday proximity | Days to nearest fortnightly payday |
| Week of month | Integer week index |
| Dark flag | True when sun altitude < ?6° |

### Key Output: `TemporalFeatureVector`
See `docs/DATA_STRUCTURES.md` for full field listing.

---

## Calibration Analysis

**File**: `src/benchmark/CalibrationAnalyser.h/cpp`

### Metrics
- **ECE** (Expected Calibration Error): `?_b (|b| / n) · |acc(b) - conf(b)|`
- **MCE** (Maximum Calibration Error): `max_b |acc(b) - conf(b)|`
- **ACE** (Average Calibration Error): `(1/B) ?_b |acc(b) - conf(b)|`
- **Brier Score**: `(1/n) — (p?? - y?)²`
- **Log Loss**: `-(1/n) — [y? log(p??) + (1-y?) log(1-p??)]`

### Recalibration
Isotonic regression (PAVA algorithm) fitted to `(predicted_prob, observed_outcome)` pairs. Monotonicity constraint enforced post-fit.
