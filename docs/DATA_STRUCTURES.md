# SENTINEL — Core Data Structures

All shared structures are defined in `src/core/CrimeEvent.h`.

---

## Primary Event

### `CrimeEvent`
Represents a single crime record from any source.

| Field | Type | Description |
|---|---|---|
| `eventId` | `QString` | Primary key |
| `source` | `QString` | Data source identifier (e.g. "uk_police_v1") |
| `sourceVersion` | `QString` | Source schema version |
| `ingestedAt` | `QDateTime` | When the record was stored |
| `occurredAt` | `std::optional<QDateTime>` | When the crime occurred (UTC) |
| `crimeType` | `QString` | Normalised crime category |
| `crimeSubtype` | `std::optional<QString>` | More specific category |
| `lat`, `lon` | `std::optional<double>` | WGS-84 coordinates |
| `suburb` | `QString` | Suburb or area name |
| `street` | `QString` | Street name (may be anonymised) |
| `postcode` | `QString` | UK postcode or equivalent |
| `narrative` | `std::optional<QString>` | Free-text description |
| `outcome` | `QString` | "resolved" \| "unresolved" \| "unknown" |
| `qualityScore` | `double` | DataQualityScorer composite [0,1] |
| `meta` | `QJsonObject` | Source-specific raw fields |

### `QualityReport`
Output of `DataQualityScorer::score()`.

| Field | Type | Description |
|---|---|---|
| `eventId` | `QString` | Matching event |
| `completeness` | `double` | Fraction of required fields non-null |
| `temporalPrecision` | `QString` | "hour" \| "day" \| "month" \| "unknown" |
| `spatialPrecision` | `QString` | "exact" \| "block" \| "suburb" \| "unknown" |
| `sourceReliability` | `double` | Per-source reliability [0,1] |
| `compositeScore` | `double` | Weighted composite [0,1] |
| `quarantined` | `bool` | True if compositeScore < 0.3 |

---

## NLP Structures

### `MOFeatures`
Extracted from narrative text by `MOExtractor`.

| Field | Type | Description |
|---|---|---|
| `entryMethod` | `std::optional<QString>` | "forced_entry" \| "unlocked" \| "deception" \| "tailgating" |
| `targetType` | `std::optional<QString>` | "residential" \| "commercial" \| "vehicle" \| "person" |
| `timeOfDay` | `std::optional<QString>` | "early_morning" \| "morning" \| "afternoon" \| "evening" \| "night" |
| `weaponType` | `std::optional<QString>` | "firearm" \| "knife" \| "blunt" \| "other" |
| `itemsTaken` | `std::vector<QString>` | Stolen items (normalised) |
| `victimProfile` | `std::optional<QString>` | "elderly" \| "child" \| "female" \| "male" \| "vulnerable" |
| `soloOrGroup` | `std::optional<QString>` | "solo" \| "group" |

### `NLPResult`
Classification result from `CrimeClassifier`.

| Field | Type | Description |
|---|---|---|
| `eventId` | `QString` | Associated event |
| `crimeType` | `std::optional<QString>` | Classified crime category |
| `crimeTypeConfidence` | `double` | TF-IDF classification confidence [0,1] |
| `severityScore` | `double` | Crime severity [0.0–1.0] |
| `sentimentCompound` | `double` | VADER-style compound score [-1,1] |
| `threatSignal` | `bool` | True if threat language detected |
| `moFeatures` | `MOFeatures` | Extracted MO features |

---

## Prediction Structures

### `PoissonPrediction`
| Field | Type | Description |
|---|---|---|
| `lambda` | `double` | Expected count per window (λ) |
| `probAtLeastOne` | `double` | P(X ≥ 1) = 1 – exp(–λ) |
| `expectedCount` | `double` | E[N] |
| `ci90` | `std::pair<double,double>` | (5th, 95th) percentile on count |
| `nObservations` | `int` | Number of training windows |
| `model` | `QString` | "poisson" \| "negative_binomial" |

### `HawkesParams`
| Field | Type | Description |
|---|---|---|
| `mu` | `double` | Background intensity |
| `alpha` | `double` | Excitation magnitude (α < β for stationarity) |
| `beta` | `double` | Temporal decay rate (1/days) |
| `sigma` | `double` | Spatial bandwidth (degrees) |
| `logLik` | `double` | Log-likelihood at fitted parameters |

### `EnsemblePrediction`
| Field | Type | Description |
|---|---|---|
| `probCrime` | `double` | P(≥1 crime) post-calibration [0,1] |
| `expectedCount` | `double` | E[N] |
| `ci90` | `QPair<double,double>` | 90% CI on count |
| `ciLow95`, `ciHigh95` | `double` | 95% CI on probCrime |
| `uncertaintyAleatoric` | `double` | Irreducible randomness (CI width / 3.29) |
| `uncertaintyEpistemic` | `double` | Model disagreement \|p_poi – p_hawk\| / 2 |
| `poissonWeight`, `hawkesWeight` | `double` | Component contributions [0,1] |
| `calibrated` | `bool` | True if isotonic calibration applied |
| `dominantModel` | `QString` | "poisson" \| "hawkes" \| "equal" |

