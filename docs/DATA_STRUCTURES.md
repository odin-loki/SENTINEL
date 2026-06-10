# SENTINEL — Core Data Structures

All shared structures are defined in `src/core/CrimeEvent.h`.

---

## Primary Event

### `CrimeEvent`
Represents a single crime record from any source.

| Field | Type | Description |
|---|---|---|
| `id` | `int` | Database primary key |
| `timestamp` | `QDateTime` | Event time (UTC) |
| `crimeType` | `QString` | Normalised crime category |
| `suburb` | `QString` | Suburb or area name |
| `street` | `QString` | Street name (may be anonymised) |
| `postcode` | `QString` | UK postcode or equivalent |
| `latitude` | `double` | WGS-84 latitude |
| `longitude` | `double` | WGS-84 longitude |
| `lat` | `std::optional<double>` | Parsed latitude (CSV importer) |
| `lon` | `std::optional<double>` | Parsed longitude (CSV importer) |
| `description` | `QString` | Free-text description |
| `quality` | `double` | DataQualityScorer composite score [0,1] |
| `source` | `QString` | Data source identifier |
| `moFeatures` | `MOFeatures` | Extracted MO features |
| `nlpResult` | `NLPResult` | NLP classification result |

---

## NLP Structures

### `MOFeatures`
Extracted from `description` by `MOExtractor`.

| Field | Type | Description |
|---|---|---|
| `entryMethod` | `QString` | e.g. "forced_entry", "rear_door" |
| `targetType` | `QString` | e.g. "residential", "commercial" |
| `timeOfDay` | `QString` | "night", "day", "unknown" |
| `weapons` | `QStringList` | Weapon mentions |
| `itemsTaken` | `QStringList` | Stolen items |
| `isSolo` | `bool` | Solo offender vs group |
| `rawMO` | `QString` | Original MO text |

### `NLPResult`
Classification result from `CrimeClassifier`.

| Field | Type | Description |
|---|---|---|
| `primaryType` | `QString` | Most likely crime type |
| `secondaryTypes` | `QStringList` | Additional types |
| `confidence` | `double` | Classification confidence [0,1] |
| `severity` | `int` | 1–5 severity scale |
| `hasThreatSignal` | `bool` | Threat language detected |
| `sentimentScore` | `double` | Sentiment [-1, 1] |

---

## Prediction Structures

### `PoissonPrediction`
| Field | Type | Description |
|---|---|---|
| `meanRate` | `double` | Expected count per window |
| `probAtLeastOne` | `double` | P(X ≥ 1) |
| `lowerCI` | `double` | 5th percentile |
| `upperCI` | `double` | 95th percentile |
| `overdispersed` | `bool` | Negative Binomial used |

### `HawkesParams`
| Field | Type | Description |
|---|---|---|
| `mu` | `double` | Background intensity |
| `alpha` | `double` | Excitation magnitude |
| `beta` | `double` | Temporal decay rate |

### `EnsemblePrediction`
| Field | Type | Description |
|---|---|---|
| `probability` | `double` | Calibrated ensemble probability |
| `lowerCI` | `double` | Lower credible interval |
| `upperCI` | `double` | Upper credible interval |
| `aleatoricUncertainty` | `double` | Irreducible noise |
| `epistemicUncertainty` | `double` | Model uncertainty |
| `dominantModel` | `QString` | "Poisson" or "Hawkes" |

### `ForecastDay`
| Field | Type | Description |
|---|---|---|
| `date` | `QDate` | Forecast date |
| `riskScore` | `double` | [0,1] aggregated risk |
| `alertLevel` | `AlertLevel` | NORMAL/ELEVATED/HIGH/CRITICAL |
| `lowerCI` | `double` | Lower CI |
| `upperCI` | `double` | Upper CI |

---

## Inference Structures

### `InvestigativeLead`
Output of `HintEngine`.

| Field | Type | Description |
|---|---|---|
| `headline` | `QString` | Short lead title |
| `detail` | `QString` | Full explanation |
| `confidence` | `double` | Lead strength [0,1] |
| `sourceType` | `QString` | Lead origin type |
| `provenance` | `std::vector<QString>` | Computation chain |
| `crimeIds` | `std::vector<int>` | Contributing event IDs |

### `GeographicProfile`
Output of `GeographicProfiler`.

| Field | Type | Description |
|---|---|---|
| `peakLat` | `double` | Highest-probability anchor latitude |
| `peakLon` | `double` | Highest-probability anchor longitude |
| `peakProbability` | `double` | Maximum surface value |
| `searchArea50pct` | `double` | km² containing 50% of probability |
| `searchArea80pct` | `double` | km² containing 80% of probability |
| `grid` | `QVector<GridCell>` | Full probability grid |

### `AnomalySignal`
Output of `AnomalyDetector`.

| Field | Type | Description |
|---|---|---|
| `crimeId` | `int` | Associated event |
| `combinedScore` | `double` | Aggregated anomaly magnitude |
| `lat` | `double` | Event latitude |
| `lon` | `double` | Event longitude |
| `signalReasons` | `std::vector<QString>` | Contributing factors |

### `SeriesMatch`
Output of `SeriesDetector`.

| Field | Type | Description |
|---|---|---|
| `seriesId` | `int` | Cluster identifier |
| `linkProbability` | `double` | Linkage probability [0,1] |
| `compositeScore` | `double` | Combined distance score |
| `memberCount` | `int` | Events in series |
| `crimeType` | `QString` | Dominant crime type |

### `NetworkLead`
Output of `CoOffendingAnalyser`.

| Field | Type | Description |
|---|---|---|
| `personId` | `QString` | Suspect identifier |
| `riskScore` | `double` | Risk score [0,1] |
| `connectionType` | `QString` | Connection type (co-offending, associate) |
| `reasoning` | `QString` | Explanation of the lead |

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
| `statusLabel` | `QString` | "Well-calibrated", "Overconfident", etc. |

### `BiasReport`
| Field | Type | Description |
|---|---|---|
| `disparateImpact` | `double` | Selection rate ratio between groups |
| `equalOpportunityDiff` | `double` | TPR difference between groups |
| `feedbackLoopRisk` | `double` | Estimated feedback amplification |
| `groupStats` | `QMap<QString, GroupStats>` | Per-group statistics |

---

## Provenance

### `ProvenanceEntry`
| Field | Type | Description |
|---|---|---|
| `crimeId` | `int` | Event this entry belongs to |
| `stage` | `QString` | Pipeline stage name |
| `description` | `QString` | What was done |
| `timestamp` | `QDateTime` | When this stage ran |

### `LogEntry`
`SentinelLogger` ring buffer entry.

| Field | Type | Description |
|---|---|---|
| `level` | `QtMsgType` | Qt message level |
| `category` | `QString` | Qt logging category |
| `message` | `QString` | Log message text |
| `timestamp` | `QDateTime` | Entry time |
