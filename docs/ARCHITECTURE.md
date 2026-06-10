# SENTINEL — Architecture Reference

## Overview

SENTINEL is structured as a layered pipeline. Each layer has strictly defined responsibilities and communicates via well-typed C++ structs and Qt signals/slots. No layer knows the internals of another.

```
┌──────────────────────────────────────────────────────────────┐
│  INGEST        UKPoliceSource | WeatherSource | CsvImporter  │
│                DataQualityScorer                             │
└──────────────────────────┬───────────────────────────────────┘
                           │ CrimeEvent, WeatherData
┌──────────────────────────▼───────────────────────────────────┐
│  NLP           MOExtractor | CrimeClassifier                 │
│                MOFeatures, NLPResult, CrimeType, Severity    │
└──────────────────────────┬───────────────────────────────────┘
                           │ enriched CrimeEvent
┌──────────────────────────▼───────────────────────────────────┐
│  FEATURE ENG.  TemporalFeatures (sin/cos, lunar, payday)     │
└──────────────────────────┬───────────────────────────────────┘
                           │ TemporalFeatureVector
┌──────────────────────────▼───────────────────────────────────┐
│  MODEL STACK                                                  │
│  PoissonBaseline  HawkesProcess  SeriesDetector              │
│  KDEHotspot       GPRegression   BayesianHierarchical        │
│  RiskForecaster   EnsemblePredictor                          │
└──────────────────────────┬───────────────────────────────────┘
                           │ predictions + uncertainty
┌──────────────────────────▼───────────────────────────────────┐
│  INFERENCE ENGINE                                             │
│  GeographicProfiler  MOAnalyser      EvidenceScorer          │
│  AnomalyDetector     CoOffending     HintEngine              │
│                      LeadReportGenerator                     │
└──────────────────────────┬───────────────────────────────────┘
                           │ InvestigativeLeads, AnomalySignals
┌──────────────────────────▼───────────────────────────────────┐
│  BENCHMARKING  BenchmarkMetrics | BiasAuditor | CalibrationA.│
└──────────────────────────┬───────────────────────────────────┘
                           │
┌──────────────────────────▼───────────────────────────────────┐
│  PERSISTENCE   Database (SQLite), ProvenanceLog              │
└──────────────────────────┬───────────────────────────────────┘
                           │
┌──────────────────────────▼───────────────────────────────────┐
│  UI LAYER      MainWindow → 9 child widgets                  │
└──────────────────────────────────────────────────────────────┘
```

---

## Core Data Structures (`src/core/CrimeEvent.h`)

All inter-layer communication uses these shared structs. They are copy-constructible, movable, and serialisable to JSON.

### `CrimeEvent`
Primary data entity — one record from any source.

```
lat, lon                  double (WGS-84)
timestamp                 QDateTime
crimeType                 QString
suburb, street, postcode  QString
description               QString
quality                   double [0,1]
moFeatures                MOFeatures
nlpResult                 NLPResult
lat/lon                   std::optional<double> (from CSV importer)
```

### `InvestigativeLead`
Output of HintEngine.

```
headline     QString
detail       QString
confidence   double [0,1]
sourceType   QString
provenance   std::vector<QString>
crimeIds     std::vector<int>
```

### `GeographicProfile`
Rossmo probability surface output.

```
peakLat, peakLon      double — highest-probability anchor point
peakProbability       double
searchArea50pct       double km²
grid                  QVector<GridCell>
```

### `AnomalySignal`
Output of AnomalyDetector.

```
combinedScore    double — aggregated anomaly magnitude
lat, lon         double — event location
signalReasons    std::vector<QString>
crimeId          int
```

### `SeriesMatch`
Output of SeriesDetector.

```
seriesId         int
linkProbability  double
compositeScore   double
memberCount      int
crimeType        QString
```

### `NetworkLead`
Output of CoOffendingAnalyser.

```
personId         QString
riskScore        double
reasoning        QString
connectionType   QString
```

---

## Module Responsibilities

### `sentinel_core` (static library)
All non-UI code. Linked by both the application executable and all test executables. This allows every model and inference component to be tested without a display.

### `sentinel` (executable)
Links `sentinel_core` + UI sources. `main.cpp` creates `QApplication`, `MainWindow`, and starts the event loop.

### Tests (`tests/`)
Each test binary links `sentinel_core` + `Qt6::Test`. UI tests additionally instantiate `QApplication` with the offscreen platform (`QT_QPA_PLATFORM=offscreen`).

---

## Qt Signal/Slot Contract

Data sources emit:
- `eventFetched(CrimeEvent)` — one event ready
- `fetchComplete(int count)` — batch done
- `fetchError(QString message)` — async failure

`HintEngine` emits:
- `leadsReady(QVector<InvestigativeLead>)` — batch of leads

`SentinelLogger` emits:
- `newEntry(LogEntry)` — for live console display

---

## Database Schema (v5)

```sql
CREATE TABLE schema_version (version INTEGER PRIMARY KEY);

CREATE TABLE crime_events (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    timestamp TEXT,
    crime_type TEXT,
    latitude REAL,
    longitude REAL,
    suburb TEXT,
    street TEXT,
    postcode TEXT,
    description TEXT,
    quality REAL,
    source TEXT
);

CREATE TABLE provenance (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    crime_id INTEGER REFERENCES crime_events(id),
    stage TEXT,
    description TEXT,
    timestamp TEXT
);

CREATE TABLE mo_cases (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    crime_id INTEGER,
    mo_string TEXT,
    entry_method TEXT,
    target_type TEXT,
    outcome TEXT
);
```

WAL mode enabled for concurrent read/write. In-memory mode (`:memory:`) used in tests.

---

## AppConfig (`src/core/AppConfig.h`)

Runtime configuration struct with `QSettings` persistence. All model parameters are editable at runtime via `SettingsWidget`.

Key parameter groups:
- **Pipeline**: Hawkes history window, DBSCAN epsilon (km, days, MO weight)
- **Alerts**: Elevated/High/Critical probability thresholds
- **GP**: σ², lengthscale, noise σ²
- **Ensemble**: Poisson/Hawkes weight ratio
- **UI**: Map zoom, max lead count, theme

---

## Uncertainty Decomposition

`EnsemblePredictor` separates total prediction uncertainty:

```
total_variance = aleatoric_variance + epistemic_variance

aleatoric  = mean over models of each model's internal variance
epistemic  = variance over models of their mean predictions
```

Both are propagated to `EnsemblePrediction.lowerCI` / `upperCI` and visible in the UI calibration panel.