### `ForecastDay`
| Field | Type | Description |
|---|---|---|
| `date` | `QDate` | Forecast date |
| `riskScore` | `double` | Aggregated risk [0,1] |
| `alertLevel` | `AlertLevel` | NORMAL \| ELEVATED \| HIGH \| CRITICAL |
| `lowerCI`, `upperCI` | `double` | Credible interval bounds |

---

## Inference Structures

### `InvestigativeLead`
Output of `HintEngine`.

| Field | Type | Description |
|---|---|---|
| `rank` | `int` | Priority (1 = highest) |
| `category` | `QString` | "series_linkage" \| "mo_similarity" \| "geographic" \| "anomaly" \| "network_association" |
| `headline` | `QString` | Short lead title |
| `detail` | `QString` | Full explanation |
| `confidence` | `double` | Lead strength [0,1] |
| `confidenceMethod` | `QString` | Method used to estimate confidence |
| `supportingData` | `QJsonObject` | Structured supporting evidence |
| `contradictions` | `std::vector<QString>` | Conflicting leads detected |
| `provenance` | `std::vector<QString>` | Computation chain |
| `generatedAt` | `QDateTime` | When the lead was generated |

### `GeographicProfile`
Output of `GeographicProfiler`.

| Field | Type | Description |
|---|---|---|
| `peakLat`, `peakLon` | `double` | Highest-probability anchor point |
| `peakProbability` | `double` | Maximum surface value |
| `searchArea50pct` | `double` | km² containing 50% of probability mass |
| `searchArea80pct` | `double` | km² containing 80% of probability mass |
| `probabilitySurface` | `std::vector<std::vector<double>>` | Full probability grid |
| `gridLats`, `gridLons` | `std::vector<double>` | Grid axis coordinates |

### `AnomalySignal`
Output of `AnomalyDetector`.

| Field | Type | Description |
|---|---|---|
| `eventId` | `QString` | Associated event ID |
| `isolationScore` | `double` | Isolation Forest score |
| `zScoreTemporal` | `double` | Temporal Z-score |
| `zScoreSpatial` | `double` | Spatial Z-score |
| `combinedScore` | `double` | Aggregated anomaly magnitude [0,1] |
| `isAnomaly` | `bool` | True if above contamination threshold |
| `signalReasons` | `std::vector<QString>` | Contributing factors |

### `SeriesMatch`
Output of `SeriesDetector`.

| Field | Type | Description |
|---|---|---|
| `seriesId` | `QString` | Cluster identifier |
| `memberCount` | `int` | Events in this cluster |
| `linkProbability` | `double` | Linkage probability [0,1] |
| `spatialDistanceM` | `double` | Metres from query event |
| `temporalDistanceDays` | `double` | Days from query event |
| `moSimilarity` | `double` | Jaccard MO similarity [0,1] |
| `compositeScore` | `double` | Combined distance score |
| `method` | `QString` | Clustering method |

### `NetworkLead`
Output of `CoOffendingAnalyser`.

| Field | Type | Description |
|---|---|---|
| `personId` | `QString` | Suspect identifier |
| `connectionType` | `QString` | "direct_cooffender" \| "second_degree" \| "venue_linked" |
| `sharedIncidents` | `int` | Number of shared incidents |
| `centralityScore` | `double` | PageRank / betweenness centrality |
| `communityId` | `int` | Network community index |
| `riskScore` | `double` | Composite risk score [0,1] |
| `reasoning` | `QString` | Explanation text |

---

## Calibration & Benchmarking

### `CalibrationResult`
| Field | Type | Description |
|---|---|---|
| `ece` | `double` | Expected calibration error |
| `mce` | `double` | Maximum calibration error |
| `ace` | `double` | Average calibration error |
| `brierScore` | `double` | Brier score |
| `logLoss` | `double` | Cross-entropy loss |
| `nSamples` | `int` | Number of predictions |
| `bins` | `QVector<CalibrationBin>` | Per-bin statistics |
| `statusLabel` | `QString` | "Well-calibrated" \| "Overconfident" \| "Underconfident" |

### `BiasReport`
| Field | Type | Description |
|---|---|---|
| `disparateImpact` | `double` | Selection rate ratio between groups |
| `equalOpportunityDiff` | `double` | TPR difference between groups |
| `feedbackLoopRisk` | `double` | Estimated feedback amplification |
| `groupStats` | `QMap<QString, GroupStats>` | Per-group statistics |

---

## Provenance & Logging

### `ProvenanceEntry`
| Field | Type | Description |
|---|---|---|
| `eventId` | `QString` | Event this entry belongs to |
| `stage` | `QString` | Pipeline stage name |
| `description` | `QString` | What was done at this stage |
| `timestamp` | `QDateTime` | When this stage ran |

### `LogEntry`
`SentinelLogger` ring buffer entry.

| Field | Type | Description |
|---|---|---|
| `level` | `QtMsgType` | Qt message level |
| `category` | `QString` | Qt logging category (lcIngest, lcNlp, etc.) |
| `message` | `QString` | Log message text |
| `timestamp` | `QDateTime` | Entry time |
