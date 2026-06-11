# SENTINEL — Core Data Structures

Shared structures are defined primarily in `src/core/CrimeEvent.h`. Model-specific output structs live in their respective headers (e.g. `HotspotRegion` in `KDEHotspot.h`, `NearRepeatAlert` in `NearRepeatVictimisation.h`, `TemporalFeatureVector` in `TemporalFeatures.h`, `PersonIncidentRecord` / `NetworkNode` in `CoOffendingAnalyser.h`, `HintEngineInput` in `HintEngine.h`).

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
| `zoneId` | `QString` | Zone identifier |
| `date` | `QDate` | Forecast date |
| `riskScore` | `double` | Aggregated risk [0,1] |
| `baselineProb` | `double` | Poisson P(crime > 0) |
| `escalationFactor` | `double` | Recency boost [1, ∞) |
| `temporalFactor` | `double` | Cyclical pattern multiplier |
| `expectedCount` | `double` | E[crimes] |
| `rank` | `int` | Rank within forecast window |
| `explanation` | `QString` | Human-readable rationale |

### `ZoneForecast`
| Field | Type | Description |
|---|---|---|
| `zoneId` | `QString` | Zone identifier |
| `days` | `QVector<ForecastDay>` | Per-day forecasts |
| `weeklyRisk` | `double` | Mean of daily riskScore values over horizon |
| `alertLevel` | `int` | 0=Normal, 1=Elevated, 2=High, 3=Critical |
| `peakDayIndex` | `int` | Index into `days[]` with the highest `riskScore` |

### `ZonePosterior`
Output of `BayesianHierarchical`.

| Field | Type | Description |
|---|---|---|
| `zoneId` | `QString` | Zone identifier |
| `alphaPrior`, `betaPrior` | `double` | Gamma hyperprior parameters |
| `alphaPost`, `betaPost` | `double` | Gamma posterior parameters |
| `posteriorMean` | `double` | E[λ\|data] = α_post / β_post |
| `posteriorVar` | `double` | Var[λ\|data] |
| `credibleLow`, `credibleHigh` | `double` | 5th / 95th percentile of posterior |
| `observedCount` | `int` | Observed crime count |
| `exposure` | `double` | Training window (days) |

### `HotspotRegion`
Output of `KDEHotspot::findHotspots()`.

| Field | Type | Description |
|---|---|---|
| `centroidLat`, `centroidLon` | `double` | Peak centroid coordinates |
| `latMin`, `latMax`, `lonMin`, `lonMax` | `double` | Bounding box |
| `peakDensity` | `double` | Kernel density at centroid |
| `totalMass` | `double` | Integrated density in region |
| `crimeCount` | `int` | Crimes within bounding box |
| `rank` | `int` | 1 = hottest |

### `NearRepeatAlert`
Output of `NearRepeatVictimisation::analyse()`.

| Field | Type | Description |
|---|---|---|
| `eventId` | `QString` | Current event ID |
| `priorEventId` | `QString` | Prior event within near-repeat window |
| `alertScore` | `double` | Proximity score [0,1] |
| `spatialDistanceM` | `double` | Haversine distance in metres |
| `temporalDistanceDays` | `double` | Absolute time gap in days |

### `TemporalFeatureVector`
Output of `TemporalFeatures::compute()`.

| Field | Type | Description |
|---|---|---|
| `hourSin`, `hourCos` | `double` | Cyclical hour encoding |
| `dowSin`, `dowCos` | `double` | Cyclical day-of-week encoding |
| `monthSin`, `monthCos` | `double` | Cyclical month encoding |
| `doySin`, `doyCos` | `double` | Cyclical day-of-year encoding |
| `hourRaw`, `dowRaw` | `int` | Raw hour (0–23) and weekday (0=Mon) |
| `isWeekend`, `isNight` | `bool` | Weekend and night (22:00–05:59) flags |
| `isPublicHoliday` | `bool` | Public holiday flag |
| `daysFromPayday` | `int` | Days to nearest fortnightly payday |
| `weekOfMonth` | `int` | Week index within month |
| `lunarPhase` | `double` | 0=new moon, 0.5=full moon |
| `sunAltitudeDeg` | `double` | Approximate sun altitude (degrees) |
| `isDark` | `bool` | True when sun altitude < −6° |

