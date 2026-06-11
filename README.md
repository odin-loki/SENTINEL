# SENTINEL â€” Crime Analytics & Predictive Threat Assessment System

<p align="center">
  <strong>C++23 Â· Qt 6 Â· SQLite Â· 273 passing tests</strong>
</p>

> A fully auditable, standalone desktop application for spatiotemporal crime prediction, investigative lead generation, and anomaly detection. Every prediction is traceable to its data source, mathematical model, and quantified uncertainty. No proprietary APIs. No black-box AI.

---

## Table of Contents

- [Overview](#overview)
- [Features](#features)
- [Architecture](#architecture)
- [Statistical Models](#statistical-models)
- [Data Sources](#data-sources)
- [Project Structure](#project-structure)
- [Building](#building)
- [Testing](#testing)
- [Configuration](#configuration)
- [Design Principles](#design-principles)
- [Academic References](#academic-references)

---

## Overview

SENTINEL ingests heterogeneous public crime data streams, applies a layered probabilistic modelling pipeline, and delivers:

- **Spatiotemporal crime predictions** with confidence intervals
- **Investigative leads** ranked by evidential weight with contradiction detection  
- **Geographic offender profiles** (Rossmo CGT formula)
- **Near-repeat victimisation** alerts with decay modelling
- **Anomaly signals** with uncertainty decomposition
- **Full provenance tracing** â€” every output is linked to its source data and computation chain

---

## Features

### Data Ingestion
| Component | Description |
|---|---|
| `UKPoliceSource` | UK Police Open Data API (`data.police.uk`) â€” 43 force areas, real-time + historical |
| `WeatherSource` | Open-Meteo free weather API â€” historical hourly data for weather-crime correlation |
| `CsvImporter` | Auto-column detection for Chicago PD, NYC NYPD, LA LAPD, ABS Australian data formats |
| `DataQualityScorer` | Composite quality scoring (completeness, temporal/spatial precision, source reliability) |

### NLP Pipeline
| Component | Description |
|---|---|
| `MOExtractor` | Regex-based Modus Operandi extraction: entry method, target type, time of day, weapons, items taken, solo/group |
| `CrimeClassifier` | Weighted keyword TF-IDF classifier â€” 13 crime categories, severity scoring, sentiment analysis, threat detection |

### Statistical Models
| Model | Method |
|---|---|
| `PoissonBaseline` | Non-homogeneous Poisson process + Negative Binomial overdispersion; PMF/PPF/CI |
| `HawkesProcess` | Self-exciting Hawkes process for near-repeat victimisation (Mohler et al. 2011) |
| `SeriesDetector` | Spatiotemporal DBSCAN + MO Jaccard + Haversine linkage probability |
| `KDEHotspot` | 2D Gaussian KDE, Silverman bandwidth, PAI area fraction, greedy peak suppression |
| `GPRegression` | Gaussian Process regression, squared-exponential kernel, Cholesky posterior, log marginal likelihood |
| `BayesianHierarchical` | Gamma-Poisson conjugate hierarchy with empirical Bayes hyperparameter estimation |
| `RiskForecaster` | Multi-day zone risk forecast combining Poisson + temporal features + recent escalation |
| `EnsemblePredictor` | Weighted ensemble of Poisson + Hawkes + isotonic calibration, epistemic/aleatoric uncertainty decomposition |
| `NearRepeatVictimisation` | Knox ratio space-time clustering test, linear decay alert scoring, crime-type calibrated bandwidths |
| `TemporalFeatures` | Cyclical encoding (sin/cos), lunar phase, sun altitude, payday proximity, night/weekend flags |

### Inference Engine
| Component | Description |
|---|---|
| `GeographicProfiler` | Rossmo CGT probability surface, 50%/80% search area estimation |
| `MOAnalyser` | TF-IDF + cosine similarity against resolved case database, topK matching |
| `EvidenceScorer` | Bayesian likelihood ratio updating (Taroni et al. 2014), Bayes factor, posterior probability |
| `AnomalyDetector` | Isolation scoring + z-score temporal/spatial + combined signal with configurable contamination |
| `CoOffendingAnalyser` | PageRank, Brandes betweenness centrality, community detection, NetworkLead generation |
| `HintEngine` | Ranked investigative leads with contradiction detection, quality scoring, and full provenance |
| `LeadReportGenerator` | Markdown + HTML report generation with provenance chains |

### Benchmarking & Fairness
| Component | Description |
|---|---|
| `BenchmarkMetrics` | PAI, PEI, SER, AUC-ROC, AUC-PR, MAE, RMSE, Brier score, MRR, Precision@K |
| `BiasAuditor` | Disparate impact, equal opportunity difference, feedback loop detection |
| `CalibrationAnalyser` | ECE, MCE, ACE, reliability diagram, isotonic regression calibration |

### User Interface
| Widget | Description |
|---|---|
| `DashboardWidget` | Live stats cards, recent events, crime type distribution, Bayesian zone risk panel |
| `MapWidget` | Custom QPainter spatial view with risk heatmap overlay, KDE hotspot regions, zoom/pan |
| `EventsTableWidget` | Filterable crime event browser with quality badges and detail panel |
| `LeadsWidget` | Ranked investigative leads with interactive evidence scorer, series table |
| `AnalyticsWidget` | Qt Charts: hourly patterns, crime type pie, temporal trend, calibration curve |
| `TemporalHeatmapWidget` | Weekday Ã— hour crime density heatmap |
| `AuditLogWidget` | Filterable provenance chain viewer per event |
| `DebugConsoleWidget` | Real-time log viewer with level and category filtering |
| `SettingsWidget` | API keys, model parameters, alert thresholds, GP hyperparameters |

---

## Architecture

```
â”Œâ”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”
â”‚                        SENTINEL PIPELINE                         â”‚
â”œâ”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”¤
â”‚                                                                  â”‚
â”‚  â”Œâ”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”   â”Œâ”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”   â”Œâ”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â” â”‚
â”‚  â”‚  INGEST     â”‚   â”‚  NLP         â”‚   â”‚  FEATURE ENGINEERING  â”‚ â”‚
â”‚  â”‚             â”‚â”€â”€â–¶â”‚              â”‚â”€â”€â–¶â”‚                       â”‚ â”‚
â”‚  â”‚ UKPolice    â”‚   â”‚ MOExtractor  â”‚   â”‚ TemporalFeatures      â”‚ â”‚
â”‚  â”‚ Weather     â”‚   â”‚ CrimeClass.  â”‚   â”‚ DataQualityScorer     â”‚ â”‚
â”‚  â”‚ CsvImporter â”‚   â”‚              â”‚   â”‚                       â”‚ â”‚
â”‚  â””â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”˜   â””â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”˜   â””â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”˜ â”‚
â”‚          â”‚                                         â”‚             â”‚
â”‚          â–¼                                         â–¼             â”‚
â”‚  â”Œâ”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”   â”‚
â”‚  â”‚                   MODEL STACK                            â”‚   â”‚
â”‚  â”‚  PoissonBaseline  â”‚  HawkesProcess  â”‚  SeriesDetector   â”‚   â”‚
â”‚  â”‚  KDEHotspot       â”‚  GPRegression   â”‚  BayesianHier.    â”‚   â”‚
â”‚  â”‚  RiskForecaster   â”‚  EnsemblePredictor â”‚ NearRepeatVict.â”‚   â”‚
â”‚  â””â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”˜   â”‚
â”‚          â”‚                                                        â”‚
â”‚          â–¼                                                        â”‚
â”‚  â”Œâ”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”   â”‚
â”‚  â”‚                   INFERENCE ENGINE                       â”‚   â”‚
â”‚  â”‚  GeographicProfiler â”‚ MOAnalyser   â”‚ EvidenceScorer     â”‚   â”‚
â”‚  â”‚  AnomalyDetector    â”‚ CoOffending. â”‚ HintEngine         â”‚   â”‚
â”‚  â””â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”˜   â”‚
â”‚          â”‚                                                        â”‚
â”‚          â–¼                                                        â”‚
â”‚  â”Œâ”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”   â”Œâ”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”   â”Œâ”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”   â”‚
â”‚  â”‚  DATABASE   â”‚   â”‚  BENCHMARKING  â”‚   â”‚  UI LAYER        â”‚   â”‚
â”‚  â”‚  SQLite     â”‚   â”‚  BenchMetrics  â”‚   â”‚  Dashboard       â”‚   â”‚
â”‚  â”‚  Provenance â”‚   â”‚  BiasAuditor   â”‚   â”‚  Map / Events    â”‚   â”‚
â”‚  â”‚  Audit Log  â”‚   â”‚  Calibration   â”‚   â”‚  Leads / Audit   â”‚   â”‚
â”‚  â””â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”˜   â””â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”˜   â””â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”˜   â”‚
â”‚                                                                  â”‚
â”‚  ProvenanceLog traces every event through the entire pipeline    â”‚
â””â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”˜
```

---

## Statistical Models

### Poisson Baseline (Negative Binomial variant)
Non-homogeneous Poisson process with temporal bucket keys `zone|hour|dow|month|crimeType`. Overdispersed zones automatically switch to Negative Binomial parameterisation. Provides PMF, PPF, 90% credible intervals.

### Hawkes Self-Exciting Process
Conditional intensity: `Î»(t,x) = Î¼ + Î£áµ¢ Î±Â·exp(-Î²(t-táµ¢))Â·G(x-xáµ¢,Ïƒ)` where G is a 2D spatial Gaussian kernel. Fitted via maximum likelihood. Models near-repeat victimisation contagion.

### Spatiotemporal DBSCAN (Series Detector)
Normalises spatial (km), temporal (days) and MO similarity into a 3D feature space. Applies DBSCAN with calibrated crime-type-specific epsilon values from published near-repeat research.

### Rossmo CGT Geographic Profile
For distance `d` from grid point to crime site `cáµ¢`, with buffer radius `B`:
- `d â‰¤ B`: `B^(gâˆ’f) / (2B âˆ’ d)^g` (buffer zone â€” suppresses anchor near crime sites)
- `B < d < 4B`: `1 / d^f` (distance decay)
- `d â‰¥ 4B`: negligible

Surface is summed over all crime sites, normalised; 50%/80% search areas computed by thresholding.

### Bayesian Hierarchical Model
Gamma-Poisson conjugacy: `Î»_z | data ~ Gamma(Î±â‚€+k, Î²â‚€+E)`. Hyperparameters Î±â‚€, Î²â‚€ estimated via empirical Bayes method of moments across zones.

### GP Regression
Squared-exponential kernel, Cholesky decomposition `K = LLáµ€`, posterior mean `Î¼* = K*áµ€ Î±`, posterior variance `Ïƒ*Â² = K** - K*áµ€ (K+Ïƒâ‚™Â²I)â»Â¹ K*`. Log marginal likelihood for model comparison.

### Calibration Analysis
ECE (expected calibration error, Guo et al. 2017), MCE (maximum calibration error), ACE (unweighted average), PAVA-based isotonic regression recalibration.

---

## Data Sources

| Source | URL | Access | Data |
|---|---|---|---|
| UK Police Open Data | `data.police.uk/api` | Free, no key | 43 UK police forces, monthly crime records |
| Open-Meteo Weather | `api.open-meteo.com` | Free, no key | Hourly historical + forecast weather |
| CSV Auto-import | Local files | N/A | Chicago PD, NYPD, LAPD, ABS data formats |

---

## Project Structure

```
sentinel/
â”œâ”€â”€ CMakeLists.txt              # Build configuration
â”œâ”€â”€ README.md
â”œâ”€â”€ .gitignore
â”œâ”€â”€ resources/
â”‚   â”œâ”€â”€ icons/                  # Application icons
â”‚   â”œâ”€â”€ styles/dark.qss         # Dark theme stylesheet
â”‚   â””â”€â”€ sentinel.qrc            # Qt resource file
â”œâ”€â”€ src/
â”‚   â”œâ”€â”€ main.cpp
â”‚   â”œâ”€â”€ core/
â”‚   â”‚   â”œâ”€â”€ CrimeEvent.h        # All shared data structures
â”‚   â”‚   â”œâ”€â”€ AppConfig.h         # Runtime configuration + QSettings persistence
â”‚   â”‚   â”œâ”€â”€ Database.h/cpp      # SQLite persistence, schema versioning
â”‚   â”‚   â”œâ”€â”€ SentinelLogger.h/cpp # Qt log handler + ring buffer
â”‚   â”‚   â””â”€â”€ DataExporter.h/cpp  # Markdown/JSON/CSV/HTML export
â”‚   â”œâ”€â”€ audit/
â”‚   â”‚   â””â”€â”€ ProvenanceLog.h/cpp # Event provenance chain recording
â”‚   â”œâ”€â”€ ingest/
â”‚   â”‚   â”œâ”€â”€ DataSource.h        # Abstract async data source base
â”‚   â”‚   â”œâ”€â”€ UKPoliceSource.h/cpp # UK Police Open Data API client
â”‚   â”‚   â”œâ”€â”€ WeatherSource.h/cpp  # Open-Meteo weather API client
â”‚   â”‚   â”œâ”€â”€ CsvImporter.h/cpp   # Auto-column CSV import
â”‚   â”‚   â””â”€â”€ DataQualityScorer.h/cpp # Quality scoring
â”‚   â”œâ”€â”€ nlp/
â”‚   â”‚   â”œâ”€â”€ MOExtractor.h/cpp   # Modus operandi feature extraction
â”‚   â”‚   â””â”€â”€ CrimeClassifier.h/cpp # Crime type classification + sentiment
â”‚   â”œâ”€â”€ models/
â”‚   â”‚   â”œâ”€â”€ TemporalFeatures.h/cpp # Cyclical temporal feature engineering
â”‚   â”‚   â”œâ”€â”€ PoissonBaseline.h/cpp  # Poisson/NegBin baseline model
â”‚   â”‚   â”œâ”€â”€ HawkesProcess.h/cpp    # Self-exciting process
â”‚   â”‚   â”œâ”€â”€ SeriesDetector.h/cpp   # DBSCAN crime series detection
â”‚   â”‚   â”œâ”€â”€ KDEHotspot.h/cpp       # Kernel density hotspot estimation
â”‚   â”‚   â”œâ”€â”€ GPRegression.h/cpp     # Gaussian Process regression
â”‚   â”‚   â”œâ”€â”€ BayesianHierarchical.h/cpp # Gamma-Poisson hierarchical model
â”‚   â”‚   â”œâ”€â”€ RiskForecaster.h/cpp   # Multi-day zone risk forecasting
â”‚   â”‚   â”œâ”€â”€ EnsemblePredictor.h/cpp # Weighted ensemble + calibration
â”‚   â”‚   â””â”€â”€ NearRepeatVictimisation.h/cpp # Knox test + near-repeat alerts
â”‚   â”œâ”€â”€ inference/
â”‚   â”‚   â”œâ”€â”€ GeographicProfiler.h/cpp # Rossmo CGT surface
â”‚   â”‚   â”œâ”€â”€ MOAnalyser.h/cpp       # TF-IDF cosine MO similarity
â”‚   â”‚   â”œâ”€â”€ EvidenceScorer.h/cpp   # Bayesian likelihood ratio evidence
â”‚   â”‚   â”œâ”€â”€ AnomalyDetector.h/cpp  # Isolation + z-score anomaly detection
â”‚   â”‚   â”œâ”€â”€ CoOffendingAnalyser.h/cpp # PageRank + betweenness co-offender network
â”‚   â”‚   â”œâ”€â”€ HintEngine.h/cpp       # Lead generation + ranking + contradiction
â”‚   â”‚   â””â”€â”€ LeadReportGenerator.h/cpp # Markdown/HTML lead reports
â”‚   â”œâ”€â”€ benchmark/
â”‚   â”‚   â”œâ”€â”€ BenchmarkMetrics.h/cpp # PAI, PEI, SER, AUC, MAE, RMSE, Brier, MRR
â”‚   â”‚   â”œâ”€â”€ BiasAuditor.h/cpp      # Fairness metrics and bias detection
â”‚   â”‚   â””â”€â”€ CalibrationAnalyser.h/cpp # ECE, MCE, ACE, isotonic calibration
â”‚   â””â”€â”€ ui/
â”‚       â”œâ”€â”€ MainWindow.h/cpp
â”‚       â”œâ”€â”€ DashboardWidget.h/cpp
â”‚       â”œâ”€â”€ MapWidget.h/cpp
â”‚       â”œâ”€â”€ EventsTableWidget.h/cpp
â”‚       â”œâ”€â”€ LeadsWidget.h/cpp
â”‚       â”œâ”€â”€ AnalyticsWidget.h/cpp
â”‚       â”œâ”€â”€ TemporalHeatmapWidget.h/cpp
â”‚       â”œâ”€â”€ AuditLogWidget.h/cpp
â”‚       â”œâ”€â”€ DebugConsoleWidget.h/cpp
â”‚       â””â”€â”€ SettingsWidget.h/cpp
â””â”€â”€ tests/
    â”œâ”€â”€ CMakeLists.txt          # 273 test targets
    â””â”€â”€ test_*.cpp              # Unit, integration, stress, and UI tests
```

---

## Building

### Prerequisites

| Requirement | Version |
|---|---|
| Qt | 6.4+ (Core, Widgets, Network, Charts, Sql, Test) |
| CMake | 3.27+ |
| C++ Compiler | C++23: MSVC 2022, GCC 13+, or Clang 16+ |
| Ninja (optional) | Fastest build on Windows |

### Windows â€” MinGW (Recommended)

```bash
cd sentinel
cmake -B build -G Ninja -DCMAKE_PREFIX_PATH="C:/Qt/6.11.0/mingw_64"
cmake --build build
```

### Windows â€” MSVC

```bash
cmake -B build -DCMAKE_PREFIX_PATH="C:/Qt/6.11.0/msvc2022_64"
cmake --build build --config Release
```

### Linux / macOS

```bash
cmake -B build -DCMAKE_PREFIX_PATH=/path/to/Qt/6.x.x/gcc_64
cmake --build build -j$(nproc)
```

### Running

```bash
# Windows MinGW
./build/sentinel.exe

# Linux/macOS
./build/sentinel
```

---

## Testing

SENTINEL has **273 test targets** covering every pipeline stage. Tests are written with Qt Test and run via CTest.

```bash
# Run all tests (parallel, 4 jobs)
cd build
ctest --output-on-failure -j4

# Run a specific test
ctest -R test_poisson_statistical --output-on-failure

# List all tests
ctest -N
```

### Test Coverage by Layer

| Layer | Test Files | Coverage |
|---|---|---|
| Core / Database | `test_database_*`, `test_crime_event_*` | Schema versioning, CRUD, migration, stress |
| Ingest | `test_csv_*`, `test_uk_police_*`, `test_data_quality_*` | CSV parsing, API JSON, quality scoring |
| NLP | `test_mo_extractor_*`, `test_crime_classifier_*` | Entry methods, weapons, severity, sentiment |
| Models | `test_poisson_*`, `test_hawkes_*`, `test_series_*`, `test_kde_*`, `test_gp_*`, `test_bayesian_*`, `test_risk_*`, `test_ensemble_*`, `test_near_repeat_*` | PMF/PPF, intensity, surface, grid, posterior, Knox ratio |
| Inference | `test_geographic_*`, `test_mo_analyser_*`, `test_evidence_*`, `test_anomaly_*`, `test_cooffending_*`, `test_hint_*` | CGT surface, TF-IDF, Bayesian LR, PageRank |
| Benchmarking | `test_benchmark_*`, `test_calibration_*`, `test_bias_*` | PAI, AUC, ECE, fairness metrics |
| UI (headless) | `test_*_widget*`, `test_audit_log_*`, `test_settings_*` | Qt offscreen, signal/slot, model data |
| Integration | `test_pipeline_*`, `test_full_*`, `test_e2e_*` | End-to-end with 1000+ events |

### Test execution time

The full suite completes in under **40 seconds** on a modern CPU (stress + network tests run in parallel).

---

## Configuration

Settings are persisted via `QSettings` (INI format on Windows at `%APPDATA%/SENTINEL/`).

### Key Parameters

```ini
[pipeline]
hawkesHistoryDays=365
seriesMinEvents=3
seriesEpsKm=0.3
seriesEpsDays=14.0
qualityThreshold=0.3
forecastHorizonDays=7

[alerts]
alertElevated=0.30
alertHigh=0.50
alertCritical=0.75

[gp]
gpSigma2=1.0
gpLengthscale=0.5
gpNoiseSigma2=0.1

[ensemble]
ensemblePoissonWeight=0.5
ensembleHawkesWeight=0.5

[ui]
mapZoomLevel=14
maxLeadCount=50
theme=dark
```

---

## Design Principles

### 1. Auditability First
Every output â€” prediction, lead, anomaly signal â€” carries a full provenance chain: data source â†’ ingest time â†’ model â†’ parameters â†’ output. The `ProvenanceLog` records every stage for every event.

### 2. Uncertainty as a First-Class Output
No point estimates without confidence intervals. All probabilistic outputs include:
- 90%/95% credible intervals (Bayesian models)
- Aleatoric + epistemic uncertainty decomposition (EnsemblePredictor)
- Calibration curves and ECE scores

### 3. No Black-Box AI
Every algorithm is an open, peer-reviewed statistical method implemented in pure C++. Every weight, kernel, and threshold is inspectable, configurable, and documented.

### 4. Fairness by Design
The `BiasAuditor` computes disparate impact ratios, equal opportunity differences, and feedback loop risk scores. Alert thresholds are configurable to prevent systematic over-flagging of any demographic or geographic group.

### 5. Reproducibility
All pipelines are deterministic given the same input data and configuration. No hidden randomness.

---

## Academic References

| Method | Reference |
|---|---|
| Hawkes self-exciting process | Mohler et al. (2011). *Self-exciting point process modeling of crime.* JASA 106(493):100â€“108 |
| Near-repeat victimisation | Sherman et al. (1989). *Hot spots of predatory crime.* Criminology 27(1):27â€“56 |
| Near-repeat space-time risk | Johnson et al. (2007). *Space-time patterns of risk.* British Journal of Criminology 47(3):363â€“383 |
| Geographic profiling (Rossmo CGT) | Rossmo, D.K. (2000). *Geographic Profiling.* CRC Press |
| Bayesian evidence weighting | Taroni et al. (2014). *Bayesian Networks for Probabilistic Inference and Decision Analysis in Forensic Science.* Wiley |
| Co-offending networks (PageRank) | Page et al. (1999). *The PageRank Citation Ranking.* Stanford Tech Report |
| Betweenness centrality | Brandes (2001). *A faster algorithm for betweenness centrality.* Journal of Mathematical Sociology 25(2):163â€“177 |
| Hierarchical Bayesian crime rates | Gelman & Hill (2007). *Data Analysis Using Regression and Multilevel/Hierarchical Models.* Cambridge |
| KDE spatial hotspots | Chainey & Ratcliffe (2005). *GIS and Crime Mapping.* Wiley |
| Calibration (ECE) | Guo et al. (2017). *On calibration of modern neural networks.* ICML |
| Isotonic regression | Niculescu-Mizil & Caruana (2005). *Predicting good probabilities with supervised learning.* ICML |

---

## Technology Stack

| Layer | Technology |
|---|---|
| Language | C++23 |
| UI Framework | Qt 6 (Widgets, Charts, Network, SQL) |
| Database | SQLite (in-memory for tests, file-backed for production) |
| Build System | CMake 3.27+ with Ninja |
| HTTP Client | Qt `QNetworkAccessManager` |
| NLP | Rule-based (`std::regex`) |
| Statistics | Pure C++ implementations (no external ML libraries) |
| Testing | Qt Test, CTest, `QSignalSpy`, offscreen Qt platform |

---

*Author: Odin Loch Â· Version 1.0.0 Â· C++23 Qt6 implementation of the SENTINEL design specification*