### `SeriesEvent`
Input record for `SeriesDetector` and `NearRepeatVictimisation`.

| Field | Type | Description |
|---|---|---|
| `eventId` | `QString` | Event identifier |
| `lat`, `lon` | `double` | WGS-84 coordinates |
| `tDays` | `double` | Days since reference epoch |
| `crimeType` | `QString` | Normalised crime category |
| `moText` | `QString` | Canonical MO string for similarity |

---

## Inference Structures

### `HintEngineInput`
Input bundle for `HintEngine::generate()`.

| Field | Type | Description |
|---|---|---|
| `event` | `CrimeEvent` | Query event |
| `seriesMatches` | `QVector<SeriesMatch>` | Series linkage candidates |
| `moMatches` | `QVector<MOMatch>` | MO similarity matches |
| `geoProfile` | `std::optional<GeographicProfile>` | Rossmo CGT profile |
| `networkLeads` | `QVector<NetworkLead>` | Co-offending network leads |
| `evidenceWeights` | `QVector<EvidenceWeight>` | Bayesian evidence weights |
| `anomalySignal` | `std::optional<AnomalySignal>` | Anomaly detector output |
| `dataQuality` | `double` | Data quality multiplier [0,1] |

### `PersonIncidentRecord`
Input record for `CoOffendingAnalyser::buildGraph()`.

| Field | Type | Description |
|---|---|---|
| `personId` | `QString` | Person identifier |
| `incidentId` | `QString` | Incident identifier |
| `role` | `QString` | "suspect" \| "witness" \| "victim" \| "associate" |
| `roleWeight` | `double` | Role weight (e.g. suspect=1.0, associate=0.5) |

### `NetworkNode`
Internal graph node from `CoOffendingAnalyser::nodes()`.

| Field | Type | Description |
|---|---|---|
| `personId` | `QString` | Person identifier |
| `pageRank` | `double` | PageRank centrality |
| `betweenness` | `double` | Brandes betweenness centrality |
| `communityId` | `int` | Community label (−1 if unassigned) |
| `degree` | `int` | Node degree |
| `incidentIds` | `QStringList` | Associated incident IDs |
| `neighbours` | `QMap<QString, double>` | Adjacent personId → edge weight |

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
| `lofScore` | `double` | Local Outlier Factor score |
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
Per-group-pair fairness comparison from `BiasAuditor`.

| Field | Type | Description |
|---|---|---|
| `metric` | `QString` | "disparate_impact", "equal_opportunity", etc. |
| `groupA`, `groupB` | `QString` | Compared group labels |
| `valueA`, `valueB` | `double` | Metric value per group |
| `ratio` | `double` | valueA / valueB |
| `flagged` | `bool` | True if ratio outside [0.80, 1.25] |
| `notes` | `QString` | Explanation text |

### `GroupStats`
Per-group statistics from `BiasAuditor::groupStats()`.

| Field | Type | Description |
|---|---|---|
| `groupId` | `QString` | Group label |
| `nEvents` | `int` | Total events in group |
| `nFlagged` | `int` | Events flagged above threshold |
| `flagRate` | `double` | nFlagged / nEvents |
| `meanPred` | `double` | Mean predicted probability |
| `actualRate` | `double` | Observed positive rate |
| `nTP` | `int` | True positives |
| `nActualPos` | `int` | Total actual positives |

---

## Provenance & Logging

### `ProvenanceEntry`
| Field | Type | Description |
|---|---|---|
| `timestamp` | `QDateTime` | When this stage ran |
| `eventId` | `QString` | Event this entry belongs to |
| `stage` | `QString` | "ingest" \| "nlp" \| "model" \| "inference" \| "output" |
| `action` | `QString` | Action performed |
| `detail` | `QString` | Human-readable detail |
| `dataHash` | `QString` | SHA256 of input data (first 16 chars) |

### `LogEntry`
`SentinelLogger` ring buffer entry.

| Field | Type | Description |
|---|---|---|
| `level` | `QtMsgType` | Qt message level |
| `category` | `QString` | Qt logging category (lcIngest, lcNlp, etc.) |
| `message` | `QString` | Log message text |
| `timestamp` | `QDateTime` | Entry time |
